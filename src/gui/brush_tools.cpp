#include "brush_tools.h"

#include <QRadialGradient>
#include "../edit/library_media.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/bitmap_instance.h"
#include "../data/bitmap.h"
#include "media_import.h"
#include "../edit/brush_texture.h"

#include "cursors.h"
#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../edit/command_stack.h"
#include "../edit/curve_fit.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/region_split.h"
#include "../edit/selection.h"
#include "../edit/shape_transform.h"
#include "../geom/shape_geometry.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr int kStyleIndex = 1;

/// The centerline of a drag as a path: the raw samples joined up, or a smooth
/// fit through them as the pencil would make.
QPainterPath centerline(const std::vector<fla::Point>& points, bool smooth, double zoom)
{
    QPainterPath path;
    if (points.empty())
        return path;

    bool stayedPut = true;
    for (const fla::Point& point : points)
    {
        if (std::hypot(point.x - points[0].x, point.y - points[0].y) > 1.0e-6)
        {
            stayedPut = false;
            break;
        }
    }
    if (stayedPut)
    {
        // A tap: a dot the size of the brush, which the stroker's round caps
        // make from a line too short to see.
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
    return stroker.createStroke(centerline(points, smooth, zoom)).simplified();
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

/// A merge shape with its arrangement, for asking what it paints where.
struct Arranged
{
    fla::Shape* shape = nullptr;
    std::unique_ptr<fla::RegionSplitter> regions;
};

Arranged arrange(fla::Shape* shape)
{
    // Arrangements read raw edge coordinates, so a shape carrying its
    // position in a transform is brought down into its geometry first.
    fla::bakeTransform(*shape);
    Arranged entry;
    entry.shape = shape;
    entry.regions.reset(new fla::RegionSplitter(*shape));
    return entry;
}

/// The shapes under an area, bottom to top, each arranged. Drawing objects
/// are left out unless asked for: the brush paints over them as fresh
/// artwork, while the eraser takes from them as it does from merge art.
std::vector<Arranged> arrangeShapesIn(PhoenixView& view, const QRectF& area,
    bool includeObjects = false)
{
    std::vector<Arranged> arranged;
    for (fla::Element* element : view.elementsIn(area))
    {
        if (!element || element->elementType() != fla::Element::Type::Shape)
            continue;
        fla::Shape* shape = static_cast<fla::Shape*>(element);
        if (shape->isDrawingObject && !includeObjects)
            continue;
        arranged.push_back(arrange(shape));
    }
    return arranged;
}

/// The fills lifted out of the artwork by the selection, arranged.
std::vector<Arranged> arrangeSelectedRegions(PhoenixView& view, const fla::Selection& selection)
{
    std::vector<Arranged> arranged;
    for (fla::DOMElement* selected : selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element || element->elementType() != fla::Element::Type::Shape ||
            !view.isFloatingRegion(element))
        {
            continue;
        }
        arranged.push_back(arrange(static_cast<fla::Shape*>(element)));
    }
    return arranged;
}

/// Whether any of the shapes paints a fill at a point.
bool paintedAt(const std::vector<Arranged>& arranged, const fla::Point& point)
{
    for (const Arranged& entry : arranged)
    {
        const int face = entry.regions->faceAt(point);
        if (face >= 0 && entry.regions->faceFill(face) != -1)
            return true;
    }
    return false;
}

/// The part of \a brush lying where \a keep holds, as a new shape the
/// caller owns, or null when none of it does. The brush is arranged together
/// with the shapes' outlines, so the test is asked once per region of the
/// brush that those outlines divide it into.
fla::Shape* restrictTo(const fla::Shape& brush, const std::vector<Arranged>& arranged,
    const std::function<bool(const fla::Point&)>& keep)
{
    std::vector<fla::Curve> cutters;
    for (const Arranged& entry : arranged)
    {
        for (const fla::ShapeCurve& curve : fla::shapeCurves(*entry.shape))
            cutters.push_back(curve.curve);
    }

    fla::RegionSplitter regions(brush, cutters);
    const std::vector<int> faces = regions.facesWhere(keep);
    if (faces.empty())
        return nullptr;

    fla::Shape* piece = nullptr;
    fla::Shape* remainder = nullptr;
    if (!regions.splitSet(faces, {}, piece, remainder))
        return nullptr;
    delete remainder;
    return piece;
}

/// The topmost shape with a region, painted or merely enclosed, under a
/// point: where a stroke in the Inside modes begins. Returns false on empty
/// stage.
bool regionUnder(PhoenixView& view, const QPointF& documentPos, Arranged& found, int& face,
    bool includeObjects)
{
    const double tolerance = view.pickTolerance();
    const QRectF probe(documentPos.x() - tolerance, documentPos.y() - tolerance,
        tolerance * 2.0, tolerance * 2.0);
    std::vector<Arranged> under = arrangeShapesIn(view, probe, includeObjects);
    for (auto it = under.rbegin(); it != under.rend(); ++it)
    {
        const int candidate = it->regions->faceAt(fla::Point(documentPos.x(), documentPos.y()));
        if (candidate >= 0)
        {
            found = std::move(*it);
            face = candidate;
            return true;
        }
    }
    return false;
}

QRectF boundsOf(const fla::Shape& shape)
{
    return QRectF(shape.localBounds.topLeft.x, shape.localBounds.topLeft.y,
        shape.localBounds.width(), shape.localBounds.height());
}

} // namespace

