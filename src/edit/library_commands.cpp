#include "library_commands.h"

#include "timeline_edit.h"

#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/shape.h"
#include "../data/stroke_style.h"
#include "../data/document.h"
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/raw_element.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <functional>
#include <map>
#include <string>

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

namespace {

/// Calls \a use with the name of every library item \a element uses,
/// inside groups too.
void usesOf(Element* element, const std::function<void(const std::string&)>& use);

} // namespace

std::map<std::string, int> useCounts(Document& document)
{
    std::map<std::string, int> counts;
    const auto count = [&counts](const std::string& name) {
        if (!name.empty())
            ++counts[name];
    };
    const auto timeline = [&](Timeline* timeline) {
        if (!timeline)
            return;
        for (Layer* layer : timeline->layers)
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                for (Element* element : frame->elements)
                    usesOf(element, count);
                for (const auto& attribute : frame->extraAttributes)
                    if (attribute.first == "soundName")
                        count(attribute.second);
            }
    };
    for (Timeline* t : document.timelines)
        timeline(t);
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                for (Timeline* t : symbol->timelines)
                    timeline(t);
    return counts;
}

std::set<std::string> usedItems(Document& document)
{
    std::set<std::string> used;
    std::vector<Symbol*> pending;
    const auto use = [&](const std::string& name) {
        if (name.empty() || !used.insert(name).second)
            return;
        if (document.symbolList)
        {
            const auto it = document.symbolList->symbolMap.find(name);
            if (it != document.symbolList->symbolMap.end() && it->second)
                pending.push_back(it->second);
        }
    };
    const auto timeline = [&](Timeline* timeline) {
        if (!timeline)
            return;
        for (Layer* layer : timeline->layers)
            for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!frame)
                    continue;
                for (Element* element : frame->elements)
                    usesOf(element, use);
                for (const auto& attribute : frame->extraAttributes)
                    if (attribute.first == "soundName")
                        use(attribute.second);
            }
    };
    for (Timeline* t : document.timelines)
        timeline(t);
    // Code can make what is exported, so it counts as used.
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            if (symbol && !linkageOf(*symbol).empty())
                use(symbol->name);
    while (!pending.empty())
    {
        Symbol* symbol = pending.back();
        pending.pop_back();
        for (Timeline* t : symbol->timelines)
            timeline(t);
    }
    return used;
}

std::string linkageOf(const Symbol& symbol)
{
    std::string className;
    std::string identifier;
    bool exported = false;
    for (const auto& attribute : symbol.extraAttributes)
    {
        if (attribute.first == "linkageClassName")
            className = attribute.second;
        else if (attribute.first == "linkageIdentifier")
            identifier = attribute.second;
        else if (attribute.first == "linkageExportForAS" || attribute.first == "linkageExportForRS")
            exported = exported || attribute.second == "true";
    }
    if (!className.empty())
        return className;
    return exported ? identifier : std::string();
}

std::vector<std::pair<std::string, std::string>> withLinkage(
    std::vector<std::pair<std::string, std::string>> attributes, const std::string& className)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& attribute) {
                             return attribute.first == "linkageClassName" || attribute.first == "linkageExportForAS" ||
                                    attribute.first == "linkageExportInFirstFrame" ||
                                    attribute.first == "linkageBaseClass" || attribute.first == "linkageIdentifier";
                         }),
        attributes.end());
    if (!className.empty())
    {
        attributes.emplace_back("linkageExportForAS", "true");
        attributes.emplace_back("linkageClassName", className);
    }
    return attributes;
}

namespace {

void usesOf(Element* element, const std::function<void(const std::string&)>& use)
{
    if (!element)
        return;
    switch (element->elementType())
    {
    case Element::Type::SymbolInstance:
        use(static_cast<SymbolInstance*>(element)->libraryItemName);
        break;
    case Element::Type::BitmapInstance:
        use(static_cast<BitmapInstance*>(element)->libraryItemName);
        break;
    case Element::Type::Shape:
    {
        Shape* shape = static_cast<Shape*>(element);
        for (FillStyle* fill : shape->fills)
            if (fill && fill->type() == FillStyle::Type::BitmapFill)
                use(static_cast<BitmapFill*>(fill)->bitmapPath);
        for (StrokeStyle* stroke : shape->strokes)
            if (stroke && stroke->fill && stroke->fill->type() == FillStyle::Type::BitmapFill)
                use(static_cast<BitmapFill*>(stroke->fill)->bitmapPath);
        break;
    }
    case Element::Type::Group:
        for (Element* member : static_cast<Group*>(element)->members)
            usesOf(member, use);
        break;
    case Element::Type::Raw:
        use(static_cast<RawElement*>(element)->videoItem);
        break;
    default:
        break;
    }
}

} // namespace

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
    for (const Document::SoundInfo& sound : document.sounds)
        if (sound.name == name)
            return false;
    for (const Document::VideoInfo& video : document.videos)
        if (video.name == name)
            return false;
    return true;
}

