#include "camera.h"

#include "layer_commands.h"
#include "transform_blend.h"
#include "tween_ease.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace fla::camera {

namespace {

constexpr double kPi = 3.14159265358979323846;
const char* const kMark = "phoenixCamera";
const char* const kX = "phoenixCameraX";
const char* const kY = "phoenixCameraY";
const char* const kZoom = "phoenixCameraZoom";
const char* const kRotation = "phoenixCameraRotation";
const char* const kDepth = "phoenixDepth";
// The color effect, each part kept only when it is not at rest.
const char* const kBrightness = "phoenixCameraBrightness";
const char* const kTintAmount = "phoenixCameraTintMultiplier";
const char* const kTintColor = "phoenixCameraTintColor";
const char* const kMultipliers[4] = {"phoenixCameraRedMultiplier", "phoenixCameraGreenMultiplier",
    "phoenixCameraBlueMultiplier", "phoenixCameraAlphaMultiplier"};
const char* const kOffsets[4] = {"phoenixCameraRedOffset", "phoenixCameraGreenOffset", "phoenixCameraBlueOffset",
    "phoenixCameraAlphaOffset"};

const std::string* attribute(const std::vector<std::pair<std::string, std::string>>& attributes, const char* name)
{
    for (const auto& entry : attributes)
        if (entry.first == name)
            return &entry.second;
    return nullptr;
}

void put(std::vector<std::pair<std::string, std::string>>& attributes, const char* name, double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    for (auto& entry : attributes)
    {
        if (entry.first == name)
        {
            entry.second = buffer;
            return;
        }
    }
    attributes.emplace_back(name, buffer);
}

double number(const std::vector<std::pair<std::string, std::string>>& attributes, const char* name, double fallback)
{
    const std::string* text = attribute(attributes, name);
    return text ? std::atof(text->c_str()) : fallback;
}

void erase(std::vector<std::pair<std::string, std::string>>& attributes, const char* name)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [name](const std::pair<std::string, std::string>& a) { return a.first == name; }),
        attributes.end());
}

/// \a value under \a name, or no attribute when it is \a rest.
void putOr(std::vector<std::pair<std::string, std::string>>& attributes, const char* name, double value, double rest)
{
    if (value == rest)
        erase(attributes, name);
    else
        put(attributes, name, value);
}

ColorTransform colorOf(const std::vector<std::pair<std::string, std::string>>& attributes)
{
    ColorTransform color;
    color.brightness = std::clamp(number(attributes, kBrightness, 0.0), -1.0, 1.0);
    color.tintMultiplier = std::clamp(number(attributes, kTintAmount, 0.0), 0.0, 1.0);
    if (const std::string* tint = attribute(attributes, kTintColor))
    {
        // #RRGGBB.
        const unsigned long rgb = std::strtoul(tint->c_str() + (!tint->empty() && (*tint)[0] == '#' ? 1 : 0), nullptr, 16);
        color.tintColor[0] = static_cast<uint8_t>((rgb >> 16) & 0xff);
        color.tintColor[1] = static_cast<uint8_t>((rgb >> 8) & 0xff);
        color.tintColor[2] = static_cast<uint8_t>(rgb & 0xff);
    }
    double* multipliers[4] = {&color.redMultiplier, &color.greenMultiplier, &color.blueMultiplier,
        &color.alphaMultiplier};
    int* offsets[4] = {&color.redOffset, &color.greenOffset, &color.blueOffset, &color.alphaOffset};
    for (int i = 0; i < 4; ++i)
    {
        *multipliers[i] = number(attributes, kMultipliers[i], 1.0);
        *offsets[i] = static_cast<int>(std::lround(number(attributes, kOffsets[i], 0.0)));
    }
    return color;
}

void setColor(std::vector<std::pair<std::string, std::string>>& attributes, const ColorTransform& color)
{
    putOr(attributes, kBrightness, color.brightness, 0.0);
    putOr(attributes, kTintAmount, color.tintMultiplier, 0.0);
    if (color.tintMultiplier == 0.0)
    {
        erase(attributes, kTintColor);
    }
    else
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.tintColor[0], color.tintColor[1],
            color.tintColor[2]);
        bool found = false;
        for (auto& entry : attributes)
        {
            if (entry.first == kTintColor)
            {
                entry.second = buffer;
                found = true;
            }
        }
        if (!found)
            attributes.emplace_back(kTintColor, buffer);
    }
    const double multipliers[4] = {color.redMultiplier, color.greenMultiplier, color.blueMultiplier,
        color.alphaMultiplier};
    const int offsets[4] = {color.redOffset, color.greenOffset, color.blueOffset, color.alphaOffset};
    for (int i = 0; i < 4; ++i)
    {
        putOr(attributes, kMultipliers[i], multipliers[i], 1.0);
        putOr(attributes, kOffsets[i], offsets[i], 0.0);
    }
}

} // namespace

