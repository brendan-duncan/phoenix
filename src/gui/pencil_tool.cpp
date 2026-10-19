#include "pencil_tool.h"

#include "preferences.h"

#include "brush_library_store.h"
#include "draw_placement.h"
#include "drawing_aids_view.h"
#include "phoenix_view.h"

#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/curve_fit.h"
#include "../edit/shape_recognition.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/paint_brushes.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>
#include <algorithm>

namespace {

/// Index used for the single stroke on a generated shape; the format numbers
/// styles from one.
constexpr int kStyleIndex = 1;

/// \a path's copies across the symmetry axis.
std::vector<fla::EditablePath> copiesOf(const fla::EditablePath& path, const std::vector<QTransform>& copies)
{
    std::vector<fla::EditablePath> paths;
    for (const QTransform& copy : copies)
        paths.push_back(path.transformed(fla::Transform(copy.m11(), copy.m12(), copy.m21(), copy.m22(), copy.dx(), copy.dy())));
    return paths;
}

} // namespace

bool PencilTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (!view.activeFrame())
        return false;

    _drawing = true;
    _points.clear();
    _pen.clear();
    _axis = 0;
    beginExtension(view, documentPos);
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    takePen(view);
    view.update();
    return true;
}

void PencilTool::takePen(PhoenixView& view)
{
    if (!view.penDown())
        return;
    for (const fla::BrushSample& sample : view.takeTabletSamples())
        _pen.push_back(sample);
}

void PencilTool::beginExtension(PhoenixView& view, const QPointF& documentPos)
{
    _extending = false;
    _extendShape = nullptr;
    _extendEdge = nullptr;

    fla::DOMElement* single = _selection.single();
    fla::Element* element = single ? dynamic_cast<fla::Element*>(single) : nullptr;
    if (!element || element->elementType() != fla::Element::Type::Shape)
        return;

    fla::Shape* shape = static_cast<fla::Shape*>(element);
    const double tolerance = view.pickTolerance() * 1.5;
    double bestDistance = 0.0;

    for (fla::Edge* edge : shape->edges)
    {
        if (!edge)
            continue;
        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            if (!edge->paths[pathIndex])
                continue;
            const fla::EditablePath path =
                fla::EditablePath::fromPath(*edge->paths[pathIndex]).transformed(shape->transform);
            if (path.closed || path.anchors.size() < 2)
                continue;

            for (int end = 0; end < 2; ++end)
            {
                const fla::Point& at = end == 0 ? path.anchors.front().position
                                                : path.anchors.back().position;
                const double distance = std::hypot(at.x - documentPos.x(), at.y - documentPos.y());
                if (distance > tolerance || (_extending && distance >= bestDistance))
                    continue;
                _extending = true;
                _extendShape = shape;
                _extendEdge = edge;
                _extendPathIndex = pathIndex;
                _extendAtStart = end == 0;
                _extendBefore = fla::EditablePath::fromPath(*edge->paths[pathIndex]);
                bestDistance = distance;
            }
        }
    }
}

bool PencilTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    if (!_drawing)
        return false;

    // Everything the cursor reports is kept; thinning happens in the fit, where
    // the spacing rule can be applied consistently.
    _points.push_back(constrained(event, documentPos));
    takePen(view);
    view.update();
    return true;
}

fla::Point PencilTool::constrained(QMouseEvent* event, const QPointF& documentPos)
{
    fla::Point point(documentPos.x(), documentPos.y());
    if (!event || !(event->modifiers() & Qt::ShiftModifier) || _points.empty())
        return point;
    const fla::Point& start = _points.front();
    // The direction is set by the first few pixels of movement.
    if (_axis == 0)
    {
        const double dx = std::fabs(point.x - start.x);
        const double dy = std::fabs(point.y - start.y);
        if (std::max(dx, dy) < 3.0)
            return fla::Point(start.x, start.y);
        _axis = dx >= dy ? 1 : 2;
    }
    if (_axis == 1)
        point.y = start.y;
    else
        point.x = start.x;
    return point;
}

bool PencilTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_drawing)
        return false;

    _points.push_back(constrained(event, documentPos));
    takePen(view);
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

fla::CurveFit::Options PencilTool::fitOptions(PhoenixView& view) const
{
    fla::CurveFit::Options options;
    options.straighten = _style.pencilMode == fla::DrawingStyle::PencilMode::Straighten;

    // Smoothing is an amount in screen pixels: at 100 the fit may
    // stray five screen pixels from the stroke, at 0 a quarter of one. Ink mode
    // follows the stroke closely but still fits curves rather than keeping every
    // mouse sample as a corner.
    const double zoom = qMax(view.zoom(), 1.0e-6);
    const double screenTolerance = _style.pencilMode == fla::DrawingStyle::PencilMode::Ink
        ? 0.5
        : 0.25 + 4.75 * (qBound(0, _style.pencilSmoothing, 100) / 100.0);
    options.tolerance = screenTolerance / zoom;
    options.minimumSpacing = 0.5 / zoom;
    return options;
}

