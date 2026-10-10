#include "subselection_tool.h"

#include "phoenix_view.h"

#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {

QPointF toQPointF(const fla::Point& point)
{
    return QPointF(point.x, point.y);
}

fla::Point toPoint(const QPointF& point)
{
    return fla::Point(point.x(), point.y());
}

/// The geometry is in pixels and the format stores twips, so anchors that
/// are meant to be the same point agree to well inside half a twip. Anything
/// further apart is a different corner, and Flash could not tell them apart
/// either.
constexpr double kHalfTwip = 1.0 / 40.0;

bool samePlace(const fla::Point& a, const fla::Point& b)
{
    return std::fabs(a.x - b.x) <= kHalfTwip && std::fabs(a.y - b.y) <= kHalfTwip;
}

} // namespace

fla::Shape* SubselectionTool::editedShape() const
{
    // Only a single shape shows its anchors: a mixed or multiple selection has
    // no one path to edit.
    fla::DOMElement* single = _selection.single();
    if (!single)
        return nullptr;

    fla::Element* element = dynamic_cast<fla::Element*>(single);
    if (!element || element->elementType() != fla::Element::Type::Shape)
        return nullptr;

    return static_cast<fla::Shape*>(element);
}

QTransform SubselectionTool::shapeToDocument(PhoenixView& view) const
{
    (void)view;

    const fla::Shape* shape = editedShape();
    if (!shape)
        return QTransform();

    const fla::Transform& t = shape->transform;
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

bool SubselectionTool::isSelected(const AnchorRef& ref) const
{
    return std::find(_selected.begin(), _selected.end(), ref) != _selected.end();
}

void SubselectionTool::select(const AnchorRef& ref, bool add)
{
    if (!add)
    {
        _selected.clear();
        _selected.push_back(ref);
        return;
    }

    // Shift on a selected anchor takes it out again.
    const auto found = std::find(_selected.begin(), _selected.end(), ref);
    if (found != _selected.end())
        _selected.erase(found);
    else
        _selected.push_back(ref);
}

void SubselectionTool::anchorsAt(fla::Shape& shape, const fla::Point& position,
    std::vector<AnchorRef>& refs) const
{
    for (fla::Edge* edge : shape.edges)
    {
        if (!edge)
            continue;

        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            if (!edge->paths[pathIndex])
                continue;

            const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[pathIndex]);
            for (size_t i = 0; i < path.anchors.size(); ++i)
            {
                if (!samePlace(path.anchors[i].position, position))
                    continue;
                const AnchorRef ref{edge, pathIndex, i};
                if (std::find(refs.begin(), refs.end(), ref) == refs.end())
                    refs.push_back(ref);
            }
        }
    }
}

