#include "paint_tools.h"

#include "cursors.h"
#include "phoenix_view.h"

#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/region_fill.h"
#include "../edit/region_split.h"
#include "../edit/shape_merge.h"
#include "../edit/frame_commands.h"
#include "../edit/color_art.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "../data/frame.h"
#include "../data/transform.h"
#include "../geom/curve.h"

#include <cstdint>
#include <QMouseEvent>
#include <cmath>
#include <QPainter>
#include "../data/bitmap_fill.h"
#include "../data/radial_gradient.h"
#include "../data/linear_gradient.h"

#include <memory>
#include <algorithm>
#include <climits>
#include <map>
#include <set>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>

namespace {

QTransform toQTransform(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

using fla::regionfill::ensureFill;

/// The index of a stroke in \a shape matching the current stroke, adding one
/// when there is none. Returns -1 when strokes are switched off.
int ensureStroke(fla::Shape& shape, const fla::DrawingStyle& style)
{
    if (!style.hasStroke)
        return -1;

    for (const auto& entry : shape.strokesMap)
    {
        const fla::StrokeStyle* stroke = entry.second;
        if (stroke && style.strokeMatches(*stroke))
        {
            return entry.first;
        }
    }

    fla::StrokeStyle* stroke = style.createStroke(&shape);
    if (!stroke)
        return -1;

    const int index = shape.strokesMap.empty() ? 1 : shape.strokesMap.rbegin()->first + 1;
    shape.strokes.push_back(stroke);
    shape.strokesMap[index] = stroke;
    return index;
}

/// The shapes under a point, topmost first. The hit test only knows about
/// painted areas, while the bucket and the bottle also work inside an area
/// that is merely enclosed, so this looks at bounds and lets the caller ask
/// the arrangement.
std::vector<fla::Shape*> shapesNear(PhoenixView& view, const QPointF& documentPos)
{
    const double tolerance = view.pickTolerance();
    const QRectF probe(documentPos.x() - tolerance, documentPos.y() - tolerance,
        tolerance * 2.0, tolerance * 2.0);

    std::vector<fla::Shape*> shapes;
    const std::vector<fla::Element*> elements = view.elementsIn(probe);
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
    {
        if (*it && (*it)->elementType() == fla::Element::Type::Shape)
            shapes.push_back(static_cast<fla::Shape*>(*it));
    }
    return shapes;
}

void commitContents(PhoenixView& view, fla::CommandStack& commandStack, fla::Shape* target,
    const fla::Shape& after, const char* name)
{
    commandStack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(target, after, name)));
    commandStack.breakMergeChain();
    view.clearCaches();
    view.update();
}

/// The bucket on a color art layer: the region the line layer's strokes
/// close around the click is filled on this layer, the lines staying on
/// theirs. False when the layer is not color art or the click is in no
/// closed region, for the bucket to go on as usual.
bool paintUnderLines(PhoenixView& view, fla::CommandStack& commandStack, const fla::DrawingStyle& style,
    const QPointF& documentPos)
{
    fla::Frame* active = view.activeFrame();
    auto* layer = active ? dynamic_cast<fla::Layer*>(active->parent) : nullptr;
    auto* timeline = layer ? dynamic_cast<fla::Timeline*>(layer->parent) : nullptr;
    if (!layer || !timeline || !fla::colorart::isColorArt(*layer))
        return false;
    const fla::Layer* line = fla::colorart::lineLayerOf(*timeline, *layer);
    if (!line)
        return false;

    // The paint already on this frame under the click, painted further; else
    // the topmost paint, which the new region joins; or a new shape.
    fla::Shape* target = nullptr;
    for (auto it = active->elements.rbegin(); it != active->elements.rend(); ++it)
    {
        if (!*it || (*it)->elementType() != fla::Element::Type::Shape)
            continue;
        auto* shape = static_cast<fla::Shape*>(*it);
        if (!target)
            target = shape;
        bool inverts = false;
        const QPointF at = toQTransform(shape->transform).inverted(&inverts).map(documentPos);
        if (inverts && fla::RegionSplitter(*shape).faceAt(fla::Point(at.x(), at.y())) >= 0)
        {
            target = shape;
            break;
        }
    }
    std::unique_ptr<fla::Shape> working(target ? fla::cloneShape(*target, nullptr) : new fla::Shape(nullptr));
    bool invertible = false;
    const QTransform toShape = target ? toQTransform(target->transform).inverted(&invertible) : QTransform();
    if (target && !invertible)
        return false;
    const fla::Transform stageToShape(toShape.m11(), toShape.m12(), toShape.m21(), toShape.m22(), toShape.dx(), toShape.dy());
    const std::vector<fla::Curve> lines = fla::colorart::lineCutters(*line, view.playheadFrame(), stageToShape);
    if (lines.empty())
        return false;
    const std::vector<fla::Curve> cutters = fla::colorart::withoutShared(lines, *working);

    const QPointF local = toShape.map(documentPos);
    const fla::Point localPoint(local.x(), local.y());
    fla::RegionSplitter regions(*working, cutters);
    const int face = regions.faceAt(localPoint);
    if (face < 0)
        return false;
    const int fill = ensureFill(*working, style);
    if (fill < 0 || regions.faceFill(face) == fill)
        return true;
    std::unique_ptr<fla::Shape> after(regions.withFaceFill(face, fill));
    if (!after)
        return true;
    fla::DrawingStyle::fitFills(*after);
    if (target)
    {
        commitContents(view, commandStack, target, *after, "Paint Bucket");
        return true;
    }
    // The first paint on this frame: a keyframe here when the playhead is
    // past the span's start, then the shape.
    fla::Frame* frame = view.keyframeForDrawing(active);
    after->parent = frame;
    commandStack.push(fla::CommandPtr(new fla::AddElementCommand(frame, after.release(), "Paint Bucket")));
    commandStack.breakMergeChain();
    view.clearCaches();
    view.update();
    return true;
}

