#include "controllers.h"

#include "frame_bake.h"
#include "timeline_edit.h"
#include "transform_parts.h"

#include "../data/document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace fla {

namespace controllers {

namespace {

const char* const kKey = "phoenixControllers";

/// Fields are parted by '|', bindings and controllers by ';' and '~';
/// those and '%' are escaped as %XX.
std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '|' || c == ';' || c == '~' || c == '%')
        {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            out += buffer;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string unescaped(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, separator))
        parts.push_back(part);
    return parts;
}

std::string number(double value)
{
    std::ostringstream out;
    out.precision(10);
    out << value;
    return out.str();
}

Layer* layerNamed(Timeline& timeline, const std::string& name)
{
    for (Layer* layer : timeline.layers)
        if (layer && layer->name == name)
            return layer;
    return nullptr;
}

/// The keyframe of \a layer at \a frame, or null.
Frame* keyAt(Layer& layer, int frame)
{
    for (Frame* key : layer.frames)
        if (key && key->index == frame)
            return key;
    return nullptr;
}

/// \a instance with \a property set to \a value.
void set(SymbolInstance& instance, const std::string& property, double value)
{
    Transform& t = instance.transform;
    if (property == "x")
        t.tx = value;
    else if (property == "y")
        t.ty = value;
    else if (property == "rotation" || property == "scale" || property == "scaleX" || property == "scaleY")
    {
        TransformParts parts = TransformParts::of(t);
        if (property == "rotation")
        {
            const double turn = value - parts.skewY;
            parts.skewY += turn;
            parts.skewX += turn;
        }
        else if (property == "scale")
        {
            const double sign = parts.scaleY < 0.0 ? -1.0 : 1.0;
            parts.scaleX = value;
            parts.scaleY = sign * value;
        }
        else if (property == "scaleX")
            parts.scaleX = value;
        else
            parts.scaleY = value;
        t = parts.matrix(t.tx, t.ty);
    }
    else if (property == "alpha")
        instance.colorTransform.alphaMultiplier = std::clamp(value / 100.0, 0.0, 1.0);
    else if (property == "frame")
        instance.firstFrame = std::max(0, static_cast<int>(std::lround(value)) - 1);
}

double get(const SymbolInstance& instance, const std::string& property)
{
    const Transform& t = instance.transform;
    const TransformParts parts = TransformParts::of(t);
    if (property == "x")
        return t.tx;
    if (property == "y")
        return t.ty;
    if (property == "rotation")
        return parts.skewY;
    if (property == "scale" || property == "scaleX")
        return parts.scaleX;
    if (property == "scaleY")
        return parts.scaleY;
    if (property == "alpha")
        return instance.colorTransform.alphaMultiplier * 100.0;
    if (property == "frame")
        return instance.firstFrame + 1;
    return 0.0;
}

/// What an instance was and becomes.
struct Change
{
    SymbolInstance* instance;
    Transform transform[2];
    double alpha[2];
    int firstFrame[2];
};

/// Inserts the keyframes a move needs, then sets the instances; undo puts
/// the instances back and the keyframes go.
class ApplyCommand : public Command
{
public:
    ApplyCommand(std::vector<std::pair<Layer*, int>> keys, std::vector<std::pair<Binding, double>> targets,
        Document* document, std::string name)
        : _keys(std::move(keys))
        , _targets(std::move(targets))
        , _document(document)
        , _name(std::move(name))
        , _frameRate(document && document->frameRate > 0.0 ? document->frameRate : 24.0)
    {
        // The keyframes go in as a frame edit, which keeps everything it
        // changed (a blank frame before, a span stretched or cut, a motion
        // trimmed) and puts it all back on undo.
        std::vector<Layer*> layers;
        for (const auto& [layer, index] : _keys)
            if (layer && std::find(layers.begin(), layers.end(), layer) == layers.end())
                layers.push_back(layer);
        const double frameRate = _frameRate;
        const std::vector<std::pair<Layer*, int>> wanted = _keys;
        _frames = std::make_unique<EditFramesCommand>(layers, _name, [wanted, frameRate](std::vector<Frame*>&) {
            for (const auto& [layer, index] : wanted)
            {
                if (!layer || keyAt(*layer, index))
                    continue;
                // Cut into a classic or shape tween, the keyframe takes
                // the in-between there and both halves go on tweening.
                Frame* span = timeline::spanAt(*layer, index);
                Frame* next = nullptr;
                for (Frame* frame : layer->frames)
                    if (frame && span && frame->index == span->index + span->duration)
                        next = frame;
                const TweenType tween = span ? span->tweenType : TweenType::None;
                std::vector<Element*> between;
                if (span && next && (tween == TweenType::Motion || tween == TweenType::Shape))
                    between = frames::elementsAt(*span, next, index - span->index, frameRate, nullptr);
                timeline::insertKeyframe(*layer, index, false);
                Frame* key = keyAt(*layer, index);
                if (!key)
                {
                    for (Element* element : between)
                        delete element;
                    continue;
                }
                if (!between.empty())
                {
                    for (Element* element : key->elements)
                        delete element;
                    key->elements = between;
                    for (Element* element : key->elements)
                        element->parent = key;
                    key->tweenType = tween;
                }
            }
        });
    }

