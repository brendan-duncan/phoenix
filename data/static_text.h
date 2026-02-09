#pragma once

#include "element.h"
#include <string>
#include <vector>

struct TextRun
{
    std::string text;

    bool aliasText = false;
    double lineHeight = 0.0;
    double size = 0.0;
    double bitmapSize = 0.0;
    std::string face;
    uint8_t fillColor[4] = {0, 0, 0, 255}; // RGBA
};

class StaticText : public Element
{
public:
    std::vector<TextRun> runs;

    StaticText() = default;

    Element::Type elementType() const override { return Element::Type::StaticText; }

    std::string domTypeName() const override { return "StaticText"; }
};