/// How far apart an outline's ends may be for the bucket to close it, in
/// stage units at the current zoom; 0 when gaps are left open.
double gapFor(const PhoenixView& view, const fla::DrawingStyle& style)
{
    double screenGap = 0.0;
    if (style.gapSize == fla::DrawingStyle::GapSize::Small)
        screenGap = 3.0;
    else if (style.gapSize == fla::DrawingStyle::GapSize::Medium)
        screenGap = 8.0;
    else if (style.gapSize == fla::DrawingStyle::GapSize::Large)
        screenGap = 20.0;
    return screenGap / qMax(view.zoom(), 1.0e-6);
}

/// The layer being painted, its timeline, and on a color art layer the
/// line layer above it (else null). False when nothing is being edited.
bool editedLayer(PhoenixView& view, fla::Layer*& layer, fla::Timeline*& timeline, const fla::Layer*& line)
{
    fla::Frame* active = view.activeFrame();
    layer = active ? dynamic_cast<fla::Layer*>(active->parent) : nullptr;
    timeline = layer ? dynamic_cast<fla::Timeline*>(layer->parent) : nullptr;
    line = layer && timeline && fla::colorart::isColorArt(*layer) ? fla::colorart::lineLayerOf(*timeline, *layer) : nullptr;
    return layer && timeline;
}

/// A region's outline on the stage, to show it about to be painted.
QPainterPath outlinePath(const fla::RegionSplitter& regions, int face, const QTransform& toStage)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const std::vector<fla::Point>& ring : regions.faceOutline(face))
    {
        if (ring.size() < 3)
            continue;
        path.moveTo(ring.front().x, ring.front().y);
        for (size_t i = 1; i < ring.size(); ++i)
            path.lineTo(ring[i].x, ring[i].y);
        path.closeSubpath();
    }
    return toStage.map(path);
}

} // namespace

struct PaintBucketTool::Target
{
    /// Null for new paint under a color art layer's lines.
    fla::Shape* shape = nullptr;
    QTransform toShape;
    QTransform toStage;
    std::unique_ptr<fla::Shape> working;
    std::unique_ptr<fla::RegionSplitter> regions;
    /// The regions taken, in the order they were reached.
    std::vector<int> faces;
    std::set<int> taken;
    /// Each region's outline on the stage, for the preview.
    std::map<int, QPainterPath> outlines;
    /// A point inside each region, on the stage, for a loop.
    std::vector<std::pair<int, QPointF>> interiors;
    bool interiorsKnown = false;

    void take(int face, bool onlyEmpty)
    {
        if (face < 0 || (onlyEmpty && regions->faceFill(face) > 0) || !taken.insert(face).second)
            return;
        faces.push_back(face);
        outlines[face] = outlinePath(*regions, face, toStage);
    }
};

