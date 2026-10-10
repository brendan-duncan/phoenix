#include "free_transform_tool.h"

#include "phoenix_view.h"
#include "selection_targets.h"

#include "../data/element.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/shape_merge.h"
#include "../edit/shape_transform.h"
#include "../edit/snapping.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>

#include <algorithm>
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

double dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

/// Where (u, v) in the unit square lands on a four-cornered box: the corners
/// blended two ways, which keeps the edges straight.
QPointF bilinear(const QPolygonF& box, double u, double v)
{
    const QPointF top = box[0] * (1.0 - u) + box[1] * u;
    const QPointF bottom = box[3] * (1.0 - u) + box[2] * u;
    return top * (1.0 - v) + bottom * v;
}

/// The (u, v) whose bilinear image on \a box is \a point. A parallelogram
/// gives this exactly from its two edge vectors; a box already pulled out
/// of true is refined from that guess with a few Newton steps.
QPointF inverseBilinear(const QPolygonF& box, const QPointF& point)
{
    const QPointF ax = box[1] - box[0];
    const QPointF ay = box[3] - box[0];
    const double det = ax.x() * ay.y() - ax.y() * ay.x();
    if (std::fabs(det) < 1.0e-12)
        return QPointF(0.0, 0.0);

    const QPointF d = point - box[0];
    double u = (d.x() * ay.y() - d.y() * ay.x()) / det;
    double v = (ax.x() * d.y() - ax.y() * d.x()) / det;

    for (int step = 0; step < 6; ++step)
    {
        const QPointF residual = bilinear(box, u, v) - point;
        if (std::fabs(residual.x()) < 1.0e-9 && std::fabs(residual.y()) < 1.0e-9)
            break;
        // Partial derivatives of the bilinear map at (u, v).
        const QPointF du = (box[1] - box[0]) * (1.0 - v) + (box[2] - box[3]) * v;
        const QPointF dv = (box[3] - box[0]) * (1.0 - u) + (box[2] - box[1]) * u;
        const double j = du.x() * dv.y() - du.y() * dv.x();
        if (std::fabs(j) < 1.0e-12)
            break;
        u -= (residual.x() * dv.y() - residual.y() * dv.x()) / j;
        v -= (du.x() * residual.y() - du.y() * residual.x()) / j;
    }
    return QPointF(u, v);
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

FreeTransformTool::~FreeTransformTool()
{
    clearDistortTargets();
    clearEnvelope();
}

void FreeTransformTool::clearEnvelope()
{
    _envelope.clear();
    _envelopeBase.clear();
    for (DistortTarget& target : _envelopeOriginals)
        delete target.original;
    _envelopeOriginals.clear();
    for (DistortTarget& target : _envelopeDragBefore)
        delete target.original;
    _envelopeDragBefore.clear();
    _envelopeAtDragStart.clear();
    _envelopeAnchor = -1;
    _envelopeHandle = -1;
}

void FreeTransformTool::setUpEnvelope()
{
    clearEnvelope();
    if (!_boxValid || _box.size() != 4)
        return;

    _envelopeBase = _box;
    for (int i = 0; i < kHandleCount; ++i)
    {
        EnvelopeAnchor anchor;
        anchor.point = handlePoint(_box, i);
        _envelope.push_back(anchor);
    }
    // Straight sides to begin with: each handle a third of the way to the
    // neighbour it faces.
    for (int i = 0; i < kHandleCount; ++i)
    {
        const QPointF& a = _envelope[i].point;
        const QPointF& b = _envelope[(i + 1) % kHandleCount].point;
        _envelope[i].out = a + (b - a) / 3.0;
        _envelope[(i + 1) % kHandleCount].in = b - (b - a) / 3.0;
    }

    for (fla::Element* element : transformLeaves(_selection))
    {
        if (element && element->elementType() == fla::Element::Type::Shape)
        {
            fla::Shape* shape = static_cast<fla::Shape*>(element);
            _envelopeOriginals.push_back({shape, fla::cloneShape(*shape, nullptr)});
        }
    }
}

QPointF FreeTransformTool::envelopeSegment(int index, double t) const
{
    const EnvelopeAnchor& a = _envelope[index % kHandleCount];
    const EnvelopeAnchor& b = _envelope[(index + 1) % kHandleCount];
    const double s = 1.0 - t;
    return a.point * (s * s * s) + a.out * (3.0 * s * s * t) + b.in * (3.0 * s * t * t) +
        b.point * (t * t * t);
}

QPointF FreeTransformTool::envelopePoint(double u, double v) const
{
    // The four sides, each two segments long, read left to right and top to
    // bottom. The anchors run clockwise: 0 top left, 2 top right, 4 bottom
    // right, 6 bottom left, with the midpoints between.
    const auto top = [&](double s) {
        return s < 0.5 ? envelopeSegment(0, s * 2.0) : envelopeSegment(1, s * 2.0 - 1.0);
    };
    const auto right = [&](double s) {
        return s < 0.5 ? envelopeSegment(2, s * 2.0) : envelopeSegment(3, s * 2.0 - 1.0);
    };
    const auto bottom = [&](double s) {
        return s < 0.5 ? envelopeSegment(5, 1.0 - s * 2.0) : envelopeSegment(4, 2.0 - s * 2.0);
    };
    const auto left = [&](double s) {
        return s < 0.5 ? envelopeSegment(7, 1.0 - s * 2.0) : envelopeSegment(6, 2.0 - s * 2.0);
    };

    const QPointF p00 = _envelope[0].point;
    const QPointF p10 = _envelope[2].point;
    const QPointF p11 = _envelope[4].point;
    const QPointF p01 = _envelope[6].point;

    // Coons: blend the opposite sides both ways, less the corners counted twice.
    return top(u) * (1.0 - v) + bottom(u) * v + left(v) * (1.0 - u) + right(v) * u -
        (p00 * ((1.0 - u) * (1.0 - v)) + p10 * (u * (1.0 - v)) + p01 * ((1.0 - u) * v) + p11 * (u * v));
}

bool FreeTransformTool::envelopeHit(const QPointF& documentPos, double tolerance,
    int& anchor, int& handle) const
{
    anchor = -1;
    handle = -1;
    if (!envelopeReady())
        return false;

    // Handles first: they sit near their anchors and are the finer control.
    for (int i = 0; i < kHandleCount; ++i)
    {
        if (distance(documentPos, _envelope[i].in) <= tolerance)
        {
            anchor = i;
            handle = 0;
            return true;
        }
        if (distance(documentPos, _envelope[i].out) <= tolerance)
        {
            anchor = i;
            handle = 1;
            return true;
        }
    }
    for (int i = 0; i < kHandleCount; ++i)
    {
        if (distance(documentPos, _envelope[i].point) <= tolerance)
        {
            anchor = i;
            return true;
        }
    }
    return false;
}

void FreeTransformTool::applyEnvelope(PhoenixView& view)
{
    if (!envelopeReady() || _envelopeBase.size() != 4)
        return;

    for (const DistortTarget& target : _envelopeOriginals)
    {
        if (!target.shape || !target.original)
            continue;

        fla::setShapeContents(*target.shape, *target.original);
        // Straight runs cannot bend, so they are cut into short cubics first.
        fla::prepareForWarp(*target.shape, 20.0);

        const QTransform toDocument = toQTransform(target.shape->transform);
        bool invertible = false;
        const QTransform toShape = toDocument.inverted(&invertible);
        if (!invertible)
            continue;

        fla::warpShape(*target.shape, [&](const fla::Point& point) {
            const QPointF inDocument = toDocument.map(QPointF(point.x, point.y));
            const QPointF uv = inverseBilinear(_envelopeBase, inDocument);
            const QPointF warped = toShape.map(envelopePoint(uv.x(), uv.y()));
            return fla::Point(warped.x(), warped.y());
        });
    }

    view.clearCaches();
    view.update();
}

void FreeTransformTool::commitEnvelope(PhoenixView& view)
{
    std::vector<fla::CommandPtr> commands;
    for (const DistortTarget& before : _envelopeDragBefore)
    {
        if (!before.shape || !before.original)
            continue;
        // The command snapshots its target as the "before", so the shape is
        // put back to where the drag started first.
        std::unique_ptr<fla::Shape> after(fla::cloneShape(*before.shape, nullptr));
        fla::setShapeContents(*before.shape, *before.original);
        commands.push_back(fla::CommandPtr(
            new fla::SetShapeContentsCommand(before.shape, *after, "Envelope")));
    }
    for (DistortTarget& before : _envelopeDragBefore)
        delete before.original;
    _envelopeDragBefore.clear();

    if (!commands.empty())
    {
        const bool needsMacro = commands.size() > 1;
        if (needsMacro)
            _commandStack.beginMacro("Envelope");
        for (fla::CommandPtr& command : commands)
            _commandStack.push(std::move(command));
        if (needsMacro)
            _commandStack.endMacro();
        _commandStack.breakMergeChain();

        for (const DistortTarget& target : _envelopeOriginals)
        {
            if (target.shape)
                view.markShapeEdited(target.shape);
        }
    }

    _grip = Grip::None;
    _gripIndex = -1;
    _envelopeAnchor = -1;
    _envelopeHandle = -1;
    _targets.clear();
    view.clearCaches();
    view.update();
}

void FreeTransformTool::drawEnvelope(QPainter& painter, double scale) const
{
    if (!envelopeReady())
        return;

    // The bent sides.
    QPainterPath sides;
    sides.moveTo(_envelope[0].point);
    for (int i = 0; i < kHandleCount; ++i)
    {
        const EnvelopeAnchor& a = _envelope[i];
        const EnvelopeAnchor& b = _envelope[(i + 1) % kHandleCount];
        sides.cubicTo(a.out, b.in, b.point);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.drawPath(sides);

    // Each handle on a thin line from its anchor, as a hollow circle; the
    // anchors as hollow squares.
    const double half = kHandleScreenSize * scale;
    for (const EnvelopeAnchor& anchor : _envelope)
    {
        painter.setPen(QPen(QColor(0, 0, 0), 0.5 * scale));
        painter.drawLine(anchor.point, anchor.in);
        painter.drawLine(anchor.point, anchor.out);
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(anchor.in, half, half);
        painter.drawEllipse(anchor.out, half, half);
    }
    for (const EnvelopeAnchor& anchor : _envelope)
    {
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawRect(QRectF(anchor.point.x() - half, anchor.point.y() - half, half * 2.0, half * 2.0));
    }
}

QTransform FreeTransformTool::boxFrame(const QPolygonF& box)
{
    // Axes along the box's first side and the side after it, each a unit
    // vector; mapping a page vector gives its coordinates in that frame.
    if (box.size() != 4)
        return QTransform();

    QPointF ex = box[1] - box[0];
    QPointF ey = box[3] - box[0];
    const double lx = std::hypot(ex.x(), ex.y());
    const double ly = std::hypot(ey.x(), ey.y());
    if (lx <= 1.0e-9 || ly <= 1.0e-9)
        return QTransform();
    ex /= lx;
    ey /= ly;

    // Qt maps (x, y) to (m11 x + m21 y, m12 x + m22 y).
    return QTransform(ex.x(), ey.x(), ex.y(), ey.y(), 0.0, 0.0);
}

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
    rememberBoxOwners();
    clearEnvelope();
    placePivot();
}

void FreeTransformTool::placePivot()
{
    _pivot = centerOf(_box);

    // One element keeps its own transformation point, in its own space. A
    // shape whose point was never set reads as the origin, which is not a
    // place anyone chose, so that one stays at the centre.
    const std::vector<fla::Element*> leaves = transformLeaves(_selection);
    if (leaves.size() != 1 || !leaves.front())
        return;
    const fla::Element* element = leaves.front();
    const fla::Point& point = element->transformationPoint;
    if (element->elementType() == fla::Element::Type::Shape && point.x == 0.0 && point.y == 0.0)
        return;
    _pivot = toQTransform(element->transform).map(QPointF(point.x, point.y));
}

void FreeTransformTool::storePivot()
{
    const std::vector<fla::Element*> leaves = transformLeaves(_selection);
    if (leaves.size() != 1 || !leaves.front())
        return;
    fla::Element* element = leaves.front();
    bool invertible = false;
    const QTransform toLocal = toQTransform(element->transform).inverted(&invertible);
    if (!invertible)
        return;
    const QPointF local = toLocal.map(_pivot);
    element->transformationPoint = fla::Point(local.x(), local.y());
}

QPointF FreeTransformTool::snapPivot(PhoenixView& view, const QPointF& documentPos) const
{
    const double tolerance = qMax(view.pickTolerance(), kHandleScreenSize / qMax(view.zoom(), 1.0e-6)) * 1.5;

    // The centre and the handles are where a pivot most often belongs.
    if (_box.size() == 4)
    {
        const QPointF centre = centerOf(_box);
        if (distance(documentPos, centre) <= tolerance)
            return centre;
        for (int i = 0; i < kHandleCount; ++i)
        {
            const QPointF handle = handlePoint(_box, i);
            if (distance(documentPos, handle) <= tolerance)
                return handle;
        }
    }

    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled())
        return documentPos;
    snapper->setTolerance(view.pickTolerance() * 2.0);
    const fla::SnapResult x = snapper->snapX({documentPos.x()});
    const fla::SnapResult y = snapper->snapY({documentPos.y()});
    return QPointF(documentPos.x() + x.adjustment, documentPos.y() + y.adjustment);
}

