#include "brush_tools.h"

#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../edit/command_stack.h"
#include "../edit/curve_fit.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/shape_transform.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPathStroker>
#include <QPolygonF>

#include <cmath>
#include <memory>

namespace {

constexpr int kStyleIndex = 1;

/// The centreline of a drag as a path: the raw samples joined up, or a smooth
/// fit through them as the pencil would make.
QPainterPath centreline(const std::vector<fla::Point>& points, bool smooth, double zoom)
{
    QPainterPath path;
    if (points.empty())
        return path;

    if (points.size() == 1)
    {
        // A tap: a dot the size of the brush, which the stroker's round caps
        // make from a zero-length line.
        path.moveTo(points[0].x, points[0].y);
        path.lineTo(points[0].x + 0.01, points[0].y);
        return path;
    }

    if (smooth)
    {
        fla::CurveFit::Options options;
        options.tolerance = 2.0 / std::max(zoom, 1.0e-6);
        options.minimumSpacing = 0.5 / std::max(zoom, 1.0e-6);
        const fla::EditablePath fitted = fla::CurveFit::fit(points, options);
        if (fitted.anchors.size() >= 2)
        {
            path.moveTo(fitted.anchors[0].position.x, fitted.anchors[0].position.y);
            for (size_t i = 1; i < fitted.anchors.size(); ++i)
            {
                const fla::Anchor& from = fitted.anchors[i - 1];
                const fla::Anchor& to = fitted.anchors[i];
                if (!from.hasOutCurve() && !to.hasInCurve())
                    path.lineTo(to.position.x, to.position.y);
                else
                    path.cubicTo(from.outHandle.x, from.outHandle.y,
                        to.inHandle.x, to.inHandle.y, to.position.x, to.position.y);
            }
            return path;
        }
    }

    path.moveTo(points[0].x, points[0].y);
    for (size_t i = 1; i < points.size(); ++i)
        path.lineTo(points[i].x, points[i].y);
    return path;
}

QPainterPath brushOutline(const std::vector<fla::Point>& points, double width, bool smooth, double zoom)
{
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(width, 0.5));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    // No self-crossings: a stroke that loops back over itself is one region.
    return stroker.createStroke(centreline(points, smooth, zoom)).simplified();
}

