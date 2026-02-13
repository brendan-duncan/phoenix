#pragma once

#include "element.h"
#include "point.h"
#include <string>
#include "transform.h"

namespace fla {

class SymbolInstance : public Element
{
public:
    std::string libraryItemName;

    SymbolInstance() = default;

    Element::Type elementType() const override { return Element::Type::SymbolInstance; }

    std::string domTypeName() const override { return "SymbolInstance"; }
};

} // namespace fla
