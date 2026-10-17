#pragma once

#include "dom_element.h"

#include <map>
#include <string>
#include <vector>
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
        Hatched,
        PaintBrush
    };

    double weight = 1.0;
    std::string scaleMode;

    /// "hairline" for a stroke that is always one device pixel wide whatever
    /// the zoom or the transform. The FLA stores it with no weight attribute.
    std::string solidStyle;

    /// Cap and join styles as the file spells them: "none", "round" or
    /// "square" for caps; "miter", "round" or "bevel" for joints. Empty means
    /// the FLA default, which is round for both.
    std::string caps;
    std::string joints;
    double miterLimit = 3.0;
    bool pixelHinting = false;

    /// Dash and gap lengths of a DashedStroke, in pixels.
    double dash1 = 6.0;
    double dash2 = 6.0;

    /// Gap between the dots of a DottedStroke, in pixels.
    double dotSpace = 3.0;

    /// The document's paint brush a PaintBrushStroke draws with: its index
    /// among the definitions in PaintBrushDefinitions.xml; 0 for none.
    int paintBrushDefinition = 0;

    /// The settings of a ragged, stipple or hatched stroke, by attribute
    /// name as the file spells them ("waveHeight" = "wavy", "dotSize" =
    /// "small", "hatchThickness" = "thick"...), kept so they draw and are
    /// written back as read.
    std::map<std::string, std::string> styleAttributes;

    /// Child elements other than the fill, kept as XML text and written
    /// back: a variable-width stroke's VariablePointWidth profile, say.
    std::vector<std::string> extraChildren;

    /// A variable-width stroke's profile, read from its VariablePointWidth:
    /// points along the stroke (0 at its start, 1 at its end), each with
    /// how far the stroke reaches to its left and to its right there, as a
    /// fraction of the weight (a half and a half is the plain weight). A
    /// corner point is where the width changes course sharply. Empty for a
    /// stroke of even width.
    struct WidthMarker
    {
        double position = 0.0;
        double left = 0.5;
        double right = 0.5;
        bool corner = false;

        bool operator==(const WidthMarker& other) const
        {
            return position == other.position && left == other.left && right == other.right && corner == other.corner;
        }
        bool operator!=(const WidthMarker& other) const { return !(*this == other); }
    };
    std::vector<WidthMarker> widthProfile;

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

/// A stroke drawn with one of the document's paint brushes: its art
/// stretched along the line, or its tile repeated along it.
class PaintBrushStroke : public StrokeStyle
{
public:
    PaintBrushStroke(DOMElement* parent)
        : StrokeStyle(parent)
    {}

    std::string domTypeName() const override { return "PaintBrushStroke"; }

    DOMType domType() const override { return DOMType::PaintBrushStroke; }

    static DOMType staticDomType() { return DOMType::PaintBrushStroke; }

    Style style() const override { return Style::PaintBrush; }
};

} // namespace fla