bool isFolder(const Document& document, const std::string& name)
{
    for (const Folder* folder : document.folders)
        if (folder && folder->name == name)
            return true;
    return false;
}

namespace {

/// Every name in the library: folders, symbols, bitmaps, sounds and
/// videos.
std::vector<std::string> libraryNames(const Document& document)
{
    std::vector<std::string> names;
    for (const Folder* folder : document.folders)
        if (folder)
            names.push_back(folder->name);
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                names.push_back(symbol->name);
    for (const Resource* resource : document.resources)
        if (resource && resource->resourceType() == Resource::Type::Bitmap)
            names.push_back(static_cast<const Bitmap*>(resource)->name);
    for (const Document::SoundInfo& sound : document.sounds)
        names.push_back(sound.name);
    for (const Document::VideoInfo& video : document.videos)
        names.push_back(video.name);
    return names;
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::string xmlEscaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string xmlUnescaped(const std::string& text)
{
    static const std::pair<const char*, char> entities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    std::string out;
    for (size_t i = 0; i < text.size();)
    {
        bool matched = false;
        if (text[i] == '&')
        {
            for (const auto& entity : entities)
            {
                const size_t length = std::char_traits<char>::length(entity.first);
                if (text.compare(i, length, entity.first) == 0)
                {
                    out += entity.second;
                    i += length;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
            out += text[i++];
    }
    return out;
}

/// Calls \a visit for every element, inside groups too, on every frame of
/// the document's timelines and its symbols' timelines, and \a frameVisit
/// for every frame.
void forEverything(Document& document, const std::function<void(Element*)>& visit,
    const std::function<void(Frame*)>& frameVisit)
{
    std::function<void(Element*)> walk = [&](Element* element) {
        if (!element)
            return;
        visit(element);
        if (element->elementType() == Element::Type::Group)
            for (Element* member : static_cast<Group*>(element)->members)
                walk(member);
    };
    const auto timeline = [&](Timeline* timeline) {
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
                frameVisit(frame);
                for (Element* element : frame->elements)
                    walk(element);
            }
        }
    };
    for (Timeline* t : document.timelines)
        timeline(t);
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            if (symbol)
                for (Timeline* t : symbol->timelines)
                    timeline(t);
}

} // namespace

Renames pathRenames(const Document& document, const std::string& from, const std::string& to,
    std::string* problem)
{
    const auto fail = [problem](const std::string& why) {
        if (problem)
            *problem = why;
        return Renames();
    };
    const std::vector<std::string> names = libraryNames(document);
    if (std::find(names.begin(), names.end(), from) == names.end())
        return fail("There is no library item named " + from + ".");
    if (to.empty() || to.front() == '/' || to.back() == '/' || to.find("//") != std::string::npos)
        return fail("That is not a library name.");
    if (to == from)
        return Renames();
    const bool folder = isFolder(document, from);
    if (folder && startsWith(to, from + "/"))
        return fail("A folder cannot go inside itself.");
    const size_t slash = to.rfind('/');
    if (slash != std::string::npos && !isFolder(document, to.substr(0, slash)))
        return fail("There is no folder named " + to.substr(0, slash) + ".");

    Renames renames;
    renames.emplace_back(from, to);
    if (folder)
    {
        for (const std::string& name : names)
            if (startsWith(name, from + "/"))
                renames.emplace_back(name, to + name.substr(from.size()));
    }

    // Each new name has to be free, unless what has it is moving out of
    // the way in the same change.
    for (const auto& rename : renames)
    {
        const bool taken = std::find(names.begin(), names.end(), rename.second) != names.end();
        const bool vacated = std::find_if(renames.begin(), renames.end(),
            [&rename](const std::pair<std::string, std::string>& other) {
                return other.first == rename.second;
            }) != renames.end();
        if (taken && !vacated)
            return fail("The library already has an item named " + rename.second + ".");
    }
    return renames;
}

void applyRenames(Document& document, const Renames& renames)
{
    if (renames.empty())
        return;
    std::map<std::string, std::string> changed(renames.begin(), renames.end());
    const auto renamed = [&changed](std::string& name) {
        const auto it = changed.find(name);
        if (it == changed.end())
            return false;
        name = it->second;
        return true;
    };

    for (Folder* folder : document.folders)
        if (folder)
            renamed(folder->name);

    if (document.symbolList)
    {
        SymbolList* list = document.symbolList;
        for (Symbol* symbol : list->symbols)
        {
            if (!symbol)
                continue;
            const std::string before = symbol->name;
            if (renamed(symbol->name) && list->symbolMap.count(before) && list->symbolMap[before] == symbol)
                list->symbolMap.erase(before);
        }
        for (Symbol* symbol : list->symbols)
            if (symbol)
                list->symbolMap[symbol->name] = symbol;
    }

    for (Resource* resource : document.resources)
        if (resource && resource->resourceType() == Resource::Type::Bitmap)
            renamed(static_cast<Bitmap*>(resource)->name);

    for (Document::SoundInfo& sound : document.sounds)
        renamed(sound.name);
    for (Document::VideoInfo& video : document.videos)
        renamed(video.name);

    // A sound's or video's library item is kept as XML text; its name is the first
    // name attribute of the element.
    for (std::string& xml : document.extraMedia)
    {
        const size_t tagEnd = xml.find('>');
        const size_t at = xml.find(" name=\"");
        if (at == std::string::npos || at > tagEnd)
            continue;
        const size_t valueStart = at + 7;
        const size_t valueEnd = xml.find('"', valueStart);
        if (valueEnd == std::string::npos)
            continue;
        std::string name = xmlUnescaped(xml.substr(valueStart, valueEnd - valueStart));
        if (renamed(name))
            xml.replace(valueStart, valueEnd - valueStart, xmlEscaped(name));
    }

    forEverything(document,
        [&](Element* element) {
            if (element->elementType() == Element::Type::SymbolInstance)
            {
                renamed(static_cast<SymbolInstance*>(element)->libraryItemName);
            }
            else if (element->elementType() == Element::Type::BitmapInstance)
            {
                renamed(static_cast<BitmapInstance*>(element)->libraryItemName);
            }
            else if (element->elementType() == Element::Type::Raw)
            {
                // A video on the stage, kept as the XML it is written back as.
                RawElement* raw = static_cast<RawElement*>(element);
                if (raw->videoItem.empty() || !renamed(raw->videoItem))
                    return;
                const std::string key = "libraryItemName=\"";
                const size_t at = raw->xml.find(key);
                const size_t end = at == std::string::npos ? at : raw->xml.find('"', at + key.size());
                if (end != std::string::npos)
                    raw->xml.replace(at + key.size(), end - at - key.size(), xmlEscaped(raw->videoItem));
            }
            else if (element->elementType() == Element::Type::Shape)
            {
                Shape* shape = static_cast<Shape*>(element);
                for (FillStyle* fill : shape->fills)
                    if (fill && fill->type() == FillStyle::Type::BitmapFill)
                        renamed(static_cast<BitmapFill*>(fill)->bitmapPath);
                for (StrokeStyle* stroke : shape->strokes)
                    if (stroke && stroke->fill && stroke->fill->type() == FillStyle::Type::BitmapFill)
                        renamed(static_cast<BitmapFill*>(stroke->fill)->bitmapPath);
            }
        },
        [&](Frame* frame) {
            for (auto& attribute : frame->extraAttributes)
                if (attribute.first == "soundName")
                    renamed(attribute.second);
        });
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
    copy->otherSymbolType = symbol.otherSymbolType;
    copy->extraAttributes = symbol.extraAttributes;
    copy->presentAttributes = symbol.presentAttributes;
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
        timelineCopy->extraChildren = timeline->extraChildren;
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
            layerCopy->otherLayerType = layer->otherLayerType;
            layerCopy->extraAttributes = layer->extraAttributes;
            layerCopy->presentAttributes = layer->presentAttributes;
            layerCopy->extraChildren = layer->extraChildren;
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
    _symbol->name = to;
    list->unlink(_symbol, from);
    list->link(_symbol);
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
    _document->symbolList->link(_symbol);
    _owned = false;
}

void AddSymbolCommand::undo()
{
    if (!_document || !_symbol || !_document->symbolList)
        return;
    std::vector<Symbol*>& symbols = _document->symbolList->symbols;
    symbols.erase(std::remove(symbols.begin(), symbols.end(), _symbol), symbols.end());
    _document->symbolList->unlink(_symbol, _symbol->name);
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
    _document->symbolList->unlink(_symbol, _symbol->name);
    _owned = true;
}

void RemoveSymbolCommand::undo()
{
    if (!_document || !_symbol || !_document->symbolList || !_owned)
        return;
    std::vector<Symbol*>& symbols = _document->symbolList->symbols;
    symbols.insert(symbols.begin() + static_cast<long>(std::min(_index, symbols.size())), _symbol);
    _document->symbolList->link(_symbol);
    _owned = false;
}

DeleteSymbolCommand::DeleteSymbolCommand(Document* document, Symbol* symbol)
    : _document(document)
    , _remove(document, symbol)
{
}

DeleteSymbolCommand::~DeleteSymbolCommand()
{
    // The instances taken out are this command's while it is done.
    for (const Removed& removed : _removed)
        delete removed.element;
}

void DeleteSymbolCommand::redo()
{
    _removed.clear();
    if (!_document)
        return;
    std::function<void(std::vector<Element*>&)> sweep = [&](std::vector<Element*>& list) {
        for (size_t i = 0; i < list.size();)
        {
            Element* element = list[i];
            if (element && element->elementType() == Element::Type::SymbolInstance &&
                static_cast<SymbolInstance*>(element)->symbol == _remove.symbol())
            {
                _removed.push_back({&list, i, element});
                list.erase(list.begin() + static_cast<long>(i));
                continue;
            }
            if (element && element->elementType() == Element::Type::Group)
                sweep(static_cast<Group*>(element)->members);
            ++i;
        }
    };
    const auto timeline = [&](Timeline* timeline) {
        if (!timeline)
            return;
        for (Layer* layer : timeline->layers)
            if (layer)
                for (Frame* frame : layer->frames)
                    if (frame)
                        sweep(frame->elements);
    };
    for (Timeline* t : _document->timelines)
        timeline(t);
    if (_document->symbolList)
        for (Symbol* other : _document->symbolList->symbols)
            if (other && other != _remove.symbol())
                for (Timeline* t : other->timelines)
                    timeline(t);
    _remove.redo();
}

void DeleteSymbolCommand::undo()
{
    _remove.undo();
    // Back where each was, the last taken out first.
    for (auto it = _removed.rbegin(); it != _removed.rend(); ++it)
        it->list->insert(it->list->begin() + static_cast<long>(std::min(it->index, it->list->size())), it->element);
    _removed.clear();
}

RemoveBitmapCommand::RemoveBitmapCommand(Document* document, Bitmap* bitmap)
    : _document(document)
    , _bitmap(bitmap)
{
}

RemoveBitmapCommand::~RemoveBitmapCommand()
{
    if (_removed)
        delete _bitmap;
}

void RemoveBitmapCommand::redo()
{
    if (!_document || !_bitmap)
        return;
    std::vector<Resource*>& resources = _document->resources;
    const auto at = std::find(resources.begin(), resources.end(), _bitmap);
    if (at == resources.end())
        return;
    _index = static_cast<size_t>(at - resources.begin());
    resources.erase(at);
    _removed = true;
}

void RemoveBitmapCommand::undo()
{
    if (!_document || !_bitmap || !_removed)
        return;
    std::vector<Resource*>& resources = _document->resources;
    resources.insert(resources.begin() + static_cast<long>(std::min(_index, resources.size())), _bitmap);
    _removed = false;
}

RenameLibraryCommand::RenameLibraryCommand(Document* document, library::Renames renames, const std::string& name)
    : _document(document)
    , _renames(std::move(renames))
    , _name(name)
{
    for (const auto& rename : _renames)
        _inverse.emplace_back(rename.second, rename.first);
}

void RenameLibraryCommand::redo()
{
    if (_document)
        library::applyRenames(*_document, _renames);
}

void RenameLibraryCommand::undo()
{
    if (_document)
        library::applyRenames(*_document, _inverse);
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
