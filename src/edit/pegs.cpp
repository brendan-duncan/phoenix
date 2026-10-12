#include "pegs.h"

#include "timeline_edit.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <set>

namespace fla {

namespace pegs {

namespace {

const char* const kMark = "phoenixPeg";

} // namespace

bool isPeg(const Symbol& symbol)
{
    for (const auto& [key, value] : symbol.extraAttributes)
        if (key == kMark)
            return value == "true";
    return false;
}

Symbol* pegSymbol(const SymbolList& library)
{
    for (Symbol* symbol : library.symbols)
        if (symbol && isPeg(*symbol))
            return symbol;
    return nullptr;
}

Symbol* newPegSymbol(const std::string& name)
{
    Symbol* symbol = new Symbol(nullptr);
    symbol->name = name;
    symbol->href = name + ".xml";
    symbol->symbolType = SymbolType::Graphic;
    symbol->extraAttributes.emplace_back(kMark, "true");
    Timeline* timeline = new Timeline(symbol);
    timeline->name = name;
    Layer* layer = new Layer(timeline);
    layer->name = "Layer_1";
    layer->frames.push_back(new Frame(layer));
    timeline->layers.push_back(layer);
    symbol->timelines.push_back(timeline);
    return symbol;
}

Layer* newPegLayer(const Timeline& timeline, Symbol* peg, double x, double y)
{
    std::set<std::string> names;
    int length = 1;
    for (const Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        names.insert(layer->name);
        length = std::max(length, timeline::layerEnd(*layer));
    }
    Layer* layer = new Layer(nullptr);
    for (int n = 1;; ++n)
    {
        layer->name = "Peg " + std::to_string(n);
        if (!names.count(layer->name))
            break;
    }
    // A peg's own color in the timeline and on the stage: a steady orange.
    layer->color[0] = 255;
    layer->color[1] = 128;
    layer->color[2] = 0;
    layer->color[3] = 255;
    Frame* frame = new Frame(layer);
    frame->index = 0;
    frame->duration = length;
    SymbolInstance* instance = new SymbolInstance(frame);
    instance->libraryItemName = peg->name;
    instance->symbol = peg;
    instance->symbolType = SymbolType::Graphic;
    instance->transform.tx = x;
    instance->transform.ty = y;
    frame->elements.push_back(instance);
    layer->frames.push_back(frame);
    return layer;
}

} // namespace pegs

} // namespace fla
