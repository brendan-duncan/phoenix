#include "color_art.h"

#include "shape_merge.h"

#include "../data/edge.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/timeline.h"
#include "../data/transform.h"
#include "../geom/curve.h"
#include "../geom/shape_geometry.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace fla {

namespace colorart {

namespace {

const char* const kMark = "phoenixLineArt";

std::string attribute(const Layer& layer, const char* name)
{
    for (const auto& [key, value] : layer.extraAttributes)
        if (key == name)
            return value;
    return std::string();
}

/// \a a then \a b.
Transform combined(const Transform& a, const Transform& b)
{
    return Transform(a.m11 * b.m11 + a.m12 * b.m21, a.m11 * b.m12 + a.m12 * b.m22, a.m21 * b.m11 + a.m22 * b.m21,
        a.m21 * b.m12 + a.m22 * b.m22, a.tx * b.m11 + a.ty * b.m21 + b.tx, a.tx * b.m12 + a.ty * b.m22 + b.ty);
}

Point mapped(const Point& point, const Transform& t)
{
    return Point(t.m11 * point.x + t.m21 * point.y + t.tx, t.m12 * point.x + t.m22 * point.y + t.ty);
}

/// Keeps the edges \a keep says, with \a clear applied to each.
Shape* filtered(const Shape& shape, const std::function<bool(const Edge&)>& keep, const std::function<void(Edge&)>& clear)
{
    Shape* copy = cloneShape(shape, nullptr);
    std::vector<Edge*>& edges = copy->edges;
    for (auto it = edges.begin(); it != edges.end();)
    {
        clear(**it);
        if (!keep(**it))
        {
            delete *it;
            it = edges.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return copy;
}

const Frame* frameAt(const Layer& layer, int index)
{
    for (const Frame* frame : layer.frames)
        if (frame && index >= frame->index && index < frame->index + std::max(frame->duration, 1))
            return frame;
    return nullptr;
}

} // namespace

bool isColorArt(const Layer& layer)
{
    return !attribute(layer, kMark).empty();
}

Layer* lineLayerOf(const Timeline& timeline, const Layer& colorLayer)
{
    const std::string name = attribute(colorLayer, kMark);
    if (name.empty())
        return nullptr;
    for (Layer* layer : timeline.layers)
        if (layer && layer != &colorLayer && layer->name == name)
            return layer;
    return nullptr;
}

Shape* strokesOnly(const Shape& shape)
{
    return filtered(shape, [](const Edge& edge) { return edge.strokeStyle > 0; },
        [](Edge& edge) {
            edge.fillStyle0 = -1;
            edge.fillStyle1 = -1;
        });
}

Shape* fillsOnly(const Shape& shape)
{
    return filtered(shape, [](const Edge& edge) { return edge.fillStyle0 > 0 || edge.fillStyle1 > 0; },
        [](Edge& edge) { edge.strokeStyle = -1; });
}

Layer* newColorLayer(const Layer& line)
{
    Layer* layer = new Layer(nullptr);
    layer->name = line.name + " Color";
    layer->extraAttributes.emplace_back(kMark, line.name);
    for (int i = 0; i < 4; ++i)
        layer->color[i] = line.color[i];
    for (const Frame* frame : line.frames)
    {
        if (!frame)
            continue;
        Frame* copy = new Frame(layer);
        copy->index = frame->index;
        copy->duration = frame->duration;
        copy->keyMode = frame->keyMode;
        for (const Element* element : frame->elements)
        {
            if (!element || element->elementType() != Element::Type::Shape)
                continue;
            Shape* fills = fillsOnly(static_cast<const Shape&>(*element));
            if (fills->edges.empty())
            {
                delete fills;
                continue;
            }
            fills->parent = copy;
            copy->elements.push_back(fills);
        }
        layer->frames.push_back(copy);
    }
    return layer;
}

std::vector<Curve> lineCutters(const Layer& line, int frameIndex, const Transform& stageToTarget)
{
    std::vector<Curve> cutters;
    const Frame* frame = frameAt(line, frameIndex);
    if (!frame)
        return cutters;
    std::function<void(const Element*, const Transform&)> collect = [&](const Element* element, const Transform& outer) {
        if (!element)
            return;
        const Transform here = combined(element->transform, outer);
        if (element->elementType() == Element::Type::Group)
        {
            for (const Element* member : static_cast<const Group*>(element)->members)
                collect(member, here);
            return;
        }
        if (element->elementType() != Element::Type::Shape)
            return;
        const Transform toTarget = combined(here, stageToTarget);
        for (const ShapeCurve& curve : shapeCurves(static_cast<const Shape&>(*element)))
        {
            if (curve.strokeStyle <= 0)
                continue;
            cutters.push_back(Curve::cubic(mapped(curve.curve.controlPoint(0), toTarget),
                mapped(curve.curve.controlPoint(1), toTarget), mapped(curve.curve.controlPoint(2), toTarget),
                mapped(curve.curve.controlPoint(3), toTarget)));
        }
    };
    for (const Element* element : frame->elements)
        collect(element, Transform());
    return cutters;
}

std::vector<Curve> withoutShared(const std::vector<Curve>& cutters, const Shape& shape)
{
    const std::vector<ShapeCurve> own = shapeCurves(shape);
    const auto near = [](const Point& a, const Point& b) { return std::abs(a.x - b.x) < 0.05 && std::abs(a.y - b.y) < 0.05; };
    const auto same = [&near](const Curve& a, const Curve& b) {
        bool forward = true;
        bool backward = true;
        for (int i = 0; i < 4; ++i)
        {
            forward = forward && near(a.controlPoint(i), b.controlPoint(i));
            backward = backward && near(a.controlPoint(i), b.controlPoint(3 - i));
        }
        return forward || backward;
    };
    std::vector<Curve> kept;
    for (const Curve& cutter : cutters)
        if (std::none_of(own.begin(), own.end(), [&](const ShapeCurve& curve) { return same(curve.curve, cutter); }))
            kept.push_back(cutter);
    return kept;
}

} // namespace colorart

} // namespace fla
