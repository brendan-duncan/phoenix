// Library and its items: symbols, bitmaps, sounds, videos and folders,
// found by their names with folder paths, as the library shows them.

#include "flfile.h"
#include "extensions.h"
#include "model.h"

#include "../data/bitmap.h"
#include "../data/bitmap_decoder.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../edit/element_commands.h"
#include "../edit/layer_commands.h"
#include "../edit/library_commands.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <cstdint>
#include <memory>

namespace jsfl {

namespace {

/// Takes an empty library folder out, keeping it while it is out.
class RemoveFolderCommand : public fla::Command
{
public:
    RemoveFolderCommand(fla::Document* document, fla::Folder* folder)
        : _document(document)
        , _folder(folder)
    {}
    ~RemoveFolderCommand() override
    {
        if (_owned)
            delete _folder;
    }
    RemoveFolderCommand(const RemoveFolderCommand&) = delete;
    RemoveFolderCommand& operator=(const RemoveFolderCommand&) = delete;
    void redo() override
    {
        auto& folders = _document->folders;
        const auto found = std::find(folders.begin(), folders.end(), _folder);
        if (found == folders.end())
            return;
        _index = static_cast<size_t>(found - folders.begin());
        folders.erase(found);
        _owned = true;
    }
    void undo() override
    {
        auto& folders = _document->folders;
        folders.insert(folders.begin() + static_cast<std::ptrdiff_t>(std::min(_index, folders.size())), _folder);
        _owned = false;
    }
    std::string name() const override { return "Delete Folder"; }

private:
    fla::Document* _document;
    fla::Folder* _folder;
    size_t _index = 0;
    bool _owned = false;
};

std::string leafOf(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string folderOf(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

/// Every item, folders first, as library.items lists them.
std::vector<std::shared_ptr<Item>> allItems(Session& session, fla::FLADocument* document)
{
    fla::Document& data = session.data(document);
    std::vector<std::shared_ptr<Item>> items;
    for (fla::Folder* folder : data.folders)
    {
        if (folder)
            items.push_back(std::make_shared<Item>(session, document, Item::Kind::Folder, folder, folder->name));
    }
    if (data.symbolList)
    {
        for (fla::Symbol* symbol : data.symbolList->symbols)
        {
            if (symbol)
                items.push_back(std::make_shared<Item>(session, document, Item::Kind::Symbol, symbol, symbol->name));
        }
    }
    for (fla::Resource* resource : data.resources)
    {
        if (auto* bitmap = dynamic_cast<fla::Bitmap*>(resource))
            items.push_back(std::make_shared<Item>(session, document, Item::Kind::Bitmap, bitmap, bitmap->name));
    }
    for (const auto& sound : data.sounds)
        items.push_back(std::make_shared<Item>(session, document, Item::Kind::Sound, nullptr, sound.name));
    for (const auto& video : data.videos)
        items.push_back(std::make_shared<Item>(session, document, Item::Kind::Video, nullptr, video.name));
    return items;
}

std::string attribute(const fla::DOMElement& object, const std::string& key)
{
    for (const auto& entry : object.extraAttributes)
    {
        if (entry.first == key)
            return entry.second;
    }
    return {};
}

void setAttribute(DocumentPart& part, fla::DOMElement& object, const std::string& key, const std::string& value)
{
    std::vector<std::pair<std::string, std::string>> after = object.extraAttributes;
    auto found = std::find_if(after.begin(), after.end(), [&key](const auto& entry) { return entry.first == key; });
    if (value.empty())
    {
        if (found == after.end())
            return;
        after.erase(found);
    }
    else if (found != after.end())
        found->second = value;
    else
        after.emplace_back(key, value);
    part.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                  object.extraAttributes, object.extraAttributes, after, key)),
        false);
}

/// The symbol or bitmap the linkage lives on.
fla::DOMElement& linkageHolder(Item& item)
{
    if (fla::Symbol* symbol = item.symbol())
        return *symbol;
    if (fla::Bitmap* bitmap = item.bitmap())
        return *bitmap;
    throw Error("Only symbols and bitmaps have linkage.");
}

/// The items library methods act on: the one named, else the selected.
std::vector<std::shared_ptr<Item>> itemsFor(Library& library, const Args& args, size_t at)
{
    std::vector<std::shared_ptr<Item>> items;
    if (given(args, at))
    {
        auto item = findItem(library.session, library.document, arg(args, at).toString());
        if (item)
            items.push_back(item);
        return items;
    }
    for (const std::string& name : library.session.librarySelection(library.document))
    {
        if (auto item = findItem(library.session, library.document, name))
            items.push_back(item);
    }
    return items;
}

void makeFolders(Library& library, const std::string& path)
{
    if (path.empty() || fla::library::isFolder(library.data(), path))
        return;
    makeFolders(library, folderOf(path));
    library.push(fla::CommandPtr(new fla::AddFolderCommand(&library.data(), path)));
}

bool rename(DocumentPart& part, const std::string& from, const std::string& to)
{
    if (from == to)
        return true;
    std::string problem;
    const fla::library::Renames renames = fla::library::pathRenames(part.data(), from, to, &problem);
    if (renames.empty())
        throw Error(problem.empty() ? "Nothing in the library is called " + from + "." : problem);
    part.push(fla::CommandPtr(new fla::RenameLibraryCommand(&part.data(), renames, "Rename")));
    return true;
}

void deleteItem(Library& library, Item& item)
{
    fla::Document& data = library.data();
    library.session.host().selection(library.document).clear();
    switch (item.kind)
    {
    case Item::Kind::Symbol:
        library.push(fla::CommandPtr(new fla::DeleteSymbolCommand(&data, item.symbol())));
        break;
    case Item::Kind::Bitmap:
    {
        const std::map<std::string, int> uses = fla::library::useCounts(data);
        const auto found = uses.find(item.bitmap()->name);
        if (found != uses.end() && found->second > 0)
            throw Error("The bitmap " + item.bitmap()->name + " is still used; delete what uses it first.");
        library.push(fla::CommandPtr(new fla::RemoveBitmapCommand(&data, item.bitmap())));
        break;
    }
    case Item::Kind::Folder:
    {
        // What is inside goes first, deepest first.
        const std::string prefix = item.folder()->name + "/";
        std::vector<std::shared_ptr<Item>> inside;
        for (const auto& other : allItems(library.session, library.document))
        {
            if (other->itemName.compare(0, prefix.size(), prefix) == 0)
                inside.push_back(other);
        }
        std::sort(inside.begin(), inside.end(), [](const auto& a, const auto& b) {
            return std::count(a->itemName.begin(), a->itemName.end(), '/') > std::count(b->itemName.begin(), b->itemName.end(), '/');
        });
        for (const auto& other : inside)
        {
            if (other->kind == Item::Kind::Folder)
                library.push(fla::CommandPtr(new RemoveFolderCommand(&data, other->folder())));
            else
                deleteItem(library, *other);
        }
        library.push(fla::CommandPtr(new RemoveFolderCommand(&data, item.folder())));
        break;
    }
    default:
        throw Error("Phoenix cannot delete sounds and videos from scripts yet.");
    }
}

/// A new, empty symbol of one layer and one frame.
fla::Symbol* newSymbol(fla::Document& document, const std::string& name, fla::SymbolType type)
{
    auto* symbol = new fla::Symbol(document.symbolList);
    symbol->name = name;
    symbol->symbolType = type;
    symbol->href = name + ".xml";
    auto* timeline = new fla::Timeline(symbol);
    timeline->name = leafOf(name);
    fla::Layer* layer = fla::newLayerFor(*timeline, false);
    timeline->layers.push_back(layer);
    symbol->timelines.push_back(timeline);
    return symbol;
}

const char* symbolTypeName(fla::SymbolType type)
{
    switch (type)
    {
    case fla::SymbolType::MovieClip: return "movie clip";
    case fla::SymbolType::Button: return "button";
    default: return "graphic";
    }
}

} // namespace

