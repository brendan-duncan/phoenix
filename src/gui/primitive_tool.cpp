#include "primitive_tool.h"

#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
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

constexpr double kPi = 3.14159265358979323846;

/// Below this, in document units, a drag is a stray click rather than a shape.
constexpr double kMinimumSize = 0.5;

/// Index used for the single fill and the single stroke on a generated shape.
/// The format numbers styles from one.
constexpr int kStyleIndex = 1;

/// Appends a closed outline to a shape as one edge, with the given style
/// indices. A closed path is what the fill reconstruction expects, so the last
/// point repeats the first.
void addClosedPath(fla::Shape* shape, const std::vector<QPointF>& points,
    int fillIndex, int strokeIndex)
{
    if (points.size() < 2)
        return;

    fla::Edge* edge = new fla::Edge(shape);
    edge->fillStyle1 = fillIndex;
    edge->strokeStyle = strokeIndex;

    fla::Path* path = new fla::Path(edge);

    std::vector<fla::Point> move{fla::Point(points[0].x(), points[0].y())};
    path->segments.push_back(new fla::PathSegment(
        fla::PathSegment::Command::Move, move, path));

    for (size_t i = 1; i < points.size(); ++i)
    {
        std::vector<fla::Point> to{fla::Point(points[i].x(), points[i].y())};
        path->segments.push_back(new fla::PathSegment(
            fla::PathSegment::Command::Line, to, path));
    }

    // Back to the start, so the outline encloses an area.
    std::vector<fla::Point> close{fla::Point(points[0].x(), points[0].y())};
    path->segments.push_back(new fla::PathSegment(
        fla::PathSegment::Command::Line, close, path));

    edge->paths.push_back(path);
    shape->edges.push_back(edge);
}

/// Records the shape's extent, which the renderer uses when mapping a gradient
/// across it.
void setShapeBounds(fla::Shape* shape, const QRectF& bounds)
{
    shape->localBounds = fla::Rect(
        fla::Point(bounds.left(), bounds.top()),
        fla::Point(bounds.right(), bounds.bottom()));
    shape->bounds = shape->localBounds;
}

} // namespace

QString PrimitiveTool::name() const
{
    switch (_kind)
    {
    case Kind::Rectangle:          return "Rectangle";
    case Kind::Oval:               return "Oval";
    case Kind::Line:               return "Line";
    case Kind::PolyStar:           return "PolyStar";
    case Kind::RectanglePrimitive: return "Rectangle Primitive";
    case Kind::OvalPrimitive:      return "Oval Primitive";
    }
    return "Shape";
}

QRectF PrimitiveTool::dragRect() const
{
    return QRectF(effectiveStart(), _end).normalized();
}

QPointF PrimitiveTool::effectiveStart() const
{
    // A polystar already grows from its center, and a line has no center to
    // grow from, so neither takes the modifier.
    if (!_fromCenter || !isBoxKind())
        return _start;

    // Reflecting the press point across the cursor makes the press the middle
    // and the drag reach out to a corner in both directions at once.
    return _start - (_end - _start);
}

QPointF PrimitiveTool::snap(PhoenixView& view, const QPointF& documentPos) const
{
    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled())
        return documentPos;

    snapper->setTolerance(view.pickTolerance() * 2.0);

    const fla::SnapResult x = snapper->snapX({documentPos.x()});
    const fla::SnapResult y = snapper->snapY({documentPos.y()});
    _snapped = x.snapped() || y.snapped();
    return QPointF(documentPos.x() + x.adjustment, documentPos.y() + y.adjustment);
}

QPointF PrimitiveTool::constrain(const QPointF& documentPos) const
{
    const QPointF delta = documentPos - _start;

    if (_kind == Kind::Line || _kind == Kind::PolyStar)
    {
        // Snap the angle to 45 degree steps: a line's direction, or the way
        // a polystar points. Its radius is the drag's length either way.
        const double angle = std::atan2(delta.y(), delta.x());
        const double step = kPi / 4.0;
        const double snapped = std::round(angle / step) * step;
        const double length = std::hypot(delta.x(), delta.y());
        return _start + QPointF(length * std::cos(snapped), length * std::sin(snapped));
    }

    // Everything else becomes a square, which for an oval means a circle and for
    // a polystar keeps the radius honest.
    const double size = std::max(std::fabs(delta.x()), std::fabs(delta.y()));
    return _start + QPointF(delta.x() < 0.0 ? -size : size,
                            delta.y() < 0.0 ? -size : size);
}

