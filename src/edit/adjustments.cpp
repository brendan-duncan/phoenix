#include "adjustments.h"

#include "image_blur.h"

#include "../data/layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla::compositing {

const char* const kMatte = "phoenixMatte";
const char* const kAdjustmentLayer = "phoenixAdjustmentLayer";
const char* const kAdjustments = "phoenixAdjustments";

namespace {

using behaviors::Applied;
using behaviors::Definition;
using behaviors::ParamSpec;

ParamSpec number(const char* name, const char* label, double value, double min, double max, double step)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.value = value;
    spec.min = min;
    spec.max = max;
    spec.step = step;
    char text[32];
    std::snprintf(text, sizeof(text), "%.10g", value);
    spec.text = text;
    return spec;
}

ParamSpec textParam(const char* name, const char* label, const char* value)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "text";
    spec.text = value;
    return spec;
}

double param(const Applied& applied, const char* name, int frame, double fallback)
{
    const std::string text = applied.valueAt(name, frame, std::string());
    if (text.empty())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : value;
}

std::string attribute(const Layer& layer, const char* name)
{
    for (const auto& each : layer.extraAttributes)
        if (each.first == name)
            return each.second;
    return std::string();
}

std::vector<std::pair<std::string, std::string>> without(const Layer& layer, const char* name)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& each : layer.extraAttributes)
        if (each.first != name)
            out.push_back(each);
    return out;
}

double clamp255(double v)
{
    return std::max(0.0, std::min(255.0, v));
}

constexpr double kPi = 3.14159265358979323846;

double hash3(int seed, int x, int y, int z)
{
    uint32_t h = static_cast<uint32_t>(seed) * 374761393u + static_cast<uint32_t>(x) * 668265263u +
        static_cast<uint32_t>(y) * 2246822519u + static_cast<uint32_t>(z) * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / static_cast<double>(0xFFFFFF) * 2.0 - 1.0;
}

/// Smooth value noise from -1 to 1 over x, y and time t.
double noise3(int seed, double x, double y, double t)
{
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y)), t0 = static_cast<int>(std::floor(t));
    const auto fade = [](double v) { return v * v * (3.0 - 2.0 * v); };
    const double fx = fade(x - x0), fy = fade(y - y0), ft = fade(t - t0);
    double result = 0.0;
    for (int dt = 0; dt < 2; ++dt)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                result += hash3(seed, x0 + dx, y0 + dy, t0 + dt) * (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dt ? ft : 1 - ft);
    return result;
}

/// The premultiplied pixel at a place between pixels, its four
/// neighbors mixed; transparent outside the image.
uint32_t sample(const std::vector<uint32_t>& pixels, int width, int height, double x, double y)
{
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    double sum[4] = {0, 0, 0, 0};
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
        {
            const int px = x0 + dx, py = y0 + dy;
            if (px < 0 || py < 0 || px >= width || py >= height)
                continue;
            const double w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            const uint32_t p = pixels[static_cast<size_t>(py) * width + px];
            for (int c = 0; c < 4; ++c)
                sum[c] += ((p >> (c * 8)) & 0xFF) * w;
        }
    uint32_t out = 0;
    for (int c = 0; c < 4; ++c)
        out |= static_cast<uint32_t>(std::lround(clamp255(sum[c]))) << (c * 8);
    return out;
}

} // namespace

Matte matteOf(const Layer& layer)
{
    const std::string value = attribute(layer, kMatte);
    if (value == "alpha")
        return Matte::Alpha;
    if (value == "alphaInverted")
        return Matte::AlphaInverted;
    if (value == "luma")
        return Matte::Luma;
    if (value == "lumaInverted")
        return Matte::LumaInverted;
    return Matte::None;
}

const char* matteName(Matte matte)
{
    switch (matte)
    {
    case Matte::Alpha:
        return "alpha";
    case Matte::AlphaInverted:
        return "alphaInverted";
    case Matte::Luma:
        return "luma";
    case Matte::LumaInverted:
        return "lumaInverted";
    default:
        return "";
    }
}

std::vector<std::pair<std::string, std::string>> withMatte(const Layer& layer, Matte matte)
{
    std::vector<std::pair<std::string, std::string>> out = without(layer, kMatte);
    if (matte != Matte::None)
        out.emplace_back(kMatte, matteName(matte));
    return out;
}

bool isAdjustment(const Layer& layer)
{
    return attribute(layer, kAdjustmentLayer) == "true";
}

std::vector<Applied> effectsOf(const Layer& layer)
{
    return behaviors::listOf(layer, kAdjustments);
}

std::vector<std::pair<std::string, std::string>> withAdjustment(const Layer& layer, bool on)
{
    std::vector<std::pair<std::string, std::string>> out = without(layer, kAdjustmentLayer);
    if (on)
    {
        out.emplace_back(kAdjustmentLayer, "true");
        // A new adjustment layer starts with a blur to change.
        bool hasEffects = false;
        for (const auto& each : out)
            hasEffects = hasEffects || each.first == kAdjustments;
        if (!hasEffects)
            out.emplace_back(kAdjustments, behaviors::encode({defaults("blur", 1)}));
    }
    return out;
}

