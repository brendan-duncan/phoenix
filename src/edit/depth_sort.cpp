#include "depth_sort.h"

#include "frame_bake.h"
#include "three_d.h"
#include "transform_blend.h"
#include "tween_ease.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <numeric>

namespace fla::depthsort {

namespace {

const char* const kSort = "phoenixSortByDepth";

std::vector<Timeline*> allTimelines(Document& document)
{
    std::vector<Timeline*> timelines(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    timelines.erase(std::remove(timelines.begin(), timelines.end(), nullptr), timelines.end());
    return timelines;
}

bool identity(const std::vector<size_t>& order)
{
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] != i)
            return false;
    return true;
}

/// \a frame's elements put in \a order.
void arrange(Frame& frame, const std::vector<size_t>& order)
{
    if (order.size() != frame.elements.size() || identity(order))
        return;
    std::vector<Element*> arranged;
    arranged.reserve(order.size());
    for (size_t index : order)
        arranged.push_back(frame.elements[index]);
    frame.elements = std::move(arranged);
}

} // namespace

bool sorts(const Layer& layer)
{
    for (const auto& attribute : layer.extraAttributes)
        if (attribute.first == kSort)
            return attribute.second == "true" || attribute.second == "1";
    return false;
}

std::vector<std::pair<std::string, std::string>> withSorting(const Layer& layer, bool on)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : layer.extraAttributes)
        if (attribute.first != kSort)
            attributes.push_back(attribute);
    if (on)
        attributes.emplace_back(kSort, "true");
    return attributes;
}

double depthOf(const Element& element)
{
    if (element.elementType() != Element::Type::SymbolInstance)
        return 0.0;
    const auto& instance = static_cast<const SymbolInstance&>(element);
    return instance.symbolType == SymbolType::MovieClip ? threed::of(instance).z : 0.0;
}

std::vector<double> depthsAt(const Frame& span, const Frame* next, int framesIn)
{
    std::vector<double> depths;
    depths.reserve(span.elements.size());
    const bool tweening = next && span.tweenType == TweenType::Motion && span.duration > 0 && framesIn > 0;
    const double t = tweening ? easeProgress(static_cast<double>(framesIn) / span.duration, span.acceleration,
                                    span.hasCustomEase ? span.customEase : std::vector<Point>(), span.easeMethod)
                              : 0.0;
    const std::vector<int> pairs = tweening ? pairTweenElements(span.elements, next->elements) : std::vector<int>();
    for (size_t i = 0; i < span.elements.size(); ++i)
    {
        const Element* element = span.elements[i];
        double depth = element ? depthOf(*element) : 0.0;
        const int paired = i < pairs.size() ? pairs[i] : -1;
        if (paired >= 0 && next->elements[static_cast<size_t>(paired)])
            depth += (depthOf(*next->elements[static_cast<size_t>(paired)]) - depth) * t;
        depths.push_back(depth);
    }
    return depths;
}

std::vector<size_t> order(const std::vector<double>& depths)
{
    std::vector<size_t> indices(depths.size());
    std::iota(indices.begin(), indices.end(), size_t(0));
    std::stable_sort(indices.begin(), indices.end(), [&depths](size_t a, size_t b) { return depths[a] > depths[b]; });
    return indices;
}

const Frame* tweenTarget(const Layer& layer, const Frame& span)
{
    if (span.tweenType == TweenType::None)
        return nullptr;
    const int end = span.index + std::max(1, span.duration);
    for (const Frame* frame : layer.frames)
        if (frame && frame->index == end)
            return frame;
    return nullptr;
}

std::vector<size_t> drawOrder(const Layer& layer, const Frame& span, int framesIn)
{
    if (!sorts(layer))
    {
        std::vector<size_t> indices(span.elements.size());
        std::iota(indices.begin(), indices.end(), size_t(0));
        return indices;
    }
    return order(depthsAt(span, tweenTarget(layer, span), framesIn));
}

int bake(Document& document, double frameRate, std::vector<std::string>& notes)
{
    int baked = 0;
    for (Timeline* timeline : allTimelines(document))
        for (Layer* layer : timeline->layers)
        {
            if (!layer || !sorts(*layer))
                continue;
            int reordered = 0, split = 0;
            for (size_t k = 0; k < layer->frames.size(); ++k)
            {
                Frame* key = layer->frames[k];
                // A shape tween morphs its shapes by their place in the
                // list, which sorting would mix up.
                if (!key || key->elements.size() < 2 || key->tweenType == TweenType::Shape)
                    continue;
                const Frame* next = key->tweenType == TweenType::Motion ? tweenTarget(*layer, *key) : nullptr;
                const int first = key->index;
                const int span = std::max(1, key->duration);
                std::vector<std::vector<size_t>> orders{order(depthsAt(*key, next, 0))};
                bool changes = false;
                for (int f = 1; next && f < span; ++f)
                {
                    orders.push_back(order(depthsAt(*key, next, f)));
                    changes = changes || orders.back() != orders.front();
                }
                // Both ends of a classic tween put in the same order keep
                // their elements paired.
                if (next && order(depthsAt(*next, nullptr, 0)) != orders.front())
                    changes = true;
                if (!changes || span < 2)
                {
                    if (!identity(orders.front()))
                        ++reordered;
                    arrange(*key, orders.front());
                    continue;
                }
                // The order changes as they tween: a keyframe a frame, each
                // in its own order.
                frames::convertToFrameByFrame(*layer, first, first + span - 1, frameRate);
                for (Frame* frame : layer->frames)
                    if (frame && frame->index >= first && frame->index < first + span)
                        arrange(*frame, orders[static_cast<size_t>(frame->index - first)]);
                while (k + 1 < layer->frames.size() && layer->frames[k + 1] && layer->frames[k + 1]->index < first + span)
                    ++k;
                ++split;
            }
            layer->extraAttributes = withSorting(*layer, false);
            ++baked;
            if (reordered > 0 || split > 0)
                notes.push_back("Layer " + layer->name + " (" + timeline->name + "): its clips are listed in depth order" +
                    (split > 0 ? std::string(", a keyframe a frame where the order changes as they move in Z") : std::string()) +
                    ".");
        }
    return baked;
}

} // namespace fla::depthsort