bool PrimitiveTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (!view.activeFrame())
        return false;

    // Nothing else on stage moves while drawing, so the candidates hold for the
    // whole gesture.
    if (fla::Snapper* snapper = view.snapper())
        view.gatherSnapCandidates(*snapper, {});

    _dragging = true;
    _constrained = false;
    _fromCenter = false;
    _cornerRadius = _style.cornerRadius;
    _start = snap(view, documentPos);
    _end = _start;
    view.update();
    return true;
}

bool PrimitiveTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_dragging)
        return false;

    _constrained = (event->modifiers() & Qt::ShiftModifier) != 0;
    _fromCenter = (event->modifiers() & Qt::AltModifier) != 0;
    _end = snap(view, documentPos);
    if (_constrained)
        _end = constrain(_end);

    view.update();
    return true;
}

bool PrimitiveTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_dragging)
        return false;

    _dragging = false;
    _constrained = (event->modifiers() & Qt::ShiftModifier) != 0;
    _fromCenter = (event->modifiers() & Qt::AltModifier) != 0;
    _end = snap(view, documentPos);
    if (_constrained)
        _end = constrain(_end);

    fla::Frame* frame = view.activeFrame();
    if (!frame)
    {
        view.update();
        return true;
    }

    fla::Element* element = createElement(frame, effectiveStart(), _end);
    if (!element)
    {
        // Too small to be a shape, so the click drew nothing.
        view.update();
        return true;
    }

    placeDrawnElement(view, _commandStack, _selection, frame, element, name(),
        _style.objectDrawing);
    return true;
}

bool PrimitiveTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_dragging)
        return false;

    // Up and Down change the corner radius mid-drag, as Animate's rectangle
    // tool does; shift moves it ten at a time.
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) &&
        (_kind == Kind::Rectangle || _kind == Kind::RectanglePrimitive))
    {
        const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
        _cornerRadius += event->key() == Qt::Key_Up ? step : -step;
        view.update();
        return true;
    }

    if (event->key() != Qt::Key_Escape)
        return false;

    _dragging = false;
    view.update();
    return true;
}

void PrimitiveTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_dragging)
        return;

    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);

    switch (_kind)
    {
    case Kind::Rectangle:
    case Kind::RectanglePrimitive:
    {
        const QRectF rect = dragRect();
        const double limit = std::min(rect.width(), rect.height()) / 2.0;
        const double radius = std::max(0.0, std::min(limit, _cornerRadius));
        if (radius > 0.0)
            painter.drawRoundedRect(rect, radius, radius);
        else
            painter.drawRect(rect);
        break;
    }
    case Kind::Oval:
    case Kind::OvalPrimitive:
        painter.drawEllipse(dragRect());
        break;
    case Kind::Line:
        painter.drawLine(_start, _end);
        break;
    case Kind::PolyStar:
        // The drag runs from the center outwards, so the preview is the circle
        // the points sit on.
        painter.drawEllipse(_start, std::hypot(_end.x() - _start.x(), _end.y() - _start.y()),
            std::hypot(_end.x() - _start.x(), _end.y() - _start.y()));
        break;
    }

    // The snap ring at the corner being dragged.
    const fla::Snapper* snapper = view.snapper();
    if (snapper && snapper->objectSnapEnabled())
    {
        const double radius = (_snapped ? 6.0 : 3.5) * scale;
        painter.setPen(QPen(QColor(0, 0, 0), (_snapped ? 1.5 : 1.0) * scale, Qt::SolidLine));
        painter.drawEllipse(_end, radius, radius);
    }
}

void PrimitiveTool::deactivate(PhoenixView& view)
{
    if (!_dragging)
        return;

    _dragging = false;
    view.update();
}

