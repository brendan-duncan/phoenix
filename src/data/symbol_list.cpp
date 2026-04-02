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

} // namespace fla