/// A resolved outline as a merge shape filled with the style's fill (or, for
/// the eraser's cutter, any fill). Each loop is fitted with curves to within
/// \a tolerance.
fla::Shape* outlineShape(const QPainterPath& outline, double tolerance, fla::Frame* parent,
    const fla::DrawingStyle& style, bool asFill)
{
    if (outline.isEmpty())
        return nullptr;

    fla::Shape* shape = new fla::Shape(parent);

    // The eraser's cutter needs a fill too: only a filled area takes anything
    // away. Its color never shows.
    fla::FillStyle* fill = asFill ? style.createFill(shape) : new fla::SolidColor(shape);
    if (!fill)
    {
        delete shape;
        return nullptr;
    }
    shape->fills.push_back(fill);
    shape->fillsMap[kStyleIndex] = fill;

    // Each closed loop of the outline becomes one edge. A loop inside an odd
    // number of the others is a hole, filled on its outside, whichever way
    // the outline happens to wind it: a ring stroke keeps its middle empty.
    std::vector<QPolygonF> loops;
    {
        QPainterPath loop;
        for (int i = 0; i < outline.elementCount(); ++i)
        {
            const QPainterPath::Element& element = outline.elementAt(i);
            if (element.type == QPainterPath::MoveToElement && !loop.isEmpty())
            {
                loops.push_back(loop.toFillPolygon());
                loop = QPainterPath();
            }
            if (element.type == QPainterPath::MoveToElement)
                loop.moveTo(element.x, element.y);
            else if (element.type == QPainterPath::LineToElement)
                loop.lineTo(element.x, element.y);
            else if (element.type == QPainterPath::CurveToElement && i + 2 < outline.elementCount())
            {
                const QPainterPath::Element& c2 = outline.elementAt(i + 1);
                const QPainterPath::Element& end = outline.elementAt(i + 2);
                loop.cubicTo(element.x, element.y, c2.x, c2.y, end.x, end.y);
                i += 2;
            }
        }
        if (!loop.isEmpty())
            loops.push_back(loop.toFillPolygon());
    }
    const auto isHole = [&](const QPolygonF& ring) {
        if (ring.isEmpty())
            return false;
        // A point of the loop itself: the resolved loops do not cross.
        const QPointF probe = ring.front();
        int around = 0;
        for (const QPolygonF& other : loops)
        {
            if (&other == &ring || other.size() < 3)
                continue;
            if (other.containsPoint(probe, Qt::OddEvenFill))
                ++around;
        }
        return around % 2 == 1;
    };
    size_t loopIndex = 0;
    QPainterPath subpath;
    const auto flush = [&]() {
        if (subpath.elementCount() < 2)
        {
            subpath = QPainterPath();
            return;
        }

        fla::Edge* edge = new fla::Edge(shape);
        const bool hole = loopIndex < loops.size() && isHole(loops[loopIndex]);
        ++loopIndex;
        if ((signedArea(subpath) >= 0.0) != hole)
            edge->fillStyle1 = kStyleIndex;
        else
            edge->fillStyle0 = kStyleIndex;

        // The resolved outline is a dense polygon: the stroker and the
        // simplification both flatten curves into short straight runs. Fitted
        // again, as the pencil fits a freehand stroke, each loop becomes a
        // few cubics, which is what Animate's brush leaves behind.
        std::vector<fla::Point> ring;
        const QPolygonF polygon = subpath.toFillPolygon();
        for (const QPointF& point : polygon)
            ring.push_back(fla::Point(point.x(), point.y()));
        if (ring.size() > 1 &&
            std::hypot(ring.back().x - ring.front().x, ring.back().y - ring.front().y) > 1.0e-6)
        {
            ring.push_back(ring.front());
        }

        fla::CurveFit::Options options;
        options.tolerance = tolerance;
        options.minimumSpacing = tolerance * 0.4;
        fla::EditablePath fitted = fla::CurveFit::fit(ring, options);
        if (fitted.anchors.size() >= 2)
        {
            // Ending where it began makes it a loop; the fitter may keep the
            // seam point as a last anchor, which the closing segment covers.
            const fla::Point& first = fitted.anchors.front().position;
            const fla::Point& last = fitted.anchors.back().position;
            if (fitted.anchors.size() > 2 &&
                std::hypot(last.x - first.x, last.y - first.y) < 1.0e-6)
            {
                fitted.anchors.pop_back();
            }
            fitted.closed = true;
            fitted.applyTo(*edge);
        }
        else
        {
            // Too little to fit: the polygon as it is.
            fla::Path* path = new fla::Path(edge);
            path->segments.push_back(new fla::PathSegment(
                fla::PathSegment::Command::Move, {ring.front()}, path));
            for (size_t i = 1; i < ring.size(); ++i)
            {
                path->segments.push_back(new fla::PathSegment(
                    fla::PathSegment::Command::Line, {ring[i]}, path));
            }
            edge->paths.push_back(path);
        }

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
    fla::DrawingStyle::fitFills(*shape);
    return shape;
}

fla::Shape* brushOutlineShape(const std::vector<fla::Point>& points, double width,
    bool smooth, fla::Frame* parent, const fla::DrawingStyle& style, bool asFill)
{
    return outlineShape(brushOutline(points, width, smooth, 1.0), 0.3, parent, style, asFill);
}

QPainterPath sweptOutline(const std::vector<fla::BrushDab>& dabs, double zoom, bool resolve)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const std::vector<fla::Point>& polygon : fla::BrushStroke::sweep(dabs, zoom))
    {
        if (polygon.size() < 3)
            continue;
        QPolygonF ring;
        ring.reserve(static_cast<int>(polygon.size()));
        for (const fla::Point& point : polygon)
            ring << QPointF(point.x, point.y);
        path.addPolygon(ring);
        path.closeSubpath();
    }
    return resolve ? path.simplified() : path;
}

