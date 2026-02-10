#pragma once

#include "dom_element.h"
#include "solid_color.h"
#include "fill_style.h"
#include <string>

namespace fla {

class StrokeStyle : public DOMElement
{
public:
    enum class Style
    {
        Solid,
        Dashed,
        Ragged,
        Stipple,
        Dotted
    };

    double weight;
    std::string scaleMode;

    FillStyle* fill;

    StrokeStyle();

    ~StrokeStyle() override;

    virtual Style style() const = 0;
};

class SolidStroke : public StrokeStyle
{
public:
    SolidStroke();

    std::string domTypeName() const override { return "SolidStroke"; }

    Style style() const override { return Style::Solid; }
};

class DashedStroke : public StrokeStyle
{
public:
    DashedStroke();

    std::string domTypeName() const override { return "DashedStroke"; }

    Style style() const override { return Style::Dashed; }
};

class RaggedStroke : public StrokeStyle
{
public:
    RaggedStroke();

    std::string domTypeName() const override { return "RaggedStroke"; }

    Style style() const override { return Style::Ragged; }
};

class StippleStroke : public StrokeStyle
{
public:
    StippleStroke();

    std::string domTypeName() const override { return "StippleStroke"; }

    Style style() const override { return Style::Stipple; }
};

class DottedStroke : public StrokeStyle
{
public:
    DottedStroke();

    std::string domTypeName() const override { return "DottedStroke"; }

    Style style() const override { return Style::Dotted; }
};

} // namespace fla
