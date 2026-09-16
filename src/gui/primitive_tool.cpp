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
    case Kind::Rectangle: return "Rectangle";
    case Kind::Oval:      return "Oval";
    case Kind::Line:      return "Line";
    case Kind::PolyStar:  return "PolyStar";
    }
    return "Shape";
}

QRectF PrimitiveTool::dragRect() const
{
    return QRectF(_start, _end).normalized();
}

QPointF PrimitiveTool::snap(PhoenixView& view, const QPointF& documentPos) const
{
    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled())
        return documentPos;

    snapper->setTolerance(view.pickTolerance() * 2.0);

    const fla::SnapResult x = snapper->snapX({documentPos.x()});
    const fla::SnapResult y = snapper->snapY({documentPos.y()});
    return QPointF(documentPos.x() + x.adjustment, documentPos.y() + y.adjustment);
}

QPointF PrimitiveTool::constrain(const QPointF& documentPos) const
{
    const QPointF delta = documentPos - _start;

    if (_kind == Kind::Line)
    {
        // Snap the line's angle to 45 degree steps.
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
    _end = snap(view, documentPos);
    if (_constrained)
        _end = constrain(_end);

    fla::Frame* frame = view.activeFrame();
    if (!frame)
    {
        view.update();
        return true;
    }

    fla::Element* element = createElement(frame, _start, _end);
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
    if (event->key() != Qt::Key_Escape || !_dragging)
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

    painter.setPen(QPen(QColor(0, 170, 255), 1.0 * scale, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);

    switch (_kind)
    {
    case Kind::Rectangle:
        painter.drawRect(dragRect());
        break;
    case Kind::Oval:
        painter.drawEllipse(dragRect());
        break;
    case Kind::Line:
        painter.drawLine(_start, _end);
        break;
    case Kind::PolyStar:
        // The drag runs from the centre outwards, so the preview is the circle
        // the points sit on.
        painter.drawEllipse(_start, std::hypot(_end.x() - _start.x(), _end.y() - _start.y()),
            std::hypot(_end.x() - _start.x(), _end.y() - _start.y()));
        break;
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
    case Kind::Rectangle:
        if (rect.width() < kMinimumSize || rect.height() < kMinimumSize)
            return nullptr;
        // A primitive cannot merge, so merge mode gets a plain shape instead.
        return _style.objectDrawing ? createRectangle(frame, rect)
                                    : createRectangleShape(frame, rect);

    case Kind::Oval:
        if (rect.width() < kMinimumSize || rect.height() < kMinimumSize)
            return nullptr;
        return _style.objectDrawing ? createOval(frame, rect)
                                    : createOvalShape(frame, rect);

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

    addClosedPath(shape, {
        rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()
    }, fillIndex, strokeIndex);

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

fla::Element* PrimitiveTool::createPolyStar(fla::Frame* frame, const QPointF& centre,
    const QPointF& edge) const
{
    const int sides = std::max(3, _style.sides);
    const double radius = std::hypot(edge.x() - centre.x(), edge.y() - centre.y());

    // The drag direction decides which way the shape points, so dragging
    // upwards gives the usual point-up star.
    const double startAngle = std::atan2(edge.y() - centre.y(), edge.x() - centre.x());

    std::vector<QPointF> points;
    if (_style.star)
    {
        const double inner = radius * std::max(0.05, std::min(0.95, _style.starInnerRatio));
        points.reserve(sides * 2);
        for (int i = 0; i < sides * 2; ++i)
        {
            const double angle = startAngle + (kPi * i) / sides;
            const double r = (i % 2 == 0) ? radius : inner;
            points.push_back(QPointF(centre.x() + r * std::cos(angle),
                                     centre.y() + r * std::sin(angle)));
        }
    }
    else
    {
        points.reserve(sides);
        for (int i = 0; i < sides; ++i)
        {
            const double angle = startAngle + (2.0 * kPi * i) / sides;
            points.push_back(QPointF(centre.x() + radius * std::cos(angle),
                                     centre.y() + radius * std::sin(angle)));
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