// ---------------------------------------------------------------- brush

QString BrushTool::name() const
{
    switch (_kind)
    {
    case Kind::Fluid: return "Brush";
    case Kind::Classic: return "Nib Brush";
    case Kind::Ink: return "Ink Brush";
    }
    return "Brush";
}

const fla::BrushSettings& BrushTool::settings() const
{
    switch (_kind)
    {
    case Kind::Classic: return _style.classicBrush;
    case Kind::Ink: return _style.inkBrush;
    case Kind::Fluid: break;
    }
    return _style.fluidBrush;
}

void BrushTool::takeSamples(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (_pen)
    {
        std::vector<fla::BrushSample> taken = view.takeTabletSamples();
        for (fla::BrushSample& sample : taken)
            snapToGuide(sample);
        _samples.insert(_samples.end(), taken.begin(), taken.end());
        if (!_samples.empty())
            return;
    }
    fla::BrushSample sample;
    sample.x = documentPos.x();
    sample.y = documentPos.y();
    sample.pressure = _pen ? view.pressure() : 1.0;
    sample.time = event ? static_cast<double>(event->timestamp()) : 0.0;
    // Shift keeps the stroke straight across or down, its direction set
    // by the first few pixels of movement.
    if (event && (event->modifiers() & Qt::ShiftModifier) && !_samples.empty())
    {
        const fla::BrushSample& start = _samples.front();
        if (_axis == 0)
        {
            const double dx = std::fabs(sample.x - start.x);
            const double dy = std::fabs(sample.y - start.y);
            if (std::max(dx, dy) >= 3.0)
                _axis = dx >= dy ? 1 : 2;
        }
        if (_axis == 1)
            sample.y = start.y;
        else if (_axis == 2)
            sample.x = start.x;
        else
            return;
    }
    snapToGuide(sample);
    _samples.push_back(sample);
}

void BrushTool::snapToGuide(fla::BrushSample& sample) const
{
    if (_guideAxis == 1)
        sample.x = _guideAt;
    else if (_guideAxis == 2)
        sample.y = _guideAt;
}

fla::BrushSettings BrushTool::effective(PhoenixView& view) const
{
    fla::BrushSettings brush = settings();
    if (brush.sizeFollowsCamera)
        brush.size /= view.cameraScaleFor(view.activeFrame());
    return brush;
}

bool BrushTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !view.activeFrame() || !_style.hasFill)
        return false;

    // Paint Inside keeps to the region the stroke begins in, so that is
    // found now, before anything is drawn.
    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();
    if (_style.brushMode == fla::DrawingStyle::BrushMode::Inside)
    {
        Arranged hit;
        int face = -1;
        if (regionUnder(view, documentPos, hit, face, false))
        {
            _insideShape = hit.shape;
            _insideFace = face;
            _insideRegions = std::move(hit.regions);
        }
    }

    _drawing = true;
    _samples.clear();
    _axis = 0;
    // Snap to Guides: a stroke begun on a ruler guide keeps to it.
    _guideAxis = 0;
    if (_style.brushSnapGuides)
    {
        std::vector<double> xs;
        std::vector<double> ys;
        view.toolGuides(xs, ys);
        double nearest = 8.0 / std::max(view.zoom(), 1.0e-6);
        for (double x : xs)
        {
            if (std::fabs(documentPos.x() - x) < nearest)
            {
                nearest = std::fabs(documentPos.x() - x);
                _guideAxis = 1;
                _guideAt = x;
            }
        }
        for (double y : ys)
        {
            if (std::fabs(documentPos.y() - y) < nearest)
            {
                nearest = std::fabs(documentPos.y() - y);
                _guideAxis = 2;
                _guideAt = y;
            }
        }
    }
    _pen = view.penDown();
    takeSamples(view, event, documentPos);
    view.update();
    return true;
}

bool BrushTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    _hover = documentPos;
    _hoverValid = true;

    if (_drawing)
        takeSamples(view, event, documentPos);

    view.update();
    return _drawing;
}

bool BrushTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_drawing)
        return false;

    if (_pen)
    {
        // The pen's last readings; the release itself adds nothing.
        std::vector<fla::BrushSample> taken = view.takeTabletSamples();
        for (fla::BrushSample& sample : taken)
            snapToGuide(sample);
        _samples.insert(_samples.end(), taken.begin(), taken.end());
    }
    else
    {
        takeSamples(view, event, documentPos);
    }
    _drawing = false;
    finish(view);
    return true;
}

bool BrushTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || !_drawing)
        return false;

    _drawing = false;
    _samples.clear();
    view.update();
    return true;
}

