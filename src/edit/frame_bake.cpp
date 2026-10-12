#include "frame_bake.h"
#include "../data/timeline.h"
#include "../data/symbol.h"
#include "graphic_loop.h"

#include "element_clone.h"
#include "motion_object.h"
#include "shape_tween.h"
#include "timeline_edit.h"
#include "transform_blend.h"
#include "tween_ease.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <memory>

namespace fla {

namespace {

/// The symbol frame a graphic shows \a framesIn frames on, as it plays:
/// on, back or round its loop. Forward loops keep counting past the end, as
/// Animate's synced tweens leave them.
int playedOn(const SymbolInstance& instance, int framesIn)
{
    if (instance.loopType != LoopType::LoopReverse && instance.loopType != LoopType::PlayOnceReverse &&
        instance.lastFrame < 0)
        return instance.firstFrame + framesIn;
    int length = 1;
    if (instance.symbol && !instance.symbol->timelines.empty() && instance.symbol->timelines.front())
        for (const Layer* layer : instance.symbol->timelines.front()->layers)
            if (layer)
                length = std::max(length, timeline::layerEnd(*layer));
    return graphicFrame(instance.loopType, instance.firstFrame, instance.lastFrame, length, framesIn);
}

} // namespace

namespace frames {

namespace {

TweenRotation rotationOf(const Frame& frame, int& turns)
{
    turns = 0;
    TweenRotation rotation = TweenRotation::Auto;
    if (frame.motionTweenRotate == "clockwise")
        rotation = TweenRotation::Clockwise;
    else if (frame.motionTweenRotate == "counter-clockwise")
        rotation = TweenRotation::CounterClockwise;
    else if (frame.motionTweenRotate == "none")
        rotation = TweenRotation::None;
    if (rotation == TweenRotation::Clockwise || rotation == TweenRotation::CounterClockwise)
        turns = std::max(0, frame.motionTweenRotateTimes);
    return rotation;
}

/// The keyframe that starts at \a index, or null.
Frame* keyframeAt(Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index == index)
            return frame;
    }
    return nullptr;
}

/// The ease of a tween played backward: an ease in becomes an ease out,
/// and a custom curve is turned end for end.
void flipEase(Frame& frame)
{
    frame.acceleration = -frame.acceleration;
    if (frame.hasCustomEase)
    {
        std::vector<Point> flipped;
        for (auto it = frame.customEase.rbegin(); it != frame.customEase.rend(); ++it)
            flipped.emplace_back(1.0 - it->x, 1.0 - it->y);
        frame.customEase = flipped;
    }
    static const std::pair<const char*, const char*> mirrored[] = {{"In", "Out"}, {"Out", "In"}};
    for (const auto& pair : mirrored)
    {
        // quadIn <-> quadOut; InOut stays as it is.
        const std::string& method = frame.easeMethod;
        const std::string from = pair.first;
        if (method.size() > from.size() && method.compare(method.size() - from.size(), from.size(), from) == 0 &&
            method.find("InOut") == std::string::npos)
        {
            frame.easeMethod = method.substr(0, method.size() - from.size()) + pair.second;
            break;
        }
    }
}

} // namespace