fla::Symbol* Item::symbol() const
{
    if (kind != Kind::Symbol)
        return nullptr;
    fla::Document& document = data();
    auto* found = static_cast<fla::Symbol*>(pointer);
    if (!document.symbolList ||
        std::find(document.symbolList->symbols.begin(), document.symbolList->symbols.end(), found) == document.symbolList->symbols.end())
        throw Error("The library item " + itemName + " is no longer in the library.");
    return found;
}

fla::Bitmap* Item::bitmap() const
{
    if (kind != Kind::Bitmap)
        return nullptr;
    fla::Document& document = data();
    auto* found = static_cast<fla::Bitmap*>(pointer);
    if (std::find(document.resources.begin(), document.resources.end(), static_cast<fla::Resource*>(found)) == document.resources.end())
        throw Error("The library item " + itemName + " is no longer in the library.");
    return found;
}

fla::Folder* Item::folder() const
{
    if (kind != Kind::Folder)
        return nullptr;
    fla::Document& document = data();
    auto* found = static_cast<fla::Folder*>(pointer);
    if (std::find(document.folders.begin(), document.folders.end(), found) == document.folders.end())
        throw Error("The library folder " + itemName + " is no longer in the library.");
    return found;
}

std::string Item::name() const
{
    switch (kind)
    {
    case Kind::Symbol: return symbol()->name;
    case Kind::Bitmap: return bitmap()->name;
    case Kind::Folder: return folder()->name;
    case Kind::Sound:
        for (const auto& sound : data().sounds)
        {
            if (sound.name == itemName)
                return itemName;
        }
        break;
    case Kind::Video:
        for (const auto& video : data().videos)
        {
            if (video.name == itemName)
                return itemName;
        }
        break;
    }
    throw Error("The library item " + itemName + " is no longer in the library.");
}

