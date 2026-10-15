#include "adjustment_effects.h"

#include "image_blur.h"
#include "pixel_math.h"
#include "../util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace fla::compositing::effects {

namespace {

using behaviors::Applied;
using behaviors::Definition;
using behaviors::ParamSpec;

constexpr double kPi = 3.14159265358979323846;

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

ParamSpec choice(const char* name, const char* label, std::vector<std::string> options)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "choice";
    spec.choices = std::move(options);
    spec.text = spec.choices.empty() ? std::string() : spec.choices.front();
    return spec;
}

ParamSpec checkbox(const char* name, const char* label, bool value)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "checkbox";
    spec.value = value ? 1.0 : 0.0;
    spec.text = value ? "true" : "false";
    return spec;
}

ParamSpec seed()
{
    ParamSpec spec = number("seed", "Seed", 1, 0, 9999, 1);
    spec.type = "seed";
    return spec;
}

Definition definition(const char* id, const char* name, const char* description, std::vector<ParamSpec> params)
{
    Definition made;
    made.id = id;
    made.name = name;
    made.description = description;
    made.builtIn = true;
    made.params = std::move(params);
    return made;
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

std::string text(const Applied& applied, const char* name, int frame, const std::string& fallback)
{
    const std::string value = applied.valueAt(name, frame, std::string());
    return value.empty() ? fallback : value;
}

bool flag(const Applied& applied, const char* name, int frame, bool fallback)
{
    const std::string value = applied.valueAt(name, frame, std::string());
    return value.empty() ? fallback : value == "true" || value == "1";
}

struct Rgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

Rgb colorParam(const Applied& applied, const char* name, int frame, const char* fallback)
{
    std::string value = text(applied, name, frame, fallback);
    if (!value.empty() && value[0] == '#')
        value = value.substr(1);
    const unsigned rgb = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 16));
    return Rgb{static_cast<double>((rgb >> 16) & 0xFF), static_cast<double>((rgb >> 8) & 0xFF), static_cast<double>(rgb & 0xFF)};
}

double clamp255(double v)
{
    return std::max(0.0, std::min(255.0, v));
}

double smoothstep(double edge0, double edge1, double x)
{
    if (edge1 <= edge0)
        return x < edge0 ? 0.0 : 1.0;
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// A pixel out of premultiplied: its color (0 to 255) and alpha (0 to 255).
void unpack(uint32_t p, Rgb& rgb, double& a)
{
    a = (p >> 24) & 0xFF;
    if (a <= 0.0)
    {
        rgb = Rgb();
        return;
    }
    rgb.r = ((p >> 16) & 0xFF) * 255.0 / a;
    rgb.g = ((p >> 8) & 0xFF) * 255.0 / a;
    rgb.b = (p & 0xFF) * 255.0 / a;
}

uint32_t pack(const Rgb& rgb, double a)
{
    a = clamp255(a);
    const auto channel = [a](double v) { return static_cast<uint32_t>(std::lround(clamp255(v) * a / 255.0)); };
    return (static_cast<uint32_t>(std::lround(a)) << 24) | (channel(rgb.r) << 16) | (channel(rgb.g) << 8) | channel(rgb.b);
}

uint32_t scaled(uint32_t p, double k)
{
    k = std::clamp(k, 0.0, 1.0);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= static_cast<uint32_t>(std::lround(((p >> shift) & 0xFF) * k)) << shift;
    return out;
}

double luma(const Rgb& c)
{
    return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
}

uint32_t sample(const std::vector<uint32_t>& pixels, int width, int height, double x, double y)
{
    return samplePixel(pixels.data(), width, height, x, y);
}

/// The average of several premultiplied pixels, each with a weight.
struct Average
{
    ChannelSum sum;
    double weight = 0.0;
    void add(uint32_t p, double w = 1.0)
    {
        sum.add(p, w);
        weight += w;
    }
    uint32_t result() const { return weight <= 0.0 ? 0 : sum.pixel(weight); }
};

double hash3(int seed, int x, int y, int z)
{
    uint32_t h = static_cast<uint32_t>(seed) * 374761393u + static_cast<uint32_t>(x) * 668265263u +
        static_cast<uint32_t>(y) * 2246822519u + static_cast<uint32_t>(z) * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / static_cast<double>(0xFFFFFF);
}

/// Smooth value noise from 0 to 1 over x, y and time.
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

/// \a top over \a base by \a mode (Replace, Multiply, Screen, Overlay,
/// Add), the result as opaque as the base where it has any, else the top.
Rgb blend(const Rgb& base, const Rgb& top, const std::string& mode)
{
    const auto each = [&](double b, double t) {
        b /= 255.0;
        t /= 255.0;
        double v = t;
        if (mode == "Multiply")
            v = b * t;
        else if (mode == "Screen")
            v = 1.0 - (1.0 - b) * (1.0 - t);
        else if (mode == "Overlay")
            v = b < 0.5 ? 2.0 * b * t : 1.0 - 2.0 * (1.0 - b) * (1.0 - t);
        else if (mode == "Add")
            v = b + t;
        return v * 255.0;
    };
    return Rgb{each(base.r, top.r), each(base.g, top.g), each(base.b, top.b)};
}

/// The picture with \a make's color on every pixel, blended as \a mode
/// says: Replace paints it solid; the others work it into what is there.
template <typename Make>
void generate(uint32_t* pixels, const std::vector<uint32_t>& before, int width, int height, const std::string& mode, Make make)
{
    parallelRows(height, width, [&](int y) {
        for (int x = 0; x < width; ++x)
        {
            const size_t i = static_cast<size_t>(y) * width + x;
            const Rgb made = make(x, y);
            if (mode == "Replace")
            {
                pixels[i] = pack(made, 255.0);
                continue;
            }
            Rgb base;
            double a = 0.0;
            unpack(before[i], base, a);
            if (a <= 0.0)
                continue;
            pixels[i] = pack(blend(base, made, mode), a);
        }
    });
}

/// A per-color lookup applied to every pixel's color, keeping its alpha.
template <typename Change>
void recolor(uint32_t* pixels, const std::vector<uint32_t>& before, size_t count, Change change)
{
    parallelPixels(count, [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i)
        {
            Rgb c;
            double a = 0.0;
            unpack(before[i], c, a);
            if (a <= 0.0)
                continue;
            pixels[i] = pack(change(c), a);
        }
    });
}

