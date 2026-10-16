#include "adjustments.h"

#include "image_blur.h"
#include "pixel_math.h"
#include "../util/parallel.h"

#include "../data/dom_element.h"
#include "../data/layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla::compositing {

const char* const kMatte = "phoenixMatte";
const char* const kAdjustmentLayer = "phoenixAdjustmentLayer";
const char* const kAdjustments = "phoenixAdjustments";
const char* const kInstanceEffects = "phoenixEffects";

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
    // Not a number, or past any sensible setting (from a file, typed in):
    // kept to where the arithmetic on it stays sound.
    if (end == text.c_str() || !std::isfinite(value))
        return fallback;
    return std::clamp(value, -1.0e6, 1.0e6);
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
    return samplePixel(pixels.data(), width, height, x, y);
}

} // namespace

MaskEdge maskEdgeOf(const Layer& layer)
{
    MaskEdge edge;
    edge.feather = std::max(0.0, std::atof(attribute(layer, "phoenixMaskFeather").c_str()));
    edge.expand = std::atof(attribute(layer, "phoenixMaskExpand").c_str());
    edge.invert = attribute(layer, "phoenixMaskInvert") == "true";
    return edge;
}

std::vector<std::pair<std::string, std::string>> withMaskEdge(const Layer& layer, const MaskEdge& edge)
{
    Layer copy(nullptr);
    copy.extraAttributes = layer.extraAttributes;
    copy.extraAttributes = without(copy, "phoenixMaskFeather");
    copy.extraAttributes = without(copy, "phoenixMaskExpand");
    copy.extraAttributes = without(copy, "phoenixMaskInvert");
    const auto number = [](double v) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.10g", v);
        return std::string(text);
    };
    if (edge.feather > 0.0)
        copy.extraAttributes.emplace_back("phoenixMaskFeather", number(edge.feather));
    if (edge.expand != 0.0)
        copy.extraAttributes.emplace_back("phoenixMaskExpand", number(edge.expand));
    if (edge.invert)
        copy.extraAttributes.emplace_back("phoenixMaskInvert", "true");
    return copy.extraAttributes;
}

double elementFeatherOf(const DOMElement& element, double layerFeather)
{
    for (const auto& each : element.extraAttributes)
        if (each.first == "phoenixMaskFeather")
        {
            char* end = nullptr;
            const double value = std::strtod(each.second.c_str(), &end);
            if (end != each.second.c_str() && std::isfinite(value))
                return std::clamp(value, 0.0, 1.0e4);
        }
    return layerFeather;
}

std::vector<std::pair<std::string, std::string>> withElementFeather(const DOMElement& element, double feather)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& each : element.extraAttributes)
        if (each.first != "phoenixMaskFeather")
            out.push_back(each);
    if (feather >= 0.0)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.10g", feather);
        out.emplace_back("phoenixMaskFeather", text);
    }
    return out;
}

double blendedFeather(const DOMElement& from, const DOMElement* to, double t, double layerFeather)
{
    const double a = elementFeatherOf(from, layerFeather);
    if (!to)
        return a;
    const double b = elementFeatherOf(*to, layerFeather);
    return a + (b - a) * std::clamp(t, 0.0, 1.0);
}

const char* const kMoreMasks = "phoenixMasks";

const char* maskCombineName(MaskCombine mode)
{
    switch (mode)
    {
    case MaskCombine::Subtract: return "subtract";
    case MaskCombine::Intersect: return "intersect";
    case MaskCombine::Add: break;
    }
    return "add";
}

std::vector<MoreMask> moreMasksOf(const Layer& layer)
{
    // "mode:name" entries separated by semicolons, a semicolon or percent
    // sign in a name written as %3B or %25.
    std::vector<MoreMask> masks;
    const std::string text = attribute(layer, kMoreMasks);
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(';', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string entry = text.substr(start, end - start);
        start = end + 1;
        const size_t colon = entry.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string mode = entry.substr(0, colon);
        if (mode != "add" && mode != "subtract" && mode != "intersect")
            continue;
        MoreMask mask;
        mask.mode = mode == "subtract" ? MaskCombine::Subtract : mode == "intersect" ? MaskCombine::Intersect : MaskCombine::Add;
        const std::string coded = entry.substr(colon + 1);
        for (size_t i = 0; i < coded.size(); ++i)
        {
            if (coded.compare(i, 3, "%3B") == 0)
            {
                mask.layer += ';';
                i += 2;
            }
            else if (coded.compare(i, 3, "%25") == 0)
            {
                mask.layer += '%';
                i += 2;
            }
            else
                mask.layer += coded[i];
        }
        if (!mask.layer.empty())
            masks.push_back(mask);
    }
    return masks;
}

