#include "blend.h"

#include <algorithm>
#include <cmath>

namespace fla {
namespace display {

namespace {

struct Mode
{
    BlendMode mode;
    const char* name;
};

constexpr Mode kModes[] = {
    {BlendMode::Normal, "normal"},
    {BlendMode::Layer, "layer"},
    {BlendMode::Multiply, "multiply"},
    {BlendMode::Screen, "screen"},
    {BlendMode::Lighten, "lighten"},
    {BlendMode::Darken, "darken"},
    {BlendMode::Difference, "difference"},
    {BlendMode::Add, "add"},
    {BlendMode::Subtract, "subtract"},
    {BlendMode::Invert, "invert"},
    {BlendMode::Alpha, "alpha"},
    {BlendMode::Erase, "erase"},
    {BlendMode::Overlay, "overlay"},
    {BlendMode::Hardlight, "hardlight"},
    {BlendMode::Shader, "shader"},
};

double hardlight(double cs, double cb)
{
    if (cs <= 0.5)
        return cb * 2.0 * cs;
    const double s = 2.0 * cs - 1.0;
    return cb + s - cb * s;
}

} // namespace

const char* blendModeName(BlendMode mode)
{
    for (const Mode& each : kModes)
        if (each.mode == mode)
            return each.name;
    return "normal";
}

bool blendModeNamed(const std::string& name, BlendMode& mode)
{
    for (const Mode& each : kModes)
        if (name == each.name)
        {
            mode = each.mode;
            return true;
        }
    mode = BlendMode::Normal;
    return false;
}

uint32_t blendPixel(BlendMode mode, uint32_t source, uint32_t backdrop)
{
    const uint32_t sa8 = source >> 24;
    if (mode == BlendMode::Normal || mode == BlendMode::Layer || mode == BlendMode::Shader)
    {
        if (sa8 == 255)
            return source;
        if (sa8 == 0)
            return backdrop;
        const uint32_t keep = 255 - sa8;
        uint32_t out = 0;
        for (int shift = 0; shift < 32; shift += 8)
        {
            const uint32_t s = (source >> shift) & 0xff;
            const uint32_t b = (backdrop >> shift) & 0xff;
            out |= std::min<uint32_t>(255, s + (b * keep + 127) / 255) << shift;
        }
        return out;
    }
    const double as = sa8 / 255.0;
    const double ab = (backdrop >> 24) / 255.0;
    if (mode == BlendMode::Alpha || mode == BlendMode::Erase)
    {
        // What is under keeps as much of itself as the source covers, or as
        // much as it does not.
        const double keep = mode == BlendMode::Alpha ? as : 1.0 - as;
        uint32_t out = 0;
        for (int shift = 0; shift < 32; shift += 8)
            out |= static_cast<uint32_t>(std::lround(((backdrop >> shift) & 0xff) * keep)) << shift;
        return out;
    }
    const double ao = as + ab * (1.0 - as);
    uint32_t out = static_cast<uint32_t>(std::lround(ao * 255.0)) << 24;
    for (int shift = 0; shift < 24; shift += 8)
    {
        const double sp = ((source >> shift) & 0xff) / 255.0;
        const double bp = ((backdrop >> shift) & 0xff) / 255.0;
        const double cs = as > 0.0 ? std::min(1.0, sp / as) : 0.0;
        const double cb = ab > 0.0 ? std::min(1.0, bp / ab) : 0.0;
        double mixed = cs;
        switch (mode)
        {
        case BlendMode::Multiply: mixed = cs * cb; break;
        case BlendMode::Screen: mixed = cs + cb - cs * cb; break;
        case BlendMode::Lighten: mixed = std::max(cs, cb); break;
        case BlendMode::Darken: mixed = std::min(cs, cb); break;
        case BlendMode::Difference: mixed = std::fabs(cb - cs); break;
        case BlendMode::Add: mixed = std::min(1.0, cs + cb); break;
        case BlendMode::Subtract: mixed = std::max(0.0, cb - cs); break;
        case BlendMode::Invert: mixed = 1.0 - cb; break;
        case BlendMode::Overlay: mixed = hardlight(cb, cs); break;
        case BlendMode::Hardlight: mixed = hardlight(cs, cb); break;
        default: break;
        }
        const double co = sp * (1.0 - ab) + bp * (1.0 - as) + as * ab * mixed;
        out |= static_cast<uint32_t>(std::lround(std::max(0.0, std::min(ao, co)) * 255.0)) << shift;
    }
    return out;
}

} // namespace display
} // namespace fla
