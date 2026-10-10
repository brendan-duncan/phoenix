#include "shape_tween.h"

#include "shape_merge.h"
#include "transform_blend.h"

#include "../data/edge.h"
#include "../data/linear_gradient.h"
#include "../data/morph_shape.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

double mix(double a, double b, double t)
{
    return a + (b - a) * t;
}

uint8_t mixByte(uint8_t a, uint8_t b, double t)
{
    return static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(mix(a, b, t)))));
}

Point apply(const Transform& m, const Point& p)
{
    return Point(m.m11 * p.x + m.m21 * p.y + m.tx, m.m12 * p.x + m.m22 * p.y + m.ty);
}

template <typename Entry>
void blendEntries(std::vector<Entry>& entries, const std::vector<Entry>& others, double t)
{
    for (size_t i = 0; i < entries.size() && i < others.size(); ++i)
    {
        entries[i].ratio = mix(entries[i].ratio, others[i].ratio, t);
        for (int c = 0; c < 4; ++c)
            entries[i].color[c] = mixByte(entries[i].color[c], others[i].color[c], t);
    }
}

} // namespace

FillStyle* blendFills(const FillStyle* a, const FillStyle* b, double t, DOMElement* parent)
{
    if (!a)
        return b ? cloneFill(b, parent) : nullptr;
    if (!b || b->type() != a->type())
        return cloneFill(a, parent);

    switch (a->type())
    {
    case FillStyle::Type::SolidColor:
    {
        const SolidColor* from = static_cast<const SolidColor*>(a);
        const SolidColor* to = static_cast<const SolidColor*>(b);
        SolidColor* result = new SolidColor(parent);
        for (int c = 0; c < 4; ++c)
            result->color[c] = mixByte(from->color[c], to->color[c], t);
        return result;
    }

    case FillStyle::Type::LinearGradient:
    {
        const LinearGradient* from = static_cast<const LinearGradient*>(a);
        const LinearGradient* to = static_cast<const LinearGradient*>(b);
        LinearGradient* result = static_cast<LinearGradient*>(cloneFill(a, parent));
        if (from->entries.size() == to->entries.size())
        {
            blendEntries(result->entries, to->entries, t);
            result->transform = blendTransforms(from->transform, to->transform, t);
        }
        return result;
    }

    case FillStyle::Type::RadialGradient:
    {
        const RadialGradient* from = static_cast<const RadialGradient*>(a);
        const RadialGradient* to = static_cast<const RadialGradient*>(b);
        RadialGradient* result = static_cast<RadialGradient*>(cloneFill(a, parent));
        if (from->entries.size() == to->entries.size())
        {
            blendEntries(result->entries, to->entries, t);
            result->transform = blendTransforms(from->transform, to->transform, t);
            result->focalPointRatio = mix(from->focalPointRatio, to->focalPointRatio, t);
        }
        return result;
    }

    case FillStyle::Type::BitmapFill:
        break;
    }
    return cloneFill(a, parent);
}

StrokeStyle* blendStrokes(const StrokeStyle* a, const StrokeStyle* b, double t, DOMElement* parent)
{
    if (!a)
        return b ? cloneStroke(b, parent) : nullptr;
    StrokeStyle* result = cloneStroke(a, parent);
    if (!result || !b)
        return result;

    result->weight = mix(a->weight, b->weight, t);
    if (a->fill)
    {
        delete result->fill;
        result->fill = blendFills(a->fill, b->fill, t, result);
    }
    return result;
}

Shape* morphedShape(const MorphShape& morph, const Shape& from, const Shape& to, double t,
    const Transform& fromToDocument, const Transform& toToDocument,
    const Transform& documentToLocal)
{
    Shape* shape = new Shape(nullptr);

    // Styles by index: the first keyframe's table, each blended with the
    // last keyframe's style of the same index.
    for (const auto& entry : from.fillsMap)
    {
        FillStyle* fill = blendFills(entry.second, to.getFillStyleByIndex(entry.first), t, shape);
        if (!fill)
            continue;
        shape->fills.push_back(fill);
        shape->fillsMap[entry.first] = fill;
    }
    for (const auto& entry : from.strokesMap)
    {
        StrokeStyle* stroke = blendStrokes(entry.second, to.getStrokeStyleByIndex(entry.first), t, shape);
        if (!stroke)
            continue;
        shape->strokes.push_back(stroke);
        shape->strokesMap[entry.first] = stroke;
    }

    // Some files leave every segment unstyled; those draw with the shape's
    // first fill and stroke, which is what they have.
    bool anyStyled = false;
    for (const MorphSegment* segment : morph.segments)
    {
        if (segment && (segment->fillIndex1 || segment->fillIndex2 ||
                        segment->strokeIndex1 || segment->strokeIndex2))
        {
            anyStyled = true;
        }
    }
    const int firstFill = shape->fillsMap.empty() ? -1 : shape->fillsMap.begin()->first;
    const int firstStroke = shape->strokesMap.empty() ? -1 : shape->strokesMap.begin()->first;

    const auto at = [&](const Point& a, const Point& b) {
        const Point pa = apply(fromToDocument, a);
        const Point pb = apply(toToDocument, b);
        return apply(documentToLocal, Point(mix(pa.x, pb.x, t), mix(pa.y, pb.y, t)));
    };

    bool first = true;
    Rect bounds;
    const auto include = [&](const Point& p) {
        if (first)
        {
            bounds = Rect(p, p);
            first = false;
            return;
        }
        bounds.topLeft.x = std::min(bounds.topLeft.x, p.x);
        bounds.topLeft.y = std::min(bounds.topLeft.y, p.y);
        bounds.bottomRight.x = std::max(bounds.bottomRight.x, p.x);
        bounds.bottomRight.y = std::max(bounds.bottomRight.y, p.y);
    };

    for (const MorphSegment* segment : morph.segments)
    {
        if (!segment)
            continue;

        Edge* edge = new Edge(shape);
        if (anyStyled)
        {
            // Index 1 is the fill on the right of the segment's direction,
            // index 2 the one on the left, as the file's own edges number
            // their sides; zero is none.
            edge->fillStyle0 = segment->fillIndex1 > 0 ? segment->fillIndex1 : -1;
            edge->fillStyle1 = segment->fillIndex2 > 0 ? segment->fillIndex2 : -1;
            const int stroke = segment->strokeIndex1 > 0 ? segment->strokeIndex1 : segment->strokeIndex2;
            edge->strokeStyle = stroke > 0 ? stroke : -1;
        }
        else
        {
            edge->fillStyle1 = firstFill;
            edge->strokeStyle = firstStroke;
        }

        Path* path = new Path(edge);
        const Point start = at(segment->startPointA, segment->startPointB);
        include(start);
        path->segments.push_back(new PathSegment(PathSegment::Command::Move, {start}, path));
        for (const MorphCurves* curve : segment->curves)
        {
            if (!curve)
                continue;
            const Point control = at(curve->controlPointA, curve->controlPointB);
            const Point anchor = at(curve->anchorPointA, curve->anchorPointB);
            include(control);
            include(anchor);
            path->segments.push_back(new PathSegment(PathSegment::Command::Quad, {control, anchor}, path));
        }
        edge->paths.push_back(path);
        shape->edges.push_back(edge);
    }

    shape->localBounds = bounds;
    shape->bounds = bounds;
    return shape;
}

} // namespace fla