fla::EditablePath PencilTool::fitted(PhoenixView& view) const
{
    return fla::CurveFit::fit(steadied(view, false), fitOptions(view));
}

std::vector<fla::Point> PencilTool::steadied(PhoenixView& view, bool finished) const
{
    std::vector<fla::Point> points = drawaids::snapped(view, _points);
    if (_style.pencilStabilizer <= 0.0 || points.size() < 2)
        return points;
    std::vector<fla::BrushSample> samples;
    samples.reserve(points.size());
    for (const fla::Point& point : points)
    {
        fla::BrushSample sample;
        sample.x = point.x;
        sample.y = point.y;
        samples.push_back(sample);
    }
    const double zoom = qMax(view.zoom(), 1.0e-6);
    points.clear();
    for (const fla::BrushSample& sample : fla::BrushStroke::stabilized(samples, _style.pencilStabilizer / zoom, finished))
        points.emplace_back(sample.x, sample.y);
    return points;
}

void PencilTool::finish(PhoenixView& view)
{
    const fla::CurveFit::Options options = fitOptions(view);
    const double zoom = qMax(view.zoom(), 1.0e-6);

    // Kept to a drawing guide, stabilized, and copied across the symmetry
    // axis.
    _points = steadied(view, true);
    const std::vector<QTransform> copies = drawaids::copiesInScope(view);
    fla::EditablePath path = fla::CurveFit::fit(_points, options);

    // Straighten mode tidies what it drew: ends that nearly meet are joined,
    // and a stroke that is clearly a rectangle or an oval becomes one.
    if (options.straighten && !_extending)
    {
        fla::StrokeTidyOptions tidy;
        tidy.connectLines = _style.connectLines;
        tidy.recognizeShapes = _style.recognizeShapes;
        // The Connect Lines reach the preferences set, in screen pixels.
        tidy.closeTolerance = Preferences::connectPixels() / zoom;
        fla::tidyStraightenedStroke(path, _points, tidy);
    }

    _points.clear();

    if (path.anchors.size() < 2)
    {
        // A tap, or a stroke that went nowhere.
        _extending = false;
        view.update();
        return;
    }

    if (_extending && _extendEdge && _extendShape &&
        _extendPathIndex < _extendEdge->paths.size())
    {
        // The stroke carries the existing path on from the end it started
        // at: the old path, turned round if that was its first anchor, then
        // the stroke, joined at a corner since the two were drawn apart.
        bool invertible = false;
        const fla::Transform& t = _extendShape->transform;
        const QTransform toDocument(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
        const QTransform toShape = toDocument.inverted(&invertible);
        if (invertible)
        {
            fla::EditablePath joined = _extendBefore.transformed(t);
            if (_extendAtStart)
                joined = joined.reversed();
            joined.closed = false;

            fla::Anchor& junction = joined.anchors.back();
            junction.outHandle = path.anchors.front().outHandle;
            junction.smooth = false;
            for (size_t i = 1; i < path.anchors.size(); ++i)
                joined.anchors.push_back(path.anchors[i]);

            const fla::Transform back(toShape.m11(), toShape.m12(), toShape.m21(),
                toShape.m22(), toShape.dx(), toShape.dy());
            const fla::EditablePath after = joined.transformed(back);

            // The copies are new lines, drawn as one step with the extension.
            if (!copies.empty())
                _commandStack.beginMacro("Pencil");
            _commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
                _extendEdge, _extendBefore, after, "Pencil", _extendPathIndex)));
            _commandStack.breakMergeChain();
            view.markShapeEdited(_extendShape);
            view.clearCaches();
            fla::Frame* frame = view.activeFrame();
            if (!copies.empty())
            {
                if (frame)
                {
                    placeDrawnElement(view, _commandStack, _selection, frame, createShape(frame, copiesOf(path, copies)),
                        "Pencil", _style.objectDrawing);
                }
                _commandStack.endMacro();
            }
        }
        _extending = false;
        _extendShape = nullptr;
        _extendEdge = nullptr;
        view.update();
        return;
    }

    fla::Frame* frame = view.activeFrame();
    if (!frame)
    {
        view.update();
        return;
    }

    // The Paint Brush's brush, copied into the document from the brush
    // library on first use, as part of the stroke's step; with a pen, the
    // stroke's width follows its pressure within the brush's range.
    const bool paintBrush = _style.strokeLook.kind == fla::StrokeStyle::Style::PaintBrush;
    if (paintBrush)
    {
        view.flushPendingMerge();
        _commandStack.beginMacro(_name.toStdString());
    }
    const int brush = paintBrush ? paintBrushIndex(view) : 0;
    std::vector<fla::StrokeStyle::WidthMarker> profile;
    if (paintBrush && !_pen.empty())
    {
        const fla::Document* document = view.flaDocument() ? view.flaDocument()->document : nullptr;
        const auto library = document ? fla::paintBrushesOf(*document) : nullptr;
        const fla::PaintBrushDefinition* definition =
            library ? library->find(brush > 0 ? brush : _style.strokeLook.paintBrush) : nullptr;
        if (definition)
            profile = fla::paintBrushWidthProfile(*definition, _pen);
    }
    _pen.clear();

    std::vector<fla::EditablePath> paths = {path};
    for (fla::EditablePath& copy : copiesOf(path, copies))
        paths.push_back(std::move(copy));
    fla::Element* shape = createShape(frame, paths, brush, profile);
    if (shape)
        placeDrawnElement(view, _commandStack, _selection, frame, shape, "Pencil", _style.objectDrawing);
    else
        view.update();
    if (paintBrush)
        _commandStack.endMacro();
}