std::vector<Element*> elementsAt(const Frame& span, const Frame* next, int framesIn, double frameRate,
    DOMElement* parent)
{
    std::vector<Element*> copies;
    const double rate = frameRate > 0.0 ? frameRate : 24.0;

    // An object motion tween moves its object by the motion's curves.
    if (span.otherTweenType == "motion object")
    {
        std::string* xml = timeline::motionOf(const_cast<Frame&>(span));
        MotionObject motion;
        const bool moving = xml && motion.parse(*xml);
        for (const Element* element : span.elements)
        {
            Element* copy = element ? cloneElement(*element, parent) : nullptr;
            if (!copy)
                continue;
            if (moving)
                copy->transform = applyMotion(element->transform, element->transformationPoint, motion.at(0.0, rate),
                    motion.at(framesIn, rate));
            copies.push_back(copy);
        }
        return copies;
    }

    const bool tweening = next && span.duration > 0 && framesIn > 0 && span.tweenType != TweenType::None;
    double t = 0.0;
    if (tweening)
    {
        t = easeProgress(static_cast<double>(framesIn) / span.duration, span.acceleration,
            span.hasCustomEase ? span.customEase : std::vector<Point>(), span.easeMethod);
    }

    // A shape tween shows the morph of its first shapes.
    const Shape* morphFrom = nullptr;
    const Shape* morphTo = nullptr;
    std::unique_ptr<Shape> morphed;
    if (tweening && span.tweenType == TweenType::Shape && tweenShapes(span, *next, morphFrom, morphTo))
    {
        ensureMorph(const_cast<Frame&>(span), *next);
        if (span.morphShape)
            morphed.reset(morphedShape(*span.morphShape, *morphFrom, *morphTo, t, morphFrom->transform,
                morphTo->transform, Transform()));
    }

    int turns = 0;
    const TweenRotation rotation = rotationOf(span, turns);
    for (size_t i = 0; i < span.elements.size(); ++i)
    {
        const Element* element = span.elements[i];
        if (!element)
            continue;
        if (morphed && element == morphFrom)
        {
            // The morph is built in the parent's space, so it stands as it is.
            Element* copy = cloneElement(*morphed, parent);
            if (copy)
            {
                copy->transform = Transform();
                copies.push_back(copy);
            }
            continue;
        }
        Element* copy = cloneElement(*element, parent);
        if (!copy)
            continue;
        const Element* toward = tweening && span.tweenType == TweenType::Motion && i < next->elements.size()
            ? next->elements[i] : nullptr;
        if (toward && toward->elementType() == element->elementType())
        {
            copy->transform = blendTransforms(element->transform, toward->transform, t, rotation, turns);
            if (element->elementType() == Element::Type::SymbolInstance)
            {
                auto* instance = static_cast<SymbolInstance*>(copy);
                const auto* from = static_cast<const SymbolInstance*>(element);
                const auto* to = static_cast<const SymbolInstance*>(toward);
                instance->colorTransform = blendColorTransforms(from->colorTransform, to->colorTransform, t);
                instance->filters = blendFilters(from->filters, to->filters, t);
                // A graphic plays on through the tween.
                if (from->symbolType == SymbolType::Graphic && from->loopType != LoopType::SingleFrame)
                    instance->firstFrame = playedOn(*from, framesIn);
            }
        }
        else if (element->elementType() == Element::Type::SymbolInstance)
        {
            auto* instance = static_cast<SymbolInstance*>(copy);
            const auto* from = static_cast<const SymbolInstance*>(element);
            if (from->symbolType == SymbolType::Graphic && from->loopType != LoopType::SingleFrame)
                instance->firstFrame = playedOn(*from, framesIn);
        }
        copies.push_back(copy);
    }
    return copies;
}

void convertToFrameByFrame(Layer& layer, int first, int last, double frameRate)
{
    // The spans that overlap the range, in order, with the keyframe after.
    std::vector<Frame*> spans;
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index <= last && frame->index + frame->duration - 1 >= first && frame->duration > 1)
            spans.push_back(frame);
    }
    for (Frame* span : spans)
    {
        Frame* next = keyframeAt(layer, span->index + span->duration);
        std::vector<Frame*> made;
        for (int in = 1; in < span->duration; ++in)
        {
            auto* frame = new Frame(&layer);
            frame->index = span->index + in;
            frame->duration = 1;
            frame->keyMode = span->keyMode;
            for (Element* element : elementsAt(*span, next, in, frameRate, frame))
                frame->elements.push_back(element);
            made.push_back(frame);
        }
        span->duration = 1;
        span->tweenType = TweenType::None;
        timeline::removeMotion(*span);
        const auto at = std::find(layer.frames.begin(), layer.frames.end(), span);
        layer.frames.insert(at + 1, made.begin(), made.end());
    }
}