void FreeTransformTool::rememberBoxOwners()
{
    _boxOwners.clear();
    for (fla::Element* element : transformLeaves(_selection))
        _boxOwners.push_back({element, element->transform});
}

bool FreeTransformTool::boxMatchesSelection() const
{
    size_t count = 0;
    for (fla::Element* element : transformLeaves(_selection))
    {
        ++count;
        bool found = false;
        for (const BoxOwner& owner : _boxOwners)
        {
            if (owner.element != element)
                continue;

            const fla::Transform& t = element->transform;
            found = t.m11 == owner.transform.m11 && t.m12 == owner.transform.m12 &&
                t.m21 == owner.transform.m21 && t.m22 == owner.transform.m22 &&
                t.tx == owner.transform.tx && t.ty == owner.transform.ty;
            break;
        }
        if (!found)
            return false;
    }
    return count == _boxOwners.size();
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
        // Alt scales about the transformation point instead of the
        // opposite handle.
        const QPointF anchor = (modifiers & Qt::AltModifier)
            ? _pivotAtDragStart : anchorFor(_boxAtDragStart, _gripIndex);

        // A rotated box scales along its own sides, not the page's axes, or
        // pulling a corner would shear it. So the pointer is measured in the
        // box's frame, scaled there, and the result turned back.
        const QTransform toBox = boxFrame(_boxAtDragStart);
        const QTransform fromBox = toBox.inverted();
        const QPointF start = toBox.map(_dragStart - anchor);
        const QPointF now = toBox.map(documentPos - anchor);

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

        // Shift keeps the aspect ratio, as everywhere else in Animate: the
        // axis pulled further sets the scale for both.
        if (modifiers & Qt::ShiftModifier)
        {
            const double uniform = std::max(std::fabs(sx), std::fabs(sy));
            sx = sx < 0.0 ? -uniform : uniform;
            sy = sy < 0.0 ? -uniform : uniform;
        }

        QTransform scale;
        scale.scale(sx, sy);
        return aroundAnchor(anchor, toBox * scale * fromBox);
    }

    case Grip::Rotate:
    {
        // Rotation turns about the transformation point.
        const QPointF center = _pivotAtDragStart;
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

        // In the box's own frame, so a rotated box skews along its sides.
        const QTransform toBox = boxFrame(_boxAtDragStart);
        const QTransform fromBox = toBox.inverted();
        const QPointF edge = toBox.map(b - a);
        const QPointF lever = toBox.map((a + b) / 2.0 - anchor);
        const QPointF slide = toBox.map(delta);

        // The grabbed edge slides by the drag; everything in between slides
        // in proportion to its distance from the still edge. The lever is
        // signed: the top edge sits above the anchor, the bottom below, and
        // dividing by the signed distance is what sends each the right way.
        const bool horizontal = std::fabs(edge.x()) >= std::fabs(edge.y());

        QTransform shear;
        if (horizontal)
        {
            if (std::fabs(lever.y()) > 1.0e-6)
                shear.shear(slide.x() / lever.y(), 0.0);
        }
        else
        {
            if (std::fabs(lever.x()) > 1.0e-6)
                shear.shear(0.0, slide.y() / lever.x());
        }

        return aroundAnchor(anchor, toBox * shear * fromBox);
    }

    case Grip::Pivot:
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

    if (_grip == Grip::Body)
    {
        // A move snaps the box, not the cursor, or the object would land
        // offset by wherever it was grabbed.
        const QRectF moved = _boxAtDragStart.boundingRect().translated(documentPos - _dragStart);
        const fla::SnapResult x = snapper->snapX({moved.left(), moved.center().x(), moved.right()});
        const fla::SnapResult y = snapper->snapY({moved.top(), moved.center().y(), moved.bottom()});
        return QPointF(documentPos.x() + x.adjustment, documentPos.y() + y.adjustment);
    }

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

    // A box that no longer describes the selection -- after an undo, a nudge,
    // or an edit with another tool -- is rebuilt rather than trusted.
    if (!_boxValid || !boxMatchesSelection())
        rebuildBox(view);

    if (!_boxValid)
    {
        // Nothing selected: pick whatever is under the cursor, as the arrow
        // would, and carry on with it.
        const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
        if (!hit)
            return false;
        _selection.select(hit.element);
        rebuildBox(view);
        if (!_boxValid)
            return false;
    }

    _gripIndex = -1;

    // Envelope mode: the mesh's anchors and handles come before the box's
    // own grips, which it replaces.
    if (_style.transformMode == fla::DrawingStyle::TransformMode::Envelope && onlyShapesSelected())
    {
        if (!envelopeReady())
            setUpEnvelope();
        const double tolerance = qMax(view.pickTolerance(),
            kHandleScreenSize / qMax(view.zoom(), 1.0e-6));
        int anchor = -1;
        int handle = -1;
        if (envelopeHit(documentPos, tolerance, anchor, handle))
        {
            _grip = Grip::Envelope;
            _envelopeAnchor = anchor;
            _envelopeHandle = handle;
            _envelopeAtDragStart = _envelope;
            _dragStart = documentPos;
            for (DistortTarget& before : _envelopeDragBefore)
                delete before.original;
            _envelopeDragBefore.clear();
            for (const DistortTarget& target : _envelopeOriginals)
                _envelopeDragBefore.push_back({target.shape, fla::cloneShape(*target.shape, nullptr)});
            return true;
        }
    }

    _grip = gripAt(view, documentPos, _gripIndex);

    // Ctrl on a corner, or the Distort option, is Distort, which only shapes
    // can take: their points are rewritten, where a symbol or text has only
    // a matrix to change.
    const bool distortWanted = (event->modifiers() & Qt::ControlModifier) ||
        _style.transformMode == fla::DrawingStyle::TransformMode::Distort;
    if (_grip == Grip::Handle && _gripIndex % 2 == 0 && distortWanted && onlyShapesSelected())
        _grip = Grip::Distort;

    if (_grip == Grip::None)
    {
        // Outside the box: a click picks whatever is under the cursor and
        // starts moving it, or clears the selection on empty stage.
        const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
        if (!hit)
        {
            _selection.clear();
            _boxValid = false;
            view.update();
            return true;
        }

        if (!_selection.contains(hit.element))
            _selection.select(hit.element);
        rebuildBox(view);
        if (!_boxValid)
            return false;
        _grip = Grip::Body;
    }

    _dragStart = documentPos;
    _boxAtDragStart = _box;
    _pivotAtDragStart = _pivot;

    if (_grip == Grip::Pivot)
    {
        showCursorFor(view, _grip, -1);
        return true;
    }

    _targets.clear();
    for (fla::Element* element : transformLeaves(_selection))
        _targets.push_back({element, element->transform});

    if (_targets.empty())
    {
        _grip = Grip::None;
        return false;
    }

    if (_grip == Grip::Distort)
    {
        clearDistortTargets();
        for (const Target& target : _targets)
        {
            fla::Shape* shape = static_cast<fla::Shape*>(target.element);
            _distortTargets.push_back({shape, fla::cloneShape(*shape, nullptr)});
        }
    }

    // What this gesture can line up with, gathered once: it does not change
    // while the drag runs.
    if (fla::Snapper* snapper = view.snapper())
        view.gatherSnapCandidates(*snapper, topLevelElements(_selection));

    showCursorFor(view, _grip, _gripIndex);
    return true;
}

