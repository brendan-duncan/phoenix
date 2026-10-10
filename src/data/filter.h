#pragma once

#include "linear_gradient.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

/// One of the filters on a symbol instance, as the file records it: the
/// attributes of a BlurFilter, DropShadowFilter, GlowFilter, BevelFilter,
/// GradientGlowFilter, GradientBevelFilter or AdjustColorFilter element.
/// Fields a filter type does not use keep their defaults.
struct Filter
{
    enum class Type
    {
        Blur,
        DropShadow,
        Glow,
        Bevel,
        GradientGlow,
        GradientBevel,
        AdjustColor
    };

    Type type = Type::Blur;

    /// Whether the filter is switched on. Animate keeps switched-off filters
    /// in the file.
    bool enabled = true;

    /// Blur amount in pixels on each axis, and the number of blur passes.
    double blurX = 5.0;
    double blurY = 5.0;
    int quality = 1;

    /// Shadow and bevel: direction in degrees, clockwise from three o'clock,
    /// and how far the shadow is offset.
    double angle = 45.0;
    double distance = 5.0;

    /// How strongly the effect is applied; 1 is plain.
    double strength = 1.0;

    /// Shadow and glow colour, alpha in the fourth byte.
    uint8_t color[4] = {0, 0, 0, 255};

    /// Bevel colours.
    uint8_t highlightColor[4] = {255, 255, 255, 255};
    uint8_t shadowColor[4] = {0, 0, 0, 255};

    bool inner = false;
    bool knockout = false;
    bool hideObject = false;

    /// Bevel placement: "inner" (the default), "outer" or "full".
    std::string bevelType;

    /// Adjust Color, each -100 to 100 (hue -180 to 180).
    double brightness = 0.0;
    double contrast = 0.0;
    double saturation = 0.0;
    double hue = 0.0;

    /// Gradient glow and bevel colours.
    std::vector<GradientEntry> entries;

    /// The element name the file uses for this type.
    const char* elementName() const
    {
        switch (type)
        {
        case Type::Blur: return "BlurFilter";
        case Type::DropShadow: return "DropShadowFilter";
        case Type::Glow: return "GlowFilter";
        case Type::Bevel: return "BevelFilter";
        case Type::GradientGlow: return "GradientGlowFilter";
        case Type::GradientBevel: return "GradientBevelFilter";
        case Type::AdjustColor: return "AdjustColorFilter";
        }
        return "BlurFilter";
    }
};

} // namespace fla
