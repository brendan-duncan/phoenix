#include "pen_tool.h"

#include "preferences.h"

#include "cursors.h"
#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/snapping.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {

/// Index used for the single fill and stroke on a generated shape; the format
/// numbers styles from one.
constexpr int kStyleIndex = 1;

constexpr double kPi = 3.14159265358979323846;

QPointF toQPointF(const fla::Point& point)
{
    return QPointF(point.x, point.y);
}

fla::Point toPoint(const QPointF& point)
{
    return fla::Point(point.x(), point.y());
}

/// The anchors in the other order, with each anchor's handles swapped so the
/// curve reads the same from the other end.
fla::EditablePath reversed(const fla::EditablePath& path)
{
    fla::EditablePath result;
    result.closed = path.closed;
    for (auto it = path.anchors.rbegin(); it != path.anchors.rend(); ++it)
    {
        fla::Anchor anchor = *it;
        std::swap(anchor.inHandle, anchor.outHandle);
        result.anchors.push_back(anchor);
    }
    return result;
}

fla::EditablePath mapped(const fla::EditablePath& path, const QTransform& transform)
{
    fla::EditablePath result = path;
    for (fla::Anchor& anchor : result.anchors)
    {
        anchor.position = toPoint(transform.map(toQPointF(anchor.position)));
        anchor.inHandle = toPoint(transform.map(toQPointF(anchor.inHandle)));
        anchor.outHandle = toPoint(transform.map(toQPointF(anchor.outHandle)));
    }
    return result;
}

} // namespace

QPainterPath PenTool::previewPath(bool includeCursor) const
{
    QPainterPath painterPath;
    if (_path.anchors.empty())
        return painterPath;

    painterPath.moveTo(toQPointF(_path.anchors[0].position));

    for (size_t i = 1; i < _path.anchors.size(); ++i)
    {
        const fla::Anchor& from = _path.anchors[i - 1];
        const fla::Anchor& to = _path.anchors[i];

        if (!from.hasOutCurve() && !to.hasInCurve())
            painterPath.lineTo(toQPointF(to.position));
        else
            painterPath.cubicTo(toQPointF(from.outHandle), toQPointF(to.inHandle),
                toQPointF(to.position));
    }

    if (_closing && _path.anchors.size() >= 2)
    {
        // The closing segment, taking shape as the first anchor's in-handle
        // is dragged.
        const fla::Anchor& last = _path.anchors.back();
        const fla::Anchor& first = _path.anchors.front();
        if (!last.hasOutCurve() && !first.hasInCurve())
            painterPath.lineTo(toQPointF(first.position));
        else
            painterPath.cubicTo(toQPointF(last.outHandle), toQPointF(first.inHandle),
                toQPointF(first.position));
    }
    else if (includeCursor && !_draggingHandle)
    {
        // The rubber band shows where the next segment would land, curving if
        // the last anchor has a handle pulled out.
        const fla::Anchor& last = _path.anchors.back();
        if (last.hasOutCurve())
            painterPath.cubicTo(toQPointF(last.outHandle), _cursor, _cursor);
        else
            painterPath.lineTo(_cursor);
    }

    return painterPath;
}

QPointF PenTool::constrained(const QPointF& from, const QPointF& to)
{
    const QPointF delta = to - from;
    const double length = std::hypot(delta.x(), delta.y());
    if (length <= 1.0e-9)
        return to;

    const double step = kPi / 4.0;
    const double angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
    const QPointF direction(std::cos(angle), std::sin(angle));

    // The point's shadow on the chosen ray, so it stays under the cursor
    // along that direction rather than keeping its distance.
    const double along = delta.x() * direction.x() + delta.y() * direction.y();
    return from + direction * along;
}

QPointF PenTool::snapped(PhoenixView& view, const QPointF& documentPos) const
{
    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled())
        return documentPos;

    // The stage, and the path being drawn, which can attract the cursor just
    // as anything already placed can.
    view.gatherSnapCandidates(*snapper, {});
    for (const fla::Anchor& anchor : _path.anchors)
    {
        snapper->addCandidateX(anchor.position.x);
        snapper->addCandidateY(anchor.position.y);
    }

    snapper->setTolerance(view.pickTolerance() * 2.0);
    const fla::SnapResult x = snapper->snapX({documentPos.x()});
    const fla::SnapResult y = snapper->snapY({documentPos.y()});
    return QPointF(documentPos.x() + x.adjustment, documentPos.y() + y.adjustment);
}