std::shared_ptr<Item> findItem(Session& session, fla::FLADocument* document, const std::string& name)
{
    for (auto& item : allItems(session, document))
    {
        if (item->itemName == name)
            return item;
    }
    return nullptr;
}

std::shared_ptr<Item> wrapSymbol(Session& session, fla::FLADocument* document, fla::Symbol* symbol)
{
    return symbol ? std::make_shared<Item>(session, document, Item::Kind::Symbol, symbol, symbol->name) : nullptr;
}

void placeItem(Session& session, fla::FLADocument* document, const Item& item, double x, double y)
{
    fla::Frame* frame = currentKeyframe(session, document);
    fla::Element* element = nullptr;
    if (fla::Symbol* symbol = item.symbol())
    {
        fla::Timeline* timeline = session.host().editedTimeline(document);
        if (timeline && fla::symbolContains(symbol, dynamic_cast<fla::Symbol*>(timeline->parent)))
            throw Error("A symbol cannot be placed inside itself.");
        auto* instance = new fla::SymbolInstance(frame);
        instance->libraryItemName = symbol->name;
        instance->symbol = symbol;
        instance->symbolType = symbol->symbolType;
        instance->loopType = fla::LoopType::Loop;
        element = instance;
    }
    else if (fla::Bitmap* bitmap = item.bitmap())
    {
        auto* instance = new fla::BitmapInstance(frame);
        instance->libraryItemName = bitmap->name;
        element = instance;
    }
    else
        throw Error("Only symbols and bitmaps can be placed on the stage.");
    element->transform.tx = x;
    element->transform.ty = y;
    fla::Selection& selection = session.host().selection(document);
    session.push(document, fla::CommandPtr(new fla::AddElementCommand(frame, element, "Add Instance", &selection)));
    // Centered on the point, as the library places it.
    const Rect box = session.host().bounds(document, element);
    if (box.valid())
        setTransform(session, document, element,
            compose(fla::Transform::fromTranslate(x - (box.left + box.right) / 2, y - (box.top + box.bottom) / 2), element->transform),
            "Add Instance");
    selection.select(element);
}

