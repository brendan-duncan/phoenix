#include "document_import.h"

#include "library_commands.h"
#include "library_media.h"
#include "library_transfer.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"
#include "../parser/fla_parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>

namespace fla {
namespace documentImport {

namespace {

std::string extensionOf(const std::string& path)
{
    std::string extension = std::filesystem::u8path(path).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

/// The file's name without its extension, for the main timeline's symbol.
std::string stemOf(const std::string& path)
{
    const auto stem = std::filesystem::u8path(path).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

} // namespace

bool handles(const std::string& path)
{
    const std::string extension = extensionOf(path);
    return extension == ".fla" || extension == ".xfl" || extension == ".swf";
}

bool read(Document& target, const std::string& path, Result& result, std::string& error, const std::string& symbolName)
{
    FLAParser parser;
    std::unique_ptr<FLADocument> file(parser.parse(path));
    if (!file || !file->document)
    {
        error = parser.errorString().empty() ? stemOf(path) + " could not be read." : parser.errorString();
        return false;
    }
    result.notes = parser.warningString();
    Document& source = *file->document;

    // Its library, everything in it.
    std::vector<std::string> items;
    if (source.symbolList)
    {
        for (const Symbol* symbol : source.symbolList->symbols)
        {
            if (symbol)
                items.push_back(symbol->name);
        }
    }
    for (const Resource* resource : source.resources)
    {
        if (const auto* bitmap = dynamic_cast<const Bitmap*>(resource))
            items.push_back(bitmap->name);
    }
    for (const Document::SoundInfo& sound : source.sounds)
        items.push_back(sound.name);

    // Its main timeline, made a movie clip in the source's own library so
    // it travels with what it uses.
    std::string mainName;
    Timeline* main = source.timelines.empty() ? nullptr : source.timelines.front();
    if (main && !main->layers.empty())
    {
        const std::string base = symbolName.empty() ? stemOf(path) : symbolName;
        mainName = base.empty() ? std::string("Imported") : base;
        for (int n = 2; !library::nameIsFree(source, mainName); ++n)
            mainName = base + " " + std::to_string(n);
        if (!source.symbolList)
            source.symbolList = new SymbolList(&source);
        auto* symbol = new Symbol(source.symbolList);
        symbol->name = mainName;
        symbol->href = mainName;
        symbol->itemId = media::newItemId();
        symbol->symbolType = SymbolType::MovieClip;
        source.timelines.erase(source.timelines.begin());
        main->parent = symbol;
        main->name = mainName;
        symbol->timelines.push_back(main);
        source.symbolList->symbols.push_back(symbol);
        source.symbolList->link(symbol);
        items.insert(items.begin(), mainName);
    }
    if (items.empty())
    {
        error = stemOf(path) + " has nothing to import.";
        return false;
    }

    std::vector<std::string> added;
    std::map<std::string, std::string> renamed;
    result.command = transfer::copyItems(target, source, items, transfer::Resolution::KeepBoth, &added, &renamed);
    if (!result.command)
    {
        error = stemOf(path) + " has nothing to import.";
        return false;
    }
    result.items = added;
    if (!mainName.empty())
        result.symbol = renamed.count(mainName) ? renamed[mainName] : mainName;
    return true;
}

} // namespace documentImport
} // namespace fla