QCursor PenTool::cursor() const
{
    Cursors::Kind kind = Cursors::Kind::PenStart;
    if (_active)
        kind = _overFirstAnchor ? Cursors::Kind::PenClose : Cursors::Kind::Pen;
    else if (_hoverResume.valid)
        kind = Cursors::Kind::Pen;
    else if (_hover.grab == EdgeBender::Grab::Corner)
        kind = Cursors::Kind::PenRemove;
    else if (_hover.grab == EdgeBender::Grab::Curve)
        kind = Cursors::Kind::PenAdd;
    return Cursors::cursor(kind);
}

void PenTool::refreshCursor(PhoenixView& view)
{
    int kind = 0;
    if (_active)
        kind = _overFirstAnchor ? 2 : 1;
    else if (_hoverResume.valid)
        kind = 5;
    else if (_hover.grab == EdgeBender::Grab::Corner)
        kind = 3;
    else if (_hover.grab == EdgeBender::Grab::Curve)
        kind = 4;
    if (kind == _cursorKind)
        return;
    _cursorKind = kind;
    view.setCursor(cursor());
}

EdgeBender::Probe PenTool::probeSelection(PhoenixView& view, const QPointF& documentPos) const
{
    EdgeBender::Probe none;

    fla::DOMElement* single = _selection.single();
    fla::Element* element = single ? dynamic_cast<fla::Element*>(single) : nullptr;
    if (!element || element->elementType() != fla::Element::Type::Shape)
        return none;

    HitResult hit;
    hit.element = element;
    hit.elementToDocument = view.elementToDocumentOf(element);
    return EdgeBender::probe(view, hit, documentPos);
}

PenTool::ResumePoint PenTool::resumePointAt(PhoenixView& view, const QPointF& documentPos) const
{
    // The selected shape, or every shape on the frame being drawn on.
    std::vector<fla::Shape*> shapes;
    fla::DOMElement* single = _selection.single();
    fla::Element* element = single ? dynamic_cast<fla::Element*>(single) : nullptr;
    if (element && element->elementType() == fla::Element::Type::Shape)
        shapes.push_back(static_cast<fla::Shape*>(element));
    else if (fla::Frame* frame = view.activeFrame())
        for (fla::Element* candidate : frame->elements)
            if (candidate && candidate->elementType() == fla::Element::Type::Shape)
                shapes.push_back(static_cast<fla::Shape*>(candidate));

    const bool selectedOnly = !shapes.empty() && element == shapes.front();
    const double tolerance = view.pickTolerance() * 1.5;
    ResumePoint best;
    double bestDistance = 0.0;
    for (fla::Shape* shape : shapes)
    {
    const QTransform toDocument = view.elementToDocumentOf(shape);
    // In a shape not selected, only a loose end counts: in merge drawing
    // the edges meet one another at every corner, and those ends are not
    // where a line stops.
    std::vector<fla::Point> ends;
    if (!selectedOnly)
        for (fla::Edge* edge : shape->edges)
            for (fla::Path* path : edge ? edge->paths : std::vector<fla::Path*>())
                if (path)
                {
                    const fla::EditablePath editable = fla::EditablePath::fromPath(*path);
                    if (!editable.closed && !editable.anchors.empty())
                    {
                        ends.push_back(editable.anchors.front().position);
                        ends.push_back(editable.anchors.back().position);
                    }
                }
    const auto loose = [&ends, selectedOnly](const fla::Point& at) {
        if (selectedOnly)
            return true;
        int meeting = 0;
        for (const fla::Point& end : ends)
            meeting += std::hypot(end.x - at.x, end.y - at.y) < 0.05 ? 1 : 0;
        return meeting <= 1;
    };
    for (fla::Edge* edge : shape->edges)
    {
        if (!edge)
            continue;
        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            if (!edge->paths[pathIndex])
                continue;
            const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[pathIndex]);
            if (path.closed || path.anchors.size() < 2)
                continue;

            for (int end = 0; end < 2; ++end)
            {
                const fla::Anchor& anchor = end == 0 ? path.anchors.front() : path.anchors.back();
                if (!loose(anchor.position))
                    continue;
                const QPointF at = toDocument.map(toQPointF(anchor.position));
                const double distance = std::hypot(at.x() - documentPos.x(), at.y() - documentPos.y());
                if (distance > tolerance || (best.valid && distance >= bestDistance))
                    continue;
                best.valid = true;
                best.shape = shape;
                best.edge = edge;
                best.pathIndex = pathIndex;
                best.atStart = end == 0;
                bestDistance = distance;
            }
        }
    }
    }
    return best;
}