std::vector<std::pair<std::string, std::string>> withMoreMasks(const Layer& layer, const std::vector<MoreMask>& masks)
{
    std::vector<std::pair<std::string, std::string>> out = without(layer, kMoreMasks);
    std::string text;
    for (const MoreMask& mask : masks)
    {
        if (mask.layer.empty())
            continue;
        if (!text.empty())
            text += ';';
        text += maskCombineName(mask.mode);
        text += ':';
        for (const char c : mask.layer)
            text += c == ';' ? std::string("%3B") : c == '%' ? std::string("%25") : std::string(1, c);
    }
    if (!text.empty())
        out.emplace_back(kMoreMasks, text);
    return out;
}

void combineMasks(std::vector<uint8_t>& own, const std::vector<std::pair<MaskCombine, const std::vector<uint8_t>*>>& others)
{
    // Widened first, then limited and cut, so the order they are listed in
    // does not matter.
    for (const MaskCombine pass : {MaskCombine::Add, MaskCombine::Intersect, MaskCombine::Subtract})
        for (const auto& [mode, coverage] : others)
        {
            if (mode != pass || !coverage || coverage->size() != own.size())
                continue;
            for (size_t i = 0; i < own.size(); ++i)
            {
                const int a = own[i];
                const int b = (*coverage)[i];
                own[i] = static_cast<uint8_t>(mode == MaskCombine::Add ? std::max(a, b)
                        : mode == MaskCombine::Intersect                ? (a * b + 127) / 255
                                                                         : (a * (255 - b) + 127) / 255);
            }
        }
}

LayerMode layerModeOf(const Layer& layer)
{
    const std::string value = attribute(layer, "phoenixLayerMode");
    if (value == "stencil")
        return LayerMode::Stencil;
    if (value == "silhouette")
        return LayerMode::Silhouette;
    if (value == "preserve")
        return LayerMode::Preserve;
    return LayerMode::Normal;
}

const char* layerModeName(LayerMode mode)
{
    switch (mode)
    {
    case LayerMode::Stencil: return "stencil";
    case LayerMode::Silhouette: return "silhouette";
    case LayerMode::Preserve: return "preserve";
    case LayerMode::Normal: break;
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> withLayerMode(const Layer& layer, LayerMode mode)
{
    std::vector<std::pair<std::string, std::string>> out = without(layer, "phoenixLayerMode");
    if (mode != LayerMode::Normal)
        out.emplace_back("phoenixLayerMode", layerModeName(mode));
    return out;
}

void shapeMask(std::vector<uint8_t>& coverage, int width, int height, const MaskEdge& edge, double pixelsPerUnit)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (coverage.size() != count || count == 0)
        return;
    const double scale = std::max(1e-6, pixelsPerUnit);
    // Grown or shrunk a pixel at a time: each step takes the most (to grow)
    // or least (to shrink) of a pixel's neighbors, squares and crosses in
    // turn so the edge keeps near round. Rounded to whole pixels.
    const double reach = std::fabs(edge.expand * scale);
    const int steps = static_cast<int>(std::lround(std::isfinite(reach) ? std::min<double>(reach, std::max(width, height)) : 0.0));
    if (steps > 0)
    {
        const bool grow = edge.expand > 0.0;
        std::vector<uint8_t> next(count);
        for (int step = 0; step < steps; ++step)
        {
            const bool square = step % 2 == 0;
            parallelRows(height, width, [&](int y) {
                for (int x = 0; x < width; ++x)
                {
                    int best = coverage[static_cast<size_t>(y) * width + x];
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if ((dx == 0 && dy == 0) || (!square && dx != 0 && dy != 0))
                                continue;
                            const int nx = x + dx, ny = y + dy;
                            // Outside the picture counts as empty.
                            const int value = nx < 0 || ny < 0 || nx >= width || ny >= height
                                ? 0 : coverage[static_cast<size_t>(ny) * width + nx];
                            best = grow ? std::max(best, value) : std::min(best, value);
                        }
                    next[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(best);
                }
            });
            coverage.swap(next);
        }
    }
    const double feather = edge.feather * scale;
    if (feather >= 0.5)
    {
        std::vector<uint32_t> soft(count);
        parallelPixels(count, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
                soft[i] = static_cast<uint32_t>(coverage[i]) << 24;
        });
        boxBlur(soft.data(), width, height, feather, feather, 3);
        parallelPixels(count, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
                coverage[i] = static_cast<uint8_t>((soft[i] >> 24) & 0xFF);
        });
    }
    if (edge.invert)
        for (uint8_t& value : coverage)
            value = static_cast<uint8_t>(255 - value);
}

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

