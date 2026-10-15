#include "linked_assets.h"

#include "element_clone.h"
#include "library_commands.h"

#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>

namespace fla {

namespace links {

namespace {

const char* const kFile = "sourceFlashFilepath";
const char* const kItem = "sourceLibraryItemHRef";
const char* const kAuto = "sourceAutoUpdate";

std::string attribute(const Symbol& symbol, const char* name)
{
    for (const auto& [key, value] : symbol.extraAttributes)
        if (key == name)
            return value;
    return std::string();
}

void setAttribute(Symbol& symbol, const char* name, const std::string& value)
{
    for (auto& [key, existing] : symbol.extraAttributes)
        if (key == name)
        {
            existing = value;
            return;
        }
    symbol.extraAttributes.emplace_back(name, value);
}

/// Every element of \a symbol's timelines, inside groups too.
void forEachElement(const Symbol& symbol, const std::function<void(Element&)>& visit)
{
    std::function<void(Element*)> walk = [&](Element* element) {
        if (!element)
            return;
        visit(*element);
        if (element->elementType() == Element::Type::Group)
            for (Element* member : static_cast<Group*>(element)->members)
                walk(member);
    };
    for (const Timeline* timeline : symbol.timelines)
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (Element* element : frame ? frame->elements : std::vector<Element*>())
                    walk(element);
}

/// The symbols and bitmaps \a symbol uses, directly or through the
/// symbols it uses, by name.
void dependencies(const Document& master, const Symbol& symbol, std::set<std::string>& symbols,
    std::set<std::string>& bitmaps)
{
    forEachElement(symbol, [&](Element& element) {
        if (element.elementType() == Element::Type::SymbolInstance)
        {
            const std::string& name = static_cast<SymbolInstance&>(element).libraryItemName;
            if (!symbols.insert(name).second || !master.symbolList)
                return;
            const auto found = master.symbolList->symbolMap.find(name);
            if (found != master.symbolList->symbolMap.end() && found->second)
                dependencies(master, *found->second, symbols, bitmaps);
        }
        else if (element.elementType() == Element::Type::BitmapInstance)
        {
            bitmaps.insert(static_cast<BitmapInstance&>(element).libraryItemName);
        }
        else if (element.elementType() == Element::Type::Shape)
        {
            const Shape& shape = static_cast<Shape&>(element);
            for (const FillStyle* fill : shape.fills)
                if (fill && fill->type() == FillStyle::Type::BitmapFill)
                    bitmaps.insert(static_cast<const BitmapFill*>(fill)->bitmapPath);
            for (const StrokeStyle* stroke : shape.strokes)
                if (stroke && stroke->fill && stroke->fill->type() == FillStyle::Type::BitmapFill)
                    bitmaps.insert(static_cast<const BitmapFill*>(stroke->fill)->bitmapPath);
        }
    });
}

/// \a into takes \a from's contents: its timelines (moved), type and
/// guides. \a into keeps its name, file and the attributes it had.
void replaceContents(Symbol& into, Symbol& from)
{
    for (Timeline* timeline : into.timelines)
        delete timeline;
    into.timelines = std::move(from.timelines);
    from.timelines.clear();
    for (Timeline* timeline : into.timelines)
    {
        timeline->parent = &into;
        timeline->name = into.name;
    }
    into.symbolType = from.symbolType;
    into.otherSymbolType = from.otherSymbolType;
    into.scale9 = from.scale9;
    into.scaleGridLeft = from.scaleGridLeft;
    into.scaleGridTop = from.scaleGridTop;
    into.scaleGridRight = from.scaleGridRight;
    into.scaleGridBottom = from.scaleGridBottom;
}

} // namespace

bool linkOf(const Symbol& symbol, Link& link)
{
    link.master = attribute(symbol, kFile);
    link.item = attribute(symbol, kItem);
    link.autoUpdate = attribute(symbol, kAuto) == "true";
    if (link.item.empty())
        link.item = symbol.name;
    return !link.master.empty();
}

void setLink(Symbol& symbol, const Link& link)
{
    setAttribute(symbol, kFile, link.master);
    setAttribute(symbol, kItem, link.item.empty() ? symbol.name : link.item);
    setAttribute(symbol, kAuto, link.autoUpdate ? "true" : "false");
}

void clearLink(Symbol& symbol)
{
    auto& attributes = symbol.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) {
                             return pair.first == kFile || pair.first == kItem || pair.first == kAuto ||
                                    pair.first == "sourceLastModified";
                         }),
        attributes.end());
}

