#include "symbol.h"

#include "document.h"
#include "frame.h"
#include "group.h"
#include "layer.h"
#include "symbol_instance.h"
#include "symbol_list.h"

#include <algorithm>
#include <functional>
#include <set>
#include <vector>

namespace fla {

namespace {

/// Calls \a visit with every instance \a symbol places directly: on its
/// timelines and inside its groups, but not inside other symbols.
void forEachPlacedInstance(const Symbol& symbol, const std::function<void(SymbolInstance*)>& visit)
{
    std::vector<Element*> pending;
    for (Timeline* timeline : symbol.timelines)
    {
        if (!timeline)
            continue;
        for (Layer* layer : timeline->layers)
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                if (frame)
                    pending.insert(pending.end(), frame->elements.begin(), frame->elements.end());
    }
    while (!pending.empty())
    {
        Element* element = pending.back();
        pending.pop_back();
        if (!element)
            continue;
        if (element->elementType() == Element::Type::SymbolInstance)
            visit(static_cast<SymbolInstance*>(element));
        else if (element->elementType() == Element::Type::Group)
        {
            const Group* group = static_cast<const Group*>(element);
            pending.insert(pending.end(), group->members.begin(), group->members.end());
        }
    }
}

} // namespace

bool symbolContains(const Symbol* outer, const Symbol* target)
{
    if (!outer || !target)
        return false;
    std::set<const Symbol*> visited;
    std::vector<const Symbol*> pending{outer};
    while (!pending.empty())
    {
        const Symbol* symbol = pending.back();
        pending.pop_back();
        if (symbol == target)
            return true;
        if (!visited.insert(symbol).second)
            continue;
        forEachPlacedInstance(*symbol, [&](SymbolInstance* instance) {
            if (instance->symbol && !visited.count(instance->symbol))
                pending.push_back(instance->symbol);
        });
    }
    return false;
}

bool placingNestsSymbol(const Element& element, const Symbol* outer)
{
    if (!outer)
        return false;
    if (element.elementType() == Element::Type::SymbolInstance)
        return symbolContains(static_cast<const SymbolInstance&>(element).symbol, outer);
    if (element.elementType() == Element::Type::Group)
        for (const Element* member : static_cast<const Group&>(element).members)
            if (member && placingNestsSymbol(*member, outer))
                return true;
    return false;
}

int breakSymbolCycles(Document& document)
{
    if (!document.symbolList)
        return 0;
    int cut = 0;
    for (Symbol* symbol : document.symbolList->symbols)
    {
        if (!symbol)
            continue;
        forEachPlacedInstance(*symbol, [&](SymbolInstance* instance) {
            if (!instance->symbol || !symbolContains(instance->symbol, symbol))
                return;
            auto users = document.symbolInstancesMap.find(instance->symbol);
            if (users != document.symbolInstancesMap.end())
                users->second.erase(std::remove(users->second.begin(), users->second.end(), instance),
                    users->second.end());
            instance->symbol = nullptr;
            ++cut;
        });
    }
    return cut;
}

Symbol::~Symbol()
{
    for (Timeline* timeline : timelines)
    {
        delete timeline;
    }
    timelines.clear();
}

} // namespace fla