FreeTransformTool::Grip FreeTransformTool::gripAt(PhoenixView& view, const QPointF& documentPos,
    int& index) const
{
    index = -1;
    if (!_boxValid || _box.size() != 4)
        return Grip::None;

    const double tolerance = view.pickTolerance();
    const double handleTolerance = qMax(tolerance, kHandleScreenSize / qMax(view.zoom(), 1.0e-6));
    const double rotateBand = kRotateBandScreenSize / qMax(view.zoom(), 1.0e-6);

    // The transformation point sits inside the box, often on a handle or
    // the centre, and a press on it moves it rather than the box.
    if (distance(documentPos, _pivot) <= handleTolerance)
        return Grip::Pivot;

    const int handle = handleAt(documentPos, handleTolerance);
    if (handle >= 0)
    {
        index = handle;
        return Grip::Handle;
    }

    const int edge = edgeAt(documentPos, handleTolerance);
    if (edge >= 0)
    {
        index = edge;
        return Grip::Skew;
    }

    if (_box.containsPoint(documentPos, Qt::OddEvenFill))
        return Grip::Body;

    // Just outside a corner is the rotate band, the way Animate does it.
    const int nearCorner = handleAt(documentPos, handleTolerance + rotateBand);
    if (nearCorner >= 0 && nearCorner % 2 == 0)
    {
        index = nearCorner;
        return Grip::Rotate;
    }
    return Grip::None;
}