PaintBucketTool::PaintBucketTool(fla::CommandStack& commandStack, const fla::DrawingStyle& style)
    : _commandStack(commandStack)
    , _style(style)
{}

PaintBucketTool::~PaintBucketTool() = default;

QCursor PaintBucketTool::cursor() const
{
    return Cursors::cursor(Cursors::Kind::Bucket);
}

bool PaintBucketTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_style.hasFill)
        return false;

    // A line just drawn is part of the artwork as far as the bucket is
    // concerned, so anything still floating goes in first.
    view.flushPendingMerge();

    // Scribble and Lasso: the regions are gathered as the drag goes, and
    // painted on the release.
    if (_style.bucketMode != fla::DrawingStyle::BucketMode::Click)
    {
        clearScribble();
        _scribbling = true;
        _pressAt = documentPos;
        _gap = gapFor(view, _style);
        fla::Layer* layer = nullptr;
        fla::Timeline* timeline = nullptr;
        const fla::Layer* line = nullptr;
        if (editedLayer(view, layer, timeline, line) && line)
        {
            // Under a color art layer's lines: the paint on this frame, or
            // new paint.
            _colorArt = true;
            fla::Shape* paint = nullptr;
            fla::Frame* active = view.activeFrame();
            for (auto it = active->elements.rbegin(); it != active->elements.rend() && !paint; ++it)
                if (*it && (*it)->elementType() == fla::Element::Type::Shape)
                    paint = static_cast<fla::Shape*>(*it);
            targetFor(view, paint);
        }
        _scribble.push_back(documentPos);
        if (_style.bucketMode == fla::DrawingStyle::BucketMode::Scribble)
            scribbleTo(view, documentPos);
        view.update();
        return true;
    }

    // Fill Across: the same region on other drawings too.
    if (_style.fillAcross != fla::DrawingStyle::FillAcross::ThisDrawing && fillAcrossFrames(view, documentPos))
        return true;

    // On a color art layer, the lines it paints within are on the layer above.
    if (paintUnderLines(view, _commandStack, _style, documentPos))
        return true;

    pressRegion(view, documentPos);
    return true;
}

void PaintBucketTool::pressRegion(PhoenixView& view, const QPointF& documentPos)
{
    for (fla::Shape* shape : shapesNear(view, documentPos))
    {
        bool invertible = false;
        const QTransform toShape = toQTransform(shape->transform).inverted(&invertible);
        if (!invertible)
            continue;

        const QPointF local = toShape.map(documentPos);

        // Work on a copy that carries the new fill, so the arrangement is
        // built once with the style already in place.
        std::unique_ptr<fla::Shape> painted(fla::cloneShape(*shape, nullptr));
        fla::RegionSplitter open(*painted);
        const fla::Point localPoint(local.x(), local.y());
        int face = open.faceAt(localPoint);

        // An outline that does not quite meet: join its loose ends across
        // gaps up to the chosen size, and ask again. The
        // joins stay in the result as fill boundaries with no stroke.
        std::unique_ptr<fla::RegionSplitter> closed;
        if (face < 0 && _style.gapSize != fla::DrawingStyle::GapSize::None)
        {
            const std::vector<fla::Curve> joins = open.gapJoins(gapFor(view, _style));
            if (!joins.empty())
            {
                closed.reset(new fla::RegionSplitter(*painted, joins));
                face = closed->faceAt(localPoint);
            }
        }
        if (face < 0)
            continue;

        const fla::RegionSplitter& regions = closed ? *closed : open;

        const int fill = ensureFill(*painted, _style);
        if (fill < 0)
            return;
        if (regions.faceFill(face) == fill)
            return;

        // The arrangement was built before the fill was added, which is fine:
        // styles do not change the geometry, and the result is cloned from
        // the painted copy, style table and all.
        std::unique_ptr<fla::Shape> after(regions.withFaceFill(face, fill));
        if (!after)
            return;

        // Fill All Regions: every other enclosed region with the fill the
        // clicked one had, one at a time, the shape divided afresh each time.
        if (_style.fillAllRegions)
        {
            const int was = regions.faceFill(face);
            for (const fla::Point& inside : regions.regionsFilledWith(was))
            {
                fla::RegionSplitter again(*after);
                const int other = again.faceAt(inside);
                if (other < 0 || again.faceFill(other) != was)
                    continue;
                if (fla::Shape* more = again.withFaceFill(other, fill))
                    after.reset(more);
            }
        }

        // The paint is placed on release, so a gradient can be dragged.
        _pending = std::move(after);
        _pendingTarget = shape;
        _pendingFill = fill;
        _pressAt = documentPos;
        _dragAt = documentPos;
        _toShape = toShape;
        return;
    }
}

