#include "function_curves.h"

#include "camera.h"
#include "frame_bake.h"
#include "property_command.h"
#include "transform_parts.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace fla {

namespace curves {

namespace {

const Frame* spanAt(const Layer& layer, int frame, const Frame** next)
{
    *next = nullptr;
    for (size_t i = 0; i < layer.frames.size(); ++i)
    {
        const Frame* span = layer.frames[i];
        if (span && frame >= span->index && frame < span->index + std::max(span->duration, 1))
        {
            if (i + 1 < layer.frames.size())
                *next = layer.frames[i + 1];
            return span;
        }
    }
    return nullptr;
}

const SymbolInstance* firstInstance(const std::vector<Element*>& elements)
{
    for (const Element* element : elements)
        if (const auto* instance = dynamic_cast<const SymbolInstance*>(element))
            return instance;
    return nullptr;
}

bool cameraValue(const camera::State& state, const std::string& key, double& value)
{
    if (key == "x")
        value = state.x;
    else if (key == "y")
        value = state.y;
    else if (key == "zoom")
        value = state.zoom;
    else if (key == "rotation")
        value = state.rotation;
    else
        return false;
    return true;
}

/// What an instance was and becomes.
class SetInstanceCommand : public Command
{
public:
    SetInstanceCommand(SymbolInstance* instance, const std::string& key, double value)
        : _instance(instance)
        , _transform(instance->transform)
        , _color(instance->colorTransform)
        , _firstFrame(instance->firstFrame)
        , _key(key)
        , _value(value)
    {
    }
    void redo() override { set(*_instance, _key, _value); }
    void undo() override
    {
        _instance->transform = _transform;
        _instance->colorTransform = _color;
        _instance->firstFrame = _firstFrame;
    }
    std::string name() const override { return "Change Curve"; }

private:
    SymbolInstance* _instance;
    Transform _transform;
    ColorTransform _color;
    int _firstFrame;
    std::string _key;
    double _value;
};

} // namespace

std::vector<Property> propertiesOf(const Layer& layer)
{
    if (camera::isCamera(layer))
        return {{"x", "Camera X"}, {"y", "Camera Y"}, {"zoom", "Zoom"}, {"rotation", "Camera Rotation"}};
    return {{"x", "X"}, {"y", "Y"}, {"rotation", "Rotation"}, {"scaleX", "Scale X"}, {"scaleY", "Scale Y"},
        {"skew", "Skew"}, {"alpha", "Alpha"}, {"brightness", "Brightness"}, {"tint", "Tint"}, {"frame", "Frame (graphic)"}};
}

double get(const SymbolInstance& instance, const std::string& key)
{
    const Transform& t = instance.transform;
    const TransformParts parts = TransformParts::of(t);
    if (key == "x")
        return t.tx;
    if (key == "y")
        return t.ty;
    if (key == "rotation")
        return parts.skewY;
    if (key == "scaleX")
        return parts.scaleX;
    if (key == "scaleY")
        return parts.scaleY;
    if (key == "skew")
        return parts.skewX - parts.skewY;
    if (key == "alpha")
        return instance.colorTransform.alphaMultiplier * 100.0;
    if (key == "brightness")
        return instance.colorTransform.brightness * 100.0;
    if (key == "tint")
        return instance.colorTransform.tintMultiplier * 100.0;
    if (key == "frame")
        return instance.firstFrame + 1;
    return 0.0;
}

void set(SymbolInstance& instance, const std::string& key, double value)
{
    Transform& t = instance.transform;
    if (key == "x")
        t.tx = value;
    else if (key == "y")
        t.ty = value;
    else if (key == "rotation" || key == "scaleX" || key == "scaleY" || key == "skew")
    {
        TransformParts parts = TransformParts::of(t);
        if (key == "rotation")
        {
            const double turn = value - parts.skewY;
            parts.skewY += turn;
            parts.skewX += turn;
        }
        else if (key == "scaleX")
            parts.scaleX = value;
        else if (key == "scaleY")
            parts.scaleY = value;
        else
            parts.skewX = parts.skewY + value;
        t = parts.matrix(t.tx, t.ty);
    }
    else if (key == "alpha")
        instance.colorTransform.alphaMultiplier = std::clamp(value / 100.0, 0.0, 1.0);
    else if (key == "brightness")
        instance.colorTransform.brightness = std::clamp(value / 100.0, -1.0, 1.0);
    else if (key == "tint")
        instance.colorTransform.tintMultiplier = std::clamp(value / 100.0, 0.0, 1.0);
    else if (key == "frame")
        instance.firstFrame = std::max(0, static_cast<int>(std::lround(value)) - 1);
}

bool valueAt(const Layer& layer, int frame, const std::string& key, const Context& context, double& value)
{
    if (camera::isCamera(layer))
    {
        if (!context.timeline)
            return false;
        return cameraValue(camera::stateAt(*context.timeline, frame, context.width, context.height), key, value);
    }
    const Frame* next = nullptr;
    const Frame* span = spanAt(layer, frame, &next);
    if (!span)
        return false;
    if (frame == span->index || (span->tweenType == TweenType::None && span->otherTweenType.empty()))
    {
        const SymbolInstance* instance = firstInstance(span->elements);
        if (!instance)
            return false;
        value = get(*instance, key);
        return true;
    }
    // Between keyframes, what the tween draws there.
    std::vector<Element*> shown = frames::elementsAt(*span, next, frame - span->index, context.frameRate, nullptr);
    const SymbolInstance* instance = firstInstance(shown);
    const bool found = instance != nullptr;
    if (found)
        value = get(*instance, key);
    for (Element* element : shown)
        delete element;
    return found;
}

std::vector<Key> keys(const Layer& layer, const std::string& key, const Context& context)
{
    std::vector<Key> found;
    const bool isCamera = camera::isCamera(layer);
    for (const Frame* frame : layer.frames)
    {
        if (!frame)
            continue;
        double value = 0.0;
        if (isCamera)
        {
            if (!cameraValue(camera::stateOf(*frame, context.width, context.height), key, value))
                continue;
        }
        else
        {
            const SymbolInstance* instance = firstInstance(frame->elements);
            if (!instance)
                continue;
            value = get(*instance, key);
        }
        found.push_back({frame->index, value});
    }
    return found;
}

CommandPtr setKey(Layer& layer, int frameIndex, const std::string& key, double value, const Context& context)
{
    Frame* frame = nullptr;
    for (Frame* candidate : layer.frames)
        if (candidate && candidate->index == frameIndex)
            frame = candidate;
    if (!frame)
        return nullptr;
    if (camera::isCamera(layer))
    {
        camera::State state = camera::stateOf(*frame, context.width, context.height);
        if (key == "x")
            state.x = value;
        else if (key == "y")
            state.y = value;
        else if (key == "zoom")
            state.zoom = std::max(1.0, value);
        else if (key == "rotation")
            state.rotation = value;
        auto after = frame->extraAttributes;
        camera::set(after, state);
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        return CommandPtr(new SetPropertyCommand<Attributes>(frame->extraAttributes, frame->extraAttributes, after, "Change Curve"));
    }
    for (Element* element : frame->elements)
        if (auto* instance = dynamic_cast<SymbolInstance*>(element))
            return CommandPtr(new SetInstanceCommand(instance, key, value));
    return nullptr;
}

} // namespace curves

} // namespace fla