int PencilTool::paintBrushIndex(PhoenixView& view)
{
    fla::Document* document = view.flaDocument() ? view.flaDocument()->document : nullptr;
    const std::string& guid = _style.strokeLook.paintBrushGuid;
    if (!document || guid.empty())
        return 0;
    return paintBrushIndexFor(*document, _commandStack, guid);
}

fla::Element* PencilTool::createShape(fla::Frame* frame, const std::vector<fla::EditablePath>& paths, int paintBrush,
    const std::vector<fla::StrokeStyle::WidthMarker>& profile) const
{
    fla::Shape* shape = new fla::Shape(frame);

    // A pencil stroke is a line, so it is stroke only however the fill is
    // set, and drawn in the stroke's color, weight and style (its paint
    // brush too) whether or not new shapes are set to get a stroke (Stroke
    // in the Tool tab), as the brushes paint in the fill color.
    fla::DrawingStyle drawn = _style;
    drawn.hasStroke = true;
    fla::StrokeStyle* stroke = drawn.createStroke(shape);
    if (stroke && stroke->style() == fla::StrokeStyle::Style::PaintBrush)
    {
        if (paintBrush > 0)
            stroke->paintBrushDefinition = paintBrush;
        if (!profile.empty())
            stroke->widthProfile = profile;
    }
    if (!stroke)
    {
        // A style with no stroke to make at all: a hairline rather than
        // adding nothing to the document.
        fla::SolidStroke* fallback = new fla::SolidStroke(shape);
        fallback->weight = 1.0;
        fallback->fill = new fla::SolidColor(fallback);
        stroke = fallback;
    }

    shape->strokes.push_back(stroke);
    shape->strokesMap[kStyleIndex] = stroke;

    double left = paths.front().anchors[0].position.x;
    double top = paths.front().anchors[0].position.y;
    double right = left;
    double bottom = top;
    for (const fla::EditablePath& path : paths)
    {
        fla::Edge* edge = new fla::Edge(shape);
        edge->strokeStyle = kStyleIndex;
        path.applyTo(*edge);
        shape->edges.push_back(edge);
        for (const fla::Anchor& anchor : path.anchors)
        {
            left = qMin(left, anchor.position.x);
            top = qMin(top, anchor.position.y);
            right = qMax(right, anchor.position.x);
            bottom = qMax(bottom, anchor.position.y);
        }
    }

    shape->localBounds = fla::Rect(fla::Point(left, top), fla::Point(right, bottom));
    shape->bounds = shape->localBounds;

    return shape;
}

void PencilTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    if (!_drawing || _points.size() < 2)
        return;

    // The raw stroke as drawn, faintly, so the line follows the cursor with
    // no lag, and over it the fit as it stands, so what the release will
    // leave is visible before it happens.
    QPainterPath preview;
    preview.moveTo(_points[0].x, _points[0].y);
    for (size_t i = 1; i < _points.size(); ++i)
        preview.lineTo(_points[i].x, _points[i].y);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 170, 255, 90), 1.0 * scale));
    painter.drawPath(preview);

    // Where the line is while the stabilizer trails the pen.
    if (_style.pencilStabilizer > 0.0)
    {
        const std::vector<fla::Point> line = steadied(view, false);
        drawaids::paintStabilizer(painter, QPointF(_points.back().x, _points.back().y),
            QPointF(line.back().x, line.back().y), _style.pencilStabilizer * scale, scale);
    }

    const fla::EditablePath fit = fitted(view);
    if (fit.anchors.size() < 2)
        return;

    QPainterPath fitPath;
    fitPath.moveTo(fit.anchors[0].position.x, fit.anchors[0].position.y);
    for (size_t i = 1; i < fit.anchors.size(); ++i)
    {
        const fla::Anchor& from = fit.anchors[i - 1];
        const fla::Anchor& to = fit.anchors[i];
        if (!from.hasOutCurve() && !to.hasInCurve())
            fitPath.lineTo(to.position.x, to.position.y);
        else
            fitPath.cubicTo(from.outHandle.x, from.outHandle.y, to.inHandle.x, to.inHandle.y,
                to.position.x, to.position.y);
    }
    painter.setPen(QPen(QColor(0, 170, 255), 1.0 * scale));
    painter.drawPath(drawaids::withCopies(fitPath, drawaids::copiesInScope(view)));
}

void PencilTool::deactivate(PhoenixView& view)
{
    if (!_drawing)
        return;

    _drawing = false;
    _points.clear();
    view.update();
}
