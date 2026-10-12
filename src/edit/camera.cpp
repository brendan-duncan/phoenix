#include "camera.h"

#include "layer_commands.h"
#include "tween_ease.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla::camera {

namespace {

constexpr double kPi = 3.14159265358979323846;
const char* const kMark = "phoenixCamera";
const char* const kX = "phoenixCameraX";
const char* const kY = "phoenixCameraY";
const char* const kZoom = "phoenixCameraZoom";
const char* const kRotation = "phoenixCameraRotation";
const char* const kDepth = "phoenixDepth";

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
    return state;
}

void set(std::vector<std::pair<std::string, std::string>>& attributes, const State& state)
{
    put(attributes, kX, state.x);
    put(attributes, kY, state.y);
    put(attributes, kZoom, state.zoom);
    put(attributes, kRotation, state.rotation);
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
    }
    return state;
}

double depthOf(const Layer& layer)
{
    return number(layer.extraAttributes, kDepth, 0.0);
}

void setDepth(std::vector<std::pair<std::string, std::string>>& attributes, double depth)
{
    if (depth == 0.0)
    {
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                             [](const std::pair<std::string, std::string>& a) { return a.first == kDepth; }),
            attributes.end());
        return;
    }
    put(attributes, kDepth, depth);
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
    // A guide: Animate keeps it and never draws it.
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
