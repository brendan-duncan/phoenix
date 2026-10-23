#include "artist_edit.h"

#include "timeline_edit.h"

#include "../data/element.h"
#include "../data/frame.h"
#include "../data/layer.h"

#include <algorithm>

namespace fla {
namespace artist {

const char* const kEases[5] = {"jump", "even", "in", "out", "both"};

namespace {

Frame* spanCovering(const Layer& layer, int index)
{
    return timeline::spanAt(const_cast<Layer&>(layer), index);
}

/// The FLA ease preset for a named ease; empty for "even".
std::string methodFor(const std::string& ease)
{
    if (ease == "in")
        return "quadIn";
    if (ease == "out")
        return "quadOut";
    if (ease == "both")
        return "quadInOut";
    return std::string();
}

bool endsWith(const std::string& text, const char* suffix)
{
    const std::string end(suffix);
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

} // namespace

int addFrameAfter(Layer& layer, int index, bool copy)
{
    if (layer.frames.empty())
    {
        timeline::insertKeyframe(layer, 0, !copy);
        return 0;
    }
    const Frame* span = spanCovering(layer, std::max(index, 0));
    const int target = span ? span->index + std::max(span->duration, 1) : timeline::layerEnd(layer);
    // The span (or the last one) grows by a frame, moving what follows
    // along; the frame it grew by then becomes a keyframe of its own.
    timeline::insertFrames(layer, target - 1, 1);
    timeline::insertKeyframe(layer, target, !copy);
    return target;
}

bool insideSpan(const Layer& layer, int index)
{
    const Frame* span = spanCovering(layer, index);
    return span && span->index != index;
}

bool tweenable(const Frame& span)
{
    for (const Element* element : span.elements)
        if (!element || element->elementType() != Element::Type::SymbolInstance)
            return false;
    return true;
}

Frame* keyForMove(Layer& layer, int index)
{
    if (!insideSpan(layer, index))
        return nullptr;
    Frame* span = spanCovering(layer, index);
    Frame* key = timeline::insertKeyframe(layer, index, false);
    if (key)
        timeline::setTween(layer, span->index, true);
    return key;
}

std::string easeOf(const Layer& layer, int index)
{
    const Frame* span = spanCovering(layer, index);
    if (!span || span->tweenType != TweenType::Motion)
        return "jump";
    const std::string& method = span->easeMethod;
    if (!method.empty() && method != "none" && method != "classic")
    {
        if (endsWith(method, "InOut"))
            return "both";
        if (endsWith(method, "In"))
            return "in";
        if (endsWith(method, "Out"))
            return "out";
    }
    if (span->hasCustomEase)
        return "both";
    if (span->acceleration < 0)
        return "in";
    if (span->acceleration > 0)
        return "out";
    return "even";
}

bool setEase(Layer& layer, int index, const std::string& ease)
{
    Frame* span = spanCovering(layer, index);
    if (!span)
        return false;
    if (ease == "jump")
        return timeline::setTween(layer, index, false);
    if (std::find(std::begin(kEases), std::end(kEases), ease) == std::end(kEases))
        return false;
    const bool tweened = timeline::setTween(layer, index, true);
    if (span->tweenType != TweenType::Motion)
        return false;
    const std::string method = methodFor(ease);
    const bool same = span->easeMethod == method && span->acceleration == 0 && !span->hasCustomEase;
    if (same && !tweened)
        return false;
    span->acceleration = 0;
    span->hasCustomEase = false;
    span->customEase.clear();
    span->customEaseName.clear();
    span->easeMethod = method;
    span->hasEase = span->hasEase || !method.empty();
    // The preset's name travels with it, as the file keeps it.
    auto& attributes = span->extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& a) { return a.first == "easeMethodName"; }),
        attributes.end());
    if (!method.empty())
        attributes.emplace_back("easeMethodName", method);
    return true;
}

} // namespace artist
} // namespace fla
