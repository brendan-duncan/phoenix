#pragma once

#include "fill_style.h"
#include <cstdint>
#include <vector>

namespace fla {

struct RadialEntry
{
    double ratio;
    uint8_t color[4];
};

class RadialGradient : public FillStyle
{
public:
    std::vector<RadialEntry> entries;

    RadialGradient(DOMElement* parent)
        : FillStyle(parent)
    {}

    Type type() const override { return Type::RadialGradient; }

    std::string domTypeName() const override { return "RadialGradient"; }

    DOMType domType() const override { return DOMType::RadialGradient; }

    static DOMType staticDomType() { return DOMType::RadialGradient; }
};

} // namespace fla
