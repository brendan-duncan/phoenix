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

    double weight = 1.0;
    std::string scaleMode;

    FillStyle* fill = nullptr;

    StrokeStyle(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~StrokeStyle() override;

    virtual Style style() const = 0;
};

class SolidStroke : public StrokeStyle
{
public:
    SolidStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "SolidStroke"; }

    DOMType domType() const override { return DOMType::SolidStroke; }

    static DOMType staticDomType() { return DOMType::SolidStroke; }

    Style style() const override { return Style::Solid; }
};

class DashedStroke : public StrokeStyle
{
public:
    DashedStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "DashedStroke"; }

    DOMType domType() const override { return DOMType::DashedStroke; }

    static DOMType staticDomType() { return DOMType::DashedStroke; }

    Style style() const override { return Style::Dashed; }
};

class RaggedStroke : public StrokeStyle
{
public:
    RaggedStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "RaggedStroke"; }

    DOMType domType() const override { return DOMType::RaggedStroke; }

    static DOMType staticDomType() { return DOMType::RaggedStroke; }

    Style style() const override { return Style::Ragged; }
};

class StippleStroke : public StrokeStyle
{
public:
    StippleStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "StippleStroke"; }

    DOMType domType() const override { return DOMType::StippleStroke; }

    static DOMType staticDomType() { return DOMType::StippleStroke; }

    Style style() const override { return Style::Stipple; }
};

class DottedStroke : public StrokeStyle
{
public:
    DottedStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "DottedStroke"; }

    DOMType domType() const override { return DOMType::DottedStroke; }

    static DOMType staticDomType() { return DOMType::DottedStroke; }

    Style style() const override { return Style::Dotted; }
};

} // namespace fla
