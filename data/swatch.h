#pragma once

#include "dom_element.h"

namespace fla {

class Swatch : public DOMElement
{
public:
    enum class Type
    {
        Solid,
        LinearGradient,
        RadialGradient,
    };

    Swatch() = default;
    virtual Type swatchType() const = 0;
    std::string domTypeName() const override { return "Swatch"; }
};

class SolidSwatch : public Swatch
{
public:
    uint8_t color[4] = {0, 0, 0, 255};
    SolidSwatch() = default;
    Type swatchType() const override { return Type::Solid; }
    std::string domTypeName() const override { return "SolidSwatch"; }
};

class LinearGradientSwatch : public Swatch
{
public:
    LinearGradientSwatch() = default;
    Type swatchType() const override { return Type::LinearGradient; }
    std::string domTypeName() const override { return "LinearGradientSwatch"; }
};

class RadialGradientSwatch : public Swatch
{
public:
    RadialGradientSwatch() = default;
    Type swatchType() const override { return Type::RadialGradient; }
    std::string domTypeName() const override { return "RadialGradientSwatch"; }
};

} // namespace fla