/// Signed area of a closed subpath, from its flattened outline. Positive is
/// clockwise on screen, which is the orientation whose inside lies on the
/// left of the edge direction.
double signedArea(const QPainterPath& subpath)
{
    const QPolygonF polygon = subpath.toFillPolygon();
    double area = 0.0;
    for (int i = 0; i < polygon.size(); ++i)
    {
        const QPointF& a = polygon[i];
        const QPointF& b = polygon[(i + 1) % polygon.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area;
}

} // namespace

fla::Shape* brushOutlineShape(const std::vector<fla::Point>& points, double width,
    bool smooth, fla::Frame* parent, const fla::DrawingStyle& style, bool asFill)
{
    const QPainterPath outline = brushOutline(points, width, smooth, 1.0);
    if (outline.isEmpty())
        return nullptr;

    fla::Shape* shape = new fla::Shape(parent);

    // The eraser's cutter needs a fill too: only a filled area takes anything
    // away. Its colour never shows.
    fla::FillStyle* fill = asFill ? style.createFill(shape) : new fla::SolidColor(shape);
    if (!fill)
    {
        delete shape;
        return nullptr;
    }
    shape->fills.push_back(fill);
    shape->fillsMap[kStyleIndex] = fill;

    // Each closed loop of the outline becomes one edge. Which side the fill
    // goes on follows the loop's direction, so an outer loop and the hole a
    // self-crossing left both come out right.
    QPainterPath subpath;
    const auto flush = [&]() {
        if (subpath.elementCount() < 2)
        {
            subpath = QPainterPath();
            return;
        }

        fla::Edge* edge = new fla::Edge(shape);
        if (signedArea(subpath) >= 0.0)
            edge->fillStyle1 = kStyleIndex;
        else
            edge->fillStyle0 = kStyleIndex;

        fla::Path* path = new fla::Path(edge);
        const QPointF start(subpath.elementAt(0).x, subpath.elementAt(0).y);
        path->segments.push_back(new fla::PathSegment(
            fla::PathSegment::Command::Move, {fla::Point(start.x(), start.y())}, path));

        QPointF last = start;
        for (int i = 1; i < subpath.elementCount(); ++i)
        {
            const QPainterPath::Element& element = subpath.elementAt(i);
            if (element.type == QPainterPath::LineToElement)
            {
                last = QPointF(element.x, element.y);
                path->segments.push_back(new fla::PathSegment(
                    fla::PathSegment::Command::Line, {fla::Point(last.x(), last.y())}, path));
            }
            else if (element.type == QPainterPath::CurveToElement && i + 2 < subpath.elementCount())
            {
                const QPainterPath::Element& c2 = subpath.elementAt(i + 1);
                const QPainterPath::Element& end = subpath.elementAt(i + 2);
                last = QPointF(end.x, end.y);
                path->segments.push_back(new fla::PathSegment(
                    fla::PathSegment::Command::Cubic,
                    {fla::Point(element.x, element.y), fla::Point(c2.x, c2.y),
                     fla::Point(last.x(), last.y())}, path));
                i += 2;
            }
        }

        // Back to the start, which is what tells the arrangement the loop
        // encloses an area.
        if (std::hypot(last.x() - start.x(), last.y() - start.y()) > 1.0e-6)
        {
            path->segments.push_back(new fla::PathSegment(
                fla::PathSegment::Command::Line, {fla::Point(start.x(), start.y())}, path));
        }

        edge->paths.push_back(path);
        shape->edges.push_back(edge);
        subpath = QPainterPath();
    };

    for (int i = 0; i < outline.elementCount(); ++i)
    {
        const QPainterPath::Element& element = outline.elementAt(i);
        if (element.type == QPainterPath::MoveToElement)
        {
            flush();
            subpath.moveTo(element.x, element.y);
        }
        else if (element.type == QPainterPath::LineToElement)
        {
            subpath.lineTo(element.x, element.y);
        }
        else if (element.type == QPainterPath::CurveToElement && i + 2 < outline.elementCount())
        {
            const QPainterPath::Element& c2 = outline.elementAt(i + 1);
            const QPainterPath::Element& end = outline.elementAt(i + 2);
            subpath.cubicTo(element.x, element.y, c2.x, c2.y, end.x, end.y);
            i += 2;
        }
    }
    flush();

    if (shape->edges.empty())
    {
        delete shape;
        return nullptr;
    }

    const QRectF bounds = outline.boundingRect();
    shape->localBounds = fla::Rect(fla::Point(bounds.left(), bounds.top()),
        fla::Point(bounds.right(), bounds.bottom()));
    shape->bounds = shape->localBounds;
    return shape;
}

// ---------------------------------------------------------------- brush

bool BrushTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !view.activeFrame() || !_style.hasFill)
        return false;

    _drawing = true;
    _points.clear();
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    view.update();
    return true;
}

bool BrushTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    _hover = documentPos;
    _hoverValid = true;

    if (_drawing)
        _points.push_back(fla::Point(documentPos.x(), documentPos.y()));

    view.update();
    return _drawing;
}

bool BrushTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_drawing)
        return false;

    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    _drawing = false;
    finish(view);
    return true;
}

bool BrushTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || !_drawing)
        return false;

    _drawing = false;
    _points.clear();
    view.update();
    return true;
}

void BrushTool::finish(PhoenixView& view)
{
    fla::Frame* frame = view.activeFrame();
    if (!frame || _points.empty())
    {
        _points.clear();
        view.update();
        return;
    }

    fla::Shape* shape = brushOutlineShape(_points, _style.brushSize, true, frame, _style, true);
    _points.clear();
    if (!shape)
    {
        view.update();
        return;
    }

    placeDrawnElement(view, _commandStack, _selection, frame, shape, "Brush",
        _style.objectDrawing);
}

void BrushTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    painter.setPen(Qt::NoPen);

    if (_drawing && !_points.empty())
    {
        // The stroke as it will land: the fill colour along the drag.
        painter.setBrush(QColor(_style.fillColor[0], _style.fillColor[1],
            _style.fillColor[2], _style.fillColor[3]));
        painter.drawPath(brushOutline(_points, _style.brushSize, false, 1.0));
    }

    if (_hoverValid)
    {
        // The brush itself: a ring the size of the brush under the cursor.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.drawEllipse(_hover, _style.brushSize / 2.0, _style.brushSize / 2.0);
    }
}

void BrushTool::deactivate(PhoenixView& view)
{
    _drawing = false;
    _points.clear();
    _hoverValid = false;
    view.update();
}

// ---------------------------------------------------------------- eraser

bool EraserTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !view.activeFrame())
        return false;

    _erasing = true;
    _points.clear();
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    view.update();
    return true;
}

bool EraserTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    _hover = documentPos;
    _hoverValid = true;

    if (_erasing)
        _points.push_back(fla::Point(documentPos.x(), documentPos.y()));

    view.update();
    return _erasing;
}

bool EraserTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_erasing)
        return false;

    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    _erasing = false;
    finish(view);
    return true;
}

bool EraserTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || !_erasing)
        return false;

    _erasing = false;
    _points.clear();
    view.update();
    return true;
}

void EraserTool::finish(PhoenixView& view)
{
    fla::Frame* frame = view.activeFrame();
    if (!frame || _points.empty())
    {
        _points.clear();
        view.update();
        return;
    }

    std::unique_ptr<fla::Shape> cutter(brushOutlineShape(_points, _style.eraserSize, false,
        nullptr, _style, false));
    _points.clear();
    if (!cutter)
    {
        view.update();
        return;
    }

    // Anything floating goes in first, so the eraser works on the artwork as
    // it will be.
    view.flushPendingMerge();

    const QRectF swept(cutter->localBounds.topLeft.x, cutter->localBounds.topLeft.y,
        cutter->localBounds.width(), cutter->localBounds.height());

    std::vector<fla::Shape*> targets;
    for (fla::Element* element : view.elementsIn(swept))
    {
        if (!element || element->elementType() != fla::Element::Type::Shape)
            continue;
        fla::Shape* shape = static_cast<fla::Shape*>(element);
        // Drawing objects keep to themselves; the eraser works on merge art.
        if (shape->isDrawingObject)
            continue;
        targets.push_back(shape);
    }

    if (targets.empty())
    {
        view.update();
        return;
    }

    _commandStack.beginMacro("Eraser");
    for (fla::Shape* target : targets)
    {
        // Cutting reads raw edge coordinates, so a target carrying its
        // position in a transform is brought down into its geometry first.
        fla::bakeTransform(*target);
        _commandStack.push(fla::CommandPtr(new fla::SubtractShapeCommand(target, *cutter, "Eraser")));
    }
    _commandStack.endMacro();
    _commandStack.breakMergeChain();

    view.clearCaches();
    view.update();
}

void EraserTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    painter.setPen(Qt::NoPen);

    if (_erasing && !_points.empty())
    {
        // What will be taken away, shown as the stage colour so it reads as
        // already gone.
        painter.setBrush(QColor(255, 255, 255, 200));
        painter.drawPath(brushOutline(_points, _style.eraserSize, false, 1.0));
    }

    if (_hoverValid)
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.drawEllipse(_hover, _style.eraserSize / 2.0, _style.eraserSize / 2.0);
    }
}

void EraserTool::deactivate(PhoenixView& view)
{
    _erasing = false;
    _points.clear();
    _hoverValid = false;
    view.update();
}
