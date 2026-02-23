#pragma once

#include "element.h"
#include "point.h"
#include "color_transform.h"
#include <string>


namespace fla {

class SymbolInstance : public Element
{
public:
    std::string libraryItemName;
    int firstFrame = 0;
    SymbolType symbolType = SymbolType::Graphic;
    LoopType loopType = LoopType::PlayOnce;
    ColorTransform colorTransform;

    SymbolInstance(DOMElement* parent)
        : Element(parent)
    {}

    Element::Type elementType() const override { return Element::Type::SymbolInstance; }

    std::string domTypeName() const override { return "SymbolInstance"; }

    DOMType domType() const override { return DOMType::SymbolInstance; }
};

} // namespace fla