bool PaintBucketTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (_scribbling)
    {
        if (_style.bucketMode == fla::DrawingStyle::BucketMode::Lasso)
            lassoTo(view, documentPos);
        else
            scribbleTo(view, documentPos);
        view.update();
        return true;
    }
    if (!_pending)
        return false;
    _dragAt = documentPos;
    view.update();
    return true;
}

bool PaintBucketTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    if (_scribbling)
    {
        const double moved = std::hypot(documentPos.x() - _pressAt.x(), documentPos.y() - _pressAt.y()) * std::max(view.zoom(), 1.0e-6);
        if (moved > 4.0 || _scribble.size() > 2)
        {
            if (_style.bucketMode == fla::DrawingStyle::BucketMode::Lasso)
                lassoTo(view, documentPos);
            else
                scribbleTo(view, documentPos);
            finishScribble(view);
            return true;
        }
        // Barely a drag: a click, which paints the region as ever.
        clearScribble();
        if (!paintUnderLines(view, _commandStack, _style, _pressAt))
        {
            pressRegion(view, _pressAt);
            place(view, nullptr);
        }
        view.update();
        return true;
    }
    if (!_pending)
        return false;
    _dragAt = documentPos;
    const double moved = std::hypot(_dragAt.x() - _pressAt.x(), _dragAt.y() - _pressAt.y()) * std::max(view.zoom(), 1.0e-6);
    place(view, moved > 4.0 ? &_dragAt : nullptr);
    return true;
}

void PaintBucketTool::deactivate(PhoenixView& view)
{
    // A fill waiting for the release points at the shape it fills, which an
    // undo can take away.
    _pending.reset();
    _pendingTarget = nullptr;
    clearScribble();
    view.update();
}

void PaintBucketTool::clearScribble()
{
    _scribbling = false;
    _scribble.clear();
    _targets.clear();
    _colorArt = false;
}

PaintBucketTool::Target* PaintBucketTool::targetFor(PhoenixView& view, fla::Shape* shape)
{
    for (const auto& target : _targets)
        if (target->shape == shape)
            return target->regions ? target.get() : nullptr;

    auto target = std::make_unique<Target>();
    Target* made = target.get();
    _targets.push_back(std::move(target));
    made->shape = shape;
    bool invertible = true;
    made->toStage = shape ? toQTransform(shape->transform) : QTransform();
    made->toShape = made->toStage.inverted(&invertible);
    if (!invertible)
        return nullptr;
    made->working.reset(shape ? fla::cloneShape(*shape, nullptr) : new fla::Shape(nullptr));

    // The lines of a color art layer divide its paint; elsewhere the shape's
    // own outlines do. Gaps up to the chosen size are closed either way.
    std::vector<fla::Curve> cutters;
    fla::Layer* layer = nullptr;
    fla::Timeline* timeline = nullptr;
    const fla::Layer* line = nullptr;
    if (_colorArt && editedLayer(view, layer, timeline, line) && line)
    {
        const QTransform& m = made->toShape;
        const fla::Transform stageToShape(m.m11(), m.m12(), m.m21(), m.m22(), m.dx(), m.dy());
        cutters = fla::colorart::withoutShared(fla::colorart::lineCutters(*line, view.playheadFrame(), stageToShape), *made->working);
    }
    std::unique_ptr<fla::RegionSplitter> open(cutters.empty() ? new fla::RegionSplitter(*made->working)
                                                              : new fla::RegionSplitter(*made->working, cutters));
    if (!open->valid())
        return nullptr;
    if (_gap > 0.0)
    {
        const std::vector<fla::Curve> joins = open->gapJoins(_gap);
        if (!joins.empty())
        {
            cutters.insert(cutters.end(), joins.begin(), joins.end());
            open.reset(new fla::RegionSplitter(*made->working, cutters));
        }
    }
    made->regions = std::move(open);
    return made;
}