void BrushTool::finish(PhoenixView& view)
{
    fla::Frame* frame = view.activeFrame();
    if (!frame || _samples.empty())
    {
        _samples.clear();
        view.update();
        return;
    }

    // Precision sets how closely the curves fitted to the outline follow
    // it, from a sixth of a screen pixel at 100 to a pixel and a half at 0.
    const double zoom = std::max(view.zoom(), 1.0e-6);
    const fla::BrushSettings brush = effective(view);
    const double precision = std::max(0, std::min(100, brush.precision)) / 100.0;
    const double tolerance = (0.15 + (1.0 - precision) * 1.35) / zoom;
    const std::vector<fla::BrushDab> dabs = fla::BrushStroke::dabs(_samples, brush, zoom, true);
    // The pen's touch over the stroke, for a texture whose strength
    // follows it.
    double meanPressure = 1.0;
    if (_pen && !_samples.empty())
    {
        double sum = 0.0;
        for (const fla::BrushSample& sample : _samples)
            sum += sample.pressure;
        meanPressure = sum / static_cast<double>(_samples.size());
    }
    if (_style.bitmapBrush)
    {
        _samples.clear();
        paintBitmap(view, frame, dabs);
        return;
    }
    QPainterPath swept = sweptOutline(dabs, zoom, false);
    // Auto Fill: a stroke that closes on itself is filled in.
    const std::vector<fla::Point> loop = closedLoop(dabs, brush.size, zoom);
    if (loop.size() >= 3)
    {
        QPolygonF ring;
        for (const fla::Point& point : loop)
            ring << QPointF(point.x, point.y);
        swept.addPolygon(ring);
        swept.closeSubpath();
    }
    fla::Shape* shape = outlineShape(swept.simplified(), tolerance, frame, _style, true);
    _samples.clear();
    // Lock Fill: a gradient or bitmap over the whole stage, not the stroke.
    if (shape && _style.lockFill)
    {
        const QRectF stage = view.stageInScope();
        fla::DrawingStyle::fitFillsTo(*shape, stage.left(), stage.top(), stage.right(), stage.bottom());
    }
    if (!shape)
    {
        view.update();
        return;
    }

    // A textured stroke keeps its grain as its own object, so merging never
    // spreads the grain to other paint or takes it away.
    const bool textured = !_style.brushTexture.empty();
    const auto texture = [this, meanPressure](fla::Shape* painted) {
        if (!painted || _style.brushTexture.empty())
            return;
        fla::texture::Spec spec = textureSpec();
        // A light touch shows more of the grain, as chalk does.
        if (_style.brushTexturePressure)
            spec.strength = std::clamp(spec.strength * (1.75 - 1.5 * meanPressure), 0.0, 1.0);
        fla::texture::set(painted->extraAttributes, spec);
        painted->isDrawingObject = true;
    };
    texture(shape);

    using BrushMode = fla::DrawingStyle::BrushMode;
    const BrushMode mode = _style.brushMode;

    if (mode == BrushMode::Normal || mode == BrushMode::Fills)
    {
        placeDrawnElement(view, _commandStack, _selection, frame, shape, "Brush",
            _style.objectDrawing || textured, mode == BrushMode::Fills);
        return;
    }

    // The other modes keep only part of the stroke.
    std::unique_ptr<fla::Shape> whole(shape);
    fla::Shape* piece = nullptr;
    std::vector<Arranged> arranged;
    switch (mode)
    {
    case BrushMode::Behind:
        arranged = arrangeShapesIn(view, boundsOf(*whole));
        piece = restrictTo(*whole, arranged, [&](const fla::Point& point) {
            return !paintedAt(arranged, point);
        });
        break;

    case BrushMode::Selection:
        arranged = arrangeSelectedRegions(view, _selection);
        if (!arranged.empty())
        {
            piece = restrictTo(*whole, arranged, [&](const fla::Point& point) {
                return paintedAt(arranged, point);
            });
        }
        break;

    case BrushMode::Inside:
        if (_insideShape && _insideRegions)
        {
            Arranged began;
            began.shape = _insideShape;
            began.regions = std::move(_insideRegions);
            arranged.push_back(std::move(began));
            const int face = _insideFace;
            piece = restrictTo(*whole, arranged, [&](const fla::Point& point) {
                return arranged.front().regions->faceAt(point) == face;
            });
        }
        else
        {
            // Begun on empty stage: only empty stage is painted.
            arranged = arrangeShapesIn(view, boundsOf(*whole));
            piece = restrictTo(*whole, arranged, [&](const fla::Point& point) {
                return !paintedAt(arranged, point);
            });
        }
        break;

    case BrushMode::Repaint:
        // Only over paint already there, which it recolors.
        arranged = arrangeShapesIn(view, boundsOf(*whole));
        piece = restrictTo(*whole, arranged, [&](const fla::Point& point) {
            return paintedAt(arranged, point);
        });
        break;

    case BrushMode::Normal:
    case BrushMode::Fills:
        break;
    }

    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();

    if (!piece)
    {
        view.update();
        return;
    }

    piece->parent = frame;
    texture(piece);
    placeDrawnElement(view, _commandStack, _selection, frame, piece, "Brush",
        _style.objectDrawing || textured, true);
}