void toHsl(const Rgb& c, double& h, double& s, double& l)
{
    const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    const double high = std::max({r, g, b}), low = std::min({r, g, b});
    l = (high + low) / 2.0;
    if (high == low)
    {
        h = s = 0.0;
        return;
    }
    const double d = high - low;
    s = l > 0.5 ? d / (2.0 - high - low) : d / (high + low);
    if (high == r)
        h = (g - b) / d + (g < b ? 6.0 : 0.0);
    else if (high == g)
        h = (b - r) / d + 2.0;
    else
        h = (r - g) / d + 4.0;
    h *= 60.0;
}

Rgb fromHsl(double h, double s, double l)
{
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 360.0;
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    if (s == 0.0)
        return Rgb{l * 255.0, l * 255.0, l * 255.0};
    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    const auto hue = [p, q](double t) {
        t -= std::floor(t);
        if (t < 1.0 / 6.0)
            return p + (q - p) * 6.0 * t;
        if (t < 0.5)
            return q;
        if (t < 2.0 / 3.0)
            return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    return Rgb{hue(h + 1.0 / 3.0) * 255.0, hue(h) * 255.0, hue(h - 1.0 / 3.0) * 255.0};
}

/// A curve through "x,y x,y ..." points (0 to 255) as a table of 256,
/// smooth between the points and flat past the ends.
std::vector<double> curveTable(const std::string& points)
{
    std::vector<std::pair<double, double>> knots;
    const char* at = points.c_str();
    while (*at)
    {
        char* end = nullptr;
        const double x = std::strtod(at, &end);
        if (end == at)
        {
            ++at;
            continue;
        }
        at = end;
        while (*at == ',' || *at == ' ')
            ++at;
        const double y = std::strtod(at, &end);
        if (end == at)
            break;
        at = end;
        knots.emplace_back(std::clamp(x, 0.0, 255.0), std::clamp(y, 0.0, 255.0));
    }
    std::sort(knots.begin(), knots.end());
    std::vector<double> table(256);
    if (knots.size() < 2)
    {
        for (int i = 0; i < 256; ++i)
            table[i] = i;
        return table;
    }
    for (int i = 0; i < 256; ++i)
    {
        const double x = i;
        if (x <= knots.front().first)
        {
            table[i] = knots.front().second;
            continue;
        }
        if (x >= knots.back().first)
        {
            table[i] = knots.back().second;
            continue;
        }
        size_t k = 1;
        while (k < knots.size() && knots[k].first < x)
            ++k;
        // Catmull-Rom through the points either side.
        const auto y = [&](size_t n) { return knots[std::min(n, knots.size() - 1)].second; };
        const double x1 = knots[k - 1].first, x2 = knots[k].first;
        const double t = x2 > x1 ? (x - x1) / (x2 - x1) : 0.0;
        const double p0 = y(k >= 2 ? k - 2 : k - 1), p1 = y(k - 1), p2 = y(k), p3 = y(k + 1);
        const double t2 = t * t, t3 = t2 * t;
        table[i] = clamp255(0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3));
    }
    return table;
}

/// Where across the picture a direction at \a angle degrees runs, for a
/// wipe or blinds: each pixel's place along it, and the picture's least and
/// most.
struct Across
{
    double ax = 1.0;
    double ay = 0.0;
    double low = 0.0;
    double high = 1.0;
    Across(double angle, int width, int height)
    {
        ax = std::cos(angle * kPi / 180.0);
        ay = std::sin(angle * kPi / 180.0);
        const double corners[4] = {0.0, width * ax, height * ay, width * ax + height * ay};
        low = *std::min_element(corners, corners + 4);
        high = *std::max_element(corners, corners + 4);
    }
    double at(double x, double y) const { return x * ax + y * ay; }
};

} // namespace