void reverseFrames(Layer& layer, int first, int last, std::vector<Frame*>& removed)
{
    (void)removed;
    if (last <= first)
        return;
    // Keyframes at both ends, so the range is whole spans.
    timeline::insertKeyframe(layer, first, false);
    timeline::insertKeyframe(layer, last, false);
    // What follows the range is not touched: the last keyframe keeps any
    // frames it holds past the range.
    Frame* end = keyframeAt(layer, last);
    const int tail = end ? end->duration : 1;

    std::vector<Frame*> range;
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index >= first && frame->index <= last)
            range.push_back(frame);
    }
    if (range.size() < 2)
        return;

    // Each keyframe goes to the mirror of its place; the gaps between them
    // keep their lengths, reversed.
    struct Settings
    {
        TweenType tweenType = TweenType::None;
        int acceleration = 0;
        bool hasCustomEase = false;
        std::vector<Point> customEase;
        std::string easeMethod;
        std::string rotate;
        int rotateTimes = 0;
    };
    std::vector<Settings> tweens(range.size());
    for (size_t i = 0; i < range.size(); ++i)
    {
        Frame* frame = range[i];
        if (frame->otherTweenType == "motion object")
            timeline::removeMotion(*frame);
        tweens[i] = Settings{frame->tweenType, frame->acceleration, frame->hasCustomEase, frame->customEase,
            frame->easeMethod, frame->motionTweenRotate, frame->motionTweenRotateTimes};
        frame->tweenType = TweenType::None;
    }
    const int mirror = first + last;
    for (Frame* frame : range)
        frame->index = mirror - frame->index;
    std::reverse(range.begin(), range.end());
    for (size_t i = 0; i + 1 < range.size(); ++i)
        range[i]->duration = range[i + 1]->index - range[i]->index;
    range.back()->duration = tail;

    // A tween from keyframe k to k + 1 now runs from k + 1 (earlier) to k:
    // the settings move to the keyframe that comes first, played backward.
    const size_t n = range.size();
    for (size_t i = 0; i + 1 < n; ++i)
    {
        // range is reversed: range[i] was keyframe n - 1 - i.
        const Settings& was = tweens[n - 2 - i];
        if (was.tweenType == TweenType::None)
            continue;
        Frame* frame = range[i];
        frame->tweenType = was.tweenType;
        frame->acceleration = was.acceleration;
        frame->hasCustomEase = was.hasCustomEase;
        frame->customEase = was.customEase;
        frame->easeMethod = was.easeMethod;
        // Turning the other way, as many times.
        frame->motionTweenRotate = was.rotate == "clockwise" ? "counter-clockwise"
            : was.rotate == "counter-clockwise" ? "clockwise" : was.rotate;
        frame->motionTweenRotateTimes = was.rotateTimes;
        flipEase(*frame);
        // A shape tween's morph runs the other way now: marked stale, so it
        // is built afresh for the new pair when next shown.
        if (frame->tweenType == TweenType::Shape)
            frame->morphSignature = 1;
    }

    // In index order again.
    std::stable_sort(layer.frames.begin(), layer.frames.end(),
        [](const Frame* a, const Frame* b) { return a->index < b->index; });
}

void insertFrames(Layer& layer, int at, const std::vector<const Frame*>& copied, std::vector<Frame*>& removed)
{
    int length = 0;
    for (const Frame* frame : copied)
    {
        if (frame)
            length = std::max(length, frame->index + frame->duration);
    }
    if (length <= 0)
        return;
    // Room first, past the end of the layer as well, then the copies in it.
    if (at < timeline::layerEnd(layer))
        timeline::insertFrames(layer, at, length);
    timeline::pasteFrames(layer, at, copied, removed);
}

} // namespace frames

} // namespace fla
