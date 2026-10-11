#include "anchor_point_tool.h"

#include "phoenix_view.h"

#include "../data/edge.h"
#include "../data/frame.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

QPointF toQ(const fla::Point& p)
{
    return QPointF(p.x, p.y);
}

/// How near an anchor or a path the cursor must be, in screen pixels.
constexpr double kReach = 6.0;

} // namespace

QString AnchorPointTool::name() const
{
    switch (_mode)
    {
    case Mode::Add: return "Add Anchor Point";
    case Mode::Delete: return "Delete Anchor Point";
    case Mode::Convert: return "Convert Anchor Point";
    }
    return "Anchor Point";
}

bool AnchorPointTool::shapeStillThere() const
{
    return _shape && _frame &&
        std::find(_frame->elements.begin(), _frame->elements.end(), _shape) != _frame->elements.end();
}

bool AnchorPointTool::pickShape(PhoenixView& view, const QPointF& documentPos)
{
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (hit && hit.element->elementType() == fla::Element::Type::Shape)
    {
        _shape = static_cast<fla::Shape*>(hit.element);
        _frame = hit.frame;
        _toDocument = hit.elementToDocument;
        return true;
    }
    // Near the shape already showing its anchors, it stays.
    return shapeStillThere();
}

bool AnchorPointTool::anchorNear(const QPointF& local, double tolerance, fla::Edge*& edge, size_t& pathIndex,
    size_t& anchor) const
{
    double best = tolerance;
    bool found = false;
    for (fla::Edge* candidate : _shape->edges)
    {
        if (!candidate)
            continue;
        for (size_t p = 0; p < candidate->paths.size(); ++p)
        {
            if (!candidate->paths[p])
                continue;
            const fla::EditablePath path = fla::EditablePath::fromPath(*candidate->paths[p]);
            for (size_t i = 0; i < path.anchors.size(); ++i)
            {
                const double d = std::hypot(path.anchors[i].position.x - local.x(), path.anchors[i].position.y - local.y());
                if (d <= best)
                {
                    best = d;
                    edge = candidate;
                    pathIndex = p;
                    anchor = i;
                    found = true;
                }
            }
        }
    }
    return found;
}

void AnchorPointTool::commit(PhoenixView& view, fla::Edge* edge, size_t pathIndex, const fla::EditablePath& before,
    const fla::EditablePath& after, const char* name)
{
    after.applyTo(*edge, pathIndex);
    _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(edge, before, after, name, pathIndex)));
    _commandStack.breakMergeChain();
    view.forgetShape(_shape);
}

bool AnchorPointTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    if (!pickShape(view, documentPos))
        return true;
    bool invertible = false;
    const QTransform toLocal = _toDocument.inverted(&invertible);
    if (!invertible)
        return true;
    const QPointF local = toLocal.map(documentPos);
    const double tolerance = PhoenixView::toleranceInLocalSpace(_toDocument, kReach / std::max(view.zoom(), 1.0e-6));

    if (_mode == Mode::Add)
    {
        // The nearest point on any path: a new anchor goes there.
        fla::Edge* bestEdge = nullptr;
        size_t bestPath = 0;
        fla::EditablePath::PathPoint best;
        for (fla::Edge* edge : _shape->edges)
        {
            if (!edge)
                continue;
            for (size_t p = 0; p < edge->paths.size(); ++p)
            {
                if (!edge->paths[p])
                    continue;
                const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[p]);
                const fla::EditablePath::PathPoint point = path.closestPoint(fla::Point(local.x(), local.y()));
                if (point.valid && point.distance <= tolerance && (!best.valid || point.distance < best.distance))
                {
                    best = point;
                    bestEdge = edge;
                    bestPath = p;
                }
            }
        }
        if (bestEdge && best.t > 1.0e-4 && best.t < 1.0 - 1.0e-4)
        {
            const fla::EditablePath before = fla::EditablePath::fromPath(*bestEdge->paths[bestPath]);
            fla::EditablePath after = before;
            if (after.splitSegment(best.segment, best.t) >= 0)
                commit(view, bestEdge, bestPath, before, after, "Add Anchor Point");
        }
        return true;
    }

    fla::Edge* edge = nullptr;
    size_t pathIndex = 0;
    size_t anchor = 0;
    if (!anchorNear(local, tolerance, edge, pathIndex, anchor))
        return true;
    const fla::EditablePath before = fla::EditablePath::fromPath(*edge->paths[pathIndex]);

    if (_mode == Mode::Delete)
    {
        // The ends of an open path are where it meets the rest of the shape;
        // taking them out would open a gap.
        if (!before.closed && (anchor == 0 || anchor + 1 == before.anchors.size()))
            return true;
        fla::EditablePath after = before;
        if (after.removeAnchor(anchor))
            commit(view, edge, pathIndex, before, after, "Delete Anchor Point");
        return true;
    }

    // Convert: a click makes a smooth anchor a corner; a drag pulls handles.
    _converting = true;
    _dragged = false;
    _edge = edge;
    _pathIndex = pathIndex;
    _anchor = anchor;
    _before = before;
    _pressPos = local;
    return true;
}