bool FreeTransformTool::onlyShapesSelected() const
{
    const std::vector<fla::Element*> leaves = transformLeaves(_selection);
    if (leaves.empty())
        return false;
    for (fla::Element* leaf : leaves)
    {
        if (!leaf || leaf->elementType() != fla::Element::Type::Shape)
            return false;
    }
    return true;
}

int FreeTransformTool::eighthTurnsFor(const QPointF& direction)
{
    if (std::abs(direction.x()) < 1.0e-9 && std::abs(direction.y()) < 1.0e-9)
        return 0;
    const double degrees = std::atan2(direction.y(), direction.x()) * 180.0 / kPi;
    const int turns = static_cast<int>(std::lround(degrees / 45.0));
    return ((turns % 8) + 8) % 8;
}

void FreeTransformTool::showCursorFor(PhoenixView& view, Grip grip, int index)
{
    Cursors::Kind kind = Cursors::Kind::Arrow;
    int turns = 0;
    switch (grip)
    {
    case Grip::Body:
        kind = Cursors::Kind::ArrowMove;
        break;
    case Grip::Handle:
        // Along the line from the centre out through the handle: diagonal at
        // a corner, across the edge at its midpoint.
        kind = Cursors::Kind::Scale;
        if (index >= 0 && _box.size() == 4)
            turns = eighthTurnsFor(handlePoint(_box, index) - centerOf(_box));
        break;
    case Grip::Skew:
        // Along the edge being pushed.
        kind = Cursors::Kind::Skew;
        if (index >= 0 && _box.size() == 4)
            turns = eighthTurnsFor(_box[(index + 1) % 4] - _box[index]);
        break;
    case Grip::Rotate:
        kind = Cursors::Kind::Rotate;
        break;
    case Grip::Distort:
        kind = Cursors::Kind::ArrowDistort;
        break;
    case Grip::Envelope:
    case Grip::Pivot:
        kind = Cursors::Kind::ArrowMove;
        break;
    case Grip::None:
        break;
    }

    if (kind != _hoverKind || turns != _hoverTurns)
    {
        _hoverKind = kind;
        _hoverTurns = turns;
        view.setCursor(cursor());
    }
}