bool PenTool::joinAt(PhoenixView& view, const ResumePoint& target)
{
    if (!target.valid || _resuming || _path.anchors.empty() || target.pathIndex >= target.edge->paths.size())
        return false;
    // The other path, on the stage, starting from the end clicked.
    const QTransform toDocument = view.elementToDocumentOf(target.shape);
    const fla::EditablePath before = fla::EditablePath::fromPath(*target.edge->paths[target.pathIndex]);
    fla::EditablePath other = mapped(before, toDocument);
    if (!target.atStart)
        other = reversed(other);
    // The path drawn, then on into the other: the click is the other's end,
    // which takes the curve the drawn path arrives with.
    fla::EditablePath joined = _path;
    fla::Anchor meeting = other.anchors.front();
    meeting.inHandle = meeting.position;
    joined.anchors.push_back(meeting);
    for (size_t i = 1; i < other.anchors.size(); ++i)
        joined.anchors.push_back(other.anchors[i]);
    joined.closed = false;

    bool invertible = false;
    const QTransform toShape = toDocument.inverted(&invertible);
    if (!invertible)
        return false;
    _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
        target.edge, before, mapped(joined, toShape), "Join Paths", target.pathIndex)));
    _commandStack.breakMergeChain();
    view.markShapeEdited(target.shape);
    view.clearCaches();
    cancel(view);
    return true;
}

void PenTool::beginResume(PhoenixView& view, const ResumePoint& point)
{
    _resumeToDocument = view.elementToDocumentOf(point.shape);
    _resumeBefore = fla::EditablePath::fromPath(*point.edge->paths[point.pathIndex]);

    // The pen works in document space and always adds at the end, so the
    // path is brought to the stage and, when picked up by its first anchor,
    // turned round.
    fla::EditablePath path = mapped(_resumeBefore, _resumeToDocument);
    if (point.atStart)
        path = reversed(path);
    path.closed = false;

    _path = path;
    _resuming = true;
    _resumeShape = point.shape;
    _resumeEdge = point.edge;
    _resumePathIndex = point.pathIndex;
    _active = true;
    _draggingHandle = false;
    _closing = false;
    _outOnly = false;
    _overFirstAnchor = false;
    _hoverResume = ResumePoint();
    _cursor = toQPointF(_path.anchors.back().position);
    refreshCursor(view);
    view.update();
}

bool PenTool::editSelectedShape(PhoenixView& view, const QPointF& documentPos)
{
    const EdgeBender::Probe probe = probeSelection(view, documentPos);
    if (probe.grab == EdgeBender::Grab::None || !probe.edge ||
        probe.pathIndex >= probe.edge->paths.size() || !probe.edge->paths[probe.pathIndex])
    {
        return false;
    }

    const fla::EditablePath before = fla::EditablePath::fromPath(*probe.edge->paths[probe.pathIndex]);
    fla::EditablePath after = before;
    const char* gesture = nullptr;

    if (probe.grab == EdgeBender::Grab::Corner)
    {
        constexpr double kHalfTwip = 1.0 / 40.0;
        bool removed = false;
        for (size_t i = 0; i < before.anchors.size() && !removed; ++i)
        {
            const fla::Point& at = before.anchors[i].position;
            if (std::fabs(at.x - probe.position.x) <= kHalfTwip &&
                std::fabs(at.y - probe.position.y) <= kHalfTwip)
            {
                removed = after.removeAnchor(i);
            }
        }
        if (!removed)
            return false;
        gesture = "Delete Anchor";
    }
    else
    {
        if (after.splitSegment(probe.segment, probe.t) < 0)
            return false;
        gesture = "Add Anchor";
    }

    _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
        probe.edge, before, after, gesture, probe.pathIndex)));
    _commandStack.breakMergeChain();

    if (probe.shape)
        view.markShapeEdited(probe.shape);

    view.clearCaches();
    _hover = probeSelection(view, documentPos);
    refreshCursor(view);
    view.update();
    return true;
}