    void redo() override
    {
        // The keyframes first; the first time, the values are then set on
        // the instances on them and what they were is kept.
        _frames->redo();
        if (!_made)
        {
            _made = true;
            _changes.clear();
            for (const auto& [binding, value] : _targets)
            {
                Timeline* timeline = timelineOf(*_document, binding.timeline);
                Layer* layer = timeline ? layerNamed(*timeline, binding.layer) : nullptr;
                Frame* key = nullptr;
                for (const auto& [owner, index] : _keys)
                    if (owner == layer)
                        key = keyAt(*layer, index);
                if (!key)
                    continue;
                for (Element* element : key->elements)
                {
                    auto* instance = dynamic_cast<SymbolInstance*>(element);
                    if (!instance)
                        continue;
                    Change change{instance, {instance->transform, instance->transform},
                        {instance->colorTransform.alphaMultiplier, instance->colorTransform.alphaMultiplier},
                        {instance->firstFrame, instance->firstFrame}};
                    // Several bindings may touch one instance; the first
                    // record keeps what it was.
                    auto existing = std::find_if(_changes.begin(), _changes.end(),
                        [instance](const Change& c) { return c.instance == instance; });
                    set(*instance, binding.property, value);
                    if (existing == _changes.end())
                    {
                        change.transform[1] = instance->transform;
                        change.alpha[1] = instance->colorTransform.alphaMultiplier;
                        change.firstFrame[1] = instance->firstFrame;
                        _changes.push_back(change);
                    }
                    else
                    {
                        existing->transform[1] = instance->transform;
                        existing->alpha[1] = instance->colorTransform.alphaMultiplier;
                        existing->firstFrame[1] = instance->firstFrame;
                    }
                }
            }
            return;
        }
        for (Change& change : _changes)
        {
            change.instance->transform = change.transform[1];
            change.instance->colorTransform.alphaMultiplier = change.alpha[1];
            change.instance->firstFrame = change.firstFrame[1];
        }
    }

    void undo() override
    {
        for (Change& change : _changes)
        {
            change.instance->transform = change.transform[0];
            change.instance->colorTransform.alphaMultiplier = change.alpha[0];
            change.instance->firstFrame = change.firstFrame[0];
        }
        _frames->undo();
    }

    std::string name() const override { return _name; }

private:
    std::vector<std::pair<Layer*, int>> _keys;
    std::vector<std::pair<Binding, double>> _targets;
    Document* _document;
    std::string _name;
    bool _made = false;
    std::unique_ptr<EditFramesCommand> _frames;
    std::vector<Change> _changes;
    double _frameRate = 24.0;
};

} // namespace

const std::vector<std::string>& properties()
{
    static const std::vector<std::string> list = {"x", "y", "rotation", "scale", "scaleX", "scaleY", "alpha", "frame"};
    return list;
}

