#include "free_transform_tool.h"

#include "phoenix_view.h"

#include "../data/element.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/snapping.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>

namespace {

/// Handle and grip sizes, in screen pixels. Converted to document units at use
/// so they stay the same size however far the view is zoomed.
constexpr double kHandleScreenSize = 4.0;
constexpr double kRotateBandScreenSize = 12.0;

QTransform toQTransform(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

fla::Transform fromQTransform(const QTransform& q)
{
    return fla::Transform(q.m11(), q.m12(), q.m21(), q.m22(), q.dx(), q.dy());
}

/// Builds a document-space matrix that leaves \a anchor where it is and applies
/// \a inner around it.
QTransform aroundAnchor(const QPointF& anchor, const QTransform& inner)
{
    // Qt composes left to right, so this reads as: shift the anchor to the
    // origin, do the work, shift back.
    return QTransform::fromTranslate(-anchor.x(), -anchor.y())
        * inner
        * QTransform::fromTranslate(anchor.x(), anchor.y());
}

constexpr double kPi = 3.14159265358979323846;

double distance(const QPointF& a, const QPointF& b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

/// Shortest distance from \a point to the segment \a a -> \a b.
double distanceToSegment(const QPointF& point, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    if (lengthSquared < 1.0e-12)
        return distance(point, a);

    double t = ((point.x() - a.x()) * ab.x() + (point.y() - a.y()) * ab.y()) / lengthSquared;
    t = qBound(0.0, t, 1.0);
    return distance(point, a + ab * t);
}

} // namespace

QPointF FreeTransformTool::centerOf(const QPolygonF& box)
{
    if (box.size() != 4)
        return QPointF();

    return (box[0] + box[1] + box[2] + box[3]) / 4.0;
}

QPointF FreeTransformTool::handlePoint(const QPolygonF& box, int index)
{
    if (box.size() != 4 || index < 0 || index >= kHandleCount)
        return QPointF();

    // Even indices are corners, odd indices the midpoints between them.
    const int corner = index / 2;
    if (index % 2 == 0)
        return box[corner];

    return (box[corner] + box[(corner + 1) % 4]) / 2.0;
}

QPointF FreeTransformTool::anchorFor(const QPolygonF& box, int index)
{
    if (box.size() != 4)
        return QPointF();

    // Scaling holds the opposite handle still, which is what makes dragging a
    // corner feel like pulling the box from that corner.
    return handlePoint(box, (index + 4) % kHandleCount);
}

int FreeTransformTool::handleAt(const QPointF& documentPos, double tolerance) const
{
    for (int i = 0; i < kHandleCount; ++i)
    {
        if (distance(documentPos, handlePoint(_box, i)) <= tolerance)
            return i;
    }
    return -1;
}

int FreeTransformTool::edgeAt(const QPointF& documentPos, double tolerance) const
{
    if (_box.size() != 4)
        return -1;

    for (int i = 0; i < 4; ++i)
    {
        const QPointF a = _box[i];
        const QPointF b = _box[(i + 1) % 4];
        if (distanceToSegment(documentPos, a, b) <= tolerance)
            return i;
    }
    return -1;
}

void FreeTransformTool::rebuildBox(PhoenixView& view)
{
    _box.clear();
    _boxValid = false;

    QRectF bounds;
    bool first = true;

    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element)
            continue;

        const QRectF elementBounds = view.elementBounds(element);
        if (!elementBounds.isValid())
            continue;

        bounds = first ? elementBounds : bounds.united(elementBounds);
        first = false;
    }

    if (first || !bounds.isValid())
        return;

    // Starts axis-aligned; later gestures carry the box with them, so a rotated
    // object keeps a rotated box.
    _box = QPolygonF(bounds);
    if (_box.size() == 5)
        _box.removeLast(); // QPolygonF(QRectF) closes the ring
    _boxValid = _box.size() == 4;
}