bool PenTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (!view.activeFrame())
        return false;

    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;

    if (!_active)
    {
        // Before a path starts, a press on an end of the selected shape's
        // open path carries that path on, and a press elsewhere on its
        // outline edits it, rather than beginning a new path.
        const ResumePoint resume = resumePointAt(view, documentPos);
        if (resume.valid)
        {
            beginResume(view, resume);
            return true;
        }
        if (editSelectedShape(view, documentPos))
            return true;
    }

    // Ctrl-click ends the open path being drawn.
    if (_active && (event->modifiers() & Qt::ControlModifier))
    {
        finish(view, false);
        return true;
    }

    // Ending on an end of another open path joins the two.
    if (_active && !_resuming && _path.anchors.size() >= 1)
    {
        const ResumePoint target = resumePointAt(view, documentPos);
        if (target.valid && joinAt(view, target))
            return true;
    }

    QPointF position = snapped(view, documentPos);
    if (shift && _active && !_path.anchors.empty())
        position = constrained(toQPointF(_path.anchors.back().position), position);

    if (_active && _path.anchors.size() >= 2)
    {
        // Pressing on the first point closes the path on release; the drag
        // in between shapes the closing curve.
        const QPointF first = toQPointF(_path.anchors.front().position);
        if (std::hypot(documentPos.x() - first.x(), documentPos.y() - first.y())
                <= view.pickTolerance() * 2.0)
        {
            _closing = true;
            _draggingHandle = true;
            _dragIndex = 0;
            _outOnly = false;
            _cursor = first;
            refreshCursor(view);
            view.update();
            return true;
        }
    }

    if (_active && !_path.anchors.empty())
    {
        // Pressing on the last point again retracts its outgoing handle, so
        // the next segment leaves it as a corner; dragging on from there
        // pulls a fresh outgoing handle alone.
        fla::Anchor& last = _path.anchors.back();
        const QPointF at = toQPointF(last.position);
        if (std::hypot(documentPos.x() - at.x(), documentPos.y() - at.y())
                <= view.pickTolerance() * 2.0)
        {
            last.outHandle = last.position;
            last.smooth = false;
            _draggingHandle = true;
            _dragIndex = _path.anchors.size() - 1;
            _outOnly = true;
            _cursor = at;
            refreshCursor(view);
            view.update();
            return true;
        }
    }

    _active = true;
    _path.anchors.push_back(fla::Anchor(toPoint(position)));
    _draggingHandle = true;
    _dragIndex = _path.anchors.size() - 1;
    _outOnly = false;
    _cursor = position;
    _overFirstAnchor = false;

    refreshCursor(view);
    view.update();
    return true;
}

