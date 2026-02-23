#pragma once

#include "element.h"
#include <string>
#include <vector>

namespace fla {

struct TextRun
{
    enum class Alignment
    {
        Left,
        Center,
        Right
    };

    std::string text;

    Alignment alignment = Alignment::Left;
    bool aliasText = false;
    bool autoKern = false;
    double letterSpacing = 0.0;
    double lineHeight = 0.0;
    double size = 0.0;
    double bitmapSize = 0.0;
    std::string face;
    uint8_t fillColor[4] = {0, 0, 0, 255}; // RGBA
};

class StaticText : public Element
{
public:
    double left = 0.0;
    double top = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool autoExpand = false;

    std::vector<TextRun> runs;

    StaticText(DOMElement* parent)
        : Element(parent)
    {}

    Element::Type elementType() const override { return Element::Type::StaticText; }

    std::string domTypeName() const override { return "StaticText"; }

    DOMType domType() const override { return DOMType::StaticText; }
};

} // namespace fla
