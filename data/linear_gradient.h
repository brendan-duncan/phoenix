#pragma once

#include "fill_style.h"
#include <cstdint>
#include <vector>

namespace fla {

struct GradientEntry
{
    double ratio;
    uint8_t color[4];
};

class LinearGradient : public FillStyle
{
public:
    std::vector<GradientEntry> entries;

    LinearGradient();

    Type type() const override { return Type::LinearGradient; }

    std::string domTypeName() const override { return "LinearGradient"; }
};

} // namespace fla
