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
    int firstFrame = 0;
    SymbolType symbolType = SymbolType::Graphic;
    LoopType loopType = LoopType::PlayOnce;

    SymbolInstance() = default;

    Element::Type elementType() const override { return Element::Type::SymbolInstance; }

    std::string domTypeName() const override { return "SymbolInstance"; }
};

} // namespace fla
