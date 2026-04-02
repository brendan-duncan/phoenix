#pragma once

#include "element.h"
#include <string>

namespace fla {

class BitmapInstance : public Element
{
public:
    std::string libraryItemName;

    BitmapInstance(DOMElement* parent)
        : Element(parent)
    {}

    Element::Type elementType() const override { return Element::Type::BitmapInstance; }

    std::string domTypeName() const override { return "BitmapInstance"; }

    DOMType domType() const override { return DOMType::BitmapInstance; }

    static DOMType staticDomType() { return DOMType::BitmapInstance; }
};

} // namespace fla
