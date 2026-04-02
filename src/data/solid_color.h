#pragma once

#include "fill_style.h"
#include <cstdint>

namespace fla {

class SolidColor : public FillStyle
{
public:
    uint8_t color[4] = {0, 0, 0, 255}; // RGBA format

    SolidColor(DOMElement* parent)
        : FillStyle(parent)
    {}

    Type type() const override { return Type::SolidColor; }

    std::string domTypeName() const override { return "SolidColor"; }

    DOMType domType() const override { return DOMType::SolidColor; }

    static DOMType staticDomType() { return DOMType::SolidColor; }
};

} // namespace fla
