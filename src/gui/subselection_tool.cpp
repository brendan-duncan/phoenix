#include "subselection_tool.h"

#include "phoenix_view.h"

#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>

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
            view.update();
            return true;
        }

        _selection.clear();
        view.update();
        return true;
    }

    bool invertible = false;
    const QTransform toShape = shapeToDocument(view).inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF localPos = toShape.map(documentPos);
    const double tolerance = view.pickTolerance() * 1.5;

    // Handles are checked before anchors: a handle pulled hard back toward its
    // anchor would otherwise be impossible to grab.
    for (fla::Edge* edge : shape->edges)
    {
        if (!edge || edge->paths.empty() || !edge->paths[0])
            continue;

        const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[0]);

        for (size_t i = 0; i < path.anchors.size(); ++i)
        {
            const fla::Anchor& anchor = path.anchors[i];

            const auto grab = [&](const fla::Point& point) {
                return std::hypot(point.x - localPos.x(), point.y - localPos.y()) <= tolerance;
            };

            Grip grip = Grip::None;
            if (anchor.hasInCurve() && grab(anchor.inHandle))
                grip = Grip::InHandle;
            else if (anchor.hasOutCurve() && grab(anchor.outHandle))
                grip = Grip::OutHandle;
            else if (grab(anchor.position))
                grip = Grip::Anchor;

            if (grip == Grip::None)
                continue;

            _edge = edge;
            _path = path;
            _pathAtDragStart = path;
            _grip = grip;
            _anchorIndex = i;
            _dragStart = localPos;
            return true;
        }
    }

    // Clicking off the anchors picks whatever is under the cursor instead.
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (hit)
        _selection.select(hit.element);
    else
        _selection.clear();

    view.update();
    return true;
}

bool SubselectionTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (_grip == Grip::None || !_edge)
        return false;

    bool invertible = false;
    const QTransform toShape = shapeToDocument(view).inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF localPos = toShape.map(documentPos);

    if (_anchorIndex >= _pathAtDragStart.anchors.size())
        return false;

    // Rebuilt from the geometry the drag started with, so a long drag does not
    // accumulate rounding error.
    _path = _pathAtDragStart;
    fla::Anchor& anchor = _path.anchors[_anchorIndex];

    switch (_grip)
    {
    case Grip::Anchor:
    {
        const QPointF delta = localPos - _dragStart;
        anchor.translate(delta.x(), delta.y());
        break;
    }

    case Grip::InHandle:
        // Alt breaks the tangent for this drag, letting the two sides diverge.
        anchor.smooth = (event->modifiers() & Qt::AltModifier) == 0 && anchor.smooth;
        anchor.setInHandle(toPoint(localPos));
        break;

    case Grip::OutHandle:
        anchor.smooth = (event->modifiers() & Qt::AltModifier) == 0 && anchor.smooth;
        anchor.setOutHandle(toPoint(localPos));
        break;

    case Grip::None:
        return false;
    }

    _path.applyTo(*_edge);

    // The shape's own path geometry changed, so its cached paths are stale.
    view.clearCaches();
    view.update();
    return true;
}

bool SubselectionTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;

    if (event->button() != Qt::LeftButton || _grip == Grip::None || !_edge)
        return false;

    const char* gesture = _grip == Grip::Anchor ? "Move Anchor" : "Move Handle";

    _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
        _edge, _pathAtDragStart, _path, gesture)));
    _commandStack.breakMergeChain();

    _edge = nullptr;
    _grip = Grip::None;
    view.update();
    return true;
}

bool SubselectionTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape)
        return false;

    if (_grip != Grip::None)
    {
        cancelDrag(view);
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

void SubselectionTool::cancelDrag(PhoenixView& view)
{
    if (_edge)
        _pathAtDragStart.applyTo(*_edge);

    _edge = nullptr;
    _grip = Grip::None;
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
        if (!edge || edge->paths.empty() || !edge->paths[0])
            continue;

        const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[0]);

        for (const fla::Anchor& anchor : path.anchors)
        {
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

            // Smooth points are drawn round and corners square, the way every
            // pen tool distinguishes them.
            painter.setPen(QPen(QColor(0, 120, 200), 1.0 * scale));
            painter.setBrush(QColor(255, 255, 255));
            if (anchor.smooth)
                painter.drawEllipse(position, anchorSize, anchorSize);
            else
                painter.drawRect(QRectF(position.x() - anchorSize, position.y() - anchorSize,
                    anchorSize * 2.0, anchorSize * 2.0));
        }
    }

    painter.restore();
}

void SubselectionTool::deactivate(PhoenixView& view)
{
    if (_grip != Grip::None)
        cancelDrag(view);
}
