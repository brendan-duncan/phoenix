#include "behavior_frames.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/controllers.h"
#include "../edit/element_clone.h"
#include "../edit/frame_bake.h"
#include "../edit/function_curves.h"
#include "../edit/graphic_loop.h"
#include "../edit/transform_blend.h"
#include "../edit/tween_ease.h"

#include <algorithm>
#include <memory>

namespace fla::behaviors {

namespace {

Hearing& hearing()
{
    static Hearing set;
    return set;
}

} // namespace

void setHearing(Hearing given)
{
    hearing() = std::move(given);
}

namespace {

Rigging& rigging()
{
    static Rigging set;
    return set;
}

} // namespace

void setRigging(Rigging given)
{
    rigging() = std::move(given);
}

namespace {

const Frame* keyframeAt(const Layer& layer, int frame)
{
    for (const Frame* keyframe : layer.frames)
        if (keyframe && frame >= keyframe->index && frame < keyframe->index + std::max(1, keyframe->duration))
            return keyframe;
    return nullptr;
}

bool holds(const Frame* keyframe, size_t index, const std::string& symbol)
{
    if (!keyframe || index >= keyframe->elements.size() || !keyframe->elements[index] ||
        keyframe->elements[index]->elementType() != Element::Type::SymbolInstance)
        return false;
    return static_cast<const SymbolInstance*>(keyframe->elements[index])->libraryItemName == symbol;
}

TweenRotation rotationOf(const Frame& frame)
{
    if (frame.motionTweenRotate == "clockwise")
        return TweenRotation::Clockwise;
    if (frame.motionTweenRotate == "counter-clockwise")
        return TweenRotation::CounterClockwise;
    if (frame.motionTweenRotate == "none")
        return TweenRotation::None;
    return TweenRotation::Auto;
}

} // namespace

void runOf(const Layer& layer, const Frame& keyframe, size_t index, const std::string& symbol, int& start, int& end)
{
    start = keyframe.index;
    end = keyframe.index + std::max(1, keyframe.duration) - 1;
    for (const Frame* before = keyframeAt(layer, start - 1); start > 0 && holds(before, index, symbol);
         before = keyframeAt(layer, start - 1))
        start = before->index;
    for (const Frame* after = keyframeAt(layer, end + 1); holds(after, index, symbol); after = keyframeAt(layer, end + 1))
        end = after->index + std::max(1, after->duration) - 1;
}

bool keyedOn(const Layer& layer, size_t index, const std::string& symbol, int frame, Transform& transform, Keyed& keyed)
{
    const Frame* span = keyframeAt(layer, frame);
    if (!holds(span, index, symbol))
        return false;
    const auto* instance = static_cast<const SymbolInstance*>(span->elements[index]);
    transform = instance->transform;
    // Part way through a classic tween, eased and turned as it plays.
    if (span->tweenType == TweenType::Motion && frame > span->index)
    {
        const int endIndex = span->index + std::max(1, span->duration);
        for (const Frame* next : layer.frames)
        {
            if (!next || next->index != endIndex || index >= next->elements.size() || !next->elements[index])
                continue;
            const double t = easeProgress(static_cast<double>(frame - span->index) / std::max(1, span->duration),
                span->acceleration, span->hasCustomEase ? span->customEase : std::vector<Point>(), span->easeMethod);
            const TweenRotation rotation = rotationOf(*span);
            const int turns = rotation == TweenRotation::Clockwise || rotation == TweenRotation::CounterClockwise
                ? std::max(0, span->motionTweenRotateTimes) : 0;
            transform = blendTransforms(instance->transform, next->elements[index]->transform, t, rotation, turns);
            break;
        }
    }
    keyed = keyedOf(transform, instance->transformationPoint, instance->colorTransform);
    return true;
}

Copies evaluateOn(Engine& engine, const SymbolInstance& instance, const Transform& keyed, int frame, double frameRate,
    std::string* error)
{
    const auto* keyframe = dynamic_cast<const Frame*>(instance.parent);
    const auto* layer = keyframe ? dynamic_cast<const Layer*>(keyframe->parent) : nullptr;
    if (!keyframe || !layer)
        return Copies(1);
    const auto place = std::find(keyframe->elements.begin(), keyframe->elements.end(), &instance);
    const size_t index = static_cast<size_t>(place - keyframe->elements.begin());
    const std::string& symbol = instance.libraryItemName;
    Moment moment;
    moment.frame = frame;
    moment.frameRate = frameRate > 0.0 ? frameRate : 24.0;
    runOf(*layer, *keyframe, index, symbol, moment.start, moment.end);
    moment.keyed = keyedOf(keyed, instance.transformationPoint, instance.colorTransform);
    moment.keyedAt = [layer, index, &symbol](int at, Keyed& out) {
        Transform ignored;
        return keyedOn(*layer, index, symbol, at, ignored, out);
    };
    // The motion guide the layer is under: the strokes on its keyframe at
    // this frame.
    GuidePath path;
    const Layer* guide = layer->parentLayer;
    if (guide && guide->layerType == Layer::Type::Guide)
    {
        if (const Frame* guideFrame = keyframeAt(*guide, frame))
        {
            std::vector<const Shape*> shapes;
            for (const Element* element : guideFrame->elements)
                if (element && element->elementType() == Element::Type::Shape)
                    shapes.push_back(static_cast<const Shape*>(element));
            if (!shapes.empty())
                path.build(shapes);
        }
    }
    if (!path.empty())
        moment.path = &path;

    // Other instances on the timeline, by name: as keyed, and moved by
    // their own behaviors one level deep, so two that look at each other
    // do not go round for ever.
    static thread_local int depth = 0;
    const auto* timeline = dynamic_cast<const Timeline*>(layer->parent);
    moment.other = [&engine, timeline, frameRate](const std::string& name, int at, Keyed& out) {
        if (!timeline || name.empty())
            return false;
        for (const Layer* each : timeline->layers)
        {
            const Frame* keyframe = each ? keyframeAt(*each, at) : nullptr;
            if (!keyframe)
                continue;
            for (size_t i = 0; i < keyframe->elements.size(); ++i)
            {
                const Element* element = keyframe->elements[i];
                if (!element || element->elementType() != Element::Type::SymbolInstance)
                    continue;
                const auto* target = static_cast<const SymbolInstance*>(element);
                if (target->name != name)
                    continue;
                Transform transform;
                if (!keyedOn(*each, i, target->libraryItemName, at, transform, out))
                    return false;
                if (depth == 0 && any(*target))
                {
                    ++depth;
                    const Copies moved = evaluateOn(engine, *target, transform, at, frameRate);
                    --depth;
                    if (!moved.empty())
                        out = keyedOf(apply(transform, target->transformationPoint, moved.front()),
                            target->transformationPoint, target->colorTransform);
                }
                return true;
            }
        }
        return false;
    };
    // The master controllers, kept in the document.
    const Document* document = nullptr;
    for (const DOMElement* up = layer->parent; up && !document; up = up->parent)
        document = dynamic_cast<const Document*>(up);
    moment.controller = [document](const std::string& name, double& value) {
        if (!document)
            return false;
        for (const controllers::Controller& each : controllers::load(*document))
            if (each.name == name)
            {
                value = each.value;
                return true;
            }
        return false;
    };
    moment.loudness = [document](int at) { return document && hearing() ? hearing()(*document, at) : 0.0; };
    moment.parentAt = [layer](int at, double matrix[6]) { return rigging() && rigging()(*layer, at, matrix); };
    return engine.evaluate(of(instance), moment, error);
}

bool drivenValue(Engine& engine, const Layer& layer, int frame, const std::string& key, double frameRate, double& value)
{
    const Frame* keyframe = keyframeAt(layer, frame);
    if (!keyframe)
        return false;
    for (size_t i = 0; i < keyframe->elements.size(); ++i)
    {
        const Element* element = keyframe->elements[i];
        if (!element || element->elementType() != Element::Type::SymbolInstance ||
            !any(*static_cast<const SymbolInstance*>(element)))
            continue;
        const auto* instance = static_cast<const SymbolInstance*>(element);
        Transform transform;
        Keyed ignored;
        if (!keyedOn(layer, i, instance->libraryItemName, frame, transform, ignored))
            return false;
        const Copies copies = evaluateOn(engine, *instance, transform, frame, frameRate);
        if (copies.empty())
            return false;
        // Read off a copy left where the behaviors put it.
        std::unique_ptr<Element> shown(cloneElement(*instance, nullptr));
        auto* moved = static_cast<SymbolInstance*>(shown.get());
        moved->transform = apply(transform, instance->transformationPoint, copies.front());
        moved->colorTransform.alphaMultiplier *= copies.front().alpha;
        value = curves::get(*moved, key);
        return true;
    }
    return false;
}

int lengthOf(const Symbol* symbol)
{
    int length = 1;
    if (!symbol)
        return length;
    for (const Timeline* timeline : symbol->timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                if (frame)
                    length = std::max(length, frame->index + std::max(frame->duration, 1));
    return length;
}

int bake(Engine& engine, Layer& layer, int first, int last, double frameRate, std::vector<std::string>* errors)
{
    // Everything is worked out first, from the frames as they are: a
    // behavior that looks back along the keyed motion must not see what
    // is baked.
    struct Job
    {
        int frame = 0;
        size_t index = 0;
        Transform keyed;
        Copies copies;
    };
    std::vector<Job> jobs;
    for (int f = first; f <= last; ++f)
    {
        const Frame* keyframe = keyframeAt(layer, f);
        if (!keyframe)
            continue;
        for (size_t i = 0; i < keyframe->elements.size(); ++i)
        {
            const Element* element = keyframe->elements[i];
            if (!element || element->elementType() != Element::Type::SymbolInstance)
                continue;
            const auto* instance = static_cast<const SymbolInstance*>(element);
            if (!any(*instance))
                continue;
            Job job;
            job.frame = f;
            job.index = i;
            Keyed ignored;
            if (!keyedOn(layer, i, instance->libraryItemName, f, job.keyed, ignored))
                continue;
            std::string error;
            job.copies = evaluateOn(engine, *instance, job.keyed, f, frameRate, &error);
            if (!error.empty() && errors && std::find(errors->begin(), errors->end(), error) == errors->end())
                errors->push_back(error);
            jobs.push_back(job);
        }
    }
    if (jobs.empty())
        return 0;

    frames::convertToFrameByFrame(layer, first, last, frameRate);

    // Later places first, so the copies put in do not move the instances
    // still to bake on that frame.
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        return a.frame != b.frame ? a.frame < b.frame : a.index > b.index;
    });
    int baked = 0;
    for (const Job& job : jobs)
    {
        Frame* keyframe = nullptr;
        for (Frame* each : layer.frames)
            if (each && each->index == job.frame)
                keyframe = each;
        if (!keyframe || job.index >= keyframe->elements.size() || !keyframe->elements[job.index] ||
            keyframe->elements[job.index]->elementType() != Element::Type::SymbolInstance)
            continue;
        auto* instance = static_cast<SymbolInstance*>(keyframe->elements[job.index]);
        // No copies left (all slid off a path): it is not shown there.
        if (job.copies.empty())
        {
            keyframe->elements.erase(keyframe->elements.begin() + static_cast<std::ptrdiff_t>(job.index));
            delete instance;
            ++baked;
            continue;
        }
        instance->extraAttributes = withBehaviors(*instance, {});
        const Copies& copies = job.copies;
        // The copies start as the instance is, before it is placed.
        std::vector<SymbolInstance*> made;
        for (size_t c = 1; c < copies.size(); ++c)
            made.push_back(static_cast<SymbolInstance*>(cloneElement(*instance, keyframe)));
        const Point pivot = instance->transformationPoint;
        const double alpha = instance->colorTransform.alphaMultiplier;
        const LoopType loop = instance->loopType;
        const int firstFrame = instance->firstFrame;
        const int lastFrame = instance->lastFrame;
        const int length = lengthOf(instance->symbol);
        const auto place = [&](SymbolInstance& target, const Delta& delta) {
            target.transform = apply(job.keyed, pivot, delta);
            target.colorTransform.alphaMultiplier = alpha * delta.alpha;
            // A graphic's loop, moved on.
            if (delta.frame > 0 && target.symbolType == SymbolType::Graphic)
                target.firstFrame = graphicFrame(loop, firstFrame, lastFrame, length, delta.frame);
        };
        place(*instance, copies.front());
        for (size_t c = 1; c < copies.size(); ++c)
        {
            place(*made[c - 1], copies[c]);
            keyframe->elements.insert(keyframe->elements.begin() + static_cast<std::ptrdiff_t>(job.index + c), made[c - 1]);
        }
        ++baked;
    }
    // A keyframe only where something changes.
    frames::mergeHeldFrames(layer, first, last);
    return baked;
}

