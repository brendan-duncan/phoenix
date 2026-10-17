#include "library_transfer.h"

#include "element_clone.h"
#include "library_commands.h"
#include "library_media.h"
#include "paint_brushes.h"

#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/folder.h"
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
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <cstdint>

namespace fla {

namespace transfer {

namespace {

/// \a text as an XML attribute value.
std::string escaped(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
    }
    return out;
}

const Symbol* symbolIn(const Document& document, const std::string& name)
{
    if (!document.symbolList)
        return nullptr;
    const auto found = document.symbolList->symbolMap.find(name);
    return found == document.symbolList->symbolMap.end() ? nullptr : found->second;
}

const Bitmap* bitmapIn(const Document& document, const std::string& name)
{
    for (const Resource* resource : document.resources)
        if (resource && resource->resourceType() == Resource::Type::Bitmap &&
            static_cast<const Bitmap*>(resource)->name == name)
            return static_cast<const Bitmap*>(resource);
    return nullptr;
}

const Document::SoundInfo* soundIn(const Document& document, const std::string& name)
{
    for (const Document::SoundInfo& sound : document.sounds)
        if (sound.name == name)
            return &sound;
    return nullptr;
}

/// The DOMSoundItem text of the sound \a name, among the document's media.
std::string soundXml(const Document& document, const std::string& name)
{
    const std::string key = "name=\"" + escaped(name) + "\"";
    for (const std::string& xml : document.extraMedia)
        if (xml.find("<DOMSoundItem") != std::string::npos && xml.find(key) != std::string::npos)
            return xml;
    return std::string();
}

std::vector<uint8_t> fileOf(const Document& document, const std::string& path)
{
    for (const auto& file : document.extraFiles)
        if (file.first == path)
            return file.second;
    return {};
}

/// \a e and, for a group, its members, all the way in.
void visitElement(Element* e, const std::function<void(Element&)>& element)
{
    if (!e)
        return;
    element(*e);
    if (e->elementType() == Element::Type::Group)
        for (Element* member : static_cast<Group*>(e)->members)
            visitElement(member, element);
}

/// Every element and frame of \a symbol, inside groups too.
void walk(const Symbol& symbol, const std::function<void(Element&)>& element, const std::function<void(Frame&)>& frame)
{
    for (const Timeline* timeline : symbol.timelines)
        for (Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (Frame* f : layer ? layer->frames : std::vector<Frame*>())
            {
                if (!f)
                    continue;
                frame(*f);
                for (Element* e : f->elements)
                    visitElement(e, element);
            }
}

void bitmapFills(Shape& shape, const std::function<void(std::string&)>& visit)
{
    for (FillStyle* fill : shape.fills)
        if (fill && fill->type() == FillStyle::Type::BitmapFill)
            visit(static_cast<BitmapFill*>(fill)->bitmapPath);
    for (StrokeStyle* stroke : shape.strokes)
        if (stroke && stroke->fill && stroke->fill->type() == FillStyle::Type::BitmapFill)
            visit(static_cast<BitmapFill*>(stroke->fill)->bitmapPath);
}

/// The library names \a element uses itself.
void namesOf(Element& element, std::set<std::string>& names)
{
    switch (element.elementType())
    {
    case Element::Type::SymbolInstance:
        names.insert(static_cast<SymbolInstance&>(element).libraryItemName);
        break;
    case Element::Type::BitmapInstance:
        names.insert(static_cast<BitmapInstance&>(element).libraryItemName);
        break;
    case Element::Type::Shape:
        bitmapFills(static_cast<Shape&>(element), [&](std::string& name) { names.insert(name); });
        break;
    default:
        break;
    }
}

/// What \a element uses, named as \a rename says.
void renameIn(Element& element, const std::function<void(std::string&)>& rename)
{
    if (element.elementType() == Element::Type::SymbolInstance)
        rename(static_cast<SymbolInstance&>(element).libraryItemName);
    else if (element.elementType() == Element::Type::BitmapInstance)
        rename(static_cast<BitmapInstance&>(element).libraryItemName);
    else if (element.elementType() == Element::Type::Shape)
        bitmapFills(static_cast<Shape&>(element), rename);
}

/// Every library name a symbol uses, through the symbols it uses.
void uses(const Document& source, const Symbol& symbol, std::set<std::string>& names)
{
    walk(symbol,
        [&](Element& element) {
            switch (element.elementType())
            {
            case Element::Type::SymbolInstance:
            {
                const std::string& name = static_cast<SymbolInstance&>(element).libraryItemName;
                if (names.insert(name).second)
                    if (const Symbol* nested = symbolIn(source, name))
                        uses(source, *nested, names);
                break;
            }
            case Element::Type::BitmapInstance:
                names.insert(static_cast<BitmapInstance&>(element).libraryItemName);
                break;
            case Element::Type::Shape:
                bitmapFills(static_cast<Shape&>(element), [&](std::string& name) { names.insert(name); });
                break;
            default:
                break;
            }
        },
        [&](Frame& frame) {
            for (const auto& attribute : frame.extraAttributes)
                if (attribute.first == "soundName")
                    names.insert(attribute.second);
        });
}

bool present(const Document& document, const std::string& name)
{
    return symbolIn(document, name) || bitmapIn(document, name) || soundIn(document, name);
}

/// The copy: what it adds, and what it replaces, both ways.
class TransferCommand : public Command
{
public:
    explicit TransferCommand(Document& target)
        : _target(target)
    {
    }

    ~TransferCommand() override
    {
        // Whatever is out of the document belongs to the command.
        if (!_applied)
        {
            for (Symbol* symbol : _symbols)
                delete symbol;
            for (Bitmap* bitmap : _bitmaps)
                delete bitmap;
            for (Folder* folder : _folders)
                delete folder;
        }
        for (auto& swap : _symbolSwaps)
            delete swap.second;
        for (auto& swap : _bitmapSwaps)
            delete swap.second;
    }

    std::vector<Symbol*> _symbols;
    std::vector<Bitmap*> _bitmaps;
    std::vector<Folder*> _folders;
    struct Sound
    {
        std::string xml;
        Document::SoundInfo info;
        std::vector<uint8_t> data;
    };
    std::vector<Sound> _sounds;
    /// A target symbol and a symbol holding the contents it swaps with.
    std::vector<std::pair<Symbol*, Symbol*>> _symbolSwaps;
    std::vector<std::pair<Bitmap*, Bitmap*>> _bitmapSwaps;
    /// The paint brushes the copied symbols draw with that the target
    /// lacked, added with them.
    CommandPtr _brushes;

    void redo() override { apply(true); }
    void undo() override { apply(false); }
    std::string name() const override { return "Copy Library Items"; }

private:
    static void swapContents(Symbol& a, Symbol& b)
    {
        std::swap(a.timelines, b.timelines);
        for (Timeline* timeline : a.timelines)
        {
            timeline->parent = &a;
            timeline->name = a.name;
        }
        for (Timeline* timeline : b.timelines)
            timeline->parent = &b;
        std::swap(a.symbolType, b.symbolType);
        std::swap(a.otherSymbolType, b.otherSymbolType);
        std::swap(a.scale9, b.scale9);
        std::swap(a.scaleGridLeft, b.scaleGridLeft);
        std::swap(a.scaleGridTop, b.scaleGridTop);
        std::swap(a.scaleGridRight, b.scaleGridRight);
        std::swap(a.scaleGridBottom, b.scaleGridBottom);
    }

    static void swapPixels(Bitmap& a, Bitmap& b)
    {
        std::swap(a.imageData, b.imageData);
        std::swap(a.datData, b.datData);
        std::swap(a.frameRight, b.frameRight);
        std::swap(a.frameBottom, b.frameBottom);
        std::swap(a.isJPEG, b.isJPEG);
        std::swap(a.sourceExternalFilepath, b.sourceExternalFilepath);
    }

    void apply(bool add)
    {
        if (add == _applied)
            return;
        _applied = add;
        if (_brushes)
        {
            if (add)
                _brushes->redo();
            else
                _brushes->undo();
        }
        if (!_target.symbolList)
            _target.symbolList = new SymbolList(&_target);
        SymbolList& library = *_target.symbolList;
        for (Symbol* symbol : _symbols)
        {
            if (add)
            {
                symbol->parent = &library;
                library.symbols.push_back(symbol);
                library.symbolMap[symbol->name] = symbol;
            }
            else
            {
                library.symbols.erase(std::remove(library.symbols.begin(), library.symbols.end(), symbol),
                    library.symbols.end());
                library.symbolMap.erase(symbol->name);
            }
        }
        for (Folder* folder : _folders)
        {
            if (add)
            {
                folder->parent = &_target;
                _target.folders.push_back(folder);
            }
            else
            {
                _target.folders.erase(std::remove(_target.folders.begin(), _target.folders.end(), folder),
                    _target.folders.end());
            }
        }
        for (Bitmap* bitmap : _bitmaps)
        {
            if (add)
            {
                bitmap->parent = &_target;
                _target.resources.push_back(bitmap);
            }
            else
            {
                _target.resources.erase(std::remove(_target.resources.begin(), _target.resources.end(), bitmap),
                    _target.resources.end());
            }
        }
        for (const Sound& sound : _sounds)
        {
            if (add)
            {
                _target.extraMedia.push_back(sound.xml);
                _target.sounds.push_back(sound.info);
                _target.extraFiles.emplace_back(sound.info.dataPath, sound.data);
            }
            else
            {
                auto& media = _target.extraMedia;
                media.erase(std::remove(media.begin(), media.end(), sound.xml), media.end());
                auto& sounds = _target.sounds;
                sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
                                 [&](const Document::SoundInfo& s) { return s.name == sound.info.name; }),
                    sounds.end());
                auto& files = _target.extraFiles;
                files.erase(std::remove_if(files.begin(), files.end(),
                                [&](const auto& file) { return file.first == sound.info.dataPath; }),
                    files.end());
            }
        }
        // Replacing is a swap, the same both ways.
        for (auto& [into, held] : _symbolSwaps)
            swapContents(*into, *held);
        for (auto& [into, held] : _bitmapSwaps)
            swapPixels(*into, *held);
        // Instances in the library point at its symbols as they now are.
        for (Symbol* symbol : library.symbols)
            if (symbol)
                walk(*symbol, [&library](Element& element) { relinkSymbols(element, &library); }, [](Frame&) {});
        for (Timeline* timeline : _target.timelines)
            for (Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
                for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                    for (Element* element : frame ? frame->elements : std::vector<Element*>())
                        if (element)
                            relinkSymbols(*element, &library);
    }

    Document& _target;
    bool _applied = false;
};

} // namespace

std::vector<std::string> withDependencies(const Document& source, const std::vector<std::string>& items)
{
    std::set<std::string> names(items.begin(), items.end());
    for (const std::string& item : items)
        if (const Symbol* symbol = symbolIn(source, item))
            uses(source, *symbol, names);
    std::vector<std::string> known;
    for (const std::string& name : names)
        if (present(source, name))
            known.push_back(name);
    return known;
}

std::vector<std::string> conflicts(const Document& target, const Document& source, const std::vector<std::string>& items)
{
    std::vector<std::string> clashes;
    for (const std::string& name : withDependencies(source, items))
        if (present(target, name))
            clashes.push_back(name);
    return clashes;
}

CommandPtr copyItems(Document& target, const Document& source, const std::vector<std::string>& items,
    Resolution resolution, std::vector<std::string>* added, std::map<std::string, std::string>* renamedOut)
{
    const std::vector<std::string> names = withDependencies(source, items);
    if (names.empty())
        return nullptr;
    auto command = std::make_unique<TransferCommand>(target);

    // The name each item has in the target.
    std::map<std::string, std::string> renamed;
    std::set<std::string> taken;
    const auto freeName = [&](const std::string& base) {
        std::string name = library::freeCopyName(target, base);
        for (int n = 2; taken.count(name) || !library::nameIsFree(target, name); ++n)
            name = base + " copy " + std::to_string(n);
        taken.insert(name);
        return name;
    };
    for (const std::string& name : names)
    {
        if (present(target, name) && resolution == Resolution::KeepBoth)
            renamed[name] = freeName(name);
        else
            renamed[name] = name;
    }
    const auto rename = [&renamed](std::string& name) {
        const auto found = renamed.find(name);
        if (found != renamed.end())
            name = found->second;
    };
    // What a copied symbol uses goes by its name in the target.
    const auto renameUses = [&](Symbol& symbol) {
        walk(symbol,
            [&](Element& element) { renameIn(element, rename); },
            [&](Frame& frame) {
                for (auto& attribute : frame.extraAttributes)
                    if (attribute.first == "soundName")
                        rename(attribute.second);
            });
    };

    // A data file name the target has not got, nor this copy yet.
    int dataNumber = 0;
    const auto newData = [&]() {
        const std::string base = media::newDataName(target);
        std::string name;
        do
            name = base.substr(0, base.size() - 4) + " " + std::to_string(++dataNumber) + ".dat";
        while (taken.count(name));
        taken.insert(name);
        return name;
    };

    bool any = false;
    for (const std::string& name : names)
    {
        const std::string& to = renamed[name];
        const bool clash = present(target, name) && resolution != Resolution::KeepBoth;
        if (clash && resolution == Resolution::Skip)
            continue;
        if (const Symbol* symbol = symbolIn(source, name))
        {
            Symbol* copy = library::cloneSymbol(*symbol, to);
            copy->itemId = media::newItemId();
            renameUses(*copy);
            if (clash)
            {
                Symbol* existing = const_cast<Symbol*>(symbolIn(target, name));
                if (!existing)
                {
                    delete copy;
                    continue;
                }
                command->_symbolSwaps.emplace_back(existing, copy);
            }
            else
            {
                command->_symbols.push_back(copy);
            }
            any = true;
        }
        else if (const Bitmap* bitmap = bitmapIn(source, name))
        {
            if (clash)
            {
                Bitmap* existing = const_cast<Bitmap*>(bitmapIn(target, name));
                command->_bitmapSwaps.emplace_back(existing, new Bitmap(*bitmap));
            }
            else
            {
                Bitmap* copy = new Bitmap(*bitmap);
                copy->name = to;
                copy->href = to;
                copy->itemId = media::newItemId();
                copy->bitmapDataHRef = newData();
                command->_bitmaps.push_back(copy);
            }
            any = true;
        }
        else if (const Document::SoundInfo* sound = soundIn(source, name))
        {
            if (clash)
                continue;
            TransferCommand::Sound entry;
            entry.info = *sound;
            entry.info.name = to;
            entry.data = fileOf(source, sound->dataPath);
            const std::string data = newData();
            entry.info.dataPath = "bin/" + data;
            entry.xml = soundXml(source, name);
            // Named and filed as it is in the target.
            const auto replaceAttribute = [&entry](const std::string& key, const std::string& value) {
                const std::string start = key + "=\"";
                const size_t at = entry.xml.find(start);
                if (at == std::string::npos)
                    return;
                const size_t end = entry.xml.find('"', at + start.size());
                entry.xml.replace(at + start.size(), end - at - start.size(), value);
            };
            replaceAttribute("name", escaped(to));
            replaceAttribute("href", escaped(to));
            replaceAttribute("soundDataHRef", data);
            replaceAttribute("itemID", media::newItemId());
            if (entry.xml.empty())
                continue;
            command->_sounds.push_back(entry);
            any = true;
        }
    }
    if (renamedOut)
        *renamedOut = renamed;
    if (!any)
        return nullptr;
    // The paint brushes the copies' strokes draw with come along, each
    // once by its guid, the strokes pointed at them as the target numbers
    // them.
    std::vector<Symbol*> copies = command->_symbols;
    for (const auto& swap : command->_symbolSwaps)
        copies.push_back(swap.second);
    std::vector<const Element*> drawn;
    for (Symbol* copy : copies)
        walk(*copy, [&drawn](Element& element) { drawn.push_back(&element); }, [](Frame&) {});
    const std::vector<PaintBrushDefinition> brushes = paintBrushesFor(source, drawn);
    if (!brushes.empty())
    {
        std::map<int, int> indexes;
        command->_brushes = adoptPaintBrushes(target, brushes, indexes);
        for (Symbol* copy : copies)
            walk(*copy,
                [&indexes](Element& element) {
                    // Groups' members are walked to on their own.
                    if (element.elementType() != Element::Type::Group)
                        remapPaintBrushes(element, indexes);
                },
                [](Frame&) {});
    }
    // The folders what came in sits in, that the target lacks.
    std::set<std::string> folders;
    const auto addFolders = [&](const std::string& name) {
        for (size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1))
            folders.insert(name.substr(0, slash));
    };
    for (const Symbol* symbol : command->_symbols)
        addFolders(symbol->name);
    for (const Bitmap* bitmap : command->_bitmaps)
        addFolders(bitmap->name);
    for (const TransferCommand::Sound& sound : command->_sounds)
        addFolders(sound.info.name);
    for (const std::string& path : folders)
    {
        const bool there = std::any_of(target.folders.begin(), target.folders.end(),
            [&path](const Folder* folder) { return folder && folder->name == path; });
        if (there)
            continue;
        Folder* folder = new Folder(&target);
        folder->name = path;
        folder->itemId = media::newItemId();
        command->_folders.push_back(folder);
    }
    if (added)
        for (const std::string& item : items)
            added->push_back(renamed.count(item) ? renamed[item] : item);
    return command;
}

