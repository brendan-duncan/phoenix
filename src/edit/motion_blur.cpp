#include "motion_blur.h"

#include "../data/document.h"
#include "../data/layer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fla::motionblur {

const char* const kLayer = "phoenixMotionBlur";
const char* const kShutter = "phoenixShutterAngle";
const char* const kSamples = "phoenixMotionBlurSamples";

namespace {

const std::string* attribute(const DOMElement& element, const char* name)
{
    for (const auto& each : element.extraAttributes)
        if (each.first == name)
            return &each.second;
    return nullptr;
}

using Attributes = std::vector<std::pair<std::string, std::string>>;

Attributes with(const Attributes& attributes, const char* name, const std::string* value)
{
    Attributes out;
    for (const auto& each : attributes)
        if (each.first != name)
            out.push_back(each);
    if (value)
        out.emplace_back(name, *value);
    return out;
}

} // namespace

bool on(const Layer& layer)
{
    const std::string* value = attribute(layer, kLayer);
    return value && *value == "true";
}

std::vector<std::pair<std::string, std::string>> withOn(const Layer& layer, bool on)
{
    const std::string yes = "true";
    return with(layer.extraAttributes, kLayer, on ? &yes : nullptr);
}

double shutter(const Document& document)
{
    const std::string* value = attribute(document, kShutter);
    return value ? std::clamp(std::atof(value->c_str()), 0.0, 720.0) : 180.0;
}

int samples(const Document& document)
{
    const std::string* value = attribute(document, kSamples);
    return value ? std::clamp(std::atoi(value->c_str()), 2, 64) : 16;
}

std::vector<std::pair<std::string, std::string>> withSettings(const Document& document, double shutter, int samples)
{
    // Left out at their defaults, so a document that never set them says
    // nothing.
    const std::string angle = std::to_string(static_cast<int>(std::clamp(shutter, 0.0, 720.0) + 0.5));
    const std::string count = std::to_string(std::clamp(samples, 2, 64));
    const Attributes first = with(document.extraAttributes, kShutter, shutter == 180.0 ? nullptr : &angle);
    return with(first, kSamples, samples == 16 ? nullptr : &count);
}

LayerTime layerTimeOf(const Layer& layer)
{
    LayerTime time;
    if (const std::string* echo = attribute(layer, "phoenixEcho"))
    {
        int count = 0, interval = 2;
        double decay = 0.6;
        if (std::sscanf(echo->c_str(), "%d,%d,%lf", &count, &interval, &decay) >= 1)
        {
            time.echoes = std::clamp(count, 0, 32);
            time.interval = std::clamp(interval, 1, 240);
            time.decay = std::clamp(decay, 0.0, 1.0);
        }
    }
    if (const std::string* hold = attribute(layer, "phoenixPosterize"))
        time.posterize = std::clamp(std::atoi(hold->c_str()), 1, 240);
    return time;
}

std::vector<std::pair<std::string, std::string>> withLayerTime(const Layer& layer, const LayerTime& time)
{
    Attributes out = with(layer.extraAttributes, "phoenixEcho", nullptr);
    out = with(out, "phoenixPosterize", nullptr);
    if (time.echoes > 0)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%d,%d,%g", time.echoes, time.interval, time.decay);
        const std::string value = text;
        out = with(out, "phoenixEcho", &value);
    }
    if (time.posterize > 1)
    {
        const std::string value = std::to_string(time.posterize);
        out = with(out, "phoenixPosterize", &value);
    }
    return out;
}

int posterized(const LayerTime& time, int frame)
{
    if (time.posterize <= 1 || frame < 0)
        return frame;
    return frame - frame % time.posterize;
}

std::vector<double> sampleTimes(double shutter, int samples)
{
    std::vector<double> times;
    const int count = std::clamp(samples, 1, 64);
    const double open = std::clamp(shutter, 0.0, 720.0) / 360.0;
    for (int i = 0; i < count; ++i)
        times.push_back(open * ((i + 0.5) / count - 0.5));
    return times;
}

} // namespace fla::motionblur
