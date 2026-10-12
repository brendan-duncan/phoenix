#include "layer_effects.h"

#include "timeline_edit.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fla {

namespace effects {

namespace {

const char* const kKey = "phoenixEffect";

std::string attribute(const Layer& layer)
{
    for (const auto& [key, value] : layer.extraAttributes)
        if (key == kKey)
            return value;
    return std::string();
}

} // namespace

const char* kindName(Kind kind)
{
    switch (kind)
    {
    case Kind::Tone: return "Tone";
    case Kind::Highlight: return "Highlight";
    case Kind::Glow: return "Glow";
    }
    return "Tone";
}

bool isEffect(const Layer& layer)
{
    return !attribute(layer).empty();
}

Effect effectOf(const Layer& layer)
{
    // kind;RRGGBBAA;blur;opacity;clip
    Effect effect;
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(attribute(layer));
    while (std::getline(in, part, ';'))
        parts.push_back(part);
    if (parts.empty())
        return effect;
    effect = defaults(parts[0] == "highlight" ? Kind::Highlight : parts[0] == "glow" ? Kind::Glow : Kind::Tone);
    if (parts.size() > 1 && parts[1].size() >= 6)
        for (int i = 0; i < 4; ++i)
            effect.color[i] = static_cast<uint8_t>(parts[1].size() >= static_cast<size_t>(2 * i + 2)
                    ? std::strtol(parts[1].substr(static_cast<size_t>(2 * i), 2).c_str(), nullptr, 16) : 255);
    if (parts.size() > 2)
        effect.blur = std::clamp(std::atof(parts[2].c_str()), 0.0, 200.0);
    if (parts.size() > 3)
        effect.opacity = std::clamp(std::atof(parts[3].c_str()), 0.0, 1.0);
    if (parts.size() > 4)
        effect.clip = parts[4] != "0";
    return effect;
}

std::vector<std::pair<std::string, std::string>> withEffect(const Layer& layer, const Effect& effect)
{
    char color[12];
    std::snprintf(color, sizeof(color), "%02X%02X%02X%02X", effect.color[0], effect.color[1], effect.color[2], effect.color[3]);
    std::ostringstream value;
    value << (effect.kind == Kind::Highlight ? "highlight" : effect.kind == Kind::Glow ? "glow" : "tone") << ";" << color
          << ";" << effect.blur << ";" << effect.opacity << ";" << (effect.clip ? 1 : 0);
    std::vector<std::pair<std::string, std::string>> attributes = layer.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) { return pair.first == kKey; }),
        attributes.end());
    attributes.emplace_back(kKey, value.str());
    return attributes;
}

Effect defaults(Kind kind)
{
    Effect effect;
    effect.kind = kind;
    switch (kind)
    {
    case Kind::Tone:
        effect.color[0] = 60;
        effect.color[1] = 40;
        effect.color[2] = 110;
        effect.opacity = 0.45;
        effect.blur = 6.0;
        effect.clip = true;
        break;
    case Kind::Highlight:
        effect.color[0] = 255;
        effect.color[1] = 250;
        effect.color[2] = 220;
        effect.opacity = 0.55;
        effect.blur = 6.0;
        effect.clip = true;
        break;
    case Kind::Glow:
        effect.color[0] = 255;
        effect.color[1] = 200;
        effect.color[2] = 90;
        effect.opacity = 0.8;
        effect.blur = 16.0;
        effect.clip = false;
        break;
    }
    effect.color[3] = 255;
    return effect;
}

Layer* newEffectLayer(const Timeline& timeline, const Layer& target, const Effect& effect)
{
    Layer* layer = new Layer(nullptr);
    layer->name = target.name + " " + kindName(effect.kind);
    layer->layerType = Layer::Type::Guide;
    layer->color[0] = effect.color[0];
    layer->color[1] = effect.color[1];
    layer->color[2] = effect.color[2];
    layer->color[3] = 255;
    layer->extraAttributes = withEffect(*layer, effect);
    int length = 1;
    for (const Layer* other : timeline.layers)
        if (other)
            length = std::max(length, timeline::layerEnd(*other));
    Frame* frame = new Frame(layer);
    frame->duration = length;
    layer->frames.push_back(frame);
    return layer;
}

} // namespace effects

} // namespace fla
