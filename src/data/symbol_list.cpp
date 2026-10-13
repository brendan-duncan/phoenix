#include "symbol_list.h"

namespace fla {
SymbolList::~SymbolList()
{
    for (Symbol* symbol : symbols)
    {
        delete symbol;
    }
    symbolMap.clear();
}

void SymbolList::link(Symbol* symbol)
{
    if (symbol)
        symbolMap[symbol->name] = symbol;
}

void SymbolList::unlink(Symbol* symbol, const std::string& name)
{
    const auto found = symbolMap.find(name);
    if (found == symbolMap.end() || found->second != symbol)
        return;
    symbolMap.erase(found);
    for (Symbol* other : symbols)
        if (other && other != symbol && other->name == name)
        {
            symbolMap[name] = other;
            break;
        }
}

} // namespace fla