const std::vector<Definition>& definitions()
{
    static const std::vector<Definition> list = [] {
        std::vector<Definition> made;
        Definition blur;
        blur.id = "blur";
        blur.name = "Blur";
        blur.description = "Blurs everything below the layer.";
        blur.builtIn = true;
        blur.params = {number("radius", "Radius (pixels)", 6, 0, 500, 0.5), number("amount", "Amount (percent)", 100, 0, 100, 1)};
        made.push_back(blur);
        Definition color;
        color.id = "color";
        color.name = "Color Correction";
        color.description = "Changes the brightness, contrast, saturation and hue of everything below the layer.";
        color.builtIn = true;
        color.params = {number("brightness", "Brightness", 0, -100, 100, 1), number("contrast", "Contrast", 0, -100, 100, 1),
            number("saturation", "Saturation", 0, -100, 100, 1), number("hue", "Hue (degrees)", 0, -180, 180, 1),
            number("amount", "Amount (percent)", 100, 0, 100, 1)};
        made.push_back(color);
        Definition tint;
        tint.id = "tint";
        tint.name = "Tint";
        tint.description = "Tints everything below the layer toward a color, keeping its light and dark: night, sunset, "
                           "flashback.";
        tint.builtIn = true;
        tint.params = {textParam("color", "Color (#RRGGBB)", "#3050A0"), number("amount", "Amount (percent)", 50, 0, 100, 1)};
        made.push_back(tint);
        Definition turbulent;
        turbulent.id = "turbulent";
        turbulent.name = "Turbulent Displace";
        turbulent.description = "Pushes everything below the layer about in smooth, random swirls that change over time: "
                                "heat haze, water, a dream, a wobbling picture.";
        turbulent.builtIn = true;
        turbulent.params = {number("amount", "Amount (pixels)", 8, 0, 500, 0.5), number("size", "Size (pixels)", 60, 2, 2000, 1),
            number("speed", "Changes per second", 1, 0, 30, 0.1), number("seed", "Seed", 1, 0, 9999, 1)};
        made.push_back(turbulent);
        Definition wave;
        wave.id = "wave";
        wave.name = "Wave Displace";
        wave.description = "Pushes everything below the layer in waves that travel across it: a flag, a reflection, "
                           "a shimmer.";
        wave.builtIn = true;
        wave.params = {number("amount", "Height (pixels)", 6, 0, 500, 0.5), number("wavelength", "Wave length (pixels)", 80, 2, 4000, 1),
            number("direction", "Direction (degrees)", 0, -360, 360, 1), number("speed", "Waves per second", 1, -30, 30, 0.1)};
        made.push_back(wave);
        return made;
    }();
    return list;
}

const Definition* find(const std::string& id)
{
    for (const Definition& each : definitions())
        if (each.id == id)
            return &each;
    return nullptr;
}

Applied defaults(const std::string& id, int)
{
    Applied applied;
    applied.type = id;
    if (const Definition* each = find(id))
        for (const ParamSpec& spec : each->params)
            applied.params[spec.name] = spec.text;
    return applied;
}

