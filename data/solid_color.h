#pragma once

#include "fill_style.h"
#include <cstdint>

class SolidColor : public FillStyle
{
public:
    uint8_t color[4];

    SolidColor();

    Type type() const override { return Type::SolidColor; }

    std::string domTypeName() const override { return "SolidColor"; }
};