std::vector<Applied> instanceEffectsOf(const DOMElement& element)
{
    return behaviors::listOf(element, kInstanceEffects);
}

bool hasInstanceEffects(const DOMElement& element)
{
    for (const auto& each : element.extraAttributes)
        if (each.first == kInstanceEffects)
        {
            for (const Applied& applied : behaviors::decode(each.second))
                if (applied.enabled && find(applied.type))
                    return true;
            return false;
        }
    return false;
}

double reach(const std::vector<Applied>& list, int frame)
{
    double total = 0.0;
    for (const Applied& applied : list)
    {
        if (!applied.enabled)
            continue;
        const std::string& type = applied.type;
        double far = 0.0;
        // A box blur run three times spreads about three times its radius.
        if (type == "blur")
            far = 3.0 * param(applied, "radius", frame, 6.0);
        else if (type == "glow")
            far = 3.0 * param(applied, "radius", frame, 12.0);
        else if (type == "outline")
            far = 3.0 * param(applied, "width", frame, 4.0);
        else if (type == "lensblur")
            far = param(applied, "radius", frame, 8.0);
        else if (type == "dirblur")
            far = param(applied, "length", frame, 20.0) / 2.0;
        else if (type == "chromatic")
            far = param(applied, "distance", frame, 4.0);
        else if (type == "turbulent" || type == "wave")
            far = param(applied, "amount", frame, type == "wave" ? 6.0 : 8.0);
        else if (type == "ripple")
            far = param(applied, "height", frame, 6.0);
        else if (type == "displace")
            far = std::max(std::fabs(param(applied, "pushX", frame, 20.0)), std::fabs(param(applied, "pushY", frame, 20.0)));
        else if (type == "bulge")
            far = param(applied, "radius", frame, 150.0) * std::fabs(param(applied, "height", frame, 50.0)) / 200.0;
        // Each works on what the ones before left, so their reaches add up.
        total += std::clamp(std::fabs(far), 0.0, 2000.0);
    }
    return std::min(total, 4000.0);
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
        // The rest: blurs, generators, distortions, color and transitions.
        for (const Definition& each : effects::definitions())
            made.push_back(each);
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
    const uint8_t* coverage, double frameRate, const effects::LayerPictures* layers, bool ownPicture)
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
        else if (effects::handles(applied.type))
            effects::apply(applied, pixels, before, width, height, frame, seconds, pixelsPerUnit, layers, ownPicture);
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
            parallelRows(height, width, [&](int y) {
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
            });
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
            parallelPixels(count, [&](size_t from, size_t to) {
                for (size_t i = from; i < to; ++i)
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
            });
        }
        // Part way, and only where the layer's own art covers.
        if (amount < 1.0 || coverage)
        {
            parallelPixels(count, [&](size_t from, size_t to) {
                for (size_t i = from; i < to; ++i)
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
            });
        }
    }
}

std::vector<uint8_t> matteStrength(const uint32_t* matte, int width, int height, Matte kind)
{
    const size_t count = static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height));
    std::vector<uint8_t> out(count, 0);
    parallelPixels(count, [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i)
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
    });
    return out;
}

} // namespace fla::compositing