std::vector<std::string> usedBy(const std::vector<const Element*>& elements)
{
    std::set<std::string> names;
    for (const Element* element : elements)
        visitElement(const_cast<Element*>(element), [&names](Element& e) { namesOf(e, names); });
    names.erase(std::string());
    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> usedByFrames(const std::vector<const Frame*>& frames)
{
    std::set<std::string> names;
    for (const Frame* frame : frames)
    {
        if (!frame)
            continue;
        for (const auto& attribute : frame->extraAttributes)
            if (attribute.first == "soundName")
                names.insert(attribute.second);
        for (Element* element : frame->elements)
            visitElement(element, [&names](Element& e) { namesOf(e, names); });
    }
    names.erase(std::string());
    return std::vector<std::string>(names.begin(), names.end());
}

void renameUses(Element& element, const std::map<std::string, std::string>& names)
{
    const auto rename = [&names](std::string& name) {
        const auto found = names.find(name);
        if (found != names.end())
            name = found->second;
    };
    visitElement(&element, [&rename](Element& e) { renameIn(e, rename); });
}

void renameUses(Frame& frame, const std::map<std::string, std::string>& names)
{
    for (auto& attribute : frame.extraAttributes)
        if (attribute.first == "soundName")
        {
            const auto found = names.find(attribute.second);
            if (found != names.end())
                attribute.second = found->second;
        }
    for (Element* element : frame.elements)
        if (element)
            renameUses(*element, names);
}

} // namespace transfer

} // namespace fla