bool FreeTransformTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (_grip == Grip::None)
    {
        // Hovering: the cursor says what a press here would do.
        if (!_selection.isEmpty() && (!_boxValid || !boxMatchesSelection()))
            rebuildBox(view);
        int index = -1;
        Grip grip = _selection.isEmpty() ? Grip::None : gripAt(view, documentPos, index);
        const bool distortWanted = (event->modifiers() & Qt::ControlModifier) ||
            _style.transformMode == fla::DrawingStyle::TransformMode::Distort;
        if (grip == Grip::Handle && index % 2 == 0 && distortWanted && onlyShapesSelected())
            grip = Grip::Distort;
        if (_style.transformMode == fla::DrawingStyle::TransformMode::Envelope && envelopeReady())
        {
            int anchor = -1;
            int handle = -1;
            const double tolerance = qMax(view.pickTolerance(),
                kHandleScreenSize / qMax(view.zoom(), 1.0e-6));
            grip = envelopeHit(documentPos, tolerance, anchor, handle) ? Grip::Envelope : Grip::None;
            index = -1;
        }
        showCursorFor(view, grip, index);
        return false;
    }

    if (_grip == Grip::Pivot)
    {
        _pivot = snapPivot(view, documentPos);
        view.update();
        return true;
    }

    if (_grip == Grip::Envelope)
    {
        const QPointF delta = documentPos - _dragStart;
        if (_envelopeAnchor >= 0 && _envelopeAnchor < static_cast<int>(_envelope.size()) &&
            _envelopeAtDragStart.size() == _envelope.size())
        {
            const EnvelopeAnchor& start = _envelopeAtDragStart[_envelopeAnchor];
            EnvelopeAnchor& anchor = _envelope[_envelopeAnchor];
            if (_envelopeHandle == 0)
                anchor.in = start.in + delta;
            else if (_envelopeHandle == 1)
                anchor.out = start.out + delta;
            else
            {
                // An anchor carries its handles with it.
                anchor.point = start.point + delta;
                anchor.in = start.in + delta;
                anchor.out = start.out + delta;
            }
        }
        applyEnvelope(view);
        return true;
    }

    if (_grip == Grip::Distort)
    {
        const QPolygonF box = distortedBox(snapDragPoint(view, documentPos), event->modifiers());
        applyDistort(view, box);
        _box = box;
        return true;
    }

    const QTransform adjust = adjustForDrag(snapDragPoint(view, documentPos), event->modifiers());
    applyAdjust(view, adjust);

    // Carry the box with the gesture so the handles stay on the object.
    _box = adjust.map(_boxAtDragStart);
    return true;
}

