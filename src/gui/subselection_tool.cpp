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

            _grabbed.clear();
            if (grip == Grip::Anchor)
            {
                // Every anchor at this point comes along, or the pieces of
                // outline meeting here come apart.
                grabCoincidentAnchors(*shape, anchor.position);
            }
            else
            {
                // A handle belongs to one curve, so only that one moves.
                _grabbed.push_back({edge, i, path, path});
            }

            _grip = grip;
            _dragStart = localPos;

            // Remembered so Delete knows which anchor to remove.
            _selectedEdge = edge;
            _selectedAnchor = i;
            _hasSelectedAnchor = true;

            view.update();
            return true;
        }
    }

    // Alt-clicking the outline inserts an anchor there, which is how a path
    // gains detail without redrawing it.
    if ((event->modifiers() & Qt::AltModifier) != 0)
    {
        for (fla::Edge* edge : shape->edges)
        {
            if (!edge || edge->paths.empty() || !edge->paths[0])
                continue;

            const fla::EditablePath before = fla::EditablePath::fromPath(*edge->paths[0]);
            const fla::EditablePath::PathPoint found = before.closestPoint(toPoint(localPos));
            if (!found.valid || found.distance > tolerance * 2.0)
                continue;

            fla::EditablePath after = before;
            const int inserted = after.splitSegment(found.segment, found.t);
            if (inserted < 0)
                continue;

            commitGeometry(view, edge, before, after, "Add Anchor");
            _selectedEdge = edge;
            _selectedAnchor = static_cast<size_t>(inserted);
            _hasSelectedAnchor = true;
            return true;
        }
    }

    // Clicking off the anchors picks whatever is under the cursor instead.
    _hasSelectedAnchor = false;
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
    if (_grip == Grip::None || _grabbed.empty())
        return false;

    bool invertible = false;
    const QTransform toShape = shapeToDocument(view).inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF localPos = toShape.map(documentPos);
    const QPointF delta = localPos - _dragStart;

    for (Grabbed& grabbed : _grabbed)
    {
        if (!grabbed.edge || grabbed.anchorIndex >= grabbed.before.anchors.size())
            continue;

        // Rebuilt from the geometry the drag started with, so a long drag does
        // not accumulate rounding error.
        grabbed.current = grabbed.before;
        fla::Anchor& anchor = grabbed.current.anchors[grabbed.anchorIndex];

        switch (_grip)
        {
        case Grip::Anchor:
            anchor.translate(delta.x(), delta.y());
            break;

        case Grip::InHandle:
            // Alt breaks the tangent for this drag, letting the two sides
            // diverge.
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

        grabbed.current.applyTo(*grabbed.edge);
    }

    // The shape's own path geometry changed, so its cached paths are stale.
    view.clearCaches();
    view.update();
    return true;
}

bool SubselectionTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;

    if (event->button() != Qt::LeftButton || _grip == Grip::None || _grabbed.empty())
        return false;

    const char* gesture = _grip == Grip::Anchor ? "Move Anchor" : "Move Handle";

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
            grabbed.edge, grabbed.before, grabbed.current, gesture)));
    }

    if (needsMacro)
        _commandStack.endMacro();

    _commandStack.breakMergeChain();

    _grabbed.clear();
    _grip = Grip::None;
    view.update();
    return true;
}

bool SubselectionTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        _hasSelectedAnchor && _selectedEdge && _grip == Grip::None)
    {
        if (_selectedEdge->paths.empty() || !_selectedEdge->paths[0])
            return false;

        const fla::EditablePath before =
            fla::EditablePath::fromPath(*_selectedEdge->paths[0]);

        fla::EditablePath after = before;
        if (!after.removeAnchor(_selectedAnchor))
            return false;

        commitGeometry(view, _selectedEdge, before, after, "Delete Anchor");
        _hasSelectedAnchor = false;
        return true;
    }

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

bool SubselectionTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event,
    const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    fla::Edge* edge = nullptr;
    fla::EditablePath before;
    size_t index = 0;

    bool invertible = false;
    const QTransform toShape = shapeToDocument(view).inverted(&invertible);
    if (!invertible)
        return false;

    if (!anchorAt(view, toShape.map(documentPos), edge, before, index))
        return false;

    // Double-clicking an anchor flips it between smooth and corner, which is the
    // convert gesture every pen tool has.
    fla::EditablePath after = before;
    if (!after.toggleAnchorSmooth(index))
        return false;

    commitGeometry(view, edge, before, after, "Convert Anchor");
    _selectedEdge = edge;
    _selectedAnchor = index;
    _hasSelectedAnchor = true;
    return true;
}

bool SubselectionTool::anchorAt(PhoenixView& view, const QPointF& localPos,
    fla::Edge*& edge, fla::EditablePath& path, size_t& index) const
{
    const fla::Shape* shape = editedShape();
    if (!shape)
        return false;

    const double tolerance = view.pickTolerance() * 1.5;

    for (fla::Edge* candidate : shape->edges)
    {
        if (!candidate || candidate->paths.empty() || !candidate->paths[0])
            continue;

        const fla::EditablePath candidatePath =
            fla::EditablePath::fromPath(*candidate->paths[0]);

        for (size_t i = 0; i < candidatePath.anchors.size(); ++i)
        {
            const fla::Point& position = candidatePath.anchors[i].position;
            if (std::hypot(position.x - localPos.x(), position.y - localPos.y()) <= tolerance)
            {
                edge = candidate;
                path = candidatePath;
                index = i;
                return true;
            }
        }
    }

    return false;
}

void SubselectionTool::commitGeometry(PhoenixView& view, fla::Edge* edge,
    const fla::EditablePath& before, const fla::EditablePath& after,
    const char* gesture)
{
    after.applyTo(*edge);

    _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
        edge, before, after, gesture)));
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
            grabbed.before.applyTo(*grabbed.edge);
    }

    _grabbed.clear();
    _grip = Grip::None;
    view.clearCaches();
    view.update();
}

void SubselectionTool::grabCoincidentAnchors(fla::Shape& shape, const fla::Point& position)
{
    // The geometry is in pixels and the format stores twips, so anchors that
    // are meant to be the same point agree to well inside half a twip. Anything
    // further apart is a different corner, and Flash could not tell them apart
    // either.
    constexpr double kHalfTwip = 1.0 / 40.0;

    for (fla::Edge* edge : shape.edges)
    {
        if (!edge || edge->paths.empty() || !edge->paths[0])
            continue;

        const fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[0]);

        for (size_t i = 0; i < path.anchors.size(); ++i)
        {
            const fla::Point& at = path.anchors[i].position;
            if (std::fabs(at.x - position.x) > kHalfTwip ||
                std::fabs(at.y - position.y) > kHalfTwip)
            {
                continue;
            }

            _grabbed.push_back({edge, i, path, path});
        }
    }
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
            const bool picked = _hasSelectedAnchor && edge == _selectedEdge &&
                &anchor == &path.anchors[qMin(_selectedAnchor, path.anchors.size() - 1)];

            painter.setPen(QPen(QColor(0, 120, 200), 1.0 * scale));
            painter.setBrush(picked ? QColor(255, 220, 0) : QColor(255, 255, 255));
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