void BrushTool::paintBitmap(PhoenixView& view, fla::Frame* frame, const std::vector<fla::BrushDab>& dabs)
{
    const fla::FLADocument* fla = view.flaDocument();
    fla::Document* document = fla ? fla->document : nullptr;
    if (!document || dabs.empty())
    {
        view.update();
        return;
    }
    // Canvas pixels to a stage pixel, so paint stays crisp zoomed in a little.
    constexpr double kDensity = 2.0;
    constexpr double kMaxPixels = 8192.0;

    QRectF stroke;
    for (const fla::BrushDab& dab : dabs)
    {
        const double reach = std::max(dab.width, dab.height) * 0.5 + 2.0;
        stroke = stroke.united(QRectF(dab.x - reach, dab.y - reach, reach * 2.0, reach * 2.0));
    }

    // The frame's canvas: a library bitmap placed on it, marked as Phoenix's.
    fla::BitmapInstance* canvas = nullptr;
    for (fla::Element* element : frame->elements)
    {
        auto* candidate = dynamic_cast<fla::BitmapInstance*>(element);
        if (!candidate)
            continue;
        for (const auto& attribute : candidate->extraAttributes)
            if (attribute.first == "phoenixPaint")
                canvas = candidate;
    }
    fla::Bitmap* bitmap = nullptr;
    QImage old;
    QRectF was;
    if (canvas)
    {
        for (fla::Resource* resource : document->resources)
        {
            if (resource && resource->resourceType() == fla::Resource::Type::Bitmap &&
                static_cast<fla::Bitmap*>(resource)->name == canvas->libraryItemName)
                bitmap = static_cast<fla::Bitmap*>(resource);
        }
        old = view.libraryBitmap(QString::fromStdString(canvas->libraryItemName)).toImage();
        if (!bitmap || old.isNull())
            canvas = nullptr;
        else
            was = QRectF(canvas->transform.tx, canvas->transform.ty, old.width() / kDensity, old.height() / kDensity);
    }
    if (!canvas && _style.bitmapErase)
    {
        view.update();
        return;
    }

    // Big enough for what is there and the stroke; a new canvas at the scene
    // starts as the stage.
    QRectF area = was.united(stroke.adjusted(-32.0, -32.0, 32.0, 32.0));
    if (!canvas && !view.inSymbol())
        area = area.united(QRectF(0.0, 0.0, document->width, document->height));
    area = QRectF(std::floor(area.left()), std::floor(area.top()),
        std::min(kMaxPixels / kDensity, std::ceil(area.width()) + 1.0),
        std::min(kMaxPixels / kDensity, std::ceil(area.height()) + 1.0));
    const int width = static_cast<int>(area.width() * kDensity);
    const int height = static_cast<int>(area.height() * kDensity);

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (!old.isNull())
    {
        QPainter copy(&image);
        copy.drawImage(QPointF((was.left() - area.left()) * kDensity, (was.top() - area.top()) * kDensity),
            old.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }

    // The stroke on its own first, so its opacity, blend and the paper's
    // grain apply to it as a whole. At full flow the dabs do not build up
    // where they overlap (each pixel keeps the strongest), so a stroke is
    // even; below it they do, as an airbrush's.
    QImage layer(width, height, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter paint(&layer);
        paint.setRenderHint(QPainter::Antialiasing);
        paint.setRenderHint(QPainter::SmoothPixmapTransform);
        paint.scale(kDensity, kDensity);
        paint.translate(-area.topLeft());
        paint.setPen(Qt::NoPen);
        const QColor color(_style.fillColor[0], _style.fillColor[1], _style.fillColor[2], _style.fillColor[3]);
        const double flow = std::clamp(_style.bitmapFlow / 100.0, 0.01, 1.0);
        const bool buildsUp = flow < 0.999 || _style.bitmapFlowPressure;
        paint.setCompositionMode(buildsUp ? QPainter::CompositionMode_SourceOver : QPainter::CompositionMode_Lighten);
        // Auto Fill: the loop the stroke closes, painted solid under it.
        const std::vector<fla::Point> loop = closedLoop(dabs, settings().size, view.zoom());
        if (loop.size() >= 3)
        {
            QPolygonF ring;
            for (const fla::Point& point : loop)
                ring << QPointF(point.x, point.y);
            paint.setBrush(color);
            paint.drawPolygon(ring);
        }
        const QImage tip = bitmapTip(view, color, dabs.front());
        // Dabs the spacing apart, as a percentage of the tip.
        const double spacing = std::max(0.01, _style.bitmapSpacing / 100.0);
        double since = 1.0e9;
        const fla::BrushDab* last = nullptr;
        for (const fla::BrushDab& dab : dabs)
        {
            if (last)
                since += std::hypot(dab.x - last->x, dab.y - last->y);
            last = &dab;
            const double gap = std::max(0.25, spacing * std::max(dab.width, dab.height));
            if (since < gap && &dab != &dabs.back())
                continue;
            since = 0.0;
            double alpha = dab.opacity;
            if (_style.bitmapOpacityPressure)
                alpha *= dab.pressure;
            if (buildsUp)
                alpha *= flow * (_style.bitmapFlowPressure ? dab.pressure : 1.0);
            if (alpha <= 0.001)
                continue;
            paint.save();
            paint.translate(dab.x, dab.y);
            // The dab's turn is in radians.
            paint.rotate(dab.angle * 180.0 / 3.14159265358979323846);
            paint.scale(std::max(dab.width, 0.25) / tip.width(), std::max(dab.height, 0.25) / tip.height());
            paint.setOpacity(std::clamp(alpha, 0.0, 1.0));
            paint.drawImage(QPointF(-tip.width() / 2.0, -tip.height() / 2.0), tip);
            paint.restore();
        }
        // The paper's grain, when the brush has one.
        if (!_style.brushTexture.empty())
        {
            paint.setOpacity(1.0);
            paint.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            paint.fillRect(area, view.texturedBrush(Qt::black, textureSpec()));
        }
    }
    {
        QPainter merge(&image);
        merge.setOpacity(std::clamp(_style.bitmapOpacity / 100.0, 0.0, 1.0));
        using Blend = fla::DrawingStyle::BitmapBlend;
        QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver;
        switch (_style.bitmapBlend)
        {
        case Blend::Normal: mode = QPainter::CompositionMode_SourceOver; break;
        case Blend::Multiply: mode = QPainter::CompositionMode_Multiply; break;
        case Blend::Screen: mode = QPainter::CompositionMode_Screen; break;
        case Blend::Overlay: mode = QPainter::CompositionMode_Overlay; break;
        case Blend::Darken: mode = QPainter::CompositionMode_Darken; break;
        case Blend::Lighten: mode = QPainter::CompositionMode_Lighten; break;
        case Blend::Add: mode = QPainter::CompositionMode_Plus; break;
        }
        if (_style.bitmapErase)
            mode = QPainter::CompositionMode_DestinationOut;
        merge.setCompositionMode(mode);
        merge.drawImage(0, 0, layer);
    }

    const char* name = _style.bitmapErase ? "Bitmap Erase" : "Bitmap Brush";
    const fla::Transform placed(1.0 / kDensity, 0.0, 0.0, 1.0 / kDensity, area.left(), area.top());
    _commandStack.beginMacro(name);
    if (canvas)
    {
        _commandStack.push(fla::CommandPtr(
            new fla::SetBitmapPixelsCommand(bitmap, MediaImport::losslessData(image), width, height, name)));
        if (placed.tx != canvas->transform.tx || placed.ty != canvas->transform.ty)
            _commandStack.push(fla::CommandPtr(
                new fla::SetElementTransformCommand(canvas, canvas->transform, placed, name)));
        view.forgetBitmap(QString::fromStdString(bitmap->name));
    }
    else
    {
        fla::Bitmap* made = MediaImport::bitmapFromImage(*document, image, "Paint.png");
        if (made)
        {
            auto* instance = new fla::BitmapInstance(frame);
            instance->libraryItemName = made->name;
            instance->transform = placed;
            instance->extraAttributes.emplace_back("phoenixPaint", "1");
            _commandStack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, made, name)));
            _commandStack.push(fla::CommandPtr(new fla::AddElementCommand(frame, instance, name, &_selection)));
        }
    }
    _commandStack.endMacro();
    _commandStack.breakMergeChain();
    view.clearCaches();
    view.update();
}

