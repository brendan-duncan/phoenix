#pragma once

#include "dom_element.h"
#include "symbol.h"
#include <map>
#include <vector>

namespace fla {
class SymbolList : public DOMElement
{
public:
    std::vector<Symbol*> symbols;

    std::map<std::string, Symbol*> symbolMap; // For quick lookup of symbols by name

    SymbolList(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~SymbolList() override;

    std::string domTypeName() const override { return "SymbolList"; }

    DOMType domType() const override { return DOMType::SymbolList; }
};

} // namespace fla