bool PenTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_active)
    {
        _hoverResume = resumePointAt(view, documentPos);
        _hover = _hoverResume.valid ? EdgeBender::Probe() : probeSelection(view, documentPos);
        refreshCursor(view);
        return false;
    }

    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool alt = (event->modifiers() & Qt::AltModifier) != 0;

    _cursor = documentPos;
    if (shift && !_draggingHandle && !_path.anchors.empty())
        _cursor = constrained(toQPointF(_path.anchors.back().position), documentPos);

    if (_draggingHandle && _dragIndex < _path.anchors.size())
    {
        fla::Anchor& anchor = _path.anchors[_dragIndex];
        QPointF handle = documentPos;
        if (shift)
            handle = constrained(toQPointF(anchor.position), documentPos);

        if (_closing)
        {
            // Dragging out of the first anchor shapes the curve arriving to
            // close the path: the in-handle is the mirror of the drag. Without
            // alt the out-handle follows too, so the path leaves and arrives
            // smoothly; alt leaves the first segment as it was.
            anchor.inHandle = fla::Point(2.0 * anchor.position.x - handle.x(),
                                         2.0 * anchor.position.y - handle.y());
            if (!alt)
            {
                anchor.smooth = true;
                anchor.outHandle = toPoint(handle);
            }
            else
            {
                anchor.smooth = false;
            }
        }
        else if (_outOnly)
        {
            anchor.outHandle = toPoint(handle);
            anchor.smooth = false;
        }
        else
        {
            // Dragging out of a fresh point makes it smooth, with the in-handle
            // the exact mirror of the out-handle so the curve arriving is as
            // round as the one leaving. Alt freezes the in-handle where it is
            // and lets the out-handle go its own way, which is how a corner
            // with one curved side is drawn.
            anchor.outHandle = toPoint(handle);
            if (!alt)
            {
                anchor.smooth = true;
                anchor.inHandle = fla::Point(2.0 * anchor.position.x - anchor.outHandle.x,
                                             2.0 * anchor.position.y - anchor.outHandle.y);
            }
            else
            {
                anchor.smooth = false;
            }
        }
    }
    else if (_path.anchors.size() >= 2)
    {
        const QPointF first = toQPointF(_path.anchors.front().position);
        _overFirstAnchor = std::hypot(documentPos.x() - first.x(),
            documentPos.y() - first.y()) <= view.pickTolerance() * 2.0;
    }

    refreshCursor(view);
    view.update();
    return true;
}

bool PenTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;

    if (event->button() != Qt::LeftButton || !_active)
        return false;

    _draggingHandle = false;
    _outOnly = false;
    if (_closing)
    {
        _closing = false;
        finish(view, true);
        return true;
    }
    view.update();
    return true;
}

bool PenTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;

    if (event->button() != Qt::LeftButton || !_active)
        return false;

    if (_path.anchors.size() >= 2)
    {
        const fla::Point& last = _path.anchors.back().position;
        const fla::Point& previous = _path.anchors[_path.anchors.size() - 2].position;
        if (std::hypot(last.x - previous.x, last.y - previous.y) <= view.pickTolerance())
            _path.anchors.pop_back();
    }

    _draggingHandle = false;
    _closing = false;
    finish(view, false);
    return true;
}

bool PenTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_active)
        return false;

    if (event->key() == Qt::Key_Escape)
    {
        cancel(view);
        return true;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        finish(view, false);
        return true;
    }

    return false;
}

void PenTool::finish(PhoenixView& view, bool close)
{
    // One anchor is a click, not a path.
    if (_path.anchors.size() < 2)
    {
        cancel(view);
        return;
    }

    _path.closed = close;

    if (_resuming)
    {
        // The continued path goes back where it came from, in the shape's
        // own space, as one edit of that edge.
        bool invertible = false;
        const QTransform toShape = _resumeToDocument.inverted(&invertible);
        if (invertible && _resumeEdge && _resumePathIndex < _resumeEdge->paths.size())
        {
            const fla::EditablePath after = mapped(_path, toShape);
            _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
                _resumeEdge, _resumeBefore, after, "Pen", _resumePathIndex)));
            _commandStack.breakMergeChain();
            if (_resumeShape)
                view.markShapeEdited(_resumeShape);
            view.clearCaches();
        }
        cancel(view);
        return;
    }

    fla::Frame* frame = view.activeFrame();
    if (!frame)
    {
        cancel(view);
        return;
    }

    fla::Element* shape = createShape(frame);
    if (!shape)
    {
        cancel(view);
        return;
    }

    _path = fla::EditablePath();
    _active = false;
    _draggingHandle = false;
    _closing = false;
    _outOnly = false;
    _overFirstAnchor = false;
    refreshCursor(view);

    placeDrawnElement(view, _commandStack, _selection, frame, shape, "Pen",
        _style.objectDrawing);
}

void PenTool::cancel(PhoenixView& view)
{
    _path = fla::EditablePath();
    _active = false;
    _draggingHandle = false;
    _closing = false;
    _outOnly = false;
    _overFirstAnchor = false;
    _resuming = false;
    _resumeShape = nullptr;
    _resumeEdge = nullptr;
    _resumePathIndex = 0;
    _resumeBefore = fla::EditablePath();
    _hoverResume = ResumePoint();
    refreshCursor(view);
    view.update();
}