void apply(uint32_t* pixels, int width, int height, const std::vector<Applied>& list, int frame, double pixelsPerUnit,
    const uint8_t* coverage, double frameRate)
{
    const double seconds = frame / (frameRate > 0.0 ? frameRate : 24.0);
    if (!pixels || width <= 0 || height <= 0)
        return;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (const Applied& applied : list)
    {
        if (!applied.enabled || !find(applied.type))
            continue;
        const double amount = std::max(0.0, std::min(1.0, param(applied, "amount", frame, 100.0) / 100.0));
        if (amount <= 0.0)
            continue;
        std::vector<uint32_t> before(pixels, pixels + count);
        if (applied.type == "blur")
        {
            const double radius = param(applied, "radius", frame, 6.0) * pixelsPerUnit;
            if (radius > 0.25)
                boxBlur(pixels, width, height, radius * 2.0, radius * 2.0, 3);
        }
        else if (applied.type == "turbulent" || applied.type == "wave")
        {
            // Each pixel takes the color from a place pushed about by the
            // field: smooth noise, or a traveling wave across its direction.
            const double scale = std::max(1e-6, pixelsPerUnit);
            const double reach = param(applied, "amount", frame, 8.0) * scale;
            const bool turbulent = applied.type == "turbulent";
            const double size = std::max(1.0, param(applied, turbulent ? "size" : "wavelength", frame, turbulent ? 60.0 : 80.0) * scale);
            const double speed = param(applied, "speed", frame, 1.0);
            const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0));
            const double angle = param(applied, "direction", frame, 0.0) * kPi / 180.0;
            const double ax = std::cos(angle), ay = std::sin(angle);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    double dx, dy;
                    if (turbulent)
                    {
                        dx = noise3(seedValue, x / size, y / size, seconds * speed) * reach;
                        dy = noise3(seedValue + 7919, x / size, y / size, seconds * speed) * reach;
                    }
                    else
                    {
                        // Along the direction the wave runs; the push is across it.
                        const double along = x * ax + y * ay;
                        const double push = std::sin((along / size - seconds * speed) * 2.0 * kPi) * reach;
                        dx = -ay * push;
                        dy = ax * push;
                    }
                    pixels[static_cast<size_t>(y) * width + x] = sample(before, width, height, x + dx, y + dy);
                }
        }
        else
        {
            // Per pixel, out of premultiplied and back.
            const double brightness = param(applied, "brightness", frame, 0.0) * 2.55;
            const double contrast = (100.0 + param(applied, "contrast", frame, 0.0)) / 100.0;
            const double saturation = (100.0 + param(applied, "saturation", frame, 0.0)) / 100.0;
            const double hue = param(applied, "hue", frame, 0.0) * 3.14159265358979323846 / 180.0;
            const double cosH = std::cos(hue);
            const double sinH = std::sin(hue);
            unsigned tint = 0;
            if (applied.type == "tint")
            {
                std::string text = applied.valueAt("color", frame, "#3050A0");
                if (!text.empty() && text[0] == '#')
                    text = text.substr(1);
                tint = static_cast<unsigned>(std::strtoul(text.c_str(), nullptr, 16));
            }
            for (size_t i = 0; i < count; ++i)
            {
                const uint32_t p = pixels[i];
                const double a = (p >> 24) & 0xFF;
                if (a <= 0)
                    continue;
                double r = ((p >> 16) & 0xFF) * 255.0 / a;
                double g = ((p >> 8) & 0xFF) * 255.0 / a;
                double b = (p & 0xFF) * 255.0 / a;
                if (applied.type == "tint")
                {
                    // The tint's color at the pixel's lightness.
                    const double light = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
                    r = ((tint >> 16) & 0xFF) * light * 1.6;
                    g = ((tint >> 8) & 0xFF) * light * 1.6;
                    b = (tint & 0xFF) * light * 1.6;
                }
                else
                {
                    r = (r - 128.0) * contrast + 128.0 + brightness;
                    g = (g - 128.0) * contrast + 128.0 + brightness;
                    b = (b - 128.0) * contrast + 128.0 + brightness;
                    const double gray = 0.299 * r + 0.587 * g + 0.114 * b;
                    r = gray + (r - gray) * saturation;
                    g = gray + (g - gray) * saturation;
                    b = gray + (b - gray) * saturation;
                    if (hue != 0.0)
                    {
                        // Turned round the gray axis, in YIQ.
                        const double y = 0.299 * r + 0.587 * g + 0.114 * b;
                        const double iq = 0.596 * r - 0.274 * g - 0.322 * b;
                        const double q = 0.211 * r - 0.523 * g + 0.312 * b;
                        const double i2 = iq * cosH - q * sinH;
                        const double q2 = iq * sinH + q * cosH;
                        r = y + 0.956 * i2 + 0.621 * q2;
                        g = y - 0.272 * i2 - 0.647 * q2;
                        b = y - 1.106 * i2 + 1.703 * q2;
                    }
                }
                const auto premultiplied = [a](double v) { return static_cast<uint32_t>(std::lround(clamp255(v) * a / 255.0)); };
                pixels[i] = (p & 0xFF000000u) | (premultiplied(r) << 16) | (premultiplied(g) << 8) | premultiplied(b);
            }
        }
        // Part way, and only where the layer's own art covers.
        if (amount < 1.0 || coverage)
        {
            for (size_t i = 0; i < count; ++i)
            {
                const double k = amount * (coverage ? coverage[i] / 255.0 : 1.0);
                if (k >= 1.0)
                    continue;
                uint32_t mixed = 0;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const double from = (before[i] >> shift) & 0xFF;
                    const double to = (pixels[i] >> shift) & 0xFF;
                    mixed |= static_cast<uint32_t>(std::lround(from + (to - from) * k)) << shift;
                }
                pixels[i] = mixed;
            }
        }
    }
}

std::vector<uint8_t> matteStrength(const uint32_t* matte, int width, int height, Matte kind)
{
    const size_t count = static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height));
    std::vector<uint8_t> out(count, 0);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t p = matte[i];
        const double a = (p >> 24) & 0xFF;
        double strength = a;
        if (kind == Matte::Luma || kind == Matte::LumaInverted)
        {
            // Premultiplied: the light already scaled by the alpha.
            strength = 0.299 * ((p >> 16) & 0xFF) + 0.587 * ((p >> 8) & 0xFF) + 0.114 * (p & 0xFF);
        }
        if (kind == Matte::AlphaInverted)
            strength = 255.0 - a;
        else if (kind == Matte::LumaInverted)
            strength = 255.0 - strength;
        out[i] = static_cast<uint8_t>(std::lround(clamp255(strength)));
    }
    return out;
}

} // namespace fla::compositing