void PaintBucketTool::scribbleTo(PhoenixView& view, const QPointF& documentPos)
{
    const QPointF from = _scribble.empty() ? documentPos : _scribble.back();
    if (_scribble.empty() || _scribble.back() != documentPos)
        _scribble.push_back(documentPos);

    // Sampled every couple of screen pixels, so no region the pointer
    // crosses is missed.
    const double step = 2.0 / std::max(view.zoom(), 1.0e-6);
    const double length = std::hypot(documentPos.x() - from.x(), documentPos.y() - from.y());
    const int count = std::max(1, static_cast<int>(std::ceil(length / step)));
    for (int k = 0; k <= count; ++k)
    {
        const QPointF at = from + (documentPos - from) * (static_cast<double>(k) / count);
        if (_colorArt)
        {
            if (!_targets.empty() && _targets.front()->regions)
            {
                Target& target = *_targets.front();
                const QPointF local = target.toShape.map(at);
                target.take(target.regions->faceAt(fla::Point(local.x(), local.y())), _style.bucketOnlyEmpty);
            }
            continue;
        }
        // The topmost shape with a region here has it.
        for (fla::Shape* shape : shapesNear(view, at))
        {
            Target* target = targetFor(view, shape);
            if (!target)
                continue;
            const QPointF local = target->toShape.map(at);
            const int face = target->regions->faceAt(fla::Point(local.x(), local.y()));
            if (face < 0)
                continue;
            target->take(face, _style.bucketOnlyEmpty);
            break;
        }
    }
}

void PaintBucketTool::lassoTo(PhoenixView& view, const QPointF& documentPos)
{
    if (_scribble.empty() || _scribble.back() != documentPos)
        _scribble.push_back(documentPos);
    if (_scribble.size() < 3)
        return;
    const QPolygonF loop(QList<QPointF>(_scribble.begin(), _scribble.end()));

    // Every shape the loop reaches, divided once.
    if (!_colorArt)
    {
        for (fla::Element* element : view.elementsIn(loop.boundingRect()))
            if (element && element->elementType() == fla::Element::Type::Shape)
                targetFor(view, static_cast<fla::Shape*>(element));
    }

    for (const auto& target : _targets)
    {
        if (!target->regions)
            continue;
        if (!target->interiorsKnown)
        {
            target->interiorsKnown = true;
            for (const int face : target->regions->boundedFaces())
            {
                fla::Point inside;
                if (fla::regionfill::regionMiddle(*target->regions, face, inside))
                    target->interiors.emplace_back(face, target->toStage.map(QPointF(inside.x, inside.y)));
            }
        }
        target->faces.clear();
        target->taken.clear();
        for (const auto& [face, inside] : target->interiors)
            if (loop.containsPoint(inside, Qt::OddEvenFill))
                target->take(face, _style.bucketOnlyEmpty);
    }
}

void PaintBucketTool::finishScribble(PhoenixView& view)
{
    struct Painted
    {
        Target* target;
        std::unique_ptr<fla::Shape> after;
    };
    std::vector<Painted> painted;
    int regions = 0;
    for (const auto& target : _targets)
    {
        if (!target->regions || target->faces.empty())
            continue;
        const int fill = ensureFill(*target->working, _style);
        if (fill < 0)
            break;
        std::vector<int> faces;
        for (const int face : target->faces)
            if (target->regions->faceFill(face) != fill)
                faces.push_back(face);
        if (faces.empty())
            continue;
        std::unique_ptr<fla::Shape> after(target->regions->withFacesFill(faces, fill));
        if (!after)
            continue;
        if (_style.lockFill)
        {
            const QRectF stage = target->toShape.mapRect(view.stageInScope());
            fla::DrawingStyle::fitFillsTo(*after, stage.left(), stage.top(), stage.right(), stage.bottom());
        }
        fla::DrawingStyle::fitFills(*after);
        regions += static_cast<int>(faces.size());
        painted.push_back({target.get(), std::move(after)});
    }

    if (painted.empty())
    {
        report(_style.bucketOnlyEmpty ? "No empty regions to fill there." : "No regions to fill there.");
        clearScribble();
        view.update();
        return;
    }

    // One step to undo, however many shapes it painted.
    const bool several = painted.size() > 1;
    if (several)
        _commandStack.beginMacro("Paint Bucket");
    for (Painted& each : painted)
    {
        if (each.target->shape)
        {
            _commandStack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(each.target->shape, *each.after, "Paint Bucket")));
            continue;
        }
        fla::Frame* frame = view.keyframeForDrawing(view.activeFrame());
        if (!frame)
            continue;
        each.after->parent = frame;
        _commandStack.push(fla::CommandPtr(new fla::AddElementCommand(frame, each.after.release(), "Paint Bucket")));
    }
    if (several)
        _commandStack.endMacro();
    _commandStack.breakMergeChain();
    report(regions == 1 ? QString("Filled 1 region.") : QString("Filled %1 regions.").arg(regions));
    clearScribble();
    view.clearCaches();
    view.update();
}

