#include "shape_merge.h"

#include "../data/linear_gradient.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../geom/planar_map.h"
#include "../geom/shape_geometry.h"

#include <map>
#include <vector>

namespace fla {

namespace {

/// Copies a fill so it can live in another shape's style table.
FillStyle* cloneFill(const FillStyle* fill, DOMElement* parent)
{
    if (!fill)
        return nullptr;

    switch (fill->type())
    {
    case FillStyle::Type::SolidColor:
    {
        const SolidColor* source = static_cast<const SolidColor*>(fill);
        SolidColor* copy = new SolidColor(parent);
        for (int i = 0; i < 4; ++i)
            copy->color[i] = source->color[i];
        copy->transform = source->transform;
        return copy;
    }

    case FillStyle::Type::LinearGradient:
    {
        const LinearGradient* source = static_cast<const LinearGradient*>(fill);
        LinearGradient* copy = new LinearGradient(parent);
        copy->entries = source->entries;
        copy->transform = source->transform;
        return copy;
    }

    case FillStyle::Type::RadialGradient:
    {
        const RadialGradient* source = static_cast<const RadialGradient*>(fill);
        RadialGradient* copy = new RadialGradient(parent);
        copy->entries = source->entries;
        copy->focalPointRatio = source->focalPointRatio;
        copy->transform = source->transform;
        return copy;
    }

    case FillStyle::Type::BitmapFill:
        // Not modelled by the reader either, so there is nothing to copy.
        break;
    }

    return nullptr;
}

StrokeStyle* cloneStroke(const StrokeStyle* stroke, DOMElement* parent)
{
    if (!stroke)
        return nullptr;

    StrokeStyle* copy = nullptr;
    switch (stroke->style())
    {
    case StrokeStyle::Style::Dashed:  copy = new DashedStroke(parent); break;
    case StrokeStyle::Style::Ragged:  copy = new RaggedStroke(parent); break;
    case StrokeStyle::Style::Stipple: copy = new StippleStroke(parent); break;
    case StrokeStyle::Style::Dotted:  copy = new DottedStroke(parent); break;
    case StrokeStyle::Style::Solid:   copy = new SolidStroke(parent); break;
    }

    if (!copy)
        return nullptr;

    copy->weight = stroke->weight;
    copy->scaleMode = stroke->scaleMode;
    copy->fill = cloneFill(stroke->fill, copy);
    return copy;
}

int nextIndex(const std::map<int, FillStyle*>& styles)
{
    return styles.empty() ? 1 : styles.rbegin()->first + 1;
}

int nextIndex(const std::map<int, StrokeStyle*>& styles)
{
    return styles.empty() ? 1 : styles.rbegin()->first + 1;
}

/// Builds an arrangement of a set of curves and reads the file's own fills into
/// its faces, so it can be asked what is painted where.
void arrange(PlanarMap& map, const std::vector<ShapeCurve>& curves)
{
    for (size_t i = 0; i < curves.size(); ++i)
        map.addCurve(curves[i].curve, static_cast<int>(i));

    map.build();
    attributeFillsFromSource(map, curves);
}

} // namespace

namespace {

Edge* cloneEdge(const Edge* source, DOMElement* parent)
{
    if (!source)
        return nullptr;

    Edge* copy = new Edge(parent);
    copy->fillStyle0 = source->fillStyle0;
    copy->fillStyle1 = source->fillStyle1;
    copy->strokeStyle = source->strokeStyle;
    copy->data = source->data;

    for (const Path* path : source->paths)
    {
        if (!path)
            continue;

        Path* pathCopy = new Path(copy);
        pathCopy->styleIndex = path->styleIndex;
        pathCopy->fillStyleIndex = path->fillStyleIndex;
        pathCopy->lineStyleIndex = path->lineStyleIndex;

        for (const PathSegment* segment : path->segments)
        {
            if (segment)
            {
                pathCopy->segments.push_back(new PathSegment(
                    segment->command, segment->points, pathCopy));
            }
        }

        copy->paths.push_back(pathCopy);
    }

    return copy;
}

} // namespace

Shape* cloneShape(const Shape& shape, DOMElement* parent)
{
    Shape* copy = new Shape(parent);

    copy->transform = shape.transform;
    copy->transformationPoint = shape.transformationPoint;
    copy->localBounds = shape.localBounds;
    copy->bounds = shape.bounds;

    // The index a style is filed under is part of the geometry's meaning, so the
    // table is rebuilt with the same numbers rather than renumbered.
    for (const auto& entry : shape.fillsMap)
    {
        FillStyle* fill = cloneFill(entry.second, copy);
        if (!fill)
            continue;
        copy->fills.push_back(fill);
        copy->fillsMap[entry.first] = fill;
    }

    for (const auto& entry : shape.strokesMap)
    {
        StrokeStyle* stroke = cloneStroke(entry.second, copy);
        if (!stroke)
            continue;
        copy->strokes.push_back(stroke);
        copy->strokesMap[entry.first] = stroke;
    }

    for (const Edge* edge : shape.edges)
    {
        Edge* edgeCopy = cloneEdge(edge, copy);
        if (edgeCopy)
            copy->edges.push_back(edgeCopy);
    }

    return copy;
}

void setShapeContents(Shape& target, const Shape& source)
{
    for (FillStyle* fill : target.fills)
        delete fill;
    for (StrokeStyle* stroke : target.strokes)
        delete stroke;
    for (Edge* edge : target.edges)
        delete edge;

    target.fills.clear();
    target.strokes.clear();
    target.edges.clear();
    target.fillsMap.clear();
    target.strokesMap.clear();

    for (const auto& entry : source.fillsMap)
    {
        FillStyle* fill = cloneFill(entry.second, &target);
        if (!fill)
            continue;
        target.fills.push_back(fill);
        target.fillsMap[entry.first] = fill;
    }

    for (const auto& entry : source.strokesMap)
    {
        StrokeStyle* stroke = cloneStroke(entry.second, &target);
        if (!stroke)
            continue;
        target.strokes.push_back(stroke);
        target.strokesMap[entry.first] = stroke;
    }

    for (const Edge* edge : source.edges)
    {
        Edge* copy = cloneEdge(edge, &target);
        if (copy)
            target.edges.push_back(copy);
    }

    target.localBounds = source.localBounds;
    target.bounds = source.bounds;
}

bool ShapeMerger::merge(Shape& target, const Shape& addition)
{
    return combine(target, addition, Operation::Paint);
}

bool ShapeMerger::subtract(Shape& target, const Shape& cutter)
{
    return combine(target, cutter, Operation::Erase);
}

bool ShapeMerger::combine(Shape& target, const Shape& addition, Operation operation)
{
    const std::vector<ShapeCurve> additionCurves = shapeCurves(addition);
    if (additionCurves.empty())
        return false;

    const std::vector<ShapeCurve> targetCurves = shapeCurves(target);

    // What the target paints today, so regions the addition does not cover keep
    // their colour.
    PlanarMap targetMap;
    if (!targetCurves.empty())
        arrange(targetMap, targetCurves);

    // The two shapes number their styles independently, so the addition's are
    // copied in under fresh numbers before anything else looks at them.
    std::map<int, int> fillRemap;
    for (const auto& entry : addition.fillsMap)
    {
        FillStyle* copy = cloneFill(entry.second, &target);
        if (!copy)
            continue;

        const int index = nextIndex(target.fillsMap);
        target.fills.push_back(copy);
        target.fillsMap[index] = copy;
        fillRemap[entry.first] = index;
    }

    std::map<int, int> strokeRemap;
    for (const auto& entry : addition.strokesMap)
    {
        StrokeStyle* copy = cloneStroke(entry.second, &target);
        if (!copy)
            continue;

        const int index = nextIndex(target.strokesMap);
        target.strokes.push_back(copy);
        target.strokesMap[index] = copy;
        strokeRemap[entry.first] = index;
    }

    const auto remap = [](const std::map<int, int>& table, int style) {
        if (style == -1)
            return -1;
        const auto it = table.find(style);
        return it == table.end() ? -1 : it->second;
    };

    std::vector<ShapeCurve> renumbered = additionCurves;
    for (ShapeCurve& curve : renumbered)
    {
        curve.fillStyle0 = remap(fillRemap, curve.fillStyle0);
        curve.fillStyle1 = remap(fillRemap, curve.fillStyle1);
        curve.strokeStyle = remap(strokeRemap, curve.strokeStyle);
    }

    // What the addition paints, in the target's numbering.
    PlanarMap additionMap;
    arrange(additionMap, renumbered);

    // Everything together, which is where the outlines finally cut each other.
    std::vector<ShapeCurve> combined = targetCurves;
    combined.insert(combined.end(), renumbered.begin(), renumbered.end());

    PlanarMap merged;
    for (size_t i = 0; i < combined.size(); ++i)
        merged.addCurve(combined[i].curve, static_cast<int>(i));
    merged.build();

    for (size_t face = 0; face < merged.faces().size(); ++face)
    {
        const int index = static_cast<int>(face);

        if (merged.faces()[face].unbounded)
        {
            merged.setFaceFill(index, -1);
            continue;
        }

        Point inside;
        if (!merged.interiorPoint(index, inside))
        {
            merged.setFaceFill(index, -1);
            continue;
        }

        // The newer drawing wins where it paints. Where it paints nothing --
        // outside it, or inside an unfilled part of it -- whatever was already
        // there shows through, which is how an outline drawn over a fill cuts
        // without erasing.
        const int additionFill = additionMap.fillAt(inside);

        int fill;
        if (additionFill == -1)
        {
            fill = targetMap.fillAt(inside);
        }
        else
        {
            // Erasing takes the same area away instead of painting it, which is
            // what lifting a drawing back out of the artwork leaves behind.
            fill = operation == Operation::Paint ? additionFill : -1;
        }

        merged.setFaceFill(index, fill);
    }

    // Drawing a filled shape over something covers what was there, outlines
    // included. A stroke that belonged to the target and has the addition's
    // fill on both sides of it is buried, and goes.
    //
    // The addition's own outline stays whatever it lies over: it is the thing
    // that was just drawn.
    const size_t targetCurveCount = targetCurves.size();
    const auto keepStroke = [&](const HalfEdge& half) {
        const bool fromAddition = half.source < 0 ||
            static_cast<size_t>(half.source) >= targetCurveCount;

        // Erasing leaves nothing of its own behind: the cutter is a hole being
        // punched, not artwork being added.
        if (fromAddition)
            return operation == Operation::Paint;

        // Both sides, because a stroke along the edge of the addition's fill is
        // still visible from the outside and has to stay.
        const int sides[2] = {
            half.face,
            half.twin >= 0 && half.twin < static_cast<int>(merged.halfEdges().size())
                ? merged.halfEdges()[half.twin].face : -1
        };

        for (const int face : sides)
        {
            if (face < 0 || face >= static_cast<int>(merged.faces().size()))
                return true;
            if (merged.faces()[face].unbounded)
                return true;

            Point inside;
            if (!merged.interiorPoint(face, inside))
                return true;
            if (additionMap.fillAt(inside) == -1)
                return true;
        }

        return false;
    };

    rebuildShapeEdges(target, merged, combined, keepStroke);
    return true;
}

} // namespace fla