bool SubselectionTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    fla::Shape* shape = editedShape();
    if (!shape)
    {
        // Nothing is showing anchors yet, so this click is an ordinary pick.
        const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
        if (hit)
        {
            _selection.select(hit.element);
            _selected.clear();
            view.update();
            return true;
        }

        _selection.clear();
        _selected.clear();
        view.update();
        return true;
    }

    bool invertible = false;
    const QTransform toShape = shapeToDocument(view).inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF localPos = toShape.map(documentPos);
    const double tolerance = localTolerance(view);
    const bool alt = (event->modifiers() & Qt::AltModifier) != 0;
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;

    _dragMoved = false;
    _convertDrag = false;

    // Of every anchor and handle within reach, the nearest wins, and a
    // handle only beats its own anchor when it is nearer than the anchor
    // is: an earlier anchor must not shadow a later handle that is closer,
    // and a handle pulled hard back onto its anchor stays grabbable.
    struct Candidate
    {
        AnchorRef ref;
        fla::EditablePath path;
        Grip grip = Grip::None;
        double distance = 0.0;
    };
    Candidate best;
    bool haveBest = false;

    for (fla::Edge* edge : shape->edges)
    {
        if (!edge)
            continue;

        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            if (!edge->paths[pathIndex])
                continue;

            const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[pathIndex]);

            for (size_t i = 0; i < path.anchors.size(); ++i)
            {
                const fla::Anchor& anchor = path.anchors[i];
                const auto distanceTo = [&](const fla::Point& point) {
                    return std::hypot(point.x - localPos.x(), point.y - localPos.y());
                };

                const double anchorDistance = distanceTo(anchor.position);
                double nearest = anchorDistance;
                Grip grip = anchorDistance <= tolerance ? Grip::Anchor : Grip::None;
                if (anchor.hasInCurve())
                {
                    const double d = distanceTo(anchor.inHandle);
                    if (d <= tolerance && d < nearest)
                    {
                        nearest = d;
                        grip = Grip::InHandle;
                    }
                }
                if (anchor.hasOutCurve())
                {
                    const double d = distanceTo(anchor.outHandle);
                    if (d <= tolerance && d < nearest)
                    {
                        nearest = d;
                        grip = Grip::OutHandle;
                    }
                }
                if (grip == Grip::None)
                    continue;
                if (!haveBest || nearest < best.distance)
                {
                    best.ref = AnchorRef{edge, pathIndex, i};
                    best.path = path;
                    best.grip = grip;
                    best.distance = nearest;
                    haveBest = true;
                }
            }
        }
    }

    if (haveBest)
    {
        const AnchorRef ref = best.ref;
        const fla::EditablePath path = best.path;
        const fla::Anchor& anchor = path.anchors[ref.anchorIndex];
        Grip grip = best.grip;

        _grabbed.clear();
        if (grip == Grip::Anchor && alt)
        {
            // Alt on an anchor is the convert gesture: dragging a corner
            // point pulls handles out of it, and a click on a curve point
            // makes it a corner.
            select(ref, false);
            if (anchor.smooth || anchor.hasInCurve() || anchor.hasOutCurve())
            {
                fla::EditablePath after = path;
                fla::Anchor& corner = after.anchors[ref.anchorIndex];
                corner.smooth = false;
                corner.inHandle = corner.position;
                corner.outHandle = corner.position;
                commitGeometry(view, ref.edge, path, after, "Convert Anchor", ref.pathIndex);
                return true;
            }
            grip = Grip::OutHandle;
            _convertDrag = true;
            _grabbed.push_back({ref.edge, ref.pathIndex, {ref.anchorIndex}, path, path});
        }
        else if (grip == Grip::Anchor)
        {
            // Shift adds to or takes from the selection; a plain press on an
            // anchor not yet selected selects just it. A press on one that is
            // selected keeps the selection, since the drag moves all of it.
            if (shift)
                select(ref, true);
            else if (!isSelected(ref))
                select(ref, false);
            grabSelectedAnchors(*shape);
        }
        else
        {
            // A handle belongs to one curve, so only that one moves.
            select(ref, false);
            _grabbed.push_back({ref.edge, ref.pathIndex, {ref.anchorIndex}, path, path});
        }

        _grip = grip;
        _dragStart = localPos;
        view.update();
        return true;
    }

    // Off the anchors: on something else it is a pick, on empty stage it
    // starts a marquee. Adding an anchor is the pen's job, as in Animate.
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (hit && hit.element != shape)
    {
        _selection.select(hit.element);
        _selected.clear();
        view.update();
        return true;
    }

    _grip = Grip::Marquee;
    _marqueeStart = documentPos;
    _marqueeEnd = documentPos;
    _marqueeAdds = shift;
    if (!shift)
        _selected.clear();
    view.update();
    return true;
}