bool AnchorPointTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (_converting && shapeStillThere())
    {
        bool invertible = false;
        const QPointF local = _toDocument.inverted(&invertible).map(documentPos);
        if (!_dragged && std::hypot(local.x() - _pressPos.x(), local.y() - _pressPos.y()) * view.zoom() < 3.0)
            return true;
        _dragged = true;
        fla::EditablePath after = _before;
        fla::Anchor& anchor = after.anchors[_anchor];
        anchor.smooth = true;
        anchor.outHandle = fla::Point(local.x(), local.y());
        anchor.inHandle = fla::Point(2.0 * anchor.position.x - local.x(), 2.0 * anchor.position.y - local.y());
        after.applyTo(*_edge, _pathIndex);
        view.forgetShape(_shape);
        return true;
    }

    // Hovering: the shape under the cursor shows its anchors, and the add
    // tool marks where an anchor would go.
    const fla::Shape* before = _shape;
    pickShape(view, documentPos);
    _hoverOnPath = false;
    if (_mode == Mode::Add && shapeStillThere())
    {
        bool invertible = false;
        const QPointF local = _toDocument.inverted(&invertible).map(documentPos);
        const double tolerance = kReach / std::max(view.zoom(), 1.0e-6);
        for (fla::Edge* edge : _shape->edges)
        {
            for (size_t p = 0; edge && p < edge->paths.size(); ++p)
            {
                if (!edge->paths[p])
                    continue;
                const fla::EditablePath::PathPoint point =
                    fla::EditablePath::fromPath(*edge->paths[p]).closestPoint(fla::Point(local.x(), local.y()));
                if (point.valid && point.distance <= tolerance)
                {
                    _hoverOnPath = true;
                    _hoverPoint = toQ(point.position);
                }
            }
        }
    }
    if (before != _shape || _mode == Mode::Add)
        view.update();
    return false;
}

bool AnchorPointTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    if (!_converting)
        return false;
    _converting = false;
    if (!shapeStillThere())
        return true;
    fla::EditablePath after = _before;
    if (_dragged)
    {
        // The handles as the drag left them.
        after = fla::EditablePath::fromPath(*_edge->paths[_pathIndex]);
        _before.applyTo(*_edge, _pathIndex);
        commit(view, _edge, _pathIndex, _before, after, "Convert Anchor Point");
        return true;
    }
    // A click: a smooth anchor (or one with handles) becomes a corner.
    fla::Anchor& anchor = after.anchors[_anchor];
    if (!anchor.hasInCurve() && !anchor.hasOutCurve())
        return true;
    anchor.smooth = false;
    anchor.inHandle = anchor.position;
    anchor.outHandle = anchor.position;
    commit(view, _edge, _pathIndex, _before, after, "Convert Anchor Point");
    return true;
}

void AnchorPointTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    if (!shapeStillThere())
        return;
    painter.save();
    painter.setTransform(_toDocument, true);
    const double anchorSize = 3.0 * scale;
    for (const fla::Edge* edge : _shape->edges)
    {
        for (size_t p = 0; edge && p < edge->paths.size(); ++p)
        {
            if (!edge->paths[p])
                continue;
            const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[p]);
            for (const fla::Anchor& anchor : path.anchors)
            {
                const QPointF at = toQ(anchor.position);
                painter.setPen(QPen(QColor(120, 200, 255), 1.0 * scale));
                if (anchor.hasInCurve())
                    painter.drawLine(at, toQ(anchor.inHandle));
                if (anchor.hasOutCurve())
                    painter.drawLine(at, toQ(anchor.outHandle));
                painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
                painter.setBrush(QColor(255, 255, 255));
                painter.drawRect(QRectF(at.x() - anchorSize, at.y() - anchorSize, anchorSize * 2.0, anchorSize * 2.0));
            }
        }
    }
    if (_mode == Mode::Add && _hoverOnPath)
    {
        // Where the new anchor would go.
        painter.setPen(QPen(QColor(41, 128, 238), 1.5 * scale));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(_hoverPoint, 4.0 * scale, 4.0 * scale);
        painter.drawLine(_hoverPoint - QPointF(2.5 * scale, 0), _hoverPoint + QPointF(2.5 * scale, 0));
        painter.drawLine(_hoverPoint - QPointF(0, 2.5 * scale), _hoverPoint + QPointF(0, 2.5 * scale));
    }
    painter.restore();
}

void AnchorPointTool::deactivate(PhoenixView& view)
{
    if (_converting && shapeStillThere())
    {
        _before.applyTo(*_edge, _pathIndex);
        view.forgetShape(_shape);
    }
    _converting = false;
    _shape = nullptr;
    _frame = nullptr;
    _hoverOnPath = false;
    view.update();
}