bool PaintBucketTool::fillAcrossFrames(PhoenixView& view, const QPointF& documentPos)
{
    fla::Layer* layer = nullptr;
    fla::Timeline* timeline = nullptr;
    const fla::Layer* line = nullptr;
    if (!editedLayer(view, layer, timeline, line))
        return false;

    // The range of frames.
    const int current = view.playheadFrame();
    int first = current;
    int last = current;
    QString range;
    switch (_style.fillAcross)
    {
    case fla::DrawingStyle::FillAcross::SelectedFrames:
        if (!_selectedFrames || !_selectedFrames(first, last))
        {
            report("Select the frames to fill in the timeline first.");
            return true;
        }
        range = "the selected frames";
        break;
    case fla::DrawingStyle::FillAcross::OnionSkin:
        first = std::max(0, current - view.onionBefore());
        last = current + view.onionAfter();
        range = "the onion skin range";
        break;
    case fla::DrawingStyle::FillAcross::ToEnd:
        last = INT_MAX / 2;
        range = "the rest of the layer";
        break;
    default:
        return false;
    }

    const std::vector<int> drawings = fla::regionfill::drawingsIn(*timeline, *layer, first, last);
    const std::vector<int> here = fla::regionfill::drawingsIn(*timeline, *layer, current, current);
    if (here.empty())
        return false;

    fla::regionfill::AcrossSettings settings;
    settings.style = &_style;
    settings.stagePoint = fla::Point(documentPos.x(), documentPos.y());
    settings.gap = gapFor(view, _style);
    settings.follow = _style.fillAcrossFollow;
    fla::regionfill::AcrossResult result = fla::regionfill::fillAcross(*timeline, *layer, drawings, here.front(), settings);

    if (!result.fills.empty())
    {
        const char* name = "Fill Across Frames";
        _commandStack.beginMacro(name);
        for (fla::regionfill::DrawingFill& fill : result.fills)
        {
            fla::Frame* frame = fill.frame;
            fla::Shape* target = fill.target;
            if (!frame)
            {
                // A keyframe for the paint first, copying what the span held.
                auto* insert = new fla::InsertKeyframeCommand(layer, fill.frameIndex, name);
                _commandStack.push(fla::CommandPtr(insert));
                frame = insert->frame();
                if (!frame)
                    continue;
                if (fill.elementIndex >= 0 && fill.elementIndex < static_cast<int>(frame->elements.size()))
                {
                    fla::Element* element = frame->elements[static_cast<size_t>(fill.elementIndex)];
                    if (element && element->elementType() == fla::Element::Type::Shape)
                        target = static_cast<fla::Shape*>(element);
                }
            }
            if (target)
            {
                _commandStack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(target, *fill.after, name)));
                continue;
            }
            fill.after->parent = frame;
            _commandStack.push(fla::CommandPtr(new fla::AddElementCommand(frame, fill.after.release(), name)));
        }
        _commandStack.endMacro();
        _commandStack.breakMergeChain();
    }

    // What happened, with the drawings where nothing matched.
    const int filled = static_cast<int>(result.fills.size()) + result.unchanged;
    QString text = filled == 1 ? QString("Filled the region on 1 drawing in %1.").arg(range)
                               : QString("Filled the region on %1 drawings in %2.").arg(filled).arg(range);
    if (!result.missed.empty())
    {
        QStringList frames;
        for (const int missed : result.missed)
            frames << QString::number(missed + 1);
        text += QString(" No matching region on frame%1 %2.").arg(frames.size() == 1 ? "" : "s").arg(frames.join(", "));
    }
    report(text);
    view.clearCaches();
    view.update();
    return true;
}