QTransform FreeTransformTool::adjustForDrag(const QPointF& documentPos,
    Qt::KeyboardModifiers modifiers) const
{
    switch (_grip)
    {
    case Grip::Body:
    {
        const QPointF delta = documentPos - _dragStart;
        return QTransform::fromTranslate(delta.x(), delta.y());
    }

    case Grip::Handle:
    {
        const QPointF anchor = anchorFor(_boxAtDragStart, _gripIndex);
        const QPointF start = _dragStart - anchor;
        const QPointF now = documentPos - anchor;

        // Scale along each axis by how much further the cursor is from the
        // anchor than the handle started. A near-zero starting offset would
        // divide by nothing, so that axis is left alone.
        double sx = std::fabs(start.x()) > 1.0e-6 ? now.x() / start.x() : 1.0;
        double sy = std::fabs(start.y()) > 1.0e-6 ? now.y() / start.y() : 1.0;

        // An edge handle only has one meaningful axis; the other must not move.
        if (_gripIndex % 2 == 1)
        {
            if (std::fabs(start.x()) <= std::fabs(start.y()))
                sx = 1.0;
            else
                sy = 1.0;
        }

        // Shift keeps the aspect ratio, as everywhere else in Animate.
        if (modifiers & Qt::ShiftModifier)
        {
            const double uniform = (std::fabs(sx) + std::fabs(sy)) * 0.5;
            sx = sx < 0.0 ? -uniform : uniform;
            sy = sy < 0.0 ? -uniform : uniform;
        }

        QTransform scale;
        scale.scale(sx, sy);
        return aroundAnchor(anchor, scale);
    }

    case Grip::Rotate:
    {
        const QPointF center = centerOf(_boxAtDragStart);
        const double startAngle = std::atan2(_dragStart.y() - center.y(), _dragStart.x() - center.x());
        const double nowAngle = std::atan2(documentPos.y() - center.y(), documentPos.x() - center.x());
        double degrees = (nowAngle - startAngle) * 180.0 / kPi;

        // Shift snaps to 45 degree steps.
        if (modifiers & Qt::ShiftModifier)
            degrees = std::round(degrees / 45.0) * 45.0;

        QTransform rotation;
        rotation.rotate(degrees);
        return aroundAnchor(center, rotation);
    }

    case Grip::Skew:
    {
        const QPointF delta = documentPos - _dragStart;

        // The opposite edge stays put, so the anchor is its midpoint.
        const QPointF anchor = handlePoint(_boxAtDragStart,
            ((_gripIndex * 2 + 1) + 4) % kHandleCount);

        const QPointF a = _boxAtDragStart.size() == 4 ? _boxAtDragStart[_gripIndex] : QPointF();
        const QPointF b = _boxAtDragStart.size() == 4
            ? _boxAtDragStart[(_gripIndex + 1) % 4] : QPointF();
        const QPointF edge = b - a;

        // Slide along the edge direction, measured against the box's depth.
        const double depth = qMax(1.0e-6, distance(anchor, (a + b) / 2.0));
        const bool horizontal = std::fabs(edge.x()) >= std::fabs(edge.y());

        QTransform shear;
        if (horizontal)
            shear.shear(delta.x() / depth, 0.0);
        else
            shear.shear(0.0, delta.y() / depth);

        return aroundAnchor(anchor, shear);
    }

    case Grip::None:
        break;
    }

    return QTransform();
}

QPointF FreeTransformTool::snapDragPoint(PhoenixView& view, const QPointF& documentPos) const
{
    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled())
        return documentPos;

    // Rotation is an angle, not a position, so snapping the cursor would fight
    // the shift-key angle steps rather than help.
    if (_grip == Grip::Rotate)
        return documentPos;

    snapper->setTolerance(view.pickTolerance() * 2.0);

    // For a handle drag it is the handle that should land on a grid line or an
    // edge, and the handle follows the cursor, so snapping the cursor snaps it.
    const fla::SnapResult x = snapper->snapX({documentPos.x()});
    const fla::SnapResult y = snapper->snapY({documentPos.y()});

    return QPointF(documentPos.x() + x.adjustment, documentPos.y() + y.adjustment);
}

void FreeTransformTool::applyAdjust(PhoenixView& view, const QTransform& adjust)
{
    for (const Target& target : _targets)
    {
        if (!target.element)
            continue;

        // Always recompute from the starting transform: accumulating each mouse
        // move's delta would drift over a long drag.
        const QTransform start = toQTransform(target.startTransform);
        target.element->transform = fromQTransform(start * adjust);
    }

    // Only where things sit has changed, so the path cache stays warm.
    view.invalidateBounds();
    view.update();
}

bool FreeTransformTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (!_boxValid)
        rebuildBox(view);

    if (!_boxValid)
        return false;

    const double tolerance = view.pickTolerance();
    const double handleTolerance = qMax(tolerance, kHandleScreenSize / qMax(view.zoom(), 1.0e-6));
    const double rotateBand = kRotateBandScreenSize / qMax(view.zoom(), 1.0e-6);

    _grip = Grip::None;
    _gripIndex = -1;

    const int handle = handleAt(documentPos, handleTolerance);
    if (handle >= 0)
    {
        _grip = Grip::Handle;
        _gripIndex = handle;
    }
    else
    {
        const int edge = edgeAt(documentPos, handleTolerance);
        if (edge >= 0)
        {
            _grip = Grip::Skew;
            _gripIndex = edge;
        }
        else if (_box.containsPoint(documentPos, Qt::OddEvenFill))
        {
            _grip = Grip::Body;
        }
        else
        {
            // Just outside a corner is the rotate band, the way Animate does it.
            const int nearCorner = handleAt(documentPos, handleTolerance + rotateBand);
            if (nearCorner >= 0 && nearCorner % 2 == 0)
            {
                _grip = Grip::Rotate;
                _gripIndex = nearCorner;
            }
        }
    }

    if (_grip == Grip::None)
        return false;

    _dragStart = documentPos;
    _boxAtDragStart = _box;

    _targets.clear();
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (element)
            _targets.push_back({element, element->transform});
    }

    if (_targets.empty())
    {
        _grip = Grip::None;
        return false;
    }

    // What this gesture can line up with, gathered once: it does not change
    // while the drag runs.
    if (fla::Snapper* snapper = view.snapper())
    {
        std::vector<fla::Element*> moving;
        moving.reserve(_targets.size());
        for (const Target& target : _targets)
            moving.push_back(target.element);
        view.gatherSnapCandidates(*snapper, moving);
    }

    return true;
}

