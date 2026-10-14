#include "font_items.h"

#include "tinyxml2.h"

#include <cstdlib>
#include <sstream>
#include <algorithm>

namespace fla {

namespace fonts {

namespace {

bool isFontsSection(const std::string& xml)
{
    const size_t start = xml.find_first_not_of(" \t\r\n");
    return start != std::string::npos && xml.compare(start, 6, "<fonts") == 0;
}

std::vector<int> rangesOf(const char* text)
{
    std::vector<int> ranges;
    std::stringstream in(text ? text : "");
    std::string part;
    while (std::getline(in, part, '|'))
        if (!part.empty())
            ranges.push_back(std::atoi(part.c_str()));
    return ranges;
}

} // namespace

std::vector<FontItem> itemsOf(const Document& document)
{
    std::vector<FontItem> items;
    for (const Document::RawSection& section : document.extraSections)
    {
        if (!isFontsSection(section.xml))
            continue;
        tinyxml2::XMLDocument xml;
        if (xml.Parse(section.xml.c_str(), section.xml.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement())
            continue;
        for (const tinyxml2::XMLElement* node = xml.RootElement()->FirstChildElement("DOMFontItem"); node;
             node = node->NextSiblingElement("DOMFontItem"))
        {
            FontItem item;
            item.name = node->Attribute("name") ? node->Attribute("name") : "";
            item.font = node->Attribute("font") ? node->Attribute("font") : "";
            item.itemId = node->Attribute("itemID") ? node->Attribute("itemID") : "";
            item.id = node->IntAttribute("id", 0);
            item.ranges = rangesOf(node->Attribute("embedRanges"));
            item.characters = node->Attribute("embeddedCharacters") ? node->Attribute("embeddedCharacters") : "";
            items.push_back(item);
        }
    }
    return items;
}

std::vector<Document::RawSection> withItems(const Document& document, const std::vector<FontItem>& items)
{
    std::vector<Document::RawSection> sections;
    for (const Document::RawSection& section : document.extraSections)
        if (!isFontsSection(section.xml))
            sections.push_back(section);
    if (items.empty())
        return sections;

    tinyxml2::XMLDocument xml;
    tinyxml2::XMLElement* root = xml.NewElement("fonts");
    xml.InsertEndChild(root);
    int nextId = 1;
    for (const FontItem& item : items)
        nextId = std::max(nextId, item.id + 1);
    for (const FontItem& item : items)
    {
        tinyxml2::XMLElement* node = xml.NewElement("DOMFontItem");
        node->SetAttribute("name", item.name.c_str());
        if (!item.itemId.empty())
            node->SetAttribute("itemID", item.itemId.c_str());
        node->SetAttribute("font", item.font.c_str());
        node->SetAttribute("size", 0);
        node->SetAttribute("id", item.id > 0 ? item.id : nextId++);
        std::string ranges;
        for (int range : item.ranges)
            ranges += (ranges.empty() ? "" : "|") + std::to_string(range);
        if (!ranges.empty())
            node->SetAttribute("embedRanges", ranges.c_str());
        if (!item.characters.empty())
            node->SetAttribute("embeddedCharacters", item.characters.c_str());
        root->InsertEndChild(node);
    }
    tinyxml2::XMLPrinter printer(nullptr, true);
    xml.Print(&printer);
    sections.push_back({printer.CStr(), true});
    return sections;
}

bool isSymbol(const std::string& face)
{
    return !face.empty() && face.back() == '*';
}

std::string fontOf(const Document& document, const std::string& face)
{
    if (!isSymbol(face))
        return face;
    const std::string name = face.substr(0, face.size() - 1);
    for (const FontItem& item : itemsOf(document))
        if (item.name == name)
            return item.font;
    return face;
}

} // namespace fonts

} // namespace fla