std::vector<fla::Point> BrushTool::closedLoop(const std::vector<fla::BrushDab>& dabs, double size, double zoom) const
{
    std::vector<fla::Point> loop;
    if (!_style.brushAutoFill || dabs.size() < 3)
        return loop;
    double length = 0.0;
    for (size_t i = 1; i < dabs.size(); ++i)
        length += std::hypot(dabs[i].x - dabs[i - 1].x, dabs[i].y - dabs[i - 1].y);
    // Closed when it ends near where it began, having gone round.
    const double reach = std::max(size, 8.0 / std::max(zoom, 1.0e-6));
    const double gap = std::hypot(dabs.back().x - dabs.front().x, dabs.back().y - dabs.front().y);
    if (gap > reach || length < reach * 4.0)
        return loop;
    loop.reserve(dabs.size());
    for (const fla::BrushDab& dab : dabs)
        loop.push_back(fla::Point(dab.x, dab.y));
    return loop;
}

fla::texture::Spec BrushTool::textureSpec() const
{
    fla::texture::Spec spec;
    spec.name = _style.brushTexture;
    spec.scale = std::max(0.01, _style.brushTextureScale / 100.0);
    spec.strength = std::clamp(_style.brushTextureStrength / 100.0, 0.0, 1.0);
    spec.offsetX = _style.brushTextureOffsetX;
    spec.offsetY = _style.brushTextureOffsetY;
    spec.rotation = _style.brushTextureRotation;
    spec.fixed = _style.brushTextureFixed;
    return spec;
}