QPolygonF FreeTransformTool::distortedBox(const QPointF& documentPos,
    Qt::KeyboardModifiers modifiers) const
{
    QPolygonF box = _boxAtDragStart;
    if (box.size() != 4 || _gripIndex < 0)
        return box;

    const int corner = _gripIndex / 2;
    const QPointF delta = documentPos - _dragStart;
    box[corner] += delta;

    if (modifiers & Qt::ShiftModifier)
    {
        // Taper: the neighbour along the edge most in line with the drag
        // goes the other way along that edge, and the same way across it.
        const int previous = (corner + 3) % 4;
        const int next = (corner + 1) % 4;
        const QPointF toPrevious = _boxAtDragStart[previous] - _boxAtDragStart[corner];
        const QPointF toNext = _boxAtDragStart[next] - _boxAtDragStart[corner];
        const double alongPrevious = std::fabs(dot(delta, toPrevious)) /
            qMax(1.0e-9, std::hypot(toPrevious.x(), toPrevious.y()));
        const double alongNext = std::fabs(dot(delta, toNext)) /
            qMax(1.0e-9, std::hypot(toNext.x(), toNext.y()));
        const int neighbour = alongPrevious >= alongNext ? previous : next;
        QPointF edge = neighbour == previous ? toPrevious : toNext;
        const double length = std::hypot(edge.x(), edge.y());
        if (length > 1.0e-9)
        {
            edge /= length;
            const QPointF along = edge * dot(delta, edge);
            const QPointF across = delta - along;
            box[neighbour] += across - along;
        }
    }
    return box;
}

