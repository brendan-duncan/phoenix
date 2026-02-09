#pragma once

#include "element.h"
#include "point.h"
#include <string>
#include "transform.h"

class SymbolInstance : public Element
{
public:
    std::string libraryItemName;

    SymbolInstance();

    Element::Type elementType() const override { return Element::Type::SymbolInstance; }

    std::string domTypeName() const override { return "SymbolInstance"; }
};