QImage BrushTool::bitmapTip(PhoenixView& view, const QColor& color, const fla::BrushDab& dab) const
{
    constexpr int kSize = 128;
    // Coverage first, white on clear.
    QImage coverage(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    coverage.fill(Qt::transparent);
    {
        QPainter paint(&coverage);
        paint.setRenderHint(QPainter::Antialiasing);
        paint.setRenderHint(QPainter::SmoothPixmapTransform);
        const QImage stamp = _style.bitmapTip.empty()
            ? QImage() : view.libraryBitmap(QString::fromStdString(_style.bitmapTip)).toImage();
        if (!stamp.isNull())
        {
            // A library bitmap: its alpha where it has one, else dark is
            // paint, as ink on paper.
            QImage source = stamp.convertToFormat(QImage::Format_ARGB32).scaled(kSize, kSize,
                Qt::KeepAspectRatio, Qt::SmoothTransformation);
            bool transparent = false;
            for (int y = 0; y < source.height() && !transparent; ++y)
            {
                const QRgb* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
                for (int x = 0; x < source.width(); ++x)
                    transparent = transparent || qAlpha(line[x]) < 250;
            }
            for (int y = 0; y < source.height(); ++y)
            {
                QRgb* line = reinterpret_cast<QRgb*>(source.scanLine(y));
                for (int x = 0; x < source.width(); ++x)
                {
                    const int a = transparent ? qAlpha(line[x]) : 255 - qGray(line[x]);
                    line[x] = qPremultiply(qRgba(255, 255, 255, a));
                }
            }
            paint.drawImage(QPointF((kSize - source.width()) / 2.0, (kSize - source.height()) / 2.0), source);
        }
        else if (dab.shape && dab.shape->size() >= 3)
        {
            // The tip's outline, filled.
            QPolygonF ring;
            for (const fla::Point& point : *dab.shape)
                ring << QPointF((point.x + 0.5) * kSize, (point.y + 0.5) * kSize);
            paint.setPen(Qt::NoPen);
            paint.setBrush(Qt::white);
            paint.drawPolygon(ring);
        }
        else if (dab.rectangular)
        {
            paint.fillRect(QRectF(0, 0, kSize, kSize), Qt::white);
        }
        else
        {
            // The round tip: solid to where the softness begins, then falling
            // away, slowly when hard and quickly when not.
            const double softness = std::clamp(_style.bitmapSoftness / 100.0, 0.0, 0.99);
            const double hardness = std::clamp(_style.bitmapHardness / 100.0, 0.0, 1.0);
            const double exponent = 3.0 - 2.7 * hardness;
            QRadialGradient gradient(QPointF(kSize / 2.0, kSize / 2.0), kSize / 2.0);
            gradient.setColorAt(0.0, Qt::white);
            gradient.setColorAt(1.0 - softness, Qt::white);
            for (int k = 1; k <= 6; ++k)
            {
                const double u = k / 7.0;
                QColor shade(255, 255, 255, static_cast<int>(std::lround(255.0 * std::pow(1.0 - u, exponent))));
                gradient.setColorAt(1.0 - softness + softness * u, shade);
            }
            gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
            paint.setPen(Qt::NoPen);
            paint.setBrush(gradient);
            paint.drawEllipse(QRectF(0, 0, kSize, kSize));
        }
        // The tip's own texture, turning with it.
        if (!_style.bitmapTipTexture.empty())
        {
            fla::texture::Spec spec;
            spec.name = _style.bitmapTipTexture;
            spec.scale = 0.5;
            paint.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            paint.fillRect(QRectF(0, 0, kSize, kSize), view.texturedBrush(Qt::black, spec));
        }
    }
    // Then the color, through the coverage.
    QImage tip(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    tip.fill(color);
    QPainter tint(&tip);
    tint.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    tint.drawImage(0, 0, coverage);
    return tip;
}

void BrushTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{

    painter.setPen(Qt::NoPen);

    const double zoom = std::max(view.zoom(), 1.0e-6);
    const fla::BrushSettings brush = effective(view);
    if (_drawing && !_samples.empty())
    {
        // The stroke as it will land: the same sweep, in the fill color.
        painter.setBrush(QColor(_style.fillColor[0], _style.fillColor[1],
            _style.fillColor[2], _style.fillColor[3]));
        painter.drawPath(sweptOutline(fla::BrushStroke::dabs(_samples, brush, zoom, false), zoom, false));
    }

    if (_hoverValid)
    {
        // The brush itself: the tip's outline at full size under the cursor.
        fla::BrushDab tip;
        tip.x = _hover.x();
        tip.y = _hover.y();
        tip.width = std::max(brush.size, 0.1);
        tip.height = std::max(brush.size * std::max(1.0, std::min(100.0, brush.roundness)) / 100.0, 0.1);
        tip.angle = brush.angle * 3.14159265358979323846 / 180.0;
        tip.rectangular = brush.tip == fla::BrushSettings::Tip::Rectangle;
        const std::vector<fla::Point> shape = fla::tipPolygon(brush);
        if (shape.size() >= 3)
            tip.shape = std::make_shared<const std::vector<fla::Point>>(shape);
        QPolygonF ring;
        for (const fla::Point& point : fla::BrushStroke::dabOutline(tip, zoom))
            ring << QPointF(point.x, point.y);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.drawPolygon(ring);
    }
}

void BrushTool::deactivate(PhoenixView& view)
{
    _drawing = false;
    _samples.clear();
    _hoverValid = false;
    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();
    view.update();
}

// ---------------------------------------------------------------- eraser

QCursor EraserTool::cursor() const
{
    return _style.faucet ? Cursors::cursor(Cursors::Kind::Faucet) : QCursor(Qt::BlankCursor);
}

bool EraserTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !view.activeFrame())
        return false;

    if (_style.faucet)
    {
        faucet(view, documentPos);
        return true;
    }

    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();
    if (_style.eraserMode == fla::DrawingStyle::EraserMode::Inside)
    {
        Arranged hit;
        int face = -1;
        if (regionUnder(view, documentPos, hit, face, true))
        {
            _insideShape = hit.shape;
            _insideFace = face;
            _insideRegions = std::move(hit.regions);
        }
    }

    _erasing = true;
    _points.clear();
    _samples.clear();
    _pen = view.penDown();
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    takeSamples(view, event, documentPos);
    view.update();
    return true;
}

fla::BrushSettings EraserTool::nib() const
{
    if (_style.eraserSyncWithBrush)
        return _style.classicBrush;
    fla::BrushSettings nib = _style.eraserBrush;
    nib.size = _style.eraserSize;
    return nib;
}

void EraserTool::takeSamples(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (_pen)
    {
        const std::vector<fla::BrushSample> taken = view.takeTabletSamples();
        _samples.insert(_samples.end(), taken.begin(), taken.end());
        if (!taken.empty())
            return;
    }
    fla::BrushSample sample;
    sample.x = documentPos.x();
    sample.y = documentPos.y();
    sample.pressure = _pen ? view.pressure() : 1.0;
    sample.time = event ? static_cast<double>(event->timestamp()) : 0.0;
    _samples.push_back(sample);
}

