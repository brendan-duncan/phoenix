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

    /// Makes \a symbol the one its name finds.
    void link(Symbol* symbol);

    /// Takes \a symbol's entry under \a name out of symbolMap, but only when
    /// that entry is this symbol: two symbols can share a name for a moment
    /// (a file with duplicates, an undo putting one back), and removing one
    /// must not cut the other off. The name then finds another symbol still
    /// in the list that has it, if any.
    void unlink(Symbol* symbol, const std::string& name);

    std::string domTypeName() const override { return "SymbolList"; }

    DOMType domType() const override { return DOMType::SymbolList; }

    static DOMType staticDomType() { return DOMType::SymbolList; }
};

} // namespace fla
