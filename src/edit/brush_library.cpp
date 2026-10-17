#include "brush_library.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace fla {

namespace {

/// \a text as an XML attribute value in quotes of either kind.
std::string escapedAttribute(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char c : text)
    {
        switch (c)
        {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '\'': out += "&apos;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

} // namespace

std::vector<BrushLibraryEntry> readBrushLibrary(const std::string& xml)
{
    std::vector<BrushLibraryEntry> entries;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !document.RootElement())
        return entries;
    for (const tinyxml2::XMLElement* element = document.RootElement()->FirstChildElement(); element;
         element = element->NextSiblingElement())
    {
        if (std::strcmp(element->Name(), "paintbrushdefinition") != 0)
            continue;
        const char* param = element->Attribute("brushParam");
        const char* category = element->Attribute("category");
        BrushLibraryEntry entry;
        entry.definition = paintBrushDefinition(element->IntAttribute("index", 0), param ? param : "");
        entry.category = category && *category ? category : paintBrushCategory(entry.definition);
        if (!entry.definition.json.empty())
            entries.push_back(std::move(entry));
    }
    return entries;
}

std::string writeBrushLibrary(const std::vector<BrushLibraryEntry>& entries)
{
    std::string xml = "<brushlibrary version=\"1\">";
    int index = 0;
    for (const BrushLibraryEntry& entry : entries)
    {
        if (entry.builtIn)
            continue;
        xml += "\n  <paintbrushdefinition index=\"" + std::to_string(++index) + "\" category=\"" +
            escapedAttribute(entry.category) + "\" brushParam='" + escapedAttribute(entry.definition.json) + "'/>";
    }
    return xml + "\n</brushlibrary>\n";
}

int addToBrushLibrary(std::vector<BrushLibraryEntry>& library, const std::vector<PaintBrushDefinition>& definitions,
    const std::string& category)
{
    int added = 0;
    int highest = 0;
    for (const BrushLibraryEntry& entry : library)
        highest = std::max(highest, entry.definition.index);
    for (const PaintBrushDefinition& definition : definitions)
    {
        const std::string guid = definition.guid();
        const bool there = std::any_of(library.begin(), library.end(), [&](const BrushLibraryEntry& entry) {
            return guid.empty() ? entry.definition.json == definition.json : entry.definition.guid() == guid;
        });
        if (there)
            continue;
        BrushLibraryEntry entry;
        entry.definition = definition;
        entry.definition.index = ++highest;
        entry.category = category.empty() ? paintBrushCategory(definition) : category;
        library.push_back(std::move(entry));
        ++added;
    }
    return added;
}

std::vector<DocumentBrush> documentBrushes(const Document& document)
{
    std::vector<DocumentBrush> brushes;
    const std::map<int, int> uses = paintBrushUses(document);
    if (const auto library = paintBrushesOf(document))
        for (const PaintBrushDefinition& definition : library->definitions)
        {
            DocumentBrush brush;
            brush.name = definition.name;
            brush.paintBrush = true;
            brush.definition = definition;
            brush.pattern = definition.pattern;
            const auto found = uses.find(definition.index);
            brush.strokes = found != uses.end() ? found->second : 0;
            brushes.push_back(std::move(brush));
        }
    // Symbols drawn along strokes, as Phoenix keeps them on the stroke.
    std::map<std::string, std::pair<int, bool>> symbols;
    forEachStroke(document, [&symbols](const StrokeStyle& stroke) {
        std::string name;
        bool pattern = false;
        for (const auto& [key, value] : stroke.extraAttributes)
        {
            if (key == "phoenixBrush")
                name = value;
            else if (key == "phoenixBrushPattern")
                pattern = value == "true" || value == "1";
        }
        if (name.empty())
            return;
        auto& entry = symbols[name];
        ++entry.first;
        entry.second = entry.second || pattern;
    });
    for (const auto& [name, use] : symbols)
    {
        DocumentBrush brush;
        brush.name = name;
        brush.symbol = name;
        brush.strokes = use.first;
        brush.pattern = use.second;
        brushes.push_back(std::move(brush));
    }
    return brushes;
}

const BrushLibraryEntry* findInBrushLibrary(const std::vector<BrushLibraryEntry>& library, const std::string& guid)
{
    if (guid.empty())
        return nullptr;
    for (const BrushLibraryEntry& entry : library)
        if (entry.definition.guid() == guid)
            return &entry;
    return nullptr;
}

} // namespace fla
