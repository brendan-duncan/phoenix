#include "swatches.h"

#include "../data/document.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../data/swatch.h"
#include "../data/swatch_list.h"
#include "color_art.h"
#include "property_command.h"

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

/// Whether a section the document keeps as read is its extended swatch
/// lists.
bool isExtendedSection(const std::string& xml)
{
    const size_t first = xml.find_first_not_of(" \t\r\n");
    return first != std::string::npos && xml.compare(first, 20, "<extendedSwatchLists") == 0;
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
    for (const Document::RawSection& section : document.extraSections)
        if (isExtendedSection(section.xml))
        {
            state.extended = true;
            state.extendedXml = section.xml;
            break;
        }
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

namespace {

DrawingStyle::Stop stopOf(double ratio, uint32_t rgb)
{
    DrawingStyle::Stop stop;
    stop.ratio = ratio;
    stop.color[0] = static_cast<uint8_t>(rgb >> 16);
    stop.color[1] = static_cast<uint8_t>(rgb >> 8);
    stop.color[2] = static_cast<uint8_t>(rgb);
    stop.color[3] = 255;
    return stop;
}

Gradient gradientOf(bool radial, std::vector<DrawingStyle::Stop> stops)
{
    Gradient gradient;
    gradient.radial = radial;
    gradient.stops = std::move(stops);
    return gradient;
}

} // namespace

std::vector<Gradient> defaultGradients()
{
    // Ratios in 255ths, as the file keeps them.
    const auto at = [](int step) { return step / 255.0; };
    return {
        gradientOf(false, {stopOf(0.0, 0xFFFFFF), stopOf(1.0, 0x000000)}),
        gradientOf(true, {stopOf(0.0, 0xFFFFFF), stopOf(1.0, 0x000000)}),
        gradientOf(true, {stopOf(0.0, 0xFF0000), stopOf(1.0, 0x000000)}),
        gradientOf(true, {stopOf(0.0, 0x00FF00), stopOf(1.0, 0x000000)}),
        gradientOf(true, {stopOf(0.0, 0x0000FF), stopOf(1.0, 0x000000)}),
        gradientOf(false, {stopOf(0.0, 0x0066FD), stopOf(at(96), 0xFFFFFF), stopOf(at(122), 0xFFFFFF),
                              stopOf(at(128), 0x996600), stopOf(at(170), 0xFFCC00), stopOf(1.0, 0xFFFFFF)}),
        gradientOf(false, {stopOf(0.0, 0xFF0000), stopOf(at(42), 0xFFFF00), stopOf(at(93), 0x00FF00),
                              stopOf(at(127), 0x00FFFF), stopOf(at(170), 0x0000FF), stopOf(at(212), 0xFF00FF),
                              stopOf(1.0, 0xFF0000)}),
        gradientOf(true, {stopOf(0.0, 0xEBEBEB), stopOf(at(244), 0xB2B2B2)}),
    };
}

namespace {

/// A gradient swatch in swatch list text: where its item starts and ends.
struct GradientItem
{
    size_t start = 0;
    size_t end = 0;
    Gradient gradient;
};

/// The gradient swatches of \a xml with their places, those with fewer
/// than two stops passed over.
std::vector<GradientItem> gradientItems(const std::string& xml)
{
    std::vector<GradientItem> items;
    size_t at = 0;
    while (true)
    {
        const size_t linear = xml.find("<LinearGradientSwatchItem", at);
        const size_t radial = xml.find("<RadialGradientSwatchItem", at);
        const size_t start = std::min(linear, radial);
        if (start == std::string::npos)
            break;
        const bool isRadial = start == radial;
        const size_t open = xml.find('>', start);
        if (open == std::string::npos)
            break;
        const std::string head = xml.substr(start, open + 1 - start);
        // Its stops run to the closing tag; <...SwatchItem/> has none.
        size_t end = open + 1;
        size_t itemEnd = open + 1;
        if (xml[open - 1] != '/')
        {
            const std::string close = isRadial ? "</RadialGradientSwatchItem>" : "</LinearGradientSwatchItem>";
            const size_t closing = xml.find(close, open);
            end = closing == std::string::npos ? xml.size() : closing;
            itemEnd = closing == std::string::npos ? xml.size() : closing + close.size();
        }
        Gradient gradient;
        gradient.radial = isRadial;
        gradient.spread = attributeOf(head, "spreadMethod");
        if (gradient.spread == "pad")
            gradient.spread.clear();
        gradient.linearRGB = attributeOf(head, "interpolationMethod") == "linearRGB";
        size_t entry = open + 1;
        while ((entry = xml.find("<GradientEntry", entry)) != std::string::npos && entry < end)
        {
            const size_t tagEnd = xml.find('>', entry);
            if (tagEnd == std::string::npos)
                break;
            const std::string tag = xml.substr(entry, tagEnd + 1 - entry);
            DrawingStyle::Stop stop;
            const Color color = colorOf(tag);
            std::copy(color.begin(), color.end(), stop.color);
            stop.ratio = std::max(0.0, std::min(1.0, std::atof(attributeOf(tag, "ratio").c_str())));
            gradient.stops.push_back(stop);
            entry = tagEnd + 1;
        }
        if (gradient.stops.size() >= 2)
            items.push_back({start, itemEnd, gradient});
        at = end;
    }
    return items;
}

/// \a items put in at the end of the first swatches of \a xml; false when
/// it has none.
bool insertItems(std::string& xml, const std::string& items)
{
    size_t close = xml.find("</swatches>");
    if (close == std::string::npos)
    {
        // A list with no swatches yet may be written <swatches/>.
        const size_t empty = xml.find("<swatches/>");
        if (empty == std::string::npos)
            return false;
        xml.replace(empty, 11, "<swatches></swatches>");
        close = xml.find("</swatches>");
    }
    xml.insert(close, items);
    return true;
}

/// \a state with the default gradients its own, in its extended swatch
/// lists, when it has no gradients of its own.
State withOwnGradients(const State& state)
{
    if (!gradientsOf(state).empty())
        return state;
    State after = state;
    if (!after.extended || after.extendedXml.empty())
    {
        after.extended = true;
        after.extendedXml = "<extendedSwatchLists><swatchList><swatchListInfo name=\"Default Swatches\"/>"
                            "<swatches></swatches></swatchList></extendedSwatchLists>";
    }
    std::string items;
    for (const Gradient& gradient : defaultGradients())
        items += itemOf(gradient);
    return insertItems(after.extendedXml, items) ? after : state;
}

} // namespace

std::vector<Gradient> gradientsIn(const std::string& xml)
{
    std::vector<Gradient> gradients;
    for (const GradientItem& item : gradientItems(xml))
        gradients.push_back(item.gradient);
    return gradients;
}

std::vector<Gradient> gradientsOf(const Document& document)
{
    return gradientsOf(stateOf(document));
}

std::vector<Gradient> gradientsOf(const State& state)
{
    std::vector<Gradient> gradients;
    if (state.exists)
        gradients = gradientsIn(state.xml);
    if (state.extended)
        for (const Gradient& gradient : gradientsIn(state.extendedXml))
            gradients.push_back(gradient);
    return gradients;
}

std::vector<Color> shownColors(const Document* document)
{
    std::vector<Color> colors = document ? of(*document) : std::vector<Color>();
    return colors.empty() ? defaults() : colors;
}

std::vector<Gradient> shownGradients(const Document* document)
{
    std::vector<Gradient> gradients = document ? gradientsOf(*document) : std::vector<Gradient>();
    return gradients.empty() ? defaultGradients() : gradients;
}

std::string itemOf(const Gradient& gradient)
{
    const char* name = gradient.radial ? "RadialGradientSwatchItem" : "LinearGradientSwatchItem";
    std::string item = std::string("<") + name;
    if (!gradient.spread.empty() && gradient.spread != "pad")
        item += " spreadMethod=\"" + gradient.spread + "\"";
    if (gradient.linearRGB)
        item += " interpolationMethod=\"linearRGB\"";
    item += ">";
    char buffer[64];
    for (const DrawingStyle::Stop& stop : gradient.stops)
    {
        // As the reference app writes them: black needs no color, and an
        // opaque stop no alpha.
        item += "<GradientEntry";
        if (stop.color[0] || stop.color[1] || stop.color[2])
        {
            std::snprintf(buffer, sizeof(buffer), " color=\"#%02X%02X%02X\"", stop.color[0], stop.color[1], stop.color[2]);
            item += buffer;
        }
        if (stop.color[3] != 255)
        {
            std::snprintf(buffer, sizeof(buffer), " alpha=\"%.15g\"", stop.color[3] / 255.0);
            item += buffer;
        }
        std::snprintf(buffer, sizeof(buffer), " ratio=\"%.15g\"/>", std::max(0.0, std::min(1.0, stop.ratio)));
        item += buffer;
    }
    return item + "</" + name + ">";
}

State withGradientAdded(const State& state, const Gradient& gradient)
{
    if (gradient.stops.size() < 2)
        return state;
    State after = withOwnGradients(state);
    if (!after.extended || after.extendedXml.empty())
    {
        after.extended = true;
        after.extendedXml = "<extendedSwatchLists><swatchList><swatchListInfo name=\"Default Swatches\"/>"
                            "<swatches></swatches></swatchList></extendedSwatchLists>";
    }
    return insertItems(after.extendedXml, itemOf(gradient)) ? after : state;
}

State withGradientRemoved(const State& state, size_t index)
{
    State after = withOwnGradients(state);
    // Those in the swatch list come first, then the extended lists'.
    if (after.exists)
    {
        const std::vector<GradientItem> items = gradientItems(after.xml);
        if (index < items.size())
        {
            after.xml.erase(items[index].start, items[index].end - items[index].start);
            return after;
        }
        index -= items.size();
    }
    if (!after.extended)
        return state;
    const std::vector<GradientItem> items = gradientItems(after.extendedXml);
    if (index >= items.size())
        return state;
    after.extendedXml.erase(items[index].start, items[index].end - items[index].start);
    return after;
}

State withGradientDuplicated(const State& state, size_t index)
{
    State after = withOwnGradients(state);
    // The item copied as written, so what it does not model goes along.
    const auto duplicate = [](std::string& xml, size_t& index) {
        const std::vector<GradientItem> items = gradientItems(xml);
        if (index >= items.size())
        {
            index -= items.size();
            return false;
        }
        const GradientItem& item = items[index];
        xml.insert(item.end, xml.substr(item.start, item.end - item.start));
        return true;
    };
    if (after.exists && duplicate(after.xml, index))
        return after;
    if (after.extended && duplicate(after.extendedXml, index))
        return after;
    return state;
}

void useGradient(DrawingStyle& style, const Gradient& gradient)
{
    style.hasFill = true;
    style.fillKind = gradient.radial ? DrawingStyle::FillKind::Radial : DrawingStyle::FillKind::Linear;
    style.gradient = gradient.stops;
    style.gradientSpread = gradient.spread;
    style.gradientLinearRGB = gradient.linearRGB;
}

FillStyle* gradientFill(const Gradient& gradient, DOMElement* parent)
{
    DrawingStyle style;
    useGradient(style, gradient);
    return style.createFill(parent);
}

void paintFills(Shape& shape, const Gradient& gradient)
{
    // Each fill made anew, kept where the edges and the list find it, then
    // fitted to the art it fills.
    for (auto& entry : shape.fillsMap)
    {
        FillStyle* fill = gradientFill(gradient, &shape);
        if (!fill)
            continue;
        std::replace(shape.fills.begin(), shape.fills.end(), entry.second, fill);
        delete entry.second;
        entry.second = fill;
    }
    DrawingStyle::fitFills(shape);
}

void paintFills(Shape& shape, const Color& color)
{
    for (auto& entry : shape.fillsMap)
    {
        SolidColor* solid = dynamic_cast<SolidColor*>(entry.second);
        if (!solid)
        {
            solid = new SolidColor(&shape);
            std::replace(shape.fills.begin(), shape.fills.end(), entry.second, static_cast<FillStyle*>(solid));
            delete entry.second;
            entry.second = solid;
        }
        std::copy(color.begin(), color.end(), solid->color);
    }
}

void paintStrokes(Shape& shape, const Gradient& gradient)
{
    for (auto& entry : shape.strokesMap)
    {
        if (!entry.second)
            continue;
        FillStyle* fill = gradientFill(gradient, entry.second);
        if (!fill)
            continue;
        delete entry.second->fill;
        entry.second->fill = fill;
    }
    DrawingStyle::fitStrokes(shape);
}

void paintStrokes(Shape& shape, const Color& color)
{
    for (auto& entry : shape.strokesMap)
    {
        if (!entry.second)
            continue;
        SolidColor* solid = dynamic_cast<SolidColor*>(entry.second->fill);
        if (!solid)
        {
            delete entry.second->fill;
            solid = new SolidColor(entry.second);
            entry.second->fill = solid;
        }
        std::copy(color.begin(), color.end(), solid->color);
    }
}

Shape* withoutPart(const Shape& shape, bool strokes)
{
    Shape* after = strokes ? colorart::fillsOnly(shape) : colorart::strokesOnly(shape);
    if (after && after->edges.empty())
    {
        delete after;
        return nullptr;
    }
    return after;
}

namespace {

/// The primitive's fill, or its stroke's, where it is kept; and its box.
FillStyle** primitiveSlot(Element& element, bool strokes, const Rect** box, DOMElement** parent)
{
    FillStyle** fill = nullptr;
    StrokeStyle* stroke = nullptr;
    if (element.elementType() == Element::Type::Rectangle)
    {
        auto& rectangle = static_cast<RectanglePrimitive&>(element);
        fill = &rectangle.fillStyle;
        stroke = rectangle.strokeStyle;
        *box = &rectangle.rect;
    }
    else if (element.elementType() == Element::Type::Oval)
    {
        auto& oval = static_cast<OvalPrimitive&>(element);
        fill = &oval.fillStyle;
        stroke = oval.strokeStyle;
        *box = &oval.rect;
    }
    else
        return nullptr;
    if (!strokes)
    {
        *parent = &element;
        return *fill ? fill : nullptr;
    }
    *parent = stroke;
    return stroke && stroke->fill ? &stroke->fill : nullptr;
}

} // namespace

bool isPrimitive(const Element& element)
{
    return element.elementType() == Element::Type::Rectangle || element.elementType() == Element::Type::Oval;
}

Command* paintPrimitive(Element& element, bool strokes, const Color& color, const std::string& name)
{
    const Rect* box = nullptr;
    DOMElement* parent = nullptr;
    FillStyle** slot = primitiveSlot(element, strokes, &box, &parent);
    if (!slot)
        return nullptr;
    // A solid color keeps its fill and takes the new color; anything else
    // becomes a solid fill.
    if ((*slot)->type() == FillStyle::Type::SolidColor)
    {
        uint8_t* target = static_cast<SolidColor*>(*slot)->color;
        return new SetColorCommand(target, target, color.data(), name);
    }
    auto* solid = new SolidColor(parent);
    std::copy(color.begin(), color.end(), solid->color);
    return new ReplaceOwnedCommand<FillStyle>(*slot, solid, name);
}

Command* paintPrimitive(Element& element, bool strokes, const Gradient& gradient, const std::string& name)
{
    const Rect* box = nullptr;
    DOMElement* parent = nullptr;
    FillStyle** slot = strokes ? nullptr : primitiveSlot(element, false, &box, &parent);
    if (!slot)
        return nullptr;
    FillStyle* fill = gradientFill(gradient, parent);
    if (!fill)
        return nullptr;
    // Fitted to the primitive's box, as a new one drawn with it.
    DrawingStyle::fitFill(*fill, box->topLeft.x, box->topLeft.y, box->bottomRight.x, box->bottomRight.y);
    return new ReplaceOwnedCommand<FillStyle>(*slot, fill, name);
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
    // The extended swatch lists, where the gradients are.
    std::vector<Document::RawSection>& sections = _document->extraSections;
    const auto section = std::find_if(sections.begin(), sections.end(),
        [](const Document::RawSection& raw) { return swatches::isExtendedSection(raw.xml); });
    if (state.extended && section != sections.end())
        section->xml = state.extendedXml;
    else if (state.extended)
        sections.push_back({state.extendedXml, false});
    else if (section != sections.end())
        sections.erase(section);
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