void SubselectionTool::grabSelectedAnchors(fla::Shape& shape)
{
    _grabbed.clear();

    // Every anchor sitting on a selected one comes along.
    std::vector<AnchorRef> refs;
    for (const AnchorRef& ref : _selected)
    {
        if (!ref.edge || ref.pathIndex >= ref.edge->paths.size() || !ref.edge->paths[ref.pathIndex])
            continue;
        const fla::EditablePath path = fla::EditablePath::fromPath(*ref.edge->paths[ref.pathIndex]);
        if (ref.anchorIndex >= path.anchors.size())
            continue;
        anchorsAt(shape, path.anchors[ref.anchorIndex].position, refs);
    }

    // Grouped by path, since a path is written as a whole.
    for (const AnchorRef& ref : refs)
    {
        Grabbed* group = nullptr;
        for (Grabbed& candidate : _grabbed)
        {
            if (candidate.edge == ref.edge && candidate.pathIndex == ref.pathIndex)
            {
                group = &candidate;
                break;
            }
        }
        if (!group)
        {
            const fla::EditablePath path = fla::EditablePath::fromPath(*ref.edge->paths[ref.pathIndex]);
            _grabbed.push_back({ref.edge, ref.pathIndex, {}, path, path});
            group = &_grabbed.back();
        }
        group->anchors.push_back(ref.anchorIndex);
    }
}

void SubselectionTool::moveGrabbed(PhoenixView& view, const QPointF& delta)
{
    for (Grabbed& grabbed : _grabbed)
    {
        if (!grabbed.edge)
            continue;

        // Rebuilt from the geometry the drag started with, so a long drag does
        // not accumulate rounding error.
        grabbed.current = grabbed.before;
        for (const size_t index : grabbed.anchors)
        {
            if (index < grabbed.current.anchors.size())
                grabbed.current.anchors[index].translate(delta.x(), delta.y());
        }
        grabbed.current.applyTo(*grabbed.edge, grabbed.pathIndex);
    }

    view.clearCaches();
    view.update();
}

bool SubselectionTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (_grip == Grip::Marquee)
    {
        _marqueeEnd = documentPos;
        _dragMoved = true;
        view.update();
        return true;
    }

    if (_grip == Grip::None || _grabbed.empty())
        return false;

    bool invertible = false;
    const QTransform toShape = shapeToDocument(view).inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF localPos = toShape.map(documentPos);
    const QPointF delta = localPos - _dragStart;

    // A press that has not traveled is still a click, and a click does not
    // edit anything.
    if (!_dragMoved)
    {
        const double threshold = PhoenixView::toleranceInLocalSpace(
            shapeToDocument(view), 3.0 / qMax(view.zoom(), 1.0e-6));
        if (std::hypot(delta.x(), delta.y()) <= threshold)
            return true;
        _dragMoved = true;
    }

    if (_grip == Grip::Anchor)
    {
        moveGrabbed(view, delta);
        return true;
    }

    for (Grabbed& grabbed : _grabbed)
    {
        if (!grabbed.edge || grabbed.anchors.empty() ||
            grabbed.anchors.front() >= grabbed.before.anchors.size())
        {
            continue;
        }

        grabbed.current = grabbed.before;
        fla::Anchor& anchor = grabbed.current.anchors[grabbed.anchors.front()];

        switch (_grip)
        {
        case Grip::InHandle:
            // Alt breaks the tangent for this drag, letting the two sides
            // diverge.
            anchor.smooth = (event->modifiers() & Qt::AltModifier) == 0 && anchor.smooth;
            anchor.setInHandle(toPoint(localPos));
            break;

        case Grip::OutHandle:
            if (_convertDrag)
            {
                // Pulling handles out of a corner: both grow together as exact
                // mirrors, the way the pen pulls them out of a fresh point.
                anchor.smooth = true;
                anchor.outHandle = toPoint(localPos);
                anchor.inHandle = fla::Point(2.0 * anchor.position.x - anchor.outHandle.x,
                                             2.0 * anchor.position.y - anchor.outHandle.y);
                break;
            }
            anchor.smooth = (event->modifiers() & Qt::AltModifier) == 0 && anchor.smooth;
            anchor.setOutHandle(toPoint(localPos));
            break;

        case Grip::Anchor:
        case Grip::Marquee:
        case Grip::None:
            return false;
        }

        grabbed.current.applyTo(*grabbed.edge, grabbed.pathIndex);
    }

    // The shape's own path geometry changed, so its cached paths are stale.
    view.clearCaches();
    view.update();
    return true;
}

