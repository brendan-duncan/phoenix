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
#include "../edit/region_split.h"
#include "../edit/shape_merge.h"
#include "../edit/color_art.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "../data/frame.h"
#include "../data/transform.h"
#include "../geom/curve.h"

#include <cstdint>
#include <QMouseEvent>

#include <memory>

namespace {

QTransform toQTransform(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

bool sameColor(const uint8_t a[4], const uint8_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

/// The index of a fill in \a shape matching the current fill color, adding
/// one when there is none. Returns -1 when fills are switched off.
int ensureFill(fla::Shape& shape, const fla::DrawingStyle& style)
{
    if (!style.hasFill)
        return -1;

    for (const auto& entry : shape.fillsMap)
    {
        const fla::FillStyle* fill = entry.second;
        if (fill && style.fillKind == fla::DrawingStyle::FillKind::Solid && fill->type() == fla::FillStyle::Type::SolidColor &&
            sameColor(static_cast<const fla::SolidColor*>(fill)->color, style.fillColor))
        {
            return entry.first;
        }
    }

    fla::FillStyle* fill = style.createFill(&shape);
    if (!fill)
        return -1;

    const int index = shape.fillsMap.empty() ? 1 : shape.fillsMap.rbegin()->first + 1;
    shape.fills.push_back(fill);
    shape.fillsMap[index] = fill;
    return index;
}

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

} // namespace

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

    // On a color art layer, the lines it paints within are on the layer above.
    if (paintUnderLines(view, _commandStack, _style, documentPos))
        return true;

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
        // gaps up to the chosen size, as Animate does, and ask again. The
        // joins stay in the result as fill boundaries with no stroke.
        std::unique_ptr<fla::RegionSplitter> closed;
        if (face < 0 && _style.gapSize != fla::DrawingStyle::GapSize::None)
        {
            double screenGap = 3.0;
            if (_style.gapSize == fla::DrawingStyle::GapSize::Medium)
                screenGap = 8.0;
            else if (_style.gapSize == fla::DrawingStyle::GapSize::Large)
                screenGap = 20.0;
            const double gap = screenGap / qMax(view.zoom(), 1.0e-6);

            const std::vector<fla::Curve> joins = open.gapJoins(gap);
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
            return true;
        if (regions.faceFill(face) == fill)
            return true;

        std::unique_ptr<fla::Shape> after(regions.withFaceFill(face, fill));
        if (!after)
            return true;
        fla::DrawingStyle::fitFills(*after);

        // The arrangement was built before the fill was added, which is fine:
        // styles do not change the geometry, and the result is cloned from
        // the painted copy, style table and all.
        commitContents(view, _commandStack, shape, *after, "Paint Bucket");
        return true;
    }

    return true;
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
        _style.hasFill = true;
        if (_onSampled)
            _onSampled(false);
    }

    return true;
}