std::vector<std::string> linkedSymbols(const Document& document)
{
    std::vector<std::string> names;
    if (!document.symbolList)
        return names;
    for (const Symbol* symbol : document.symbolList->symbols)
    {
        Link link;
        if (symbol && linkOf(*symbol, link))
            names.push_back(symbol->name);
    }
    return names;
}

std::string resolve(const std::string& master, const std::string& shotFile)
{
    namespace fs = std::filesystem;
    // The FLA stores the path with forward slashes; it may be absolute.
    std::string path = master;
    if (path.compare(0, 8, "file:///") == 0)
        path = path.substr(8);
    const fs::path given = fs::u8path(path);
    if (given.is_absolute() || shotFile.empty())
        return given.u8string();
    return (fs::u8path(shotFile).parent_path() / given).lexically_normal().u8string();
}

std::vector<std::string> update(Document& target, const std::string& name, const Document& master,
    const std::string& item, std::string& error)
{
    std::vector<std::string> changed;
    const Symbol* source = nullptr;
    if (master.symbolList)
    {
        const auto found = master.symbolList->symbolMap.find(item);
        if (found != master.symbolList->symbolMap.end())
            source = found->second;
    }
    if (!source)
    {
        error = "The master file has no library item " + item;
        return changed;
    }
    if (!target.symbolList)
        target.symbolList = new SymbolList(&target);
    SymbolList& library = *target.symbolList;

    std::set<std::string> usedSymbols;
    std::set<std::string> usedBitmaps;
    dependencies(master, *source, usedSymbols, usedBitmaps);

    // The symbols, the linked one under its own name and what it uses
    // under theirs: an existing one is refilled in place, so instances of
    // it elsewhere stay right; a missing one is added.
    std::vector<std::pair<std::string, const Symbol*>> copies = {{name, source}};
    for (const std::string& dependency : usedSymbols)
    {
        const auto found = master.symbolList->symbolMap.find(dependency);
        if (found != master.symbolList->symbolMap.end() && found->second && dependency != name)
            copies.emplace_back(dependency, found->second);
    }
    std::vector<Symbol*> touched;
    for (const auto& [targetName, from] : copies)
    {
        Symbol* copy = library::cloneSymbol(*from, targetName);
        const auto existing = library.symbolMap.find(targetName);
        if (existing != library.symbolMap.end() && existing->second)
        {
            replaceContents(*existing->second, *copy);
            delete copy;
            touched.push_back(existing->second);
        }
        else
        {
            copy->parent = &library;
            // A symbol added along with a rig is not linked itself.
            clearLink(*copy);
            library.symbols.push_back(copy);
            library.symbolMap[targetName] = copy;
            touched.push_back(copy);
        }
        changed.push_back(targetName);
    }
    // Instances in what was copied point at this document's symbols.
    for (Symbol* symbol : touched)
        forEachElement(*symbol, [&library](Element& element) { relinkSymbols(element, &library); });

    // Bitmaps it uses that this document lacks come too; one of the same
    // name is taken to be the same picture.
    for (const std::string& bitmapName : usedBitmaps)
    {
        const bool present = std::any_of(target.resources.begin(), target.resources.end(), [&](const Resource* resource) {
            return resource && resource->resourceType() == Resource::Type::Bitmap &&
                   static_cast<const Bitmap*>(resource)->name == bitmapName;
        });
        if (present)
            continue;
        for (const Resource* resource : master.resources)
        {
            if (resource && resource->resourceType() == Resource::Type::Bitmap &&
                static_cast<const Bitmap*>(resource)->name == bitmapName)
            {
                Bitmap* copy = new Bitmap(*static_cast<const Bitmap*>(resource));
                copy->parent = &target;
                target.resources.push_back(copy);
                changed.push_back(bitmapName);
                break;
            }
        }
    }
    return changed;
}

} // namespace links

} // namespace fla