const ClassInfo& Library::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Library> def(info, "Library");
        def.get("items", [](Library& l) {
            Value list = Value::array();
            for (auto& item : allItems(l.session, l.document))
                list.items.emplace_back(item);
            return list;
        });
        def.method("getSelectedItems", 0, [](Library& l, const Args&) {
            Value list = Value::array();
            for (const std::string& name : l.session.librarySelection(l.document))
            {
                if (auto item = findItem(l.session, l.document, name))
                    list.items.emplace_back(item);
            }
            return list;
        });
        def.method("selectItem", 3, [](Library& l, const Args& args) {
            const std::string name = arg(args, 0).toString();
            if (!findItem(l.session, l.document, name))
                return Value(false);
            std::vector<std::string>& selected = l.session.librarySelection(l.document);
            if (!given(args, 1) || arg(args, 1).toBool())
                selected.clear();
            const bool select = !given(args, 2) || arg(args, 2).toBool();
            selected.erase(std::remove(selected.begin(), selected.end(), name), selected.end());
            if (select)
                selected.push_back(name);
            return Value(true);
        });
        def.method("selectAll", 1, [](Library& l, const Args& args) {
            std::vector<std::string>& selected = l.session.librarySelection(l.document);
            selected.clear();
            if (!given(args, 0) || arg(args, 0).toBool())
            {
                for (auto& item : allItems(l.session, l.document))
                    selected.push_back(item->itemName);
            }
            return Value();
        });
        def.method("selectNone", 0, [](Library& l, const Args&) {
            l.data();
            l.session.librarySelection(l.document).clear();
            return Value();
        });
        def.method("findItemIndex", 1, [](Library& l, const Args& args) {
            const std::string name = arg(args, 0).toString();
            const auto items = allItems(l.session, l.document);
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (items[i]->itemName == name)
                    return Value(static_cast<int>(i));
            }
            // As scripts test for it: an empty string when there is none.
            return Value("");
        });
        def.method("itemExists", 1, [](Library& l, const Args& args) {
            return Value(findItem(l.session, l.document, arg(args, 0).toString()) != nullptr);
        });
        def.method("getItemType", 1, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, args, 0);
            if (items.empty())
                return Value();
            Item& item = *items.front();
            for (const Property& property : item.classInfo().properties)
            {
                if (property.name == "itemType")
                    return property.get(item);
            }
            return Value();
        });
        def.method("addNewItem", 2, [](Library& l, const Args& args) {
            fla::Document& data = l.data();
            const std::string type = arg(args, 0).toString();
            std::string name = given(args, 1) ? arg(args, 1).toString() : std::string();
            if (type == "folder")
            {
                if (name.empty())
                {
                    for (int n = 1; name.empty() || !fla::library::nameIsFree(data, name); ++n)
                        name = "untitled folder " + std::to_string(n);
                }
                if (!fla::library::nameIsFree(data, name))
                    return Value(false);
                makeFolders(l, name);
                return Value(true);
            }
            if (type != "movie clip" && type != "graphic" && type != "button")
                throw Error("Phoenix makes movie clips, graphics, buttons and folders here, not " + type + ".");
            if (name.empty())
            {
                for (int n = 1; name.empty() || !fla::library::nameIsFree(data, name); ++n)
                    name = "Symbol " + std::to_string(n);
            }
            if (!fla::library::nameIsFree(data, name))
                return Value(false);
            if (!data.symbolList)
                data.symbolList = new fla::SymbolList(&data);
            makeFolders(l, folderOf(name));
            const fla::SymbolType symbolType = type == "movie clip" ? fla::SymbolType::MovieClip
                : type == "button"                               ? fla::SymbolType::Button
                                                                 : fla::SymbolType::Graphic;
            l.push(fla::CommandPtr(new fla::AddSymbolCommand(&data, newSymbol(data, name, symbolType), "New Symbol")));
            l.session.librarySelection(l.document) = {name};
            return Value(true);
        });
        def.method("newFolder", 1, [](Library& l, const Args& args) {
            const fla::Document& data = l.data();
            std::string name = given(args, 0) ? arg(args, 0).toString() : std::string();
            if (!name.empty() && !fla::library::nameIsFree(data, name))
                return Value(false);
            if (name.empty())
            {
                for (int n = 1; name.empty() || !fla::library::nameIsFree(data, name); ++n)
                    name = "untitled folder " + std::to_string(n);
            }
            makeFolders(l, name);
            return Value(true);
        });
        def.method("deleteItem", 1, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, args, 0);
            if (items.empty())
                return Value(false);
            for (const auto& item : items)
                deleteItem(l, *item);
            l.session.librarySelection(l.document).clear();
            return Value(true);
        });
        def.method("renameItem", 1, [](Library& l, const Args& args) {
            const std::vector<std::string>& selected = l.session.librarySelection(l.document);
            if (selected.size() != 1)
                throw Error("Select one library item to rename.");
            const std::string from = selected.front();
            const std::string folder = folderOf(from);
            const std::string to = (folder.empty() ? std::string() : folder + "/") + arg(args, 0).toString();
            if (!fla::library::nameIsFree(l.data(), to))
                return Value(false);
            rename(l, from, to);
            l.session.librarySelection(l.document) = {to};
            return Value(true);
        });
        def.method("moveToFolder", 3, [](Library& l, const Args& args) {
            const std::string folder = arg(args, 0).isNullish() ? std::string() : arg(args, 0).toString();
            const auto items = itemsFor(l, args, 1);
            if (items.empty())
                return Value(false);
            makeFolders(l, folder);
            for (const auto& item : items)
            {
                const std::string from = item->name();
                const std::string to = (folder.empty() ? std::string() : folder + "/") + leafOf(from);
                if (from != to && !fla::library::nameIsFree(l.data(), to))
                {
                    // An item of that name already there is replaced only
                    // when asked.
                    if (!given(args, 2) || !arg(args, 2).toBool())
                        return Value(false);
                    deleteItem(l, *findItem(l.session, l.document, to));
                }
                rename(l, from, to);
            }
            return Value(true);
        });
        def.method("duplicateItem", 1, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, args, 0);
            if (items.size() != 1 || !items.front()->symbol())
                return Value(false);
            fla::Symbol* symbol = items.front()->symbol();
            const std::string name = fla::library::freeCopyName(l.data(), symbol->name);
            fla::Symbol* copy = fla::library::cloneSymbol(*symbol, name);
            l.push(fla::CommandPtr(new fla::AddSymbolCommand(&l.data(), copy, "Duplicate Symbol")));
            l.session.librarySelection(l.document) = {name};
            return Value(true);
        });
        def.method("editItem", 1, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, args, 0);
            if (items.empty() || !items.front()->symbol() || items.front()->symbol()->timelines.empty())
                return Value(false);
            l.session.host().editTimeline(l.document, items.front()->symbol()->timelines.front());
            return Value(true);
        });
        def.method("addItemToDocument", 2, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, args, 1);
            if (items.empty())
                return Value(false);
            const Value* x = arg(args, 0).field("x");
            const Value* y = arg(args, 0).field("y");
            placeItem(l.session, l.document, *items.front(), x ? x->toNumber() : 0.0, y ? y->toNumber() : 0.0);
            return Value(true);
        });
        def.method("getItemProperty", 1, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, Args(), 0);
            if (items.empty())
                throw Error("Select a library item first.");
            for (const ClassInfo* info = &items.front()->classInfo(); info; info = info->parent)
            {
                for (const Property& property : info->properties)
                {
                    if (property.name == arg(args, 0).toString())
                        return property.get(*items.front());
                }
            }
            return Value();
        });
        def.method("setItemProperty", 2, [](Library& l, const Args& args) {
            const auto items = itemsFor(l, Args(), 0);
            if (items.empty())
                throw Error("Select a library item first.");
            for (const auto& item : items)
            {
                bool done = false;
                for (const ClassInfo* info = &item->classInfo(); info && !done; info = info->parent)
                {
                    for (const Property& property : info->properties)
                    {
                        if (property.name == arg(args, 0).toString() && property.set)
                        {
                            property.set(*item, arg(args, 1));
                            done = true;
                            break;
                        }
                    }
                }
            }
            return Value();
        });
        def.method("unusedItems", 0, [](Library& l, const Args&) {
            const std::set<std::string> used = fla::library::usedItems(l.data());
            Value list = Value::array();
            for (auto& item : allItems(l.session, l.document))
            {
                if (item->kind != Item::Kind::Folder && !used.count(item->itemName))
                    list.items.emplace_back(item);
            }
            return list;
        });
        def.method("updateItem", 1, [](Library&, const Args&) { return Value(true); });
        def.method("expandFolder", 3, [](Library&, const Args&) { return Value(true); });
        return info;
    }();
    return info;
}