void SubselectionTool::commitGrabbed(PhoenixView& view, const char* gesture)
{
    // Moving a shared corner rewrites several edges, which is still one thing
    // the user did and so one thing to undo.
    const bool needsMacro = _grabbed.size() > 1;
    if (needsMacro)
        _commandStack.beginMacro(gesture);

    for (const Grabbed& grabbed : _grabbed)
    {
        if (!grabbed.edge)
            continue;

        _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
            grabbed.edge, grabbed.before, grabbed.current, gesture, grabbed.pathIndex)));
    }

    if (needsMacro)
        _commandStack.endMacro();

    _commandStack.breakMergeChain();

    // A reshaped shape belongs to the artwork it now covers, so letting go of it
    // drops it in the same way a drawn one is.
    if (fla::Shape* shape = editedShape())
        view.markShapeEdited(shape);

    _grabbed.clear();
    view.clearCaches();
    view.update();
}

bool SubselectionTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || _grip == Grip::None)
        return false;

    if (_grip == Grip::Marquee)
    {
        _marqueeEnd = documentPos;
        _grip = Grip::None;

        fla::Shape* shape = editedShape();
        if (shape && _dragMoved)
        {
            // Every anchor inside the marquee, in document space.
            const QRectF marquee = QRectF(_marqueeStart, _marqueeEnd).normalized();
            const QTransform toDocument = shapeToDocument(view);
            for (fla::Edge* edge : shape->edges)
            {
                if (!edge)
                    continue;
                for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
                {
                    if (!edge->paths[pathIndex])
                        continue;
                    const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[pathIndex]);
                    for (size_t i = 0; i < path.anchors.size(); ++i)
                    {
                        if (!marquee.contains(toDocument.map(toQPointF(path.anchors[i].position))))
                            continue;
                        const AnchorRef ref{edge, pathIndex, i};
                        if (!isSelected(ref))
                            _selected.push_back(ref);
                    }
                }
            }
        }
        _dragMoved = false;
        view.update();
        return true;
    }

    if (_grabbed.empty())
        return false;

    if (!_dragMoved)
    {
        // A click: the anchor is selected and nothing else happens.
        _grabbed.clear();
        _grip = Grip::None;
        _convertDrag = false;
        view.update();
        return true;
    }

    const char* gesture = _convertDrag ? "Convert Anchor"
        : _grip == Grip::Anchor ? "Move Anchor" : "Move Handle";
    commitGrabbed(view, gesture);

    _grip = Grip::None;
    _convertDrag = false;
    return true;
}

double SubselectionTool::localTolerance(PhoenixView& view) const
{
    return PhoenixView::toleranceInLocalSpace(shapeToDocument(view), view.pickTolerance() * 1.5);
}