std::vector<std::pair<int, int>> runsWithBehaviors(const Layer& layer)
{
    std::vector<std::pair<int, int>> runs;
    for (const Frame* keyframe : layer.frames)
    {
        if (!keyframe)
            continue;
        for (size_t i = 0; i < keyframe->elements.size(); ++i)
        {
            const Element* element = keyframe->elements[i];
            if (!element || element->elementType() != Element::Type::SymbolInstance ||
                !any(*static_cast<const SymbolInstance*>(element)))
                continue;
            int start = 0;
            int end = 0;
            runOf(layer, *keyframe, i, static_cast<const SymbolInstance*>(element)->libraryItemName, start, end);
            runs.emplace_back(start, end);
        }
    }
    std::sort(runs.begin(), runs.end());
    std::vector<std::pair<int, int>> joined;
    for (const auto& run : runs)
    {
        if (!joined.empty() && run.first <= joined.back().second + 1)
            joined.back().second = std::max(joined.back().second, run.second);
        else
            joined.push_back(run);
    }
    return joined;
}

int bakeAll(Engine& engine, Layer& layer, double frameRate, std::vector<std::string>* errors)
{
    int baked = 0;
    for (const auto& [first, last] : runsWithBehaviors(layer))
        baked += bake(engine, layer, first, last, frameRate, errors);
    return baked;
}

} // namespace fla::behaviors
