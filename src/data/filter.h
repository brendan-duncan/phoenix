#pragma once

#include "linear_gradient.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

/// One of the filters on a symbol instance, as the file records it: the
/// attributes of a BlurFilter, DropShadowFilter, GlowFilter, BevelFilter,
/// GradientGlowFilter, GradientBevelFilter or AdjustColorFilter element, or
/// Phoenix's own color matrix and convolution (from SWF movies), which an
/// FLA keeps as phoenix... attributes on an Adjust Color and a Blur that do
/// nothing. Fields a filter type does not use keep their defaults.
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
        AdjustColor,
        ColorMatrix,
        Convolution
    };

    Type type = Type::Blur;

    /// Whether the filter is switched on. The FLA keeps switched-off filters
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

    /// Shadow and glow color, alpha in the fourth byte.
    uint8_t color[4] = {0, 0, 0, 255};

    /// Bevel colors.
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

    /// Color matrix: four rows of five, red, green, blue and alpha out of
    /// red, green, blue, alpha (0 to 255, unpremultiplied) and an offset.
    /// Convolution: matrixX across by matrixY down, its sum divided by
    /// divisor with bias added; alpha left alone with preserveAlpha; past the
    /// edge, the edge pixels with clamp, else color.
    std::vector<double> matrix;
    int matrixX = 0;
    int matrixY = 0;
    double divisor = 1.0;
    double bias = 0.0;
    bool preserveAlpha = true;
    bool clamp = true;

    /// Gradient glow and bevel colors.
    std::vector<GradientEntry> entries;

    /// Attributes as read that Phoenix does not model, and the names of the
    /// modeled ones the file wrote, as DOMElement keeps them.
    std::vector<std::pair<std::string, std::string>> extraAttributes;
    std::vector<std::string> presentAttributes;
    /// Unknown .phx fields of the filter, kept (see DOMElement::phxUnknown).
    std::string phxUnknown;

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
        // As an FLA keeps them.
        case Type::ColorMatrix: return "AdjustColorFilter";
        case Type::Convolution: return "BlurFilter";
        }
        return "BlurFilter";
    }
};

} // namespace fla