bool isCamera(const Layer& layer)
{
    const std::string* mark = attribute(layer.extraAttributes, kMark);
    return mark && *mark == "true";
}

Layer* cameraOf(const Timeline& timeline)
{
    for (Layer* layer : timeline.layers)
        if (layer && isCamera(*layer))
            return layer;
    return nullptr;
}

State stateOf(const Frame& frame, double width, double height)
{
    State state;
    state.x = number(frame.extraAttributes, kX, width / 2.0);
    state.y = number(frame.extraAttributes, kY, height / 2.0);
    state.zoom = std::max(1.0, number(frame.extraAttributes, kZoom, 100.0));
    state.rotation = number(frame.extraAttributes, kRotation, 0.0);
    state.color = colorOf(frame.extraAttributes);
    return state;
}

void set(std::vector<std::pair<std::string, std::string>>& attributes, const State& state)
{
    put(attributes, kX, state.x);
    put(attributes, kY, state.y);
    put(attributes, kZoom, state.zoom);
    put(attributes, kRotation, state.rotation);
    setColor(attributes, state.color);
}

State stateAt(const Timeline& timeline, int frame, double width, double height)
{
    State rest;
    rest.x = width / 2.0;
    rest.y = height / 2.0;
    const Layer* layer = cameraOf(timeline);
    if (!layer || layer->frames.empty())
        return rest;
    std::vector<const Frame*> keys(layer->frames.begin(), layer->frames.end());
    keys.erase(std::remove(keys.begin(), keys.end(), nullptr), keys.end());
    std::sort(keys.begin(), keys.end(), [](const Frame* a, const Frame* b) { return a->index < b->index; });
    const Frame* current = nullptr;
    const Frame* next = nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (keys[i]->index <= frame)
        {
            current = keys[i];
            next = i + 1 < keys.size() ? keys[i + 1] : nullptr;
        }
    }
    if (!current)
        return stateOf(*keys.front(), width, height);
    State state = stateOf(*current, width, height);
    // A tweened camera keyframe eases toward the next, as a classic tween.
    if (next && current->tweenType == TweenType::Motion && next->index > current->index)
    {
        const double t = easeProgress(static_cast<double>(frame - current->index) / (next->index - current->index),
            current->acceleration, current->hasCustomEase ? current->customEase : std::vector<Point>(),
            current->easeMethod);
        const State to = stateOf(*next, width, height);
        state.x += (to.x - state.x) * t;
        state.y += (to.y - state.y) * t;
        // Zoom eases evenly as a ratio, so moving in looks steady.
        state.zoom = state.zoom * std::pow(to.zoom / state.zoom, t);
        state.rotation += (to.rotation - state.rotation) * t;
        state.color = blendColorTransforms(state.color, to.color, t);
    }
    return state;
}

double depthOf(const Layer& layer)
{
    return number(layer.extraAttributes, kDepth, 0.0);
}

void setDepth(std::vector<std::pair<std::string, std::string>>& attributes, double depth)
{
    putOr(attributes, kDepth, depth, 0.0);
}

bool view(const State& state, double depth, double width, double height, Transform& out)
{
    // Zoom is the camera moving in: at 200% it has come halfway to the
    // stage's plane. A layer d deep is then focal + d - moved away.
    const double moved = kFocal * (1.0 - 100.0 / std::max(1.0, state.zoom));
    const double distance = kFocal + depth - moved;
    if (distance <= 1.0)
        return false;
    // Size against the size at rest, and the pan as seen at that depth.
    const double scale = (kFocal + depth) / distance;
    const double pan = kFocal / distance;
    const double cx = width / 2.0;
    const double cy = height / 2.0;
    const double r = -state.rotation * kPi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    // screen = C + R * (scale * (q - C) - pan * (camera - C)).
    const double ox = -scale * cx - pan * (state.x - cx);
    const double oy = -scale * cy - pan * (state.y - cy);
    out = Transform(c * scale, s * scale, -s * scale, c * scale, cx + c * ox - s * oy, cy + s * ox + c * oy);
    return true;
}

Layer* newCameraLayer(const Timeline& timeline, int frames, double width, double height)
{
    Layer* layer = newLayerFor(timeline, false);
    layer->name = "Camera";
    layer->autoNamed = false;
    // A guide: other apps that open the file keep it and never draw it.
    layer->layerType = Layer::Type::Guide;
    layer->extraAttributes.emplace_back(kMark, "true");
    for (Frame* frame : layer->frames)
    {
        frame->duration = std::max(1, frames);
        State rest;
        rest.x = width / 2.0;
        rest.y = height / 2.0;
        set(frame->extraAttributes, rest);
    }
    layer->lastFrame = std::max(1, frames) - 1;
    return layer;
}

} // namespace fla::camera
