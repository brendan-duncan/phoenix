#include "pencil_tool.h"

#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/curve_fit.h"
#include "../edit/shape_recognition.h"
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
    options.straighten = _style.pencilMode == fla::DrawingStyle::PencilMode::Straighten;

    // Smoothing is an amount in screen pixels, as in Animate: at 100 the fit may
    // stray five screen pixels from the stroke, at 0 a quarter of one. Ink mode
    // follows the stroke closely but still fits curves rather than keeping every
    // mouse sample as a corner.
    const double zoom = qMax(view.zoom(), 1.0e-6);
    const double screenTolerance = _style.pencilMode == fla::DrawingStyle::PencilMode::Ink
        ? 0.5
        : 0.25 + 4.75 * (qBound(0, _style.pencilSmoothing, 100) / 100.0);
    options.tolerance = screenTolerance / zoom;
    options.minimumSpacing = 0.5 / zoom;

    fla::EditablePath path = fla::CurveFit::fit(_points, options);

    // Straighten mode tidies what it drew: ends that nearly meet are joined,
    // and a stroke that is clearly a rectangle or an oval becomes one.
    if (options.straighten)
    {
        fla::StrokeTidyOptions tidy;
        tidy.connectLines = _style.connectLines;
        tidy.recognizeShapes = _style.recognizeShapes;
        // Animate's normal Connect Lines reach, in screen pixels.
        tidy.closeTolerance = 15.0 / zoom;
        fla::tidyStraightenedStroke(path, _points, tidy);
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