void FreeTransformTool::applyDistort(PhoenixView& view, const QPolygonF& box)
{
    if (_boxAtDragStart.size() != 4 || box.size() != 4)
        return;

    for (const DistortTarget& target : _distortTargets)
    {
        if (!target.shape || !target.original)
            continue;

        // Every move starts again from the untouched geometry, so the warp
        // is always the one drag, not a pile of small ones.
        fla::setShapeContents(*target.shape, *target.original);
        // Straight runs cannot bend, so they are cut into short cubics first.
        fla::prepareForWarp(*target.shape, 20.0);

        const QTransform toDocument = toQTransform(target.shape->transform);
        bool invertible = false;
        const QTransform toShape = toDocument.inverted(&invertible);
        if (!invertible)
            continue;

        const QPolygonF& from = _boxAtDragStart;
        fla::warpShape(*target.shape, [&](const fla::Point& point) {
            const QPointF inDocument = toDocument.map(QPointF(point.x, point.y));
            const QPointF uv = inverseBilinear(from, inDocument);
            const QPointF warped = toShape.map(bilinear(box, uv.x(), uv.y()));
            return fla::Point(warped.x(), warped.y());
        });
    }

    view.clearCaches();
    view.update();
}

void FreeTransformTool::restoreDistortTargets()
{
    for (const DistortTarget& target : _distortTargets)
    {
        if (target.shape && target.original)
            fla::setShapeContents(*target.shape, *target.original);
    }
}

void FreeTransformTool::clearDistortTargets()
{
    for (DistortTarget& target : _distortTargets)
        delete target.original;
    _distortTargets.clear();
}

void FreeTransformTool::commitDistort(PhoenixView& view)
{
    std::vector<fla::CommandPtr> commands;
    for (const DistortTarget& target : _distortTargets)
    {
        if (!target.shape || !target.original)
            continue;

        // The command snapshots its target as the "before", so the shape is
        // put back first and the warped copy handed over as the "after".
        std::unique_ptr<fla::Shape> after(fla::cloneShape(*target.shape, nullptr));
        fla::setShapeContents(*target.shape, *target.original);
        commands.push_back(fla::CommandPtr(
            new fla::SetShapeContentsCommand(target.shape, *after, "Distort")));
    }

    if (!commands.empty())
    {
        const bool needsMacro = commands.size() > 1;
        if (needsMacro)
            _commandStack.beginMacro("Distort");
        for (fla::CommandPtr& command : commands)
            _commandStack.push(std::move(command));
        if (needsMacro)
            _commandStack.endMacro();
        _commandStack.breakMergeChain();

        for (const DistortTarget& target : _distortTargets)
        {
            if (target.shape)
                view.markShapeEdited(target.shape);
        }
    }

    clearDistortTargets();
    _grip = Grip::None;
    _gripIndex = -1;
    _targets.clear();

    // The box keeps the pulled-out shape until the selection changes, as
    // Animate's does.
    rememberBoxOwners();
    view.clearCaches();
    view.update();
}

