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

    Swatch(DOMElement* parent)
        : DOMElement(parent)
    {}

    virtual Type swatchType() const = 0;
};

class SolidSwatch : public Swatch
{
public:
    uint8_t color[4] = {0, 0, 0, 255};

    SolidSwatch(DOMElement* parent)
        : Swatch(parent)
    {}

    Type swatchType() const override { return Type::Solid; }

    std::string domTypeName() const override { return "SolidSwatch"; }

    DOMType domType() const override { return DOMType::SolidSwatch; }
};

class LinearGradientSwatch : public Swatch
{
public:
    LinearGradientSwatch(DOMElement* parent)
        : Swatch(parent)
    {}

    Type swatchType() const override { return Type::LinearGradient; }

    std::string domTypeName() const override { return "LinearGradientSwatch"; }

    DOMType domType() const override { return DOMType::LinearGradientSwatch; }
};

class RadialGradientSwatch : public Swatch
{
public:
    RadialGradientSwatch(DOMElement* parent)
        : Swatch(parent)
    {}

    Type swatchType() const override { return Type::RadialGradient; }

    std::string domTypeName() const override { return "RadialGradientSwatch"; }

    DOMType domType() const override { return DOMType::RadialGradientSwatch; }
};

} // namespace fla
