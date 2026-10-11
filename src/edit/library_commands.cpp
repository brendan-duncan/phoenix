#include "library_commands.h"

#include "timeline_edit.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <functional>

namespace fla {

namespace library {

namespace {

void collect(Element* element, const Symbol* symbol, std::vector<SymbolInstance*>& found)
{
    if (!element)
        return;
    if (element->elementType() == Element::Type::SymbolInstance)
    {
        SymbolInstance* instance = static_cast<SymbolInstance*>(element);
        if (instance->symbol == symbol)
            found.push_back(instance);
    }
    else if (element->elementType() == Element::Type::Group)
    {
        for (Element* member : static_cast<Group*>(element)->members)
            collect(member, symbol, found);
    }
}

void collectTimeline(Timeline* timeline, const Symbol* symbol, std::vector<SymbolInstance*>& found)
{
    if (!timeline)
        return;
    for (Layer* layer : timeline->layers)
    {
        if (!layer)
            continue;
        for (Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            for (Element* element : frame->elements)
                collect(element, symbol, found);
        }
    }
}

} // namespace

std::vector<SymbolInstance*> instancesOf(Document& document, const Symbol* symbol)
{
    std::vector<SymbolInstance*> found;
    for (Timeline* timeline : document.timelines)
        collectTimeline(timeline, symbol, found);
    if (document.symbolList)
    {
        for (Symbol* other : document.symbolList->symbols)
        {
            if (!other)
                continue;
            for (Timeline* timeline : other->timelines)
                collectTimeline(timeline, symbol, found);
        }
    }
    return found;
}

bool nameIsFree(const Document& document, const std::string& name)
{
    if (name.empty())
        return false;
    if (document.symbolList && document.symbolList->symbolMap.count(name))
        return false;
    for (const Folder* folder : document.folders)
        if (folder && folder->name == name)
            return false;
    for (const Resource* resource : document.resources)
        if (resource && resource->resourceType() == Resource::Type::Bitmap &&
            static_cast<const Bitmap*>(resource)->name == name)
            return false;
    return true;
}

std::string freeCopyName(const Document& document, const std::string& base)
{
    std::string name = base + " copy";
    for (int n = 2; !nameIsFree(document, name); ++n)
        name = base + " copy " + std::to_string(n);
    return name;
}

Symbol* cloneSymbol(const Symbol& symbol, const std::string& name)
{
    Symbol* copy = new Symbol(nullptr);
    copy->name = name;
    // The file it is written to follows the name: library paths become
    // folders under LIBRARY.
    copy->href = name + ".xml";
    copy->symbolType = symbol.symbolType;
    copy->scale9 = symbol.scale9;
    copy->scaleGridLeft = symbol.scaleGridLeft;
    copy->scaleGridTop = symbol.scaleGridTop;
    copy->scaleGridRight = symbol.scaleGridRight;
    copy->scaleGridBottom = symbol.scaleGridBottom;
    for (const Timeline* timeline : symbol.timelines)
    {
        if (!timeline)
            continue;
        Timeline* timelineCopy = new Timeline(copy);
        timelineCopy->name = name;
        timelineCopy->layerDepthEnabled = timeline->layerDepthEnabled;
        timelineCopy->guidesX = timeline->guidesX;
        timelineCopy->guidesY = timeline->guidesY;
        timelineCopy->extraAttributes = timeline->extraAttributes;
        for (const Layer* layer : timeline->layers)
        {
            if (!layer)
                continue;
            Layer* layerCopy = new Layer(timelineCopy);
            layerCopy->name = layer->name;
            for (int i = 0; i < 4; ++i)
                layerCopy->color[i] = layer->color[i];
            layerCopy->current = layer->current;
            layerCopy->layerType = layer->layerType;
            layerCopy->parentLayerIndex = layer->parentLayerIndex;
            layerCopy->autoNamed = layer->autoNamed;
            layerCopy->locked = layer->locked;
            layerCopy->outline = layer->outline;
            layerCopy->visible = layer->visible;
            layerCopy->firstFrame = layer->firstFrame;
            layerCopy->lastFrame = layer->lastFrame;
            for (const Frame* frame : layer->frames)
            {
                if (frame)
                    layerCopy->frames.push_back(timeline::cloneFrame(*frame, layerCopy));
            }
            timelineCopy->layers.push_back(layerCopy);
        }
        // Folder links point at the copied folders.
        for (Layer* layerCopy : timelineCopy->layers)
        {
            const int parent = layerCopy->parentLayerIndex;
            layerCopy->parentLayer = parent >= 0 && parent < static_cast<int>(timelineCopy->layers.size())
                ? timelineCopy->layers[parent] : nullptr;
        }
        copy->timelines.push_back(timelineCopy);
    }
    return copy;
}

} // namespace library

// ---------------------------------------------------------------------------

RenameSymbolCommand::RenameSymbolCommand(Document* document, Symbol* symbol, const std::string& name)
    : _document(document)
    , _symbol(symbol)
    , _before(symbol ? symbol->name : std::string())
    , _after(name)
{
}

void RenameSymbolCommand::apply(const std::string& from, const std::string& to)
{
    if (!_document || !_symbol || !_document->symbolList)
        return;
    SymbolList* list = _document->symbolList;
    list->symbolMap.erase(from);
    list->symbolMap[to] = _symbol;
    _symbol->name = to;
    for (SymbolInstance* instance : library::instancesOf(*_document, _symbol))
        instance->libraryItemName = to;
}

void RenameSymbolCommand::redo()
{
    apply(_before, _after);
}

void RenameSymbolCommand::undo()
{
    apply(_after, _before);
}

AddSymbolCommand::AddSymbolCommand(Document* document, Symbol* symbol, const std::string& name)
    : _document(document)
    , _symbol(symbol)
    , _name(name)
{
}

AddSymbolCommand::~AddSymbolCommand()
{
    if (_owned)
        delete _symbol;
}

void AddSymbolCommand::redo()
{
    if (!_document || !_symbol)
        return;
    if (!_document->symbolList)
        _document->symbolList = new SymbolList(_document);
    _symbol->parent = _document->symbolList;
    _document->symbolList->symbols.push_back(_symbol);
    _document->symbolList->symbolMap[_symbol->name] = _symbol;
    _owned = false;
}

void AddSymbolCommand::undo()
{
    if (!_document || !_symbol || !_document->symbolList)
        return;
    std::vector<Symbol*>& symbols = _document->symbolList->symbols;
    symbols.erase(std::remove(symbols.begin(), symbols.end(), _symbol), symbols.end());
    _document->symbolList->symbolMap.erase(_symbol->name);
    _owned = true;
}

RemoveSymbolCommand::RemoveSymbolCommand(Document* document, Symbol* symbol)
    : _document(document)
    , _symbol(symbol)
{
}

RemoveSymbolCommand::~RemoveSymbolCommand()
{
    if (_owned)
        delete _symbol;
}

void RemoveSymbolCommand::redo()
{
    if (!_document || !_symbol || !_document->symbolList)
        return;
    std::vector<Symbol*>& symbols = _document->symbolList->symbols;
    const auto at = std::find(symbols.begin(), symbols.end(), _symbol);
    if (at == symbols.end())
        return;
    _index = static_cast<size_t>(at - symbols.begin());
    symbols.erase(at);
    _document->symbolList->symbolMap.erase(_symbol->name);
    _owned = true;
}

void RemoveSymbolCommand::undo()
{
    if (!_document || !_symbol || !_document->symbolList || !_owned)
        return;
    std::vector<Symbol*>& symbols = _document->symbolList->symbols;
    symbols.insert(symbols.begin() + static_cast<long>(std::min(_index, symbols.size())), _symbol);
    _document->symbolList->symbolMap[_symbol->name] = _symbol;
    _owned = false;
}

AddFolderCommand::AddFolderCommand(Document* document, const std::string& name)
    : _document(document)
    , _folder(new Folder(document))
{
    _folder->name = name;
}

AddFolderCommand::~AddFolderCommand()
{
    if (_owned)
        delete _folder;
}

void AddFolderCommand::redo()
{
    if (!_document)
        return;
    _document->folders.push_back(_folder);
    _owned = false;
}

void AddFolderCommand::undo()
{
    if (!_document)
        return;
    std::vector<Folder*>& folders = _document->folders;
    folders.erase(std::remove(folders.begin(), folders.end(), _folder), folders.end());
    _owned = true;
}

} // namespace fla
