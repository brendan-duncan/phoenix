#include "brush_tools.h"

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

/// The centreline of a drag as a path: the raw samples joined up, or a smooth
/// fit through them as the pencil would make.
QPainterPath centreline(const std::vector<fla::Point>& points, bool smooth, double zoom)
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

/// The outline of a stroke whose width follows the pen's pressure: the
/// centreline is walked at a steady pace, each step offset to either side by
/// half the width the pressure there allows, the two sides joined by round
/// caps, and the ring resolved so that where it overlaps itself it is still
/// one region.
QPainterPath pressureOutline(const QPainterPath& centre, double width,
    const std::vector<fla::Point>& raw, const std::vector<double>& pressures)
{
    const double length = centre.length();
    if (length < 1.0e-6 || raw.size() != pressures.size() || raw.empty())
        return QPainterPath();

    std::vector<double> along(raw.size(), 0.0);
    for (size_t i = 1; i < raw.size(); ++i)
        along[i] = along[i - 1] + std::hypot(raw[i].x - raw[i - 1].x, raw[i].y - raw[i - 1].y);
    const double total = along.back();

    const auto pressureAt = [&](double fraction) {
        if (total <= 1.0e-9)
            return pressures.front();
        const double target = fraction * total;
        for (size_t i = 1; i < raw.size(); ++i)
        {
            if (along[i] >= target)
            {
                const double span = along[i] - along[i - 1];
                const double t = span > 1.0e-9 ? (target - along[i - 1]) / span : 1.0;
                return pressures[i - 1] + (pressures[i] - pressures[i - 1]) * t;
            }
        }
        return pressures.back();
    };

    const int steps = std::max(8, static_cast<int>(length / 1.5));
    std::vector<QPointF> left;
    std::vector<QPointF> right;
    std::vector<QPointF> centres;
    std::vector<QPointF> directions;
    for (int s = 0; s <= steps; ++s)
    {
        const double t = static_cast<double>(s) / steps;
        const QPointF p = centre.pointAtPercent(t);
        const double degrees = centre.angleAtPercent(t);
        const double radians = degrees * 3.14159265358979323846 / 180.0;
        // Qt measures the angle with y up; the stage has y down.
        const QPointF d(std::cos(radians), -std::sin(radians));
        const QPointF n(-d.y(), d.x());
        const double half = std::max(0.05, std::min(1.0, pressureAt(t))) * width * 0.5;
        left.push_back(p + n * half);
        right.push_back(p - n * half);
        centres.push_back(p);
        directions.push_back(d);
    }

    // A half circle from one side to the other round an end, bulging the way
    // the stroke travels there.
    const auto cap = [](std::vector<QPointF>& out, const QPointF& centre, const QPointF& from,
        const QPointF& travel) {
        const QPointF v = from - centre;
        QPointF turned(-v.y(), v.x());
        double sign = 1.0;
        if (turned.x() * travel.x() + turned.y() * travel.y() < 0.0)
            sign = -1.0;
        const int pieces = 12;
        for (int k = 1; k < pieces; ++k)
        {
            const double theta = sign * 3.14159265358979323846 * k / pieces;
            const double c = std::cos(theta);
            const double sn = std::sin(theta);
            out.push_back(centre + QPointF(v.x() * c - v.y() * sn, v.x() * sn + v.y() * c));
        }
    };

    std::vector<QPointF> ring(left.begin(), left.end());
    cap(ring, centres.back(), left.back(), directions.back());
    for (auto it = right.rbegin(); it != right.rend(); ++it)
        ring.push_back(*it);
    cap(ring, centres.front(), right.front(), -directions.front());

    QPainterPath outline;
    outline.setFillRule(Qt::WindingFill);
    outline.addPolygon(QPolygonF(ring.begin(), ring.end()));
    outline.closeSubpath();
    return outline.simplified();
}

QPainterPath brushOutline(const std::vector<fla::Point>& points, double width, bool smooth, double zoom,
    const std::vector<double>* pressures = nullptr)
{
    // Pressure only matters when it varied: a mouse reports 1 throughout.
    bool varied = false;
    if (pressures && pressures->size() == points.size())
    {
        for (const double p : *pressures)
            varied = varied || std::fabs(p - 1.0) > 0.01;
    }
    if (varied && points.size() >= 2)
    {
        const QPainterPath outline = pressureOutline(
            centreline(points, smooth, zoom), std::max(width, 0.5), points, *pressures);
        if (!outline.isEmpty())
            return outline;
    }

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

fla::Shape* brushOutlineShape(const std::vector<fla::Point>& points, double width,
    bool smooth, fla::Frame* parent, const fla::DrawingStyle& style, bool asFill,
    const std::vector<double>* pressures)
{
    const QPainterPath outline = brushOutline(points, width, smooth, 1.0, pressures);
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
        options.tolerance = 0.3;
        options.minimumSpacing = 0.2;
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
    return shape;
}

// ---------------------------------------------------------------- brush

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
    _points.clear();
    _pressures.clear();
    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    _pressures.push_back(_style.brushPressure ? view.pressure() : 1.0);
    view.update();
    return true;
}

bool BrushTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    _hover = documentPos;
    _hoverValid = true;

    if (_drawing)
    {
        _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
        _pressures.push_back(_style.brushPressure ? view.pressure() : 1.0);
    }

    view.update();
    return _drawing;
}

bool BrushTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_drawing)
        return false;

    _points.push_back(fla::Point(documentPos.x(), documentPos.y()));
    _pressures.push_back(_style.brushPressure ? view.pressure() : 1.0);
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

    fla::Shape* shape = brushOutlineShape(_points, _style.brushSize, true, frame, _style, true,
        &_pressures);
    _points.clear();
    _pressures.clear();
    if (!shape)
    {
        view.update();
        return;
    }

    using BrushMode = fla::DrawingStyle::BrushMode;
    const BrushMode mode = _style.brushMode;

    if (mode == BrushMode::Normal || mode == BrushMode::Fills)
    {
        placeDrawnElement(view, _commandStack, _selection, frame, shape, "Brush",
            _style.objectDrawing, mode == BrushMode::Fills);
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
    placeDrawnElement(view, _commandStack, _selection, frame, piece, "Brush",
        _style.objectDrawing, true);
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
        painter.drawPath(brushOutline(_points, _style.brushSize, false, 1.0, &_pressures));
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

    if (_erasing && !_points.empty())
    {
        // What will be taken away, shown as the stage colour so it reads as
        // already gone.
        painter.setBrush(QColor(255, 255, 255, 200));
        painter.drawPath(brushOutline(_points, _style.eraserSize, false, 1.0));
    }

    if (_hoverValid && !_style.faucet)
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
    _insideShape = nullptr;
    _insideFace = -1;
    _insideRegions.reset();
    view.update();
}
