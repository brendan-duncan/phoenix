#pragma once

#include "dom_element.h"

#include <map>
#include <string>
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
        Dotted,
        Hatched
    };

    double weight = 1.0;
    std::string scaleMode;

    /// "hairline" for a stroke that is always one device pixel wide whatever
    /// the zoom or the transform. Animate writes it with no weight attribute.
    std::string solidStyle;

    /// Cap and join styles as the file spells them: "none", "round" or
    /// "square" for caps; "miter", "round" or "bevel" for joints. Empty means
    /// Flash's default, which is round for both.
    std::string caps;
    std::string joints;
    double miterLimit = 3.0;
    bool pixelHinting = false;

    /// Dash and gap lengths of a DashedStroke, in pixels.
    double dash1 = 6.0;
    double dash2 = 6.0;

    /// Gap between the dots of a DottedStroke, in pixels.
    double dotSpace = 3.0;

    /// The settings of a ragged, stipple or hatched stroke, by attribute
    /// name as the file spells them ("waveHeight" = "wavy", "dotSize" =
    /// "small", "hatchThickness" = "thick"...), kept so they draw and are
    /// written back as read.
    std::map<std::string, std::string> styleAttributes;

    FillStyle* fill = nullptr;

    bool isHairline() const { return solidStyle == "hairline"; }

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

class HatchedStroke : public StrokeStyle
{
public:
    HatchedStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "HatchedStroke"; }

    DOMType domType() const override { return DOMType::HatchedStroke; }

    static DOMType staticDomType() { return DOMType::HatchedStroke; }

    Style style() const override { return Style::Hatched; }
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
