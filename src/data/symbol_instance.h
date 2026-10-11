#pragma once

#include "element.h"
#include "point.h"
#include "color_transform.h"
#include "filter.h"
#include <string>
#include <vector>

namespace fla {

class Symbol;

class SymbolInstance : public Element
{
public:
    std::string libraryItemName;
    /// The instance name scripts address it by; movie clips and buttons.
    std::string name;
    int firstFrame = 0;
    SymbolType symbolType = SymbolType::Graphic;
    LoopType loopType = LoopType::PlayOnce;
    ColorTransform colorTransform;

    /// How the instance is composited over what is under it, as the file
    /// names it: normal, layer, multiply, screen, lighten, darken,
    /// difference, add, subtract, invert, alpha, erase, overlay, hardlight.
    /// Empty is normal.
    std::string blendMode;

    /// A symbol type the reader does not model ("puppet", for a rigged
    /// asset), kept as spelled so it is written back; the instance draws as
    /// a graphic meanwhile.
    std::string otherSymbolType;

    /// The filters applied to the instance, in order.
    std::vector<Filter> filters;

    Symbol* symbol = nullptr; // Resolved symbol reference after parsing

    SymbolInstance(DOMElement* parent)
        : Element(parent)
    {}

    Element::Type elementType() const override { return Element::Type::SymbolInstance; }

    std::string domTypeName() const override { return "SymbolInstance"; }

    DOMType domType() const override { return DOMType::SymbolInstance; }

    static DOMType staticDomType() { return DOMType::SymbolInstance; }
};

} // namespace fla