void PaintBucketTool::place(PhoenixView& view, const QPointF* dragEnd)
{
    std::unique_ptr<fla::Shape> after = std::move(_pending);
    fla::Shape* target = _pendingTarget;
    _pendingTarget = nullptr;
    fla::FillStyle* fill = after ? const_cast<fla::FillStyle*>(after->getFillStyleByIndex(_pendingFill)) : nullptr;
    if (!after || !target)
    {
        view.update();
        return;
    }
    const bool gradient = fill && (fill->type() == fla::FillStyle::Type::LinearGradient ||
                                      fill->type() == fla::FillStyle::Type::RadialGradient);
    if (fill && gradient && dragEnd)
    {
        // Along the drag, in the shape's own space.
        constexpr double kSpan = 1638.4;
        const QPointF a = _toShape.map(_pressAt);
        const QPointF b = _toShape.map(*dragEnd);
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        if (fill->type() == fla::FillStyle::Type::LinearGradient)
            fill->transform = fla::Transform(dx / kSpan, dy / kSpan, -dy / kSpan, dx / kSpan, (a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
        else
        {
            const double reach = 2.0 * std::hypot(dx, dy) / kSpan;
            fill->transform = fla::Transform(reach, 0.0, 0.0, reach, a.x(), a.y());
        }
    }
    else if (_style.lockFill)
    {
        // Lock Fill: over the stage, wherever it is painted.
        const QRectF stage = _toShape.mapRect(view.stageInScope());
        fla::DrawingStyle::fitFillsTo(*after, stage.left(), stage.top(), stage.right(), stage.bottom());
    }
    fla::DrawingStyle::fitFills(*after);
    commitContents(view, _commandStack, target, *after, "Paint Bucket");
}

void PaintBucketTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    if (_scribbling)
    {
        // What the release will paint, in the fill color, half see-through.
        const QColor color(_style.fillColor[0], _style.fillColor[1], _style.fillColor[2], 150);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (const auto& target : _targets)
            for (const int face : target->faces)
                painter.drawPath(target->outlines[face]);
        QPen pen(QColor(_style.fillColor[0], _style.fillColor[1], _style.fillColor[2]).darker(150), 1.5 * scale);
        if (_style.bucketMode == fla::DrawingStyle::BucketMode::Lasso)
            pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPolygonF trail(QList<QPointF>(_scribble.begin(), _scribble.end()));
        if (_style.bucketMode == fla::DrawingStyle::BucketMode::Lasso)
            painter.drawPolygon(trail);
        else
            painter.drawPolyline(trail);
        return;
    }
    // The line a gradient is being dragged along.
    if (!_pending || _dragAt == _pressAt || _style.fillKind == fla::DrawingStyle::FillKind::Solid ||
        _style.fillKind == fla::DrawingStyle::FillKind::Bitmap)
        return;
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.drawLine(_pressAt, _dragAt);
    painter.setBrush(Qt::white);
    painter.drawEllipse(_pressAt, 3.0 * scale, 3.0 * scale);
    painter.drawEllipse(_dragAt, 3.0 * scale, 3.0 * scale);
}

QCursor InkBottleTool::cursor() const
{
    return Cursors::cursor(Cursors::Kind::InkBottle);
}

bool InkBottleTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_style.hasStroke)
        return false;

    view.flushPendingMerge();

    for (fla::Shape* shape : shapesNear(view, documentPos))
    {
        bool invertible = false;
        const QTransform toShape = toQTransform(shape->transform).inverted(&invertible);
        if (!invertible)
            continue;

        const QPointF local = toShape.map(documentPos);
        const double tolerance = PhoenixView::toleranceInLocalSpace(
            toQTransform(shape->transform), view.pickTolerance());

        std::unique_ptr<fla::Shape> inked(fla::cloneShape(*shape, nullptr));
        fla::RegionSplitter regions(*inked);

        // A stroke under the cursor is restyled; otherwise the fill, painted
        // or merely enclosed, gets the stroke round its outline.
        fla::RegionPick pick = regions.pickAt(fla::Point(local.x(), local.y()), tolerance, false);
        if (pick.isEmpty())
        {
            const int face = regions.faceAt(fla::Point(local.x(), local.y()));
            if (face < 0)
                continue;
            pick.kind = fla::RegionPick::Kind::Fill;
            pick.face = face;
        }

        const int stroke = ensureStroke(*inked, _style);
        if (stroke < 0)
            return true;

        std::unique_ptr<fla::Shape> after(regions.withStroke(pick, stroke));
        if (!after)
            return true;

        commitContents(view, _commandStack, shape, *after, "Ink Bottle");
        return true;
    }

    return true;
}

QCursor EyedropperTool::cursor() const
{
    return Cursors::cursor(Cursors::Kind::Eyedropper);
}