std::vector<Controller> load(const Document& document)
{
    std::vector<Controller> controllers;
    std::string text;
    for (const auto& [key, value] : document.extraAttributes)
        if (key == kKey)
            text = value;
    for (const std::string& record : split(text, '~'))
    {
        const std::vector<std::string> parts = split(record, ';');
        if (parts.empty())
            continue;
        const std::vector<std::string> head = split(parts[0], '|');
        Controller controller;
        controller.name = unescaped(head.empty() ? std::string() : head[0]);
        controller.value = head.size() > 1 ? std::atof(head[1].c_str()) : 0.0;
        for (size_t i = 1; i < parts.size(); ++i)
        {
            const std::vector<std::string> fields = split(parts[i], '|');
            if (fields.size() < 5)
                continue;
            Binding binding;
            binding.timeline = unescaped(fields[0]);
            binding.layer = unescaped(fields[1]);
            binding.property = unescaped(fields[2]);
            binding.from = std::atof(fields[3].c_str());
            binding.to = std::atof(fields[4].c_str());
            controller.bindings.push_back(binding);
        }
        if (!controller.name.empty())
            controllers.push_back(controller);
    }
    return controllers;
}

std::vector<std::pair<std::string, std::string>> saved(const Document& document, const std::vector<Controller>& controllers)
{
    std::string text;
    for (const Controller& controller : controllers)
    {
        if (!text.empty())
            text += '~';
        text += escaped(controller.name) + "|" + number(controller.value);
        for (const Binding& binding : controller.bindings)
            text += ";" + escaped(binding.timeline) + "|" + escaped(binding.layer) + "|" + escaped(binding.property) +
                    "|" + number(binding.from) + "|" + number(binding.to);
    }
    std::vector<std::pair<std::string, std::string>> attributes = document.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) { return pair.first == kKey; }),
        attributes.end());
    if (!text.empty())
        attributes.emplace_back(kKey, text);
    return attributes;
}

Timeline* timelineOf(Document& document, const std::string& name)
{
    if (name.compare(0, 6, "scene:") == 0)
    {
        const int index = std::atoi(name.c_str() + 6);
        return index >= 0 && index < static_cast<int>(document.timelines.size())
            ? document.timelines[static_cast<size_t>(index)] : nullptr;
    }
    if (name.compare(0, 7, "symbol:") == 0 && document.symbolList)
    {
        const auto found = document.symbolList->symbolMap.find(name.substr(7));
        if (found != document.symbolList->symbolMap.end() && found->second && !found->second->timelines.empty())
            return found->second->timelines.front();
    }
    return nullptr;
}

std::string nameOf(const Document& document, const Timeline* timeline)
{
    for (size_t i = 0; i < document.timelines.size(); ++i)
        if (document.timelines[i] == timeline)
            return "scene:" + std::to_string(i);
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
            if (symbol && !symbol->timelines.empty() && symbol->timelines.front() == timeline)
                return "symbol:" + symbol->name;
    return std::string();
}

double valueAt(const Binding& binding, double value)
{
    return binding.from + (binding.to - binding.from) * std::clamp(value, 0.0, 1.0);
}

bool currentValue(const Layer& layer, int frame, const std::string& property, double& value)
{
    for (const Frame* key : layer.frames)
    {
        if (!key || frame < key->index || frame >= key->index + std::max(key->duration, 1))
            continue;
        for (const Element* element : key->elements)
            if (const auto* instance = dynamic_cast<const SymbolInstance*>(element))
            {
                value = get(*instance, property);
                return true;
            }
    }
    return false;
}

CommandPtr apply(Document& document, const Controller& controller, double value, int frame)
{
    std::vector<std::pair<Layer*, int>> keys;
    std::vector<std::pair<Binding, double>> targets;
    for (const Binding& binding : controller.bindings)
    {
        Timeline* timeline = timelineOf(document, binding.timeline);
        Layer* layer = timeline ? layerNamed(*timeline, binding.layer) : nullptr;
        if (!layer || layer->isLocked() || !timeline::spanAt(*layer, frame))
            continue;
        if (std::find_if(keys.begin(), keys.end(), [layer](const auto& key) { return key.first == layer; }) == keys.end())
            keys.emplace_back(layer, frame);
        targets.emplace_back(binding, valueAt(binding, value));
    }
    if (targets.empty())
        return nullptr;
    return CommandPtr(new ApplyCommand(std::move(keys), std::move(targets), &document, controller.name));
}

} // namespace controllers

} // namespace fla
