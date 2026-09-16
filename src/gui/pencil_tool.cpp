#include "pencil_tool.h"

#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/curve_fit.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>

namespace {

/// Index used for the single stroke on a generated shape; the format numbers
/// styles from one.
constexpr int kStyleIndex = 1;

} // namespace

bool PencilTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (!view.activeFrame())
        return false;

    _drawing = true;
    _points.clear();
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    view.update();
    return true;
}

bool PencilTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    if (!_drawing)
        return false;

    // Everything the cursor reports is kept; thinning happens in the fit, where
    // the spacing rule can be applied consistently.
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    view.update();
    return true;
}

bool PencilTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_drawing)
        return false;

    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    _drawing = false;
    finish(view);
    return true;
}

bool PencilTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || !_drawing)
        return false;

    _drawing = false;
    _points.clear();
    view.update();
    return true;
}

void PencilTool::finish(PhoenixView& view)
{
    fla::CurveFit::Options options;
    options.tolerance = _style.pencilTolerance;
    options.straighten = _style.pencilMode == fla::DrawingStyle::PencilMode::Straighten;

    // Ink mode keeps every point as drawn, so the fit is skipped and only the
    // duplicate removal runs.
    if (_style.pencilMode == fla::DrawingStyle::PencilMode::Ink)
        options.tolerance = 0.0;

    fla::EditablePath path;
    if (_style.pencilMode == fla::DrawingStyle::PencilMode::Ink)
    {
        const std::vector<fla::Point> cleaned =
            fla::CurveFit::removeDuplicates(_points, options.minimumSpacing);
        for (const fla::Point& point : cleaned)
            path.anchors.push_back(fla::Anchor(point));
    }
    else
    {
        path = fla::CurveFit::fit(_points, options);
    }

    _points.clear();

    if (path.anchors.size() < 2)
    {
        // A tap, or a stroke that went nowhere.
        view.update();
        return;
    }

    fla::Frame* frame = view.activeFrame();
    if (!frame)
    {
        view.update();
        return;
    }

    fla::Element* shape = createShape(frame, path);
    if (!shape)
    {
        view.update();
        return;
    }

    placeDrawnElement(view, _commandStack, _selection, frame, shape, "Pencil",
        _style.objectDrawing);
}

fla::Element* PencilTool::createShape(fla::Frame* frame, const fla::EditablePath& path) const
{
    fla::Shape* shape = new fla::Shape(frame);

    // A pencil stroke is a line, so it is stroke only however the fill is set.
    fla::StrokeStyle* stroke = _style.createStroke(shape);
    if (!stroke)
    {
        // With strokes off the stroke would be invisible, so fall back to a
        // hairline rather than adding nothing to the document.
        fla::SolidStroke* fallback = new fla::SolidStroke(shape);
        fallback->weight = 1.0;
        fallback->fill = new fla::SolidColor(fallback);
        stroke = fallback;
    }

    shape->strokes.push_back(stroke);
    shape->strokesMap[kStyleIndex] = stroke;

    fla::Edge* edge = new fla::Edge(shape);
    edge->strokeStyle = kStyleIndex;
    path.applyTo(*edge);
    shape->edges.push_back(edge);

    double left = path.anchors[0].position.x;
    double top = path.anchors[0].position.y;
    double right = left;
    double bottom = top;
    for (const fla::Anchor& anchor : path.anchors)
    {
        left = qMin(left, anchor.position.x);
        top = qMin(top, anchor.position.y);
        right = qMax(right, anchor.position.x);
        bottom = qMax(bottom, anchor.position.y);
    }

    shape->localBounds = fla::Rect(fla::Point(left, top), fla::Point(right, bottom));
    shape->bounds = shape->localBounds;

    return shape;
}

void PencilTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_drawing || _points.size() < 2)
        return;

    // The raw stroke as drawn, before fitting, so the line follows the cursor
    // with no lag.
    QPainterPath preview;
    preview.moveTo(_points[0].x, _points[0].y);
    for (size_t i = 1; i < _points.size(); ++i)
        preview.lineTo(_points[i].x, _points[i].y);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 170, 255), 1.0 * scale));
    painter.drawPath(preview);
}

void PencilTool::deactivate(PhoenixView& view)
{
    if (!_drawing)
        return;

    _drawing = false;
    _points.clear();
    view.update();
}
