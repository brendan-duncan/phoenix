#include "swatches.h"

#include "../data/document.h"
#include "../data/swatch.h"
#include "../data/swatch_list.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla {

namespace swatches {

namespace {

std::string attributeOf(const std::string& tag, const char* name)
{
    const std::string key = std::string(" ") + name + "=\"";
    const size_t at = tag.find(key);
    if (at == std::string::npos)
        return std::string();
    const size_t start = at + key.size();
    const size_t end = tag.find('"', start);
    return end == std::string::npos ? std::string() : tag.substr(start, end - start);
}

/// Where each SolidSwatchItem tag of \a xml starts and ends.
std::vector<std::pair<size_t, size_t>> solidTags(const std::string& xml)
{
    std::vector<std::pair<size_t, size_t>> tags;
    size_t at = 0;
    while ((at = xml.find("<SolidSwatchItem", at)) != std::string::npos)
    {
        const size_t end = xml.find('>', at);
        if (end == std::string::npos)
            break;
        tags.emplace_back(at, end + 1);
        at = end + 1;
    }
    return tags;
}

Color colorOf(const std::string& tag)
{
    Color color = {0, 0, 0, 255};
    const std::string hex = attributeOf(tag, "color");
    if (hex.size() == 7 && hex[0] == '#')
    {
        const unsigned long value = std::strtoul(hex.c_str() + 1, nullptr, 16);
        color[0] = static_cast<uint8_t>(value >> 16);
        color[1] = static_cast<uint8_t>(value >> 8);
        color[2] = static_cast<uint8_t>(value);
    }
    const std::string alpha = attributeOf(tag, "alpha");
    if (!alpha.empty())
        color[3] = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(1.0, std::atof(alpha.c_str()))) * 255.0));
    return color;
}

std::string tagOf(const Color& color)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "<SolidSwatchItem color=\"#%02X%02X%02X\"", color[0], color[1], color[2]);
    std::string tag = buffer;
    if (color[3] != 255)
    {
        std::snprintf(buffer, sizeof(buffer), " alpha=\"%g\"", color[3] / 255.0);
        tag += buffer;
    }
    return tag + "/>";
}

std::vector<Color> colorsIn(const std::string& xml)
{
    std::vector<Color> colors;
    for (const auto& tag : solidTags(xml))
        colors.push_back(colorOf(xml.substr(tag.first, tag.second - tag.first)));
    return colors;
}

} // namespace

std::vector<Color> defaults()
{
    std::vector<Color> colors;
    // A gray ramp, then the primaries and secondaries.
    for (int v : {0x00, 0x33, 0x66, 0x99, 0xCC, 0xFF})
        colors.push_back({static_cast<uint8_t>(v), static_cast<uint8_t>(v), static_cast<uint8_t>(v), 255});
    for (const Color& c : {Color{255, 0, 0, 255}, Color{0, 255, 0, 255}, Color{0, 0, 255, 255},
             Color{255, 255, 0, 255}, Color{0, 255, 255, 255}, Color{255, 0, 255, 255}})
        colors.push_back(c);
    // The web-safe cube, by red, then green, then blue.
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b)
                colors.push_back({static_cast<uint8_t>(r * 51), static_cast<uint8_t>(g * 51),
                    static_cast<uint8_t>(b * 51), 255});
    return colors;
}

std::vector<Color> of(const Document& document)
{
    return stateOf(document).colors;
}

State stateOf(const Document& document)
{
    State state;
    if (document.swatchLists.empty() || !document.swatchLists.front())
        return state;
    state.exists = true;
    state.xml = document.swatchLists.front()->xml;
    state.colors = colorsIn(state.xml);
    return state;
}

State withAdded(const State& state, const Color& color)
{
    State after = state;
    if (!after.exists || after.xml.empty())
    {
        after.exists = true;
        after.xml = "<swatchList><swatchListInfo name=\"Swatches\"/><swatches></swatches></swatchList>";
    }
    size_t close = after.xml.find("</swatches>");
    if (close == std::string::npos)
    {
        // A list with no swatches yet may be written <swatches/>.
        const size_t empty = after.xml.find("<swatches/>");
        if (empty == std::string::npos)
            return state;
        after.xml.replace(empty, 11, "<swatches></swatches>");
        close = after.xml.find("</swatches>");
    }
    after.xml.insert(close, tagOf(color));
    after.colors = colorsIn(after.xml);
    return after;
}

State withRemoved(const State& state, size_t index)
{
    const auto tags = solidTags(state.xml);
    if (index >= tags.size())
        return state;
    State after = state;
    after.xml.erase(tags[index].first, tags[index].second - tags[index].first);
    after.colors = colorsIn(after.xml);
    return after;
}

} // namespace swatches

SetSwatchesCommand::SetSwatchesCommand(Document* document, swatches::State before, swatches::State after,
    const std::string& name)
    : _document(document)
    , _before(std::move(before))
    , _after(std::move(after))
    , _name(name)
{
}

void SetSwatchesCommand::redo()
{
    apply(_after);
}

void SetSwatchesCommand::undo()
{
    apply(_before);
}

void SetSwatchesCommand::apply(const swatches::State& state)
{
    if (!_document)
        return;
    std::vector<SwatchList*>& lists = _document->swatchLists;
    if (!state.exists)
    {
        if (!lists.empty())
        {
            delete lists.front();
            lists.erase(lists.begin());
        }
        return;
    }
    if (lists.empty() || !lists.front())
    {
        SwatchList* list = new SwatchList(_document);
        list->name = "Swatches";
        if (lists.empty())
            lists.push_back(list);
        else
            lists.front() = list;
    }
    SwatchList* list = lists.front();
    list->xml = state.xml;
    for (Swatch* swatch : list->swatches)
        delete swatch;
    list->swatches.clear();
    for (const swatches::Color& color : state.colors)
    {
        SolidSwatch* swatch = new SolidSwatch(list);
        for (int i = 0; i < 4; ++i)
            swatch->color[i] = color[static_cast<size_t>(i)];
        list->swatches.push_back(swatch);
    }
}

} // namespace fla