bool FreeTransformTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (_grip == Grip::None)
        return false;

    const QTransform adjust = adjustForDrag(snapDragPoint(view, documentPos), event->modifiers());
    applyAdjust(view, adjust);

    // Carry the box with the gesture so the handles stay on the object.
    _box = adjust.map(_boxAtDragStart);
    return true;
}

bool FreeTransformTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || _grip == Grip::None)
        return false;

    const QTransform adjust = adjustForDrag(snapDragPoint(view, documentPos), event->modifiers());
    applyAdjust(view, adjust);
    _box = adjust.map(_boxAtDragStart);

    commit(view);
    return true;
}

void FreeTransformTool::commit(PhoenixView& view)
{
    const char* gestureName = "Transform";
    switch (_grip)
    {
    case Grip::Body:   gestureName = "Move"; break;
    case Grip::Handle: gestureName = "Scale"; break;
    case Grip::Rotate: gestureName = "Rotate"; break;
    case Grip::Skew:   gestureName = "Skew"; break;
    case Grip::None:   break;
    }

    // The elements already hold their new transforms, so each command records
    // where its element started and where it ended up. Pushing runs redo(),
    // which simply reasserts the value that is already there.
    std::vector<fla::CommandPtr> commands;
    for (const Target& target : _targets)
    {
        if (!target.element)
            continue;

        const fla::Transform& now = target.element->transform;
        if (now.m11 == target.startTransform.m11 && now.m12 == target.startTransform.m12 &&
            now.m21 == target.startTransform.m21 && now.m22 == target.startTransform.m22 &&
            now.tx == target.startTransform.tx && now.ty == target.startTransform.ty)
        {
            continue;
        }

        commands.push_back(fla::CommandPtr(new fla::SetElementTransformCommand(
            target.element, target.startTransform, now, gestureName)));

        // A shape that has been moved or reshaped belongs to the artwork it now
        // sits on, so letting go of it drops it in as a drawn one would be.
        if (target.element->elementType() == fla::Element::Type::Shape)
            view.markShapeEdited(static_cast<fla::Shape*>(target.element));
    }

    // A click that moved nothing is not an edit.
    if (!commands.empty())
    {
        const bool needsMacro = commands.size() > 1;
        if (needsMacro)
            _commandStack.beginMacro(gestureName);

        for (fla::CommandPtr& command : commands)
            _commandStack.push(std::move(command));

        if (needsMacro)
            _commandStack.endMacro();

        // One gesture is one undo step; the next drag must start a new one.
        _commandStack.breakMergeChain();
    }

    _grip = Grip::None;
    _gripIndex = -1;
    _targets.clear();
    view.update();
}

bool FreeTransformTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || _grip == Grip::None)
        return false;

    // Put everything back where the gesture started and drop it.
    for (const Target& target : _targets)
    {
        if (target.element)
            target.element->transform = target.startTransform;
    }

    _box = _boxAtDragStart;
    _grip = Grip::None;
    _gripIndex = -1;
    _targets.clear();

    view.invalidateBounds();
    view.update();
    return true;
}

void FreeTransformTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    if (_selection.isEmpty())
    {
        _boxValid = false;
        return;
    }

    if (!_boxValid && _grip == Grip::None)
        rebuildBox(view);

    if (!_boxValid)
        return;

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 170, 255), 1.5 * scale));
    painter.drawPolygon(_box);

    const double half = kHandleScreenSize * scale;
    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(QPen(QColor(0, 120, 200), 1.0 * scale));

    for (int i = 0; i < kHandleCount; ++i)
    {
        const QPointF point = handlePoint(_box, i);
        painter.drawRect(QRectF(point.x() - half, point.y() - half, half * 2.0, half * 2.0));
    }

    // The transform origin, which rotation turns about.
    const QPointF center = centerOf(_box);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 120, 200), 1.5 * scale));
    painter.drawEllipse(center, half, half);
}

void FreeTransformTool::deactivate(PhoenixView& view)
{
    if (_grip != Grip::None)
    {
        for (const Target& target : _targets)
        {
            if (target.element)
                target.element->transform = target.startTransform;
        }
        _targets.clear();
        _grip = Grip::None;
        view.invalidateBounds();
        view.update();
    }

    _boxValid = false;
}