fla::Element* PrimitiveTool::createElement(fla::Frame* frame, const QPointF& start,
    const QPointF& end) const
{
    const QRectF rect = QRectF(start, end).normalized();

    switch (_kind)
    {
    // The rectangle and oval tools always draw shapes, as Animate's do. In
    // Object Drawing mode the shape is flagged as a drawing object when it is
    // placed; the editable primitive objects come only from the primitive
    // tools below.
    case Kind::Rectangle:
        if (rect.width() < kMinimumSize || rect.height() < kMinimumSize)
            return nullptr;
        return createRectangleShape(frame, rect);

    case Kind::Oval:
        if (rect.width() < kMinimumSize || rect.height() < kMinimumSize)
            return nullptr;
        return createOvalShape(frame, rect);

    case Kind::RectanglePrimitive:
        if (rect.width() < kMinimumSize || rect.height() < kMinimumSize)
            return nullptr;
        return createRectangle(frame, rect);

    case Kind::OvalPrimitive:
        if (rect.width() < kMinimumSize || rect.height() < kMinimumSize)
            return nullptr;
        return createOval(frame, rect);

    case Kind::Line:
        if (std::hypot(end.x() - start.x(), end.y() - start.y()) < kMinimumSize)
            return nullptr;
        return createLine(frame, start, end);

    case Kind::PolyStar:
        if (std::hypot(end.x() - start.x(), end.y() - start.y()) < kMinimumSize)
            return nullptr;
        return createPolyStar(frame, start, end);
    }

    return nullptr;
}

namespace {

/// Builds the fill and stroke a generated shape needs, filing them under the
/// usual indices and reporting which ones were actually made.
void addStylesTo(fla::Shape* shape, const fla::DrawingStyle& style,
    int& fillIndex, int& strokeIndex)
{
    fillIndex = -1;
    strokeIndex = -1;

    if (fla::FillStyle* fill = style.createFill(shape))
    {
        shape->fills.push_back(fill);
        shape->fillsMap[kStyleIndex] = fill;
        fillIndex = kStyleIndex;
    }

    if (fla::StrokeStyle* stroke = style.createStroke(shape))
    {
        shape->strokes.push_back(stroke);
        shape->strokesMap[kStyleIndex] = stroke;
        strokeIndex = kStyleIndex;
    }
}

} // namespace

fla::Element* PrimitiveTool::createRectangleShape(fla::Frame* frame, const QRectF& rect) const
{
    fla::Shape* shape = new fla::Shape(frame);

    int fillIndex = -1;
    int strokeIndex = -1;
    addStylesTo(shape, _style, fillIndex, strokeIndex);

    // The radius is clamped to half the shorter side, as Animate clamps it.
    const double radius = std::max(0.0,
        std::min(_cornerRadius, std::min(rect.width(), rect.height()) / 2.0));

    if (radius <= 0.0)
    {
        addClosedPath(shape, {
            rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()
        }, fillIndex, strokeIndex);
    }
    else
    {
        // Four sides and four quarter arcs, traced clockwise on screen like the
        // plain rectangle so the fill lands on the same side.
        const double x0 = rect.left();
        const double y0 = rect.top();
        const double x1 = rect.right();
        const double y1 = rect.bottom();
        const double r = radius;
        const double k = r * 0.5522847498;

        fla::Edge* edge = new fla::Edge(shape);
        edge->fillStyle1 = fillIndex;
        edge->strokeStyle = strokeIndex;

        fla::Path* path = new fla::Path(edge);
        const auto moveTo = [&](double x, double y) {
            path->segments.push_back(new fla::PathSegment(
                fla::PathSegment::Command::Move, {fla::Point(x, y)}, path));
        };
        const auto lineTo = [&](double x, double y) {
            path->segments.push_back(new fla::PathSegment(
                fla::PathSegment::Command::Line, {fla::Point(x, y)}, path));
        };
        const auto arcTo = [&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
            path->segments.push_back(new fla::PathSegment(
                fla::PathSegment::Command::Cubic,
                {fla::Point(c1x, c1y), fla::Point(c2x, c2y), fla::Point(x, y)}, path));
        };

        moveTo(x0 + r, y0);
        lineTo(x1 - r, y0);
        arcTo(x1 - r + k, y0, x1, y0 + r - k, x1, y0 + r);
        lineTo(x1, y1 - r);
        arcTo(x1, y1 - r + k, x1 - r + k, y1, x1 - r, y1);
        lineTo(x0 + r, y1);
        arcTo(x0 + r - k, y1, x0, y1 - r + k, x0, y1 - r);
        lineTo(x0, y0 + r);
        arcTo(x0, y0 + r - k, x0 + r - k, y0, x0 + r, y0);

        edge->paths.push_back(path);
        shape->edges.push_back(edge);
    }

    setShapeBounds(shape, rect);
    return shape;
}