bool EyedropperTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (!hit)
        return true;

    const fla::FillStyle* fill = nullptr;
    const fla::StrokeStyle* stroke = nullptr;

    switch (hit.element->elementType())
    {
    case fla::Element::Type::Shape:
    {
        const fla::Shape* shape = static_cast<const fla::Shape*>(hit.element);
        bool invertible = false;
        const QTransform toShape = hit.elementToDocument.inverted(&invertible);
        if (!invertible)
            return true;

        const QPointF local = toShape.map(documentPos);
        const double tolerance = PhoenixView::toleranceInLocalSpace(
            hit.elementToDocument, view.pickTolerance());

        fla::RegionSplitter regions(*shape);
        const fla::RegionPick pick = regions.pickAt(fla::Point(local.x(), local.y()), tolerance, false);
        if (pick.kind == fla::RegionPick::Kind::Stroke)
            stroke = shape->getStrokeStyleByIndex(regions.strokeOf(pick));
        else if (pick.kind == fla::RegionPick::Kind::Fill)
            fill = shape->getFillStyleByIndex(regions.faceFill(pick.face));
        break;
    }

    case fla::Element::Type::Rectangle:
    {
        const fla::RectanglePrimitive* rectangle = static_cast<const fla::RectanglePrimitive*>(hit.element);
        if (hit.part == HitResult::Part::Stroke)
            stroke = rectangle->strokeStyle;
        else
            fill = rectangle->fillStyle;
        break;
    }

    case fla::Element::Type::Oval:
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(hit.element);
        if (hit.part == HitResult::Part::Stroke)
            stroke = oval->strokeStyle;
        else
            fill = oval->fillStyle;
        break;
    }

    default:
        return true;
    }

    if (stroke && stroke->fill && stroke->fill->type() == fla::FillStyle::Type::SolidColor)
    {
        const fla::SolidColor* color = static_cast<const fla::SolidColor*>(stroke->fill);
        for (int i = 0; i < 4; ++i)
            _style.strokeColor[i] = color->color[i];
        _style.strokeWeight = stroke->isHairline() ? 0.25 : stroke->weight;
        _style.hasStroke = true;
        if (_onSampled)
            _onSampled(true);
        return true;
    }

    if (fill && fill->type() == fla::FillStyle::Type::SolidColor)
    {
        const fla::SolidColor* color = static_cast<const fla::SolidColor*>(fill);
        for (int i = 0; i < 4; ++i)
            _style.fillColor[i] = color->color[i];
        _style.fillKind = fla::DrawingStyle::FillKind::Solid;
        _style.hasFill = true;
        if (_onSampled)
            _onSampled(false);
    }
    else if (fill && (fill->type() == fla::FillStyle::Type::LinearGradient ||
                         fill->type() == fla::FillStyle::Type::RadialGradient))
    {
        // A gradient: its stops, spread and blending become the fill.
        std::vector<fla::DrawingStyle::Stop> stops;
        const auto take = [&stops](const auto& entries) {
            for (const auto& entry : entries)
            {
                fla::DrawingStyle::Stop stop;
                stop.ratio = entry.ratio;
                for (int i = 0; i < 4; ++i)
                    stop.color[i] = entry.color[i];
                stops.push_back(stop);
            }
        };
        if (fill->type() == fla::FillStyle::Type::LinearGradient)
            take(static_cast<const fla::LinearGradient*>(fill)->entries);
        else
            take(static_cast<const fla::RadialGradient*>(fill)->entries);
        if (stops.size() >= 2)
        {
            _style.gradient = stops;
            _style.gradientSpread = fill->spreadMethod;
            _style.gradientLinearRGB = fill->interpolationMethod == "linearRGB";
            _style.fillKind = fill->type() == fla::FillStyle::Type::LinearGradient
                ? fla::DrawingStyle::FillKind::Linear : fla::DrawingStyle::FillKind::Radial;
            _style.hasFill = true;
            if (_onSampled)
                _onSampled(false);
        }
    }
    else if (fill && fill->type() == fla::FillStyle::Type::BitmapFill)
    {
        // A bitmap fill: its bitmap tiles the next fill. The eyedropper
        // locks the fill, so the sample continues where it was taken.
        _style.fillBitmap = static_cast<const fla::BitmapFill*>(fill)->bitmapPath;
        _style.fillKind = fla::DrawingStyle::FillKind::Bitmap;
        _style.hasFill = true;
        if (_onSampled)
            _onSampled(false);
    }

    return true;
}