namespace {

struct ItemClasses
{
    ClassInfo item;
    ClassInfo symbol;
    ClassInfo bitmap;
    ClassInfo sound;
    ClassInfo video;
    ClassInfo folder;
};

void describeItem(ClassDef<Item>& def)
{
    def.prop("name", [](Item& i) { return Value(i.name()); },
        [](Item& i, const Value& v) {
            const std::string from = i.name();
            std::string to = v.toString();
            // A bare name renames it where it is.
            if (to.find('/') == std::string::npos && !folderOf(from).empty())
                to = folderOf(from) + "/" + to;
            if (i.kind == Item::Kind::Sound || i.kind == Item::Kind::Video)
                throw Error("Phoenix cannot rename sounds and videos from scripts yet.");
            rename(i, from, to);
            i.itemName = to;
        });
    def.get("itemType", [](Item& i) {
        switch (i.kind)
        {
        case Item::Kind::Symbol: return Value(symbolTypeName(i.symbol()->symbolType));
        case Item::Kind::Bitmap: i.bitmap(); return Value("bitmap");
        case Item::Kind::Sound: return Value("sound");
        case Item::Kind::Video: return Value("video");
        default: i.folder(); return Value("folder");
        }
    });
    for (const char* key : {"linkageClassName", "linkageBaseClass", "linkageIdentifier", "linkageURL"})
    {
        const std::string name = key;
        def.prop(key, [name](Item& i) { return Value(attribute(linkageHolder(i), name)); },
            [name](Item& i, const Value& v) { setAttribute(i, linkageHolder(i), name, v.isNullish() ? std::string() : v.toString()); });
    }
    for (const char* key : {"linkageExportForAS", "linkageExportForRS", "linkageImportForRS", "linkageExportInFirstFrame"})
    {
        const std::string name = key;
        def.prop(key,
            [name](Item& i) {
                const std::string value = attribute(linkageHolder(i), name);
                if (value.empty() && name == "linkageExportInFirstFrame")
                    return Value(attribute(linkageHolder(i), "linkageExportForAS") == "true");
                return Value(value == "true");
            },
            [name](Item& i, const Value& v) { setAttribute(i, linkageHolder(i), name, v.toBool() ? "true" : std::string()); });
    }
    describeItemData(def);
}

void describeSymbol(ClassDef<Item>& def)
{
    def.get("timeline", [](Item& i) {
        fla::Symbol* symbol = i.symbol();
        return symbol->timelines.empty() ? Value() : Value(wrapTimeline(i.session, i.document, symbol->timelines.front()));
    });
    def.prop("symbolType", [](Item& i) { return Value(symbolTypeName(i.symbol()->symbolType)); },
        [](Item& i, const Value& v) {
            fla::Symbol* symbol = i.symbol();
            const std::string type = v.toString();
            if (type != "movie clip" && type != "graphic" && type != "button")
                throw Error("A symbol is a movie clip, button or graphic, not " + type + ".");
            const fla::SymbolType after = type == "movie clip" ? fla::SymbolType::MovieClip
                : type == "button"                            ? fla::SymbolType::Button
                                                              : fla::SymbolType::Graphic;
            i.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::SymbolType>(symbol->symbolType, symbol->symbolType, after, "Symbol Type")), false);
        });
    def.prop("scalingGrid", [](Item& i) { return Value(i.symbol()->scale9); },
        [](Item& i, const Value& v) {
            fla::Symbol* symbol = i.symbol();
            i.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(symbol->scale9, symbol->scale9, v.toBool(), "9-Slice Scaling")), false);
        });
}