fla::Element* PenTool::createShape(fla::Frame* frame) const
{
    fla::Shape* shape = new fla::Shape(frame);

    // Only a closed outline encloses an area, so an open path is stroke only
    // however the fill is set.
    int fillIndex = -1;
    if (_path.closed)
    {
        if (fla::FillStyle* fill = _style.createFill(shape))
        {
            shape->fills.push_back(fill);
            shape->fillsMap[kStyleIndex] = fill;
            fillIndex = kStyleIndex;
        }
    }

    fla::StrokeStyle* stroke = _style.createStroke(shape);
    if (!stroke && fillIndex == -1)
    {
        // With no fill and no stroke the path would be invisible, so fall back
        // to a hairline rather than adding nothing to the document.
        fla::SolidStroke* fallback = new fla::SolidStroke(shape);
        fallback->weight = 1.0;
        fallback->fill = new fla::SolidColor(fallback);
        stroke = fallback;
    }

    int strokeIndex = -1;
    if (stroke)
    {
        shape->strokes.push_back(stroke);
        shape->strokesMap[kStyleIndex] = stroke;
        strokeIndex = kStyleIndex;
    }

    fla::Edge* edge = new fla::Edge(shape);
    edge->strokeStyle = strokeIndex;

    // Which side the fill sits on depends on which way round the outline was
    // drawn: fillStyle1 is the left of the edge's direction, which is the
    // inside of a path drawn clockwise on screen. A path drawn the other way
    // round puts its inside on fillStyle0, as the merge expects.
    double area = 0.0;
    for (size_t i = 0; i < _path.anchors.size(); ++i)
    {
        const fla::Point& a = _path.anchors[i].position;
        const fla::Point& b = _path.anchors[(i + 1) % _path.anchors.size()].position;
        area += a.x * b.y - b.x * a.y;
    }
    if (area >= 0.0)
        edge->fillStyle1 = fillIndex;
    else
        edge->fillStyle0 = fillIndex;

    _path.applyTo(*edge);
    shape->edges.push_back(edge);

    const QRectF bounds = previewPath(false).boundingRect();
    shape->localBounds = fla::Rect(
        fla::Point(bounds.left(), bounds.top()),
        fla::Point(bounds.right(), bounds.bottom()));
    shape->bounds = shape->localBounds;

    return shape;
}

void PenTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_active || _path.anchors.empty())
        return;

    // The path so far, including the rubber band to the cursor.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 170, 255), 1.0 * scale));
    painter.drawPath(previewPath(Preferences::penPreview()));

    const double anchorSize = 3.0 * scale;
    const double handleSize = 2.5 * scale;

    for (size_t i = 0; i < _path.anchors.size(); ++i)
    {
        const fla::Anchor& anchor = _path.anchors[i];
        const QPointF position = toQPointF(anchor.position);

        // Handles, drawn as a tangent line with a round grip at each end.
        painter.setPen(QPen(QColor(120, 200, 255), 1.0 * scale));
        if (anchor.hasInCurve())
        {
            painter.drawLine(position, toQPointF(anchor.inHandle));
            painter.setBrush(QColor(120, 200, 255));
            painter.drawEllipse(toQPointF(anchor.inHandle), handleSize, handleSize);
        }
        if (anchor.hasOutCurve())
        {
            painter.drawLine(position, toQPointF(anchor.outHandle));
            painter.setBrush(QColor(120, 200, 255));
            painter.drawEllipse(toQPointF(anchor.outHandle), handleSize, handleSize);
        }

        // The first anchor is highlighted once clicking it would close the path.
        const bool closeTarget = (i == 0) && (_overFirstAnchor || _closing) && _path.anchors.size() >= 2;
        painter.setPen(QPen(QColor(0, 120, 200), 1.0 * scale));
        painter.setBrush(closeTarget ? QColor(255, 220, 0) : Preferences::solidPoints() ? QColor(0, 120, 200) : QColor(255, 255, 255));
        painter.drawRect(QRectF(position.x() - anchorSize, position.y() - anchorSize,
            anchorSize * 2.0, anchorSize * 2.0));
    }
}

void PenTool::deactivate(PhoenixView& view)
{
    // Choosing another tool leaves the path drawn so far as an open path;
    // a lone anchor is dropped.
    if (_active)
        finish(view, false);
}