bool EraserTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    _hover = documentPos;
    _hoverValid = true;

    if (_erasing)
    {
        _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
        takeSamples(view, event, documentPos);
    }

    view.update();
    return _erasing;
}

bool EraserTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_erasing)
        return false;

    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    takeSamples(view, event, documentPos);
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

    // The pass swept by the eraser's nib, at the pen's pressure and tilt
    // when it uses them.
    const double zoom = std::max(view.zoom(), 1.0e-6);
    const std::vector<fla::BrushDab> dabs = fla::BrushStroke::dabs(_samples, nib(), zoom, true);
    std::unique_ptr<fla::Shape> cutter(dabs.empty()
            ? brushOutlineShape(_points, _style.eraserSize, false, nullptr, _style, false)
            : outlineShape(sweptOutline(dabs, zoom, true), 0.3 / zoom, nullptr, _style, false));
    _points.clear();
    _samples.clear();
    if (!cutter)
    {
        view.update();
        return;
    }

    using EraserMode = fla::DrawingStyle::EraserMode;
    const EraserMode mode = _style.eraserMode;

    std::vector<Arranged> targets;
    std::unique_ptr<fla::Shape> restricted;
    const fla::Shape* applied = cutter.get();
    switch (mode)
    {
    case EraserMode::Normal:
    case EraserMode::Fills:
    case EraserMode::Lines:
        // Anything floating goes in first, so the eraser works on the artwork
        // as it will be. Drawing objects lose what the eraser covers too, and
        // stay objects.
        view.flushPendingMerge();
        targets = arrangeShapesIn(view, boundsOf(*cutter), true);
        break;

    case EraserMode::SelectedFills:
        // Only what the selection lifted out, which stays lifted.
        targets = arrangeSelectedRegions(view, _selection);
        break;

    case EraserMode::Inside:
        // Only the region the pass began in; begun on empty stage, nothing.
        if (_insideShape && _insideRegions)
        {
            Arranged began;
            began.shape = _insideShape;
            began.regions = std::move(_insideRegions);
            targets.push_back(std::move(began));
            const int face = _insideFace;
            restricted.reset(restrictTo(*cutter, targets, [&](const fla::Point& point) {
                return targets.front().regions->faceAt(point) == face;
            }));
            applied = restricted.get();
        }
        break;
    }

    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();

    if (targets.empty() || !applied)
    {
        view.update();
        return;
    }

    // Every mode but Normal leaves the lines alone; Lines takes only them.
    fla::MergeOptions options;
    options.keepStrokes = mode != EraserMode::Normal;

    _commandStack.beginMacro("Eraser");
    for (const Arranged& target : targets)
    {
        if (mode == EraserMode::Lines)
        {
            _commandStack.push(fla::CommandPtr(
                new fla::EraseStrokesCommand(target.shape, *applied, "Eraser")));
        }
        else
        {
            _commandStack.push(fla::CommandPtr(
                new fla::SubtractShapeCommand(target.shape, *applied, "Eraser", options)));
        }
    }
    _commandStack.endMacro();
    _commandStack.breakMergeChain();

    view.clearCaches();
    view.update();
}

void EraserTool::faucet(PhoenixView& view, const QPointF& documentPos)
{
    const double tolerance = view.pickTolerance();
    const QRectF probe(documentPos.x() - tolerance, documentPos.y() - tolerance,
        tolerance * 2.0, tolerance * 2.0);

    // Topmost first: what the click lands on is what the stage shows there.
    std::vector<Arranged> under = arrangeShapesIn(view, probe, true);
    for (auto it = under.rbegin(); it != under.rend(); ++it)
    {
        const fla::RegionPick pick = it->regions->pickAt(
            fla::Point(documentPos.x(), documentPos.y()), tolerance, false);
        if (pick.isEmpty())
            continue;

        std::unique_ptr<fla::Shape> after(pick.kind == fla::RegionPick::Kind::Fill
            ? it->regions->withFaceFill(pick.face, -1)
            : it->regions->withStroke(pick, -1));
        if (!after)
            continue;

        _commandStack.push(fla::CommandPtr(
            new fla::SetShapeContentsCommand(it->shape, *after, "Faucet")));
        _commandStack.breakMergeChain();
        view.clearCaches();
        view.update();
        return;
    }
}

void EraserTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    painter.setPen(Qt::NoPen);

    const fla::BrushSettings eraser = nib();
    if (_erasing && !_samples.empty())
    {
        // What will be taken away, shown as the stage color so it reads as
        // already gone.
        painter.setBrush(QColor(255, 255, 255, 200));
        const double zoom = std::max(view.zoom(), 1.0e-6);
        painter.drawPath(sweptOutline(fla::BrushStroke::dabs(_samples, eraser, zoom, false), zoom, false));
    }

    if (_hoverValid && !_style.faucet)
    {
        // The nib, its shape and turn.
        fla::BrushDab tip;
        tip.x = _hover.x();
        tip.y = _hover.y();
        tip.width = eraser.size;
        tip.height = eraser.size * std::max(1.0, eraser.roundness) / 100.0;
        tip.angle = eraser.angle;
        tip.rectangular = eraser.tip == fla::BrushSettings::Tip::Rectangle;
        QPolygonF outline;
        for (const fla::Point& point : fla::BrushStroke::dabOutline(tip, 1.0))
            outline << QPointF(point.x, point.y);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.drawPolygon(outline);
    }
}

void EraserTool::deactivate(PhoenixView& view)
{
    _erasing = false;
    _points.clear();
    _hoverValid = false;
    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();
    view.update();
}
