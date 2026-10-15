#include "motion_blur.h"

#include "../data/document.h"
#include "../data/layer.h"

#include <algorithm>
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