fla::Element* PrimitiveTool::createOvalShape(fla::Frame* frame, const QRectF& rect) const
{
    fla::Shape* shape = new fla::Shape(frame);

    int fillIndex = -1;
    int strokeIndex = -1;
    addStylesTo(shape, _style, fillIndex, strokeIndex);

    // Four cubics, with the control points at the usual fraction of the radius
    // that makes a bezier follow an ellipse to within a fraction of a percent.
    const double kappa = 0.5522847498;
    const double rx = rect.width() * 0.5;
    const double ry = rect.height() * 0.5;
    const double cx = rect.center().x();
    const double cy = rect.center().y();

    const QPointF top(cx, cy - ry);
    const QPointF right(cx + rx, cy);
    const QPointF bottom(cx, cy + ry);
    const QPointF left(cx - rx, cy);

    fla::Edge* edge = new fla::Edge(shape);
    edge->fillStyle1 = fillIndex;
    edge->strokeStyle = strokeIndex;

    fla::Path* path = new fla::Path(edge);
    path->segments.push_back(new fla::PathSegment(
        fla::PathSegment::Command::Move, {fla::Point(top.x(), top.y())}, path));

    const auto arc = [&](const QPointF& from, const QPointF& to,
                         const QPointF& fromControl, const QPointF& toControl) {
        path->segments.push_back(new fla::PathSegment(
            fla::PathSegment::Command::Cubic,
            {fla::Point(fromControl.x(), fromControl.y()),
             fla::Point(toControl.x(), toControl.y()),
             fla::Point(to.x(), to.y())}, path));
        (void)from;
    };

    arc(top, right, QPointF(cx + rx * kappa, cy - ry), QPointF(cx + rx, cy - ry * kappa));
    arc(right, bottom, QPointF(cx + rx, cy + ry * kappa), QPointF(cx + rx * kappa, cy + ry));
    arc(bottom, left, QPointF(cx - rx * kappa, cy + ry), QPointF(cx - rx, cy + ry * kappa));
    arc(left, top, QPointF(cx - rx, cy - ry * kappa), QPointF(cx - rx * kappa, cy - ry));

    edge->paths.push_back(path);
    shape->edges.push_back(edge);

    setShapeBounds(shape, rect);
    return shape;
}

fla::Element* PrimitiveTool::createRectangle(fla::Frame* frame, const QRectF& rect) const
{
    fla::RectanglePrimitive* rectangle = new fla::RectanglePrimitive(frame);
    rectangle->rect = fla::Rect(
        fla::Point(rect.left(), rect.top()),
        fla::Point(rect.right(), rect.bottom()));
    rectangle->fillStyle = _style.createFill(rectangle);
    rectangle->strokeStyle = _style.createStroke(rectangle);
    rectangle->topLeftRadius = _cornerRadius;
    rectangle->topRightRadius = _cornerRadius;
    rectangle->bottomLeftRadius = _cornerRadius;
    rectangle->bottomRightRadius = _cornerRadius;
    rectangle->localBounds = rectangle->rect;
    rectangle->bounds = rectangle->rect;
    return rectangle;
}