void describeBitmap(ClassDef<Item>& def)
{
    def.prop("allowSmoothing", [](Item& i) { return Value(i.bitmap()->allowSmoothing); },
        [](Item& i, const Value& v) {
            fla::Bitmap* bitmap = i.bitmap();
            i.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(bitmap->allowSmoothing, bitmap->allowSmoothing, v.toBool(), "Smoothing")), false);
        });
    def.get("compressionType", [](Item& i) { return Value(i.bitmap()->isJPEG ? "photo" : "lossless"); });
    def.get("originalCompressionType", [](Item& i) { return Value(i.bitmap()->isJPEG ? "photo" : "lossless"); });
    def.get("quality", [](Item& i) { return Value(i.bitmap()->quality); });
    def.get("sourceFilePath", [](Item& i) { return Value(i.bitmap()->sourceExternalFilepath); });
    def.get("hPixels", [](Item& i) { return Value(i.bitmap()->frameRight / 20); });
    def.get("vPixels", [](Item& i) { return Value(i.bitmap()->frameBottom / 20); });
    def.method("exportToFile", 2, [](Item& i, const Args& args) {
        // The image as imported (PNG or JPEG), when the library kept it.
        fla::Bitmap* bitmap = i.bitmap();
        const std::vector<uint8_t>* bytes = nullptr;
        if (!bitmap->imageData.empty())
            bytes = &bitmap->imageData.get();
        else if (!bitmap->datData.empty() && fla::isJpegData(bitmap->datData.get()))
            bytes = &bitmap->datData.get();
        if (!bytes)
            throw Error("Phoenix cannot export this bitmap's pixels from a script yet.");
        std::ofstream out(std::filesystem::u8path(uri::toPath(arg(args, 0).toString())), std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        return Value(static_cast<bool>(out));
    });
}

const ItemClasses& itemClasses()
{
    // Built in place: each class points at its parent.
    static ItemClasses c;
    static const bool built = []() {
        ClassDef<Item> item(c.item, "Item");
        describeItem(item);
        ClassDef<Item> symbol(c.symbol, "SymbolItem", &c.item);
        describeSymbol(symbol);
        ClassDef<Item> bitmap(c.bitmap, "BitmapItem", &c.item);
        describeBitmap(bitmap);
        ClassDef<Item> sound(c.sound, "SoundItem", &c.item);
        ClassDef<Item> video(c.video, "VideoItem", &c.item);
        ClassDef<Item> folder(c.folder, "FolderItem", &c.item);
        return true;
    }();
    (void)built;
    return c;
}

} // namespace

const ClassInfo& Item::classInfo() const
{
    const ItemClasses& classes = itemClasses();
    switch (kind)
    {
    case Kind::Symbol: return classes.symbol;
    case Kind::Bitmap: return classes.bitmap;
    case Kind::Sound: return classes.sound;
    case Kind::Video: return classes.video;
    default: return classes.folder;
    }
}

} // namespace jsfl