const std::vector<Definition>& definitions()
{
    static const std::vector<Definition> list = {
        // Blurs.
        definition("dirblur", "Directional Blur", "Smears the picture along one direction, as fast motion would.",
            {number("angle", "Direction (degrees)", 0, -360, 360, 1), number("length", "Length (pixels)", 20, 0, 1000, 0.5),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("radialblur", "Radial Blur",
            "Spins the picture about a point, or zooms it out from the point: a spinning wheel, a rush toward the camera.",
            {choice("kind", "Kind", {"Spin", "Zoom"}), number("strength", "Strength", 10, 0, 100, 0.5),
                number("centerX", "Center across (percent)", 50, -100, 200, 0.5), number("centerY", "Center down (percent)", 50, -100, 200, 0.5),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("lensblur", "Lens Blur",
            "Blurs as an out-of-focus camera lens does, bright points opening into round discs.",
            {number("radius", "Radius (pixels)", 8, 0, 200, 0.5), number("highlights", "Highlight boost (percent)", 50, 0, 100, 1),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("glow", "Glow", "Makes the bright parts of the picture glow, spreading their light around them.",
            {number("threshold", "Threshold (percent)", 60, 0, 100, 1), number("radius", "Radius (pixels)", 12, 0, 500, 0.5),
                number("intensity", "Intensity (percent)", 100, 0, 500, 1), textParam("color", "Glow color (#RRGGBB, empty for its own)", ""),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        // Generators.
        definition("ramp", "Gradient Ramp",
            "Paints a gradient from one color to another between two points, over the picture or into it. With art on "
            "the layer, it fills just the art.",
            {textParam("startColor", "Start color (#RRGGBB)", "#000000"), textParam("endColor", "End color (#RRGGBB)", "#FFFFFF"),
                number("startX", "Start across (percent)", 0, -100, 200, 0.5), number("startY", "Start down (percent)", 50, -100, 200, 0.5),
                number("endX", "End across (percent)", 100, -100, 200, 0.5), number("endY", "End down (percent)", 50, -100, 200, 0.5),
                choice("shape", "Shape", {"Linear", "Radial"}), choice("blend", "Blend", {"Replace", "Multiply", "Screen", "Overlay", "Add"}),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("fourcolor", "4-Color Gradient", "Paints a soft blend of four colors, one at each corner.",
            {textParam("topLeft", "Top left (#RRGGBB)", "#F04080"), textParam("topRight", "Top right (#RRGGBB)", "#F0C040"),
                textParam("bottomLeft", "Bottom left (#RRGGBB)", "#4060F0"), textParam("bottomRight", "Bottom right (#RRGGBB)", "#40D0A0"),
                choice("blend", "Blend", {"Replace", "Multiply", "Screen", "Overlay", "Add"}), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("fill", "Fill", "Fills everything drawn below with one color, keeping its shape and its soft edges.",
            {textParam("color", "Color (#RRGGBB)", "#FF4060"), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("outline", "Outline", "Draws an outline round everything drawn below, outside its edges.",
            {textParam("color", "Color (#RRGGBB)", "#FFFFFF"), number("width", "Width (pixels)", 4, 0, 200, 0.5),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("fractalnoise", "Fractal Noise",
            "Clouds of soft random noise, finer and finer in layers, that can churn over time: smoke, fog, water, "
            "a texture, a map for Displacement Map.",
            {number("scale", "Size (pixels)", 120, 2, 5000, 1), number("complexity", "Complexity (layers)", 4, 1, 8, 1),
                number("contrast", "Contrast (percent)", 100, 0, 500, 1), number("brightness", "Brightness", 0, -100, 100, 1),
                number("speed", "Changes per second", 0.5, 0, 30, 0.05), textParam("dark", "Dark color (#RRGGBB)", "#000000"),
                textParam("light", "Light color (#RRGGBB)", "#FFFFFF"), choice("blend", "Blend", {"Replace", "Multiply", "Screen", "Overlay", "Add"}),
                seed(), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        // Distortions.
        definition("bulge", "Bulge", "Swells the picture out round a point, or pinches it in with less than 0: a lens, a bubble, a bump.",
            {number("centerX", "Center across (percent)", 50, -100, 200, 0.5), number("centerY", "Center down (percent)", 50, -100, 200, 0.5),
                number("radius", "Radius (pixels)", 150, 1, 5000, 1), number("height", "Height (percent)", 50, -100, 100, 1),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("ripple", "Ripple", "Rings of waves spreading out from a point, as on water.",
            {number("centerX", "Center across (percent)", 50, -100, 200, 0.5), number("centerY", "Center down (percent)", 50, -100, 200, 0.5),
                number("wavelength", "Wave length (pixels)", 40, 2, 2000, 1), number("height", "Height (pixels)", 6, 0, 200, 0.5),
                number("speed", "Waves per second", 1, -30, 30, 0.1), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("bend", "Bend", "Bends the picture into a curve, across or down.",
            {choice("direction", "Bend", {"Across", "Down"}), number("strength", "Strength (percent)", 30, -100, 100, 1),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("displace", "Displacement Map",
            "Pushes the picture about by another layer's picture: lighter parts push one way, darker the other. Name "
            "the layer, a hidden one with Fractal Noise or a gradient say, and how far it pushes.",
            {textParam("layer", "Map layer (its name)", ""), number("pushX", "Push across (pixels)", 20, -1000, 1000, 0.5),
                number("pushY", "Push down (pixels)", 20, -1000, 1000, 0.5), choice("by", "Push by", {"Brightness", "Red and green"}),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        // Color.
        definition("levels", "Levels", "Sets the picture's black point, white point and middle tones, and the range it is shown in.",
            {number("inBlack", "Input black", 0, 0, 255, 1), number("inWhite", "Input white", 255, 0, 255, 1),
                number("gamma", "Gamma", 1, 0.1, 10, 0.01), number("outBlack", "Output black", 0, 0, 255, 1),
                number("outWhite", "Output white", 255, 0, 255, 1), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("curves", "Curves",
            "Remaps the picture's tones through a curve: points as input,output pairs from 0 to 255, such as "
            "\"0,0 64,40 192,220 255,255\" for more contrast.",
            {textParam("points", "Points (input,output ...)", "0,0 64,48 192,208 255,255"), choice("channel", "Channel", {"All", "Red", "Green", "Blue"}),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("huesat", "Hue/Saturation", "Turns the picture's hues, and sets how strong and how light its colors are; Colorize makes it all one hue.",
            {number("hue", "Hue (degrees)", 0, -180, 180, 1), number("saturation", "Saturation", 0, -100, 100, 1),
                number("lightness", "Lightness", 0, -100, 100, 1), checkbox("colorize", "Colorize", false),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("tritone", "Tritone", "Maps the picture's darks, middles and lights to three colors.",
            {textParam("shadows", "Shadows (#RRGGBB)", "#101030"), textParam("midtones", "Midtones (#RRGGBB)", "#A04060"),
                textParam("highlights", "Highlights (#RRGGBB)", "#FFE0A0"), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("vignette", "Vignette", "Darkens the picture toward its edges, drawing the eye to the middle.",
            {number("darkness", "Darkness (percent)", 60, 0, 100, 1), number("size", "Size (percent)", 60, 0, 200, 1),
                number("softness", "Softness (percent)", 50, 1, 100, 1), textParam("color", "Color (#RRGGBB)", "#000000"),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("grain", "Grain", "Adds film grain: fine random speckle, still or changing every frame.",
            {number("strength", "Strength (percent)", 20, 0, 100, 1), number("size", "Size (pixels)", 1, 0.5, 20, 0.1),
                checkbox("animated", "Changes every frame", true), seed(), number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        definition("chromatic", "Chromatic Aberration",
            "Pulls the red and blue apart, as a cheap lens or a glitch does.",
            {number("distance", "Distance (pixels)", 4, -200, 200, 0.5), number("angle", "Direction (degrees)", 0, -360, 360, 1),
                number("amount", "Amount (percent)", 100, 0, 100, 1)}),
        // Transitions.
        definition("linearwipe", "Linear Wipe", "Wipes the picture away behind a moving edge; key Completion from 0 to 100.",
            {number("completion", "Completion (percent)", 0, 0, 100, 0.5), number("angle", "Direction (degrees)", 0, -360, 360, 1),
                number("feather", "Feather (pixels)", 20, 0, 2000, 1)}),
        definition("radialwipe", "Radial Wipe", "Wipes the picture away round a point like a clock hand; key Completion.",
            {number("completion", "Completion (percent)", 0, 0, 100, 0.5), number("start", "Start angle (degrees)", -90, -360, 360, 1),
                number("centerX", "Center across (percent)", 50, -100, 200, 0.5), number("centerY", "Center down (percent)", 50, -100, 200, 0.5),
                checkbox("clockwise", "Clockwise", true), number("feather", "Feather (degrees)", 5, 0, 180, 0.5)}),
        definition("blinds", "Venetian Blinds", "Wipes the picture away in stripes, all at once; key Completion.",
            {number("completion", "Completion (percent)", 0, 0, 100, 0.5), number("angle", "Direction (degrees)", 90, -360, 360, 1),
                number("width", "Stripe width (pixels)", 40, 2, 2000, 1), number("feather", "Feather (pixels)", 2, 0, 200, 0.5)}),
        definition("dissolve", "Block Dissolve", "Dissolves the picture away in random blocks; key Completion.",
            {number("completion", "Completion (percent)", 0, 0, 100, 0.5), number("block", "Block size (pixels)", 16, 1, 1000, 1), seed()}),
    };
    return list;
}

bool handles(const std::string& type)
{
    static const std::map<std::string, bool> known = [] {
        std::map<std::string, bool> made;
        for (const Definition& each : definitions())
            made[each.id] = true;
        return made;
    }();
    return known.count(type) > 0;
}

std::vector<std::string> layersNeeded(const std::vector<Applied>& list, int frame)
{
    std::vector<std::string> names;
    for (const Applied& applied : list)
        if (applied.enabled && applied.type == "displace")
        {
            const std::string name = text(applied, "layer", frame, "");
            if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(name);
        }
    return names;
}

void apply(const Applied& applied, uint32_t* pixels, const std::vector<uint32_t>& before, int width, int height, int frame,
    double seconds, double pixelsPerUnit, const LayerPictures* layers)
{
    const std::string& type = applied.type;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const double scale = std::max(1e-6, pixelsPerUnit);
    const auto centerOf = [&](double& cx, double& cy) {
        cx = param(applied, "centerX", frame, 50.0) / 100.0 * width;
        cy = param(applied, "centerY", frame, 50.0) / 100.0 * height;
    };

    if (type == "dirblur")
    {
        const double length = param(applied, "length", frame, 20.0) * scale;
        const double angle = param(applied, "angle", frame, 0.0) * kPi / 180.0;
        const int taps = std::clamp(static_cast<int>(std::ceil(length)), 2, 64);
        const double dx = std::cos(angle) * length, dy = std::sin(angle) * length;
        if (length < 0.5)
            return;
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                Average average;
                for (int k = 0; k < taps; ++k)
                {
                    const double t = static_cast<double>(k) / (taps - 1) - 0.5;
                    average.add(sample(before, width, height, x + dx * t, y + dy * t));
                }
                pixels[static_cast<size_t>(y) * width + x] = average.result();
            }
        });
    }
    else if (type == "radialblur")
    {
        double cx, cy;
        centerOf(cx, cy);
        const bool spin = text(applied, "kind", frame, "Spin") == "Spin";
        const double strength = param(applied, "strength", frame, 10.0);
        const int taps = 32;
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                Average average;
                const double rx = x - cx, ry = y - cy;
                for (int k = 0; k < taps; ++k)
                {
                    const double t = static_cast<double>(k) / (taps - 1) - 0.5;
                    if (spin)
                    {
                        const double a = t * strength * kPi / 180.0;
                        average.add(sample(before, width, height, cx + rx * std::cos(a) - ry * std::sin(a),
                            cy + rx * std::sin(a) + ry * std::cos(a)));
                    }
                    else
                    {
                        const double s = 1.0 + (t - 0.5) * strength / 100.0;
                        average.add(sample(before, width, height, cx + rx * s, cy + ry * s));
                    }
                }
                pixels[static_cast<size_t>(y) * width + x] = average.result();
            }
        });
    }
    else if (type == "lensblur")
    {
        const double radius = param(applied, "radius", frame, 8.0) * scale;
        const double boost = param(applied, "highlights", frame, 50.0) / 100.0 * 8.0;
        if (radius < 0.5)
            return;
        // A disc of taps on a sunflower spiral, the bright ones counting more.
        const int taps = std::clamp(static_cast<int>(radius * radius * 0.6), 12, 96);
        std::vector<std::pair<double, double>> disc;
        for (int k = 0; k < taps; ++k)
        {
            const double r = radius * std::sqrt((k + 0.5) / taps);
            const double a = k * 2.399963229728653;
            disc.emplace_back(r * std::cos(a), r * std::sin(a));
        }
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                Average average;
                for (const auto& [ox, oy] : disc)
                {
                    const uint32_t p = sample(before, width, height, x + ox, y + oy);
                    Rgb c;
                    double a = 0.0;
                    unpack(p, c, a);
                    const double l = luma(c) * a / 255.0;
                    average.add(p, 1.0 + boost * l * l * l * l);
                }
                pixels[static_cast<size_t>(y) * width + x] = average.result();
            }
        });
    }
    else if (type == "glow")
    {
        const double threshold = std::clamp(param(applied, "threshold", frame, 60.0) / 100.0, 0.0, 0.999);
        const double radius = param(applied, "radius", frame, 12.0) * scale;
        const double intensity = param(applied, "intensity", frame, 100.0) / 100.0;
        const std::string tint = text(applied, "color", frame, "");
        const Rgb tintColor = colorParam(applied, "color", frame, "#FFFFFF");
        std::vector<uint32_t> bright(count, 0);
        parallelPixels(count, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
            {
                Rgb c;
                double a = 0.0;
                unpack(before[i], c, a);
                const double over = (luma(c) - threshold) / (1.0 - threshold);
                if (a <= 0.0 || over <= 0.0)
                    continue;
                bright[i] = pack(tint.empty() ? c : tintColor, a * std::min(1.0, over));
            }
        });
        if (radius > 0.25)
            boxBlur(bright.data(), width, height, radius * 2.0, radius * 2.0, 3);
        parallelPixels(count, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
            {
                uint32_t out = 0;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const double v = ((before[i] >> shift) & 0xFF) + ((bright[i] >> shift) & 0xFF) * intensity;
                    out |= static_cast<uint32_t>(std::lround(clamp255(v))) << shift;
                }
                pixels[i] = out;
            }
        });
    }
    else if (type == "ramp")
    {
        const Rgb from = colorParam(applied, "startColor", frame, "#000000");
        const Rgb to = colorParam(applied, "endColor", frame, "#FFFFFF");
        const double sx = param(applied, "startX", frame, 0.0) / 100.0 * width;
        const double sy = param(applied, "startY", frame, 50.0) / 100.0 * height;
        const double ex = param(applied, "endX", frame, 100.0) / 100.0 * width;
        const double ey = param(applied, "endY", frame, 50.0) / 100.0 * height;
        const bool radial = text(applied, "shape", frame, "Linear") == "Radial";
        const double vx = ex - sx, vy = ey - sy;
        const double length2 = std::max(1e-9, vx * vx + vy * vy);
        generate(pixels, before, width, height, text(applied, "blend", frame, "Replace"), [&](int x, int y) {
            const double t = radial ? std::clamp(std::hypot(x - sx, y - sy) / std::sqrt(length2), 0.0, 1.0)
                                    : std::clamp(((x - sx) * vx + (y - sy) * vy) / length2, 0.0, 1.0);
            return Rgb{from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t, from.b + (to.b - from.b) * t};
        });
    }
    else if (type == "fourcolor")
    {
        const Rgb tl = colorParam(applied, "topLeft", frame, "#F04080");
        const Rgb tr = colorParam(applied, "topRight", frame, "#F0C040");
        const Rgb bl = colorParam(applied, "bottomLeft", frame, "#4060F0");
        const Rgb br = colorParam(applied, "bottomRight", frame, "#40D0A0");
        generate(pixels, before, width, height, text(applied, "blend", frame, "Replace"), [&](int x, int y) {
            const double u = smoothstep(0.0, 1.0, width > 1 ? static_cast<double>(x) / (width - 1) : 0.0);
            const double v = smoothstep(0.0, 1.0, height > 1 ? static_cast<double>(y) / (height - 1) : 0.0);
            const auto mix = [u, v](double a, double b, double c, double d) {
                return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
            };
            return Rgb{mix(tl.r, tr.r, bl.r, br.r), mix(tl.g, tr.g, bl.g, br.g), mix(tl.b, tr.b, bl.b, br.b)};
        });
    }
    else if (type == "fill")
    {
        const Rgb color = colorParam(applied, "color", frame, "#FF4060");
        recolor(pixels, before, count, [&](const Rgb&) { return color; });
    }
    else if (type == "outline")
    {
        const Rgb color = colorParam(applied, "color", frame, "#FFFFFF");
        const double widthPx = param(applied, "width", frame, 4.0) * scale;
        if (widthPx <= 0.0)
            return;
        // How near each pixel is to the art: its alpha spread out.
        std::vector<uint32_t> spread(count);
        parallelPixels(count, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
                spread[i] = before[i] & 0xFF000000u;
        });
        boxBlur(spread.data(), width, height, widthPx * 2.0, widthPx * 2.0, 3);
        parallelPixels(count, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
            {
                const double near = std::min(1.0, ((spread[i] >> 24) & 0xFF) / 255.0 * 3.0);
                const double own = ((before[i] >> 24) & 0xFF) / 255.0;
                const double line = near * (1.0 - own);
                if (line <= 0.0)
                    continue;
                // The outline under the art.
                const uint32_t under = pack(color, line * 255.0);
                uint32_t out = 0;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const double v = ((before[i] >> shift) & 0xFF) + ((under >> shift) & 0xFF) * (1.0 - own);
                    out |= static_cast<uint32_t>(std::lround(clamp255(v))) << shift;
                }
                pixels[i] = out;
            }
        });
    }
    else if (type == "fractalnoise")
    {
        const double size = std::max(1.0, param(applied, "scale", frame, 120.0) * scale);
        const int layersCount = std::clamp(static_cast<int>(param(applied, "complexity", frame, 4.0)), 1, 8);
        const double contrast = param(applied, "contrast", frame, 100.0) / 100.0;
        const double brightness = param(applied, "brightness", frame, 0.0) / 100.0;
        const double time = seconds * param(applied, "speed", frame, 0.5);
        const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0));
        const Rgb dark = colorParam(applied, "dark", frame, "#000000");
        const Rgb light = colorParam(applied, "light", frame, "#FFFFFF");
        generate(pixels, before, width, height, text(applied, "blend", frame, "Replace"), [&](int x, int y) {
            double value = 0.0;
            double total = 0.0;
            double weight = 1.0;
            double frequency = 1.0 / size;
            for (int layer = 0; layer < layersCount; ++layer)
            {
                value += noise3(seedValue + layer * 131, x * frequency, y * frequency, time + layer * 0.37) * weight;
                total += weight;
                weight *= 0.5;
                frequency *= 2.0;
            }
            const double v = std::clamp((value / total - 0.5) * contrast * 1.6 + 0.5 + brightness, 0.0, 1.0);
            return Rgb{dark.r + (light.r - dark.r) * v, dark.g + (light.g - dark.g) * v, dark.b + (light.b - dark.b) * v};
        });
    }
    else if (type == "bulge")
    {
        double cx, cy;
        centerOf(cx, cy);
        const double radius = std::max(1.0, param(applied, "radius", frame, 150.0) * scale);
        const double height2 = param(applied, "height", frame, 50.0) / 100.0;
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const double dx = x - cx, dy = y - cy;
                const double d = std::hypot(dx, dy) / radius;
                if (d >= 1.0)
                    continue;
                const double fall = (1.0 - d * d);
                const double s = 1.0 - height2 * fall * fall;
                pixels[static_cast<size_t>(y) * width + x] = sample(before, width, height, cx + dx * s, cy + dy * s);
            }
        });
    }
    else if (type == "ripple")
    {
        double cx, cy;
        centerOf(cx, cy);
        const double wavelength = std::max(1.0, param(applied, "wavelength", frame, 40.0) * scale);
        const double reach = param(applied, "height", frame, 6.0) * scale;
        const double speed = param(applied, "speed", frame, 1.0);
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const double dx = x - cx, dy = y - cy;
                const double d = std::hypot(dx, dy);
                if (d < 1e-6)
                    continue;
                const double push = std::sin(2.0 * kPi * (d / wavelength - seconds * speed)) * reach;
                pixels[static_cast<size_t>(y) * width + x] =
                    sample(before, width, height, x + dx / d * push, y + dy / d * push);
            }
        });
    }
    else if (type == "bend")
    {
        const bool across = text(applied, "direction", frame, "Across") == "Across";
        const double strength = param(applied, "strength", frame, 30.0) / 100.0;
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                // A parabola: the middle stays, the ends move most.
                const double u = across ? (x - width / 2.0) / std::max(1.0, width / 2.0) : (y - height / 2.0) / std::max(1.0, height / 2.0);
                const double push = strength * u * u * (across ? height : width) / 2.0;
                pixels[static_cast<size_t>(y) * width + x] =
                    across ? sample(before, width, height, x, y - push) : sample(before, width, height, x - push, y);
            }
        });
    }
    else if (type == "displace")
    {
        const std::string name = text(applied, "layer", frame, "");
        const std::vector<uint32_t>* map = layers && *layers && !name.empty() ? (*layers)(name) : nullptr;
        if (!map || map->size() != count)
            return;
        const double pushX = param(applied, "pushX", frame, 20.0) * scale;
        const double pushY = param(applied, "pushY", frame, 20.0) * scale;
        const bool channels = text(applied, "by", frame, "Brightness") == "Red and green";
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                Rgb c;
                double a = 0.0;
                unpack((*map)[i], c, a);
                // Where the map has nothing, the middle: no push.
                const double k = a / 255.0;
                const double u = channels ? c.r / 255.0 : luma(c);
                const double v = channels ? c.g / 255.0 : luma(c);
                const double dx = ((u - 0.5) * k) * 2.0 * pushX;
                const double dy = ((v - 0.5) * k) * 2.0 * pushY;
                pixels[i] = sample(before, width, height, x + dx, y + dy);
            }
        });
    }
    else if (type == "levels")
    {
        const double inBlack = param(applied, "inBlack", frame, 0.0);
        const double inWhite = std::max(inBlack + 1.0, param(applied, "inWhite", frame, 255.0));
        const double gamma = std::max(0.01, param(applied, "gamma", frame, 1.0));
        const double outBlack = param(applied, "outBlack", frame, 0.0);
        const double outWhite = param(applied, "outWhite", frame, 255.0);
        std::vector<double> table(256);
        for (int i = 0; i < 256; ++i)
        {
            const double t = std::clamp((i - inBlack) / (inWhite - inBlack), 0.0, 1.0);
            table[i] = outBlack + (outWhite - outBlack) * std::pow(t, 1.0 / gamma);
        }
        recolor(pixels, before, count, [&](const Rgb& c) {
            return Rgb{table[static_cast<int>(clamp255(c.r))], table[static_cast<int>(clamp255(c.g))], table[static_cast<int>(clamp255(c.b))]};
        });
    }
    else if (type == "curves")
    {
        const std::vector<double> table = curveTable(text(applied, "points", frame, "0,0 255,255"));
        const std::string channel = text(applied, "channel", frame, "All");
        recolor(pixels, before, count, [&](const Rgb& c) {
            Rgb out = c;
            if (channel == "All" || channel == "Red")
                out.r = table[static_cast<int>(clamp255(c.r))];
            if (channel == "All" || channel == "Green")
                out.g = table[static_cast<int>(clamp255(c.g))];
            if (channel == "All" || channel == "Blue")
                out.b = table[static_cast<int>(clamp255(c.b))];
            return out;
        });
    }
    else if (type == "huesat")
    {
        const double hue = param(applied, "hue", frame, 0.0);
        const double saturation = param(applied, "saturation", frame, 0.0) / 100.0;
        const double lightness = param(applied, "lightness", frame, 0.0) / 100.0;
        const bool colorize = flag(applied, "colorize", frame, false);
        recolor(pixels, before, count, [&](const Rgb& c) {
            double h, s, l;
            toHsl(c, h, s, l);
            if (colorize)
            {
                h = hue;
                s = std::clamp(0.5 + saturation * 0.5, 0.0, 1.0);
            }
            else
            {
                h += hue;
                s = saturation >= 0.0 ? s + (1.0 - s) * saturation * s : s * (1.0 + saturation);
            }
            l = lightness >= 0.0 ? l + (1.0 - l) * lightness : l * (1.0 + lightness);
            return fromHsl(h, s, l);
        });
    }
    else if (type == "tritone")
    {
        const Rgb shadows = colorParam(applied, "shadows", frame, "#101030");
        const Rgb mids = colorParam(applied, "midtones", frame, "#A04060");
        const Rgb lights = colorParam(applied, "highlights", frame, "#FFE0A0");
        recolor(pixels, before, count, [&](const Rgb& c) {
            const double l = luma(c);
            const Rgb& a = l < 0.5 ? shadows : mids;
            const Rgb& b = l < 0.5 ? mids : lights;
            const double t = l < 0.5 ? l * 2.0 : (l - 0.5) * 2.0;
            return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
        });
    }
    else if (type == "vignette")
    {
        const double darkness = param(applied, "darkness", frame, 60.0) / 100.0;
        const double size = param(applied, "size", frame, 60.0) / 100.0;
        const double softness = std::max(0.01, param(applied, "softness", frame, 50.0) / 100.0);
        const Rgb color = colorParam(applied, "color", frame, "#000000");
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                // Distance from the middle, 1 at the corners.
                const double u = (x - width / 2.0) / std::max(1.0, width / 2.0);
                const double v = (y - height / 2.0) / std::max(1.0, height / 2.0);
                const double d = std::sqrt(u * u + v * v) / std::sqrt(2.0);
                const double k = darkness * smoothstep(size * 0.7, size * 0.7 + softness, d);
                if (k <= 0.0)
                    continue;
                // The color laid over the picture, empty parts of the stage
                // too, so the edges darken whatever is there.
                Rgb c;
                double a = 0.0;
                unpack(before[i], c, a);
                const double cover = a / 255.0;
                const double out = cover + (1.0 - cover) * k;
                const auto over = [cover, k, out](double under, double tint) {
                    return (under * cover * (1.0 - k) + tint * k) / out;
                };
                pixels[i] = pack(Rgb{over(c.r, color.r), over(c.g, color.g), over(c.b, color.b)}, out * 255.0);
            }
        });
    }
    else if (type == "grain")
    {
        const double strength = param(applied, "strength", frame, 20.0) / 100.0;
        const double size = std::max(0.5, param(applied, "size", frame, 1.0) * scale);
        const bool animated = flag(applied, "animated", frame, true);
        const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0)) + (animated ? frame * 7919 : 0);
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                Rgb c;
                double a = 0.0;
                unpack(before[i], c, a);
                if (a <= 0.0)
                    continue;
                const double n = (size <= 1.0 ? hash3(seedValue, x, y, 0) : noise3(seedValue, x / size, y / size, 0.0)) - 0.5;
                const double push = n * strength * 255.0;
                pixels[i] = pack(Rgb{c.r + push, c.g + push, c.b + push}, a);
            }
        });
    }
    else if (type == "chromatic")
    {
        const double distance = param(applied, "distance", frame, 4.0) * scale;
        const double angle = param(applied, "angle", frame, 0.0) * kPi / 180.0;
        const double dx = std::cos(angle) * distance, dy = std::sin(angle) * distance;
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                const uint32_t red = sample(before, width, height, x + dx, y + dy);
                const uint32_t blue = sample(before, width, height, x - dx, y - dy);
                const uint32_t green = before[i];
                const uint32_t a = std::max({(red >> 24) & 0xFF, (green >> 24) & 0xFF, (blue >> 24) & 0xFF});
                pixels[i] = (a << 24) | (red & 0x00FF0000u) | (green & 0x0000FF00u) | (blue & 0x000000FFu);
            }
        });
    }
    else if (type == "linearwipe" || type == "blinds")
    {
        const double completion = std::clamp(param(applied, "completion", frame, 0.0) / 100.0, 0.0, 1.0);
        const Across across(param(applied, "angle", frame, type == "blinds" ? 90.0 : 0.0), width, height);
        const double feather = std::max(0.0, param(applied, "feather", frame, type == "blinds" ? 2.0 : 20.0) * scale);
        const double stripe = std::max(1.0, param(applied, "width", frame, 40.0) * scale);
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                const double at = across.at(x, y);
                double keep;
                if (type == "linearwipe")
                {
                    // The edge runs past the far side as it completes, so
                    // nothing is left at 100 even with a feather.
                    const double edge = across.low - feather + (across.high - across.low + 2.0 * feather) * completion;
                    keep = feather > 0.0 ? smoothstep(edge - feather / 2.0, edge + feather / 2.0, at) : (at >= edge ? 1.0 : 0.0);
                }
                else
                {
                    const double within = std::fmod(at - across.low, stripe);
                    const double edge = stripe * completion;
                    keep = feather > 0.0 ? smoothstep(edge - feather / 2.0, edge + feather / 2.0, within) : (within >= edge ? 1.0 : 0.0);
                    if (completion >= 1.0)
                        keep = 0.0;
                }
                pixels[i] = scaled(before[i], keep);
            }
        });
    }
    else if (type == "radialwipe")
    {
        double cx, cy;
        centerOf(cx, cy);
        const double completion = std::clamp(param(applied, "completion", frame, 0.0) / 100.0, 0.0, 1.0);
        const double start = param(applied, "start", frame, -90.0);
        const bool clockwise = flag(applied, "clockwise", frame, true);
        const double feather = std::max(0.0, param(applied, "feather", frame, 5.0));
        const double sweep = completion * (360.0 + feather);
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                double angle = std::atan2(y - cy, x - cx) * 180.0 / kPi - start;
                if (!clockwise)
                    angle = -angle;
                angle = std::fmod(std::fmod(angle, 360.0) + 360.0, 360.0);
                const double keep = feather > 0.0 ? smoothstep(sweep - feather, sweep, angle) : (angle >= sweep ? 1.0 : 0.0);
                pixels[i] = scaled(before[i], completion >= 1.0 ? 0.0 : keep);
            }
        });
    }
    else if (type == "dissolve")
    {
        const double completion = std::clamp(param(applied, "completion", frame, 0.0) / 100.0, 0.0, 1.0);
        const double block = std::max(1.0, param(applied, "block", frame, 16.0) * scale);
        const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0));
        parallelRows(height, width, [&](int y) {
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                const double pick = hash3(seedValue, static_cast<int>(x / block), static_cast<int>(y / block), 0);
                pixels[i] = pick < completion ? 0u : before[i];
            }
        });
    }
}

} // namespace fla::compositing::effects
