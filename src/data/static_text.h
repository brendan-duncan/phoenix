#pragma once

#include "element.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fla {

struct TextRun
{
    enum class Alignment
    {
        Left,
        Center,
        Right,
        Justify
    };

    std::string text;

    Alignment alignment = Alignment::Left;
    bool aliasText = false;
    /// On unless the file says otherwise: FLA files carry autoKern only when
    /// it is off.
    bool autoKern = true;
    double letterSpacing = 0.0;
    double lineHeight = 0.0;
    double size = 0.0;
    double bitmapSize = 0.0;
    std::string face;
    uint8_t fillColor[4] = {0, 0, 0, 255}; // RGBA

    /// DOMTextAttrs attributes as read that Phoenix does not model (bold,
    /// italic, lineSpacing, url...), and the names of the modeled ones the
    /// file wrote, as DOMElement keeps them.
    std::vector<std::pair<std::string, std::string>> extraAttributes;
    std::vector<std::string> presentAttributes;
    /// Unknown .phx fields of the run, kept (see DOMElement::phxUnknown).
    std::string phxUnknown;
};

class StaticText : public Element
{
public:
    double left = 0.0;
    double top = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool autoExpand = false;

    /// The three kinds of text field. A static field is artwork; a
    /// dynamic one can be set from a script, an input one typed into, and
    /// both are written as DOMDynamicText and DOMInputText.
    enum class TextType
    {
        Static,
        Dynamic,
        Input
    };
    TextType textType = TextType::Static;

    /// For dynamic and input text: the name a script reaches the field by,
    /// and the line type the FLA records (single line, multiline, multiline
    /// no wrap, password), carried through as written.
    std::string instanceName;
    std::string lineType;

    std::vector<TextRun> runs;

    StaticText(DOMElement* parent)
        : Element(parent)
    {}

    Element::Type elementType() const override { return Element::Type::StaticText; }

    std::string domTypeName() const override { return "StaticText"; }

    DOMType domType() const override { return DOMType::StaticText; }

    static DOMType staticDomType() { return DOMType::StaticText; }
};

} // namespace fla