fla::Element* PrimitiveTool::createOval(fla::Frame* frame, const QRectF& rect) const
{
    fla::OvalPrimitive* oval = new fla::OvalPrimitive(frame);
    oval->rect = fla::Rect(
        fla::Point(rect.left(), rect.top()),
        fla::Point(rect.right(), rect.bottom()));
    oval->fillStyle = _style.createFill(oval);
    oval->strokeStyle = _style.createStroke(oval);
    oval->startAngle = _style.ovalStartAngle;
    oval->endAngle = _style.ovalEndAngle;
    oval->innerRadius = std::max(0.0, std::min(99.0, _style.ovalInnerRadius));
    oval->localBounds = oval->rect;
    oval->bounds = oval->rect;
    return oval;
}

fla::Element* PrimitiveTool::createLine(fla::Frame* frame, const QPointF& start,
    const QPointF& end) const
{
    fla::Shape* shape = new fla::Shape(frame);

    // A line is stroke only: there is no area to fill.
    fla::StrokeStyle* stroke = _style.createStroke(shape);
    if (!stroke)
    {
        // Drawing a line with strokes switched off would produce nothing at all,
        // so fall back to a hairline rather than an invisible object.
        fla::SolidStroke* fallback = new fla::SolidStroke(shape);
        fallback->weight = 1.0;
        fallback->fill = new fla::SolidColor(fallback);
        stroke = fallback;
    }

    shape->strokes.push_back(stroke);
    shape->strokesMap[kStyleIndex] = stroke;

    fla::Edge* edge = new fla::Edge(shape);
    edge->strokeStyle = kStyleIndex;

    fla::Path* path = new fla::Path(edge);
    std::vector<fla::Point> from{fla::Point(start.x(), start.y())};
    std::vector<fla::Point> to{fla::Point(end.x(), end.y())};
    path->segments.push_back(new fla::PathSegment(
        fla::PathSegment::Command::Move, from, path));
    path->segments.push_back(new fla::PathSegment(
        fla::PathSegment::Command::Line, to, path));

    edge->paths.push_back(path);
    shape->edges.push_back(edge);

    setShapeBounds(shape, QRectF(start, end).normalized());
    return shape;
}

fla::Element* PrimitiveTool::createPolyStar(fla::Frame* frame, const QPointF& center,
    const QPointF& edge) const
{
    const int sides = std::max(3, _style.sides);
    const double radius = std::hypot(edge.x() - center.x(), edge.y() - center.y());

    // The drag direction decides which way the shape points, so dragging
    // upwards gives the usual point-up star.
    const double startAngle = std::atan2(edge.y() - center.y(), edge.x() - center.x());

    std::vector<QPointF> points;
    if (_style.star)
    {
        const double inner = radius * std::max(0.05, std::min(0.95, _style.starInnerRatio));
        points.reserve(sides * 2);
        for (int i = 0; i < sides * 2; ++i)
        {
            const double angle = startAngle + (kPi * i) / sides;
            const double r = (i % 2 == 0) ? radius : inner;
            points.push_back(QPointF(center.x() + r * std::cos(angle),
                                     center.y() + r * std::sin(angle)));
        }
    }
    else
    {
        points.reserve(sides);
        for (int i = 0; i < sides; ++i)
        {
            const double angle = startAngle + (2.0 * kPi * i) / sides;
            points.push_back(QPointF(center.x() + radius * std::cos(angle),
                                     center.y() + radius * std::sin(angle)));
        }
    }

    fla::Shape* shape = new fla::Shape(frame);

    int fillIndex = -1;
    if (fla::FillStyle* fill = _style.createFill(shape))
    {
        shape->fills.push_back(fill);
        shape->fillsMap[kStyleIndex] = fill;
        fillIndex = kStyleIndex;
    }

    int strokeIndex = -1;
    if (fla::StrokeStyle* stroke = _style.createStroke(shape))
    {
        shape->strokes.push_back(stroke);
        shape->strokesMap[kStyleIndex] = stroke;
        strokeIndex = kStyleIndex;
    }

    addClosedPath(shape, points, fillIndex, strokeIndex);

    QRectF bounds;
    for (size_t i = 0; i < points.size(); ++i)
        bounds = (i == 0) ? QRectF(points[0], points[0]) : bounds.united(QRectF(points[i], points[i]));
    setShapeBounds(shape, bounds);

    return shape;
}