void SubselectionTool::deleteSelected(PhoenixView& view)
{
    fla::Shape* shape = editedShape();
    if (!shape || _selected.empty())
        return;

    // One anchor at the end of an open piece, meeting the end of another
    // open piece with the same styles: the two are one outline that was cut
    // there, so they are joined into one path and the corner refitted away.
    if (_selected.size() == 1)
    {
        const AnchorRef ref = _selected.front();
        if (ref.edge && ref.pathIndex < ref.edge->paths.size() && ref.edge->paths[ref.pathIndex])
        {
            const fla::EditablePath first = fla::EditablePath::fromPath(*ref.edge->paths[ref.pathIndex]);
            const bool atEnd = !first.closed && first.anchors.size() >= 2 &&
                (ref.anchorIndex == 0 || ref.anchorIndex + 1 == first.anchors.size());
            if (atEnd)
            {
                std::vector<AnchorRef> meeting;
                anchorsAt(*shape, first.anchors[ref.anchorIndex].position, meeting);
                meeting.erase(std::remove(meeting.begin(), meeting.end(), ref), meeting.end());

                if (meeting.size() == 1)
                {
                    const AnchorRef other = meeting.front();
                    const fla::EditablePath second =
                        fla::EditablePath::fromPath(*other.edge->paths[other.pathIndex]);
                    const bool otherAtEnd = !second.closed && second.anchors.size() >= 2 &&
                        (other.anchorIndex == 0 || other.anchorIndex + 1 == second.anchors.size());
                    const bool sameStyles = other.edge->fillStyle0 == ref.edge->fillStyle0 &&
                        other.edge->fillStyle1 == ref.edge->fillStyle1 &&
                        other.edge->strokeStyle == ref.edge->strokeStyle &&
                        other.edge->paths[other.pathIndex]->fillStyleIndex == -1 &&
                        other.edge->paths[other.pathIndex]->lineStyleIndex == -1 &&
                        ref.edge->paths[ref.pathIndex]->fillStyleIndex == -1 &&
                        ref.edge->paths[ref.pathIndex]->lineStyleIndex == -1;

                    if (otherAtEnd && sameStyles && !(other.edge == ref.edge && other.pathIndex == ref.pathIndex))
                    {
                        // The first piece arrives at the corner, the second
                        // leaves it: turned round as needed, and joined.
                        fla::EditablePath joined = ref.anchorIndex == 0 ? first.reversed() : first;
                        const fla::EditablePath tail = other.anchorIndex == 0 ? second : second.reversed();
                        const size_t corner = joined.anchors.size() - 1;
                        joined.anchors.back().outHandle = tail.anchors.front().outHandle;
                        for (size_t i = 1; i < tail.anchors.size(); ++i)
                            joined.anchors.push_back(tail.anchors[i]);
                        joined.closed = false;
                        joined.removeAnchor(corner);

                        _commandStack.beginMacro("Delete Anchor");
                        joined.applyTo(*ref.edge, ref.pathIndex);
                        _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
                            ref.edge, first, joined, "Delete Anchor", ref.pathIndex)));
                        fla::EditablePath gone;
                        gone.applyTo(*other.edge, other.pathIndex);
                        _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
                            other.edge, second, gone, "Delete Anchor", other.pathIndex)));
                        _commandStack.endMacro();
                        _commandStack.breakMergeChain();

                        _selected.clear();
                        view.markShapeEdited(shape);
                        view.clearCaches();
                        view.update();
                        return;
                    }
                }
            }
        }
    }

    // Otherwise each path loses its selected anchors, highest index first so
    // the others keep their places.
    grabSelectedAnchors(*shape);
    std::vector<Grabbed> groups = _grabbed;
    _grabbed.clear();

    std::vector<fla::CommandPtr> commands;
    for (Grabbed& group : groups)
    {
        std::vector<size_t> indices = group.anchors;
        std::sort(indices.begin(), indices.end(), std::greater<size_t>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

        fla::EditablePath after = group.before;
        bool changed = false;
        for (const size_t index : indices)
            changed = after.removeAnchor(index) || changed;
        if (!changed)
            continue;

        after.applyTo(*group.edge, group.pathIndex);
        commands.push_back(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
            group.edge, group.before, after, "Delete Anchor", group.pathIndex)));
    }

    if (commands.empty())
        return;

    const bool needsMacro = commands.size() > 1;
    if (needsMacro)
        _commandStack.beginMacro("Delete Anchor");
    for (fla::CommandPtr& command : commands)
        _commandStack.push(std::move(command));
    if (needsMacro)
        _commandStack.endMacro();
    _commandStack.breakMergeChain();

    _selected.clear();
    view.markShapeEdited(shape);
    view.clearCaches();
    view.update();
}

