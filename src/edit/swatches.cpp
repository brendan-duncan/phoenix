#include "swatches.h"

#include "../data/document.h"
#include "../data/swatch.h"
#include "../data/swatch_list.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>

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

State withColors(const State& state, const std::vector<Color>& colors)
{
    State after = state;
    if (!after.exists || after.xml.empty())
    {
        after.exists = true;
        after.xml = "<swatchList><swatchListInfo name=\"Swatches\"/><swatches></swatches></swatchList>";
    }
    // Every solid swatch out, from the last so the places hold.
    const auto tags = solidTags(after.xml);
    for (auto it = tags.rbegin(); it != tags.rend(); ++it)
        after.xml.erase(it->first, it->second - it->first);
    after.colors.clear();
    for (const Color& color : colors)
        after = withAdded(after, color);
    after.colors = colorsIn(after.xml);
    return after;
}

std::vector<Color> sortedByColor(std::vector<Color> colors)
{
    // Hue, then saturation, then brightness; grays (no hue) first.
    const auto key = [](const Color& c) {
        const double r = c[0] / 255.0, g = c[1] / 255.0, b = c[2] / 255.0;
        const double high = std::max({r, g, b});
        const double low = std::min({r, g, b});
        const double chroma = high - low;
        double hue = -1.0;
        if (chroma > 1.0e-6)
        {
            if (high == r)
                hue = std::fmod((g - b) / chroma + 6.0, 6.0);
            else if (high == g)
                hue = (b - r) / chroma + 2.0;
            else
                hue = (r - g) / chroma + 4.0;
        }
        const double saturation = high > 0.0 ? chroma / high : 0.0;
        return std::make_tuple(hue, saturation, high);
    };
    std::stable_sort(colors.begin(), colors.end(), [&key](const Color& a, const Color& b) { return key(a) < key(b); });
    return colors;
}

std::vector<Color> readAct(const std::vector<uint8_t>& bytes)
{
    std::vector<Color> colors;
    if (bytes.size() < 768)
        return colors;
    // A table with a count says how many of its 256 entries are used.
    size_t count = 256;
    if (bytes.size() >= 772)
        count = std::min<size_t>(256, (static_cast<size_t>(bytes[768]) << 8) | bytes[769]);
    if (count == 0)
        count = 256;
    for (size_t i = 0; i < count; ++i)
        colors.push_back({bytes[i * 3], bytes[i * 3 + 1], bytes[i * 3 + 2], 255});
    return colors;
}

std::vector<uint8_t> writeAct(const std::vector<Color>& colors)
{
    std::vector<uint8_t> bytes(772, 0);
    const size_t count = std::min<size_t>(256, colors.size());
    for (size_t i = 0; i < count; ++i)
    {
        bytes[i * 3] = colors[i][0];
        bytes[i * 3 + 1] = colors[i][1];
        bytes[i * 3 + 2] = colors[i][2];
    }
    bytes[768] = static_cast<uint8_t>(count >> 8);
    bytes[769] = static_cast<uint8_t>(count & 0xFF);
    // No transparent entry.
    bytes[770] = 0xFF;
    bytes[771] = 0xFF;
    return bytes;
}

std::vector<Color> readGpl(const std::string& text)
{
    std::vector<Color> colors;
    size_t start = 0;
    bool header = true;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (header)
        {
            header = false;
            if (line.rfind("GIMP Palette", 0) == 0)
                continue;
        }
        if (line.empty() || line[0] == '#' || line.rfind("Name:", 0) == 0 || line.rfind("Columns:", 0) == 0)
            continue;
        int r = -1, g = -1, b = -1;
        char* cursor = nullptr;
        r = static_cast<int>(std::strtol(line.c_str(), &cursor, 10));
        if (cursor == line.c_str())
            continue;
        const char* next = cursor;
        g = static_cast<int>(std::strtol(next, &cursor, 10));
        if (cursor == next)
            continue;
        next = cursor;
        b = static_cast<int>(std::strtol(next, &cursor, 10));
        if (cursor == next)
            continue;
        if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255)
            colors.push_back({static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255});
        if (end == text.size())
            break;
    }
    return colors;
}

std::string writeGpl(const std::vector<Color>& colors, const std::string& name)
{
    std::string text = "GIMP Palette\nName: " + name + "\nColumns: 16\n#\n";
    char buffer[64];
    for (const Color& c : colors)
    {
        std::snprintf(buffer, sizeof(buffer), "%3d %3d %3d\t#%02X%02X%02X\n", c[0], c[1], c[2], c[0], c[1], c[2]);
        text += buffer;
    }
    return text;
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
