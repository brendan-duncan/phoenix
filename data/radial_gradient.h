#pragma once

#include "fill_style.h"
#include "transform.h"
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
    double focalPointRatio = 0.0; // Range -1.0 to 1.0, where 0 is centered, negative is towards start, positive towards end
    Transform transform;

    RadialGradient(DOMElement* parent)
        : FillStyle(parent)
    {}

    Type type() const override { return Type::RadialGradient; }

    std::string domTypeName() const override { return "RadialGradient"; }

    DOMType domType() const override { return DOMType::RadialGradient; }

    static DOMType staticDomType() { return DOMType::RadialGradient; }
};

} // namespace fla