bool FreeTransformTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || _grip == Grip::None)
        return false;

    if (_grip == Grip::Pivot)
    {
        _pivot = snapPivot(view, documentPos);
        storePivot();
        _grip = Grip::None;
        int index = -1;
        showCursorFor(view, gripAt(view, documentPos, index), index);
        view.update();
        return true;
    }

    if (_grip == Grip::Envelope)
    {
        commitEnvelope(view);
        return true;
    }

    if (_grip == Grip::Distort)
    {
        const QPolygonF box = distortedBox(snapDragPoint(view, documentPos), event->modifiers());
        applyDistort(view, box);
        _box = box;
        commitDistort(view);
        int index = -1;
        showCursorFor(view, gripAt(view, documentPos, index), index);
        return true;
    }

    const QTransform adjust = adjustForDrag(snapDragPoint(view, documentPos), event->modifiers());
    applyAdjust(view, adjust);
    _box = adjust.map(_boxAtDragStart);

    // The transformation point rides along with the object.
    _pivot = adjust.map(_pivotAtDragStart);
    storePivot();

    commit(view);

    int index = -1;
    showCursorFor(view, gripAt(view, documentPos, index), index);
    return true;
}

void FreeTransformTool::commit(PhoenixView& view)
{
    // Moving or scaling the shapes leaves the mesh behind; it is rebuilt on
    // the new box.
    clearEnvelope();

    const char* gestureName = "Transform";
    switch (_grip)
    {
    case Grip::Body:   gestureName = "Move"; break;
    case Grip::Handle: gestureName = "Scale"; break;
    case Grip::Rotate: gestureName = "Rotate"; break;
    case Grip::Skew:   gestureName = "Skew"; break;
    case Grip::Distort: gestureName = "Distort"; break;
    case Grip::Envelope: gestureName = "Envelope"; break;
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

    // The box was carried through the gesture, so it still describes the
    // selection; it just belongs to the new transforms now.
    rememberBoxOwners();
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
    if (_grip == Grip::Distort)
    {
        restoreDistortTargets();
        clearDistortTargets();
        view.clearCaches();
    }
    if (_grip == Grip::Envelope)
    {
        _envelope = _envelopeAtDragStart;
        for (DistortTarget& before : _envelopeDragBefore)
        {
            if (before.shape && before.original)
                fla::setShapeContents(*before.shape, *before.original);
            delete before.original;
        }
        _envelopeDragBefore.clear();
        _grip = Grip::None;
        _envelopeAnchor = -1;
        _envelopeHandle = -1;
        view.clearCaches();
        view.update();
        return true;
    }

    _box = _boxAtDragStart;
    _pivot = _pivotAtDragStart;
    _grip = Grip::None;
    _gripIndex = -1;
    _targets.clear();

    view.invalidateBounds();
    view.update();
    return true;
}

bool FreeTransformTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_boxValid || _box.size() != 4)
        return false;

    const double tolerance = qMax(view.pickTolerance(), kHandleScreenSize / qMax(view.zoom(), 1.0e-6));
    if (distance(documentPos, _pivot) > tolerance)
        return false;

    _pivot = centerOf(_box);
    storePivot();
    _grip = Grip::None;
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

    // A box left behind by an undo or another tool's edit no longer sits on
    // the selection, so it is rebuilt before it is drawn.
    if (_grip == Grip::None && (!_boxValid || !boxMatchesSelection()))
        rebuildBox(view);

    if (!_boxValid)
        return;

    if (_style.transformMode == fla::DrawingStyle::TransformMode::Envelope && onlyShapesSelected())
    {
        if (!envelopeReady() && _grip == Grip::None)
            const_cast<FreeTransformTool*>(this)->setUpEnvelope();
        if (envelopeReady())
        {
            drawEnvelope(painter, scale);
            return;
        }
    }

    // Animate's transform box: a thin black outline, hollow white handles with
    // a black border, and a white circle for the transformation point.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.drawPolygon(_box);

    const double half = kHandleScreenSize * scale;
    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));

    for (int i = 0; i < kHandleCount; ++i)
    {
        const QPointF point = handlePoint(_box, i);
        painter.drawRect(QRectF(point.x() - half, point.y() - half, half * 2.0, half * 2.0));
    }

    // The transformation point, which rotation turns about.
    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.drawEllipse(_pivot, half, half);
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
        if (_grip == Grip::Distort)
        {
            restoreDistortTargets();
            clearDistortTargets();
            view.clearCaches();
        }
        if (_grip == Grip::Envelope)
        {
            for (DistortTarget& before : _envelopeDragBefore)
            {
                if (before.shape && before.original)
                    fla::setShapeContents(*before.shape, *before.original);
            }
            view.clearCaches();
        }
        _targets.clear();
        _grip = Grip::None;
        view.invalidateBounds();
        view.update();
    }

    clearEnvelope();
    _boxValid = false;
    _hoverKind = Cursors::Kind::Arrow;
    _hoverTurns = 0;
}