bool SubselectionTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    const int key = event->key();

    if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && !_selected.empty() && _grip == Grip::None)
    {
        deleteSelected(view);
        return true;
    }

    if ((key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) &&
        !_selected.empty() && _grip == Grip::None)
    {
        fla::Shape* shape = editedShape();
        if (!shape)
            return false;

        // A nudge is a screen pixel, ten with shift, in the shape's space.
        const double step = ((event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0) /
            qMax(view.zoom(), 1.0e-6);
        const QPointF documentDelta(key == Qt::Key_Left ? -step : key == Qt::Key_Right ? step : 0.0,
                                    key == Qt::Key_Up ? -step : key == Qt::Key_Down ? step : 0.0);
        bool invertible = false;
        const QTransform toShape = shapeToDocument(view).inverted(&invertible);
        if (!invertible)
            return false;
        const QPointF delta = toShape.map(documentDelta) - toShape.map(QPointF(0.0, 0.0));

        grabSelectedAnchors(*shape);
        moveGrabbed(view, delta);
        commitGrabbed(view, "Nudge Anchor");
        return true;
    }

    if (key != Qt::Key_Escape)
        return false;

    if (_grip != Grip::None)
    {
        cancelDrag(view);
        return true;
    }

    if (!_selected.empty())
    {
        _selected.clear();
        view.update();
        return true;
    }

    if (!_selection.isEmpty())
    {
        _selection.clear();
        view.update();
        return true;
    }

    return false;
}

void SubselectionTool::commitGeometry(PhoenixView& view, fla::Edge* edge,
    const fla::EditablePath& before, const fla::EditablePath& after,
    const char* gesture, size_t pathIndex)
{
    after.applyTo(*edge, pathIndex);

    _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
        edge, before, after, gesture, pathIndex)));
    _commandStack.breakMergeChain();

    // The path geometry changed, so cached paths for this shape are stale.
    view.clearCaches();
    view.update();
}

void SubselectionTool::cancelDrag(PhoenixView& view)
{
    // Every edge the drag touched goes back to what it was.
    for (const Grabbed& grabbed : _grabbed)
    {
        if (grabbed.edge)
            grabbed.before.applyTo(*grabbed.edge, grabbed.pathIndex);
    }

    _grabbed.clear();
    _grip = Grip::None;
    _dragMoved = false;
    view.clearCaches();
    view.update();
}

void SubselectionTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    const fla::Shape* shape = editedShape();
    if (!shape)
        return;

    painter.save();
    painter.setTransform(shapeToDocument(view), true);

    const double anchorSize = 3.0 * scale;
    const double handleSize = 2.5 * scale;

    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge)
            continue;

        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            if (!edge->paths[pathIndex])
                continue;

            const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[pathIndex]);

            for (size_t i = 0; i < path.anchors.size(); ++i)
            {
                const fla::Anchor& anchor = path.anchors[i];
                const QPointF position = toQPointF(anchor.position);

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

                // Every anchor is a small hollow square, filled once it is
                // selected. Animate does not show smooth against corner by shape.
                const bool picked = isSelected(AnchorRef{const_cast<fla::Edge*>(edge), pathIndex, i});

                painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
                painter.setBrush(picked ? QColor(0, 0, 0) : QColor(255, 255, 255));
                painter.drawRect(QRectF(position.x() - anchorSize, position.y() - anchorSize,
                    anchorSize * 2.0, anchorSize * 2.0));
            }
        }
    }

    painter.restore();

    if (_grip == Grip::Marquee && _dragMoved)
    {
        painter.setBrush(QColor(0, 120, 255, 30));
        painter.setPen(QPen(QColor(0, 120, 255), 1.0 * scale, Qt::DashLine));
        painter.drawRect(QRectF(_marqueeStart, _marqueeEnd).normalized());
    }
}

void SubselectionTool::deactivate(PhoenixView& view)
{
    if (_grip != Grip::None && _grip != Grip::Marquee)
        cancelDrag(view);
    _grip = Grip::None;
    _selected.clear();
}
