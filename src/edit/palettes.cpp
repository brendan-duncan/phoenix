#include "palettes.h"

#include "../data/document.h"
#include "../data/fill_style.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>

namespace fla {

namespace palettes {

namespace {

const char* const kKey = "phoenixPalettes";
const char* const kLink = "phoenixColor";

using Attributes = std::vector<std::pair<std::string, std::string>>;

std::string attribute(const Attributes& attributes, const char* name)
{
    for (const auto& [key, value] : attributes)
        if (key == name)
            return value;
    return std::string();
}

Attributes withAttribute(Attributes attributes, const char* name, const std::string& value)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [name](const std::pair<std::string, std::string>& pair) { return pair.first == name; }),
        attributes.end());
    if (!value.empty())
        attributes.emplace_back(name, value);
    return attributes;
}

std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '|' || c == ';' || c == '~' || c == '%' || c == '=')
        {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            out += buffer;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string unescaped(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            out += static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, separator))
        parts.push_back(part);
    return parts;
}

std::string hex(const uint8_t rgba[4])
{
    char buffer[12];
    std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X", rgba[0], rgba[1], rgba[2], rgba[3]);
    return buffer;
}

void fromHex(const std::string& text, uint8_t rgba[4])
{
    for (int i = 0; i < 4; ++i)
        rgba[i] = static_cast<uint8_t>(text.size() >= static_cast<size_t>(2 * i + 2)
                ? std::strtol(text.substr(static_cast<size_t>(2 * i), 2).c_str(), nullptr, 16) : (i == 3 ? 255 : 0));
}

/// A color in the art that can be linked: where its value is, and the
/// attributes that hold its link.
struct Slot
{
    uint8_t* rgba;
    Attributes* attributes;
    bool stroke;
};

void slotsOf(Element* element, const std::function<void(const Slot&)>& visit)
{
    if (!element)
        return;
    const auto fill = [&visit](FillStyle* style, bool stroke) {
        if (style && style->type() == FillStyle::Type::SolidColor)
            visit({static_cast<SolidColor*>(style)->color, &style->extraAttributes, stroke});
    };
    switch (element->elementType())
    {
    case Element::Type::Shape:
    {
        Shape* shape = static_cast<Shape*>(element);
        for (FillStyle* style : shape->fills)
            fill(style, false);
        for (StrokeStyle* stroke : shape->strokes)
            if (stroke)
                fill(stroke->fill, true);
        break;
    }
    case Element::Type::Rectangle:
    {
        auto* rectangle = static_cast<RectanglePrimitive*>(element);
        fill(rectangle->fillStyle, false);
        if (rectangle->strokeStyle)
            fill(rectangle->strokeStyle->fill, true);
        break;
    }
    case Element::Type::Oval:
    {
        auto* oval = static_cast<OvalPrimitive*>(element);
        fill(oval->fillStyle, false);
        if (oval->strokeStyle)
            fill(oval->strokeStyle->fill, true);
        break;
    }
    case Element::Type::StaticText:
        for (TextRun& run : static_cast<StaticText*>(element)->runs)
            visit({run.fillColor, &run.extraAttributes, false});
        break;
    case Element::Type::Group:
        for (Element* member : static_cast<Group*>(element)->members)
            slotsOf(member, visit);
        break;
    default:
        break;
    }
}

void everySlot(const Document& document, const std::function<void(const Slot&)>& visit)
{
    const auto timeline = [&](const Timeline* timeline) {
        for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
            for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
                for (Element* element : frame ? frame->elements : std::vector<Element*>())
                    slotsOf(element, visit);
    };
    for (const Timeline* scene : document.timelines)
        timeline(scene);
    if (document.symbolList)
        for (const Symbol* symbol : document.symbolList->symbols)
            for (const Timeline* inner : symbol ? symbol->timelines : std::vector<Timeline*>())
                timeline(inner);
}

/// One color and link changed, both ways. The color is found again each time
/// by its place (ordinal) among the slots of its root, the whole document or
/// one element, never by a pointer: merges and undo rebuild fills, while the
/// history always brings the document back to the state the change was
/// recorded in, where the same walk finds the same slot.
struct Change
{
    Element* root = nullptr;
    size_t ordinal = 0;
    uint8_t before[4] = {};
    uint8_t after[4] = {};
    Attributes attributesBefore;
    Attributes attributesAfter;
};

/// The slots under \a root, in the order changes count them: the whole
/// document's when \a root is null.
std::vector<Slot> slotsUnder(const Document* document, Element* root)
{
    std::vector<Slot> slots;
    const auto collect = [&slots](const Slot& slot) { slots.push_back(slot); };
    if (root)
        slotsOf(root, collect);
    else if (document)
        everySlot(*document, collect);
    return slots;
}

class ChangesCommand : public Command
{
public:
    ChangesCommand(const Document* document, std::vector<Change> changes, std::string name)
        : _document(document)
        , _changes(std::move(changes))
        , _name(std::move(name))
    {
    }

    void redo() override { apply(true); }

    void undo() override { apply(false); }

    std::string name() const override { return _name; }

private:
    void apply(bool forward)
    {
        std::map<Element*, std::vector<Slot>> slots;
        for (const Change& change : _changes)
        {
            auto found = slots.find(change.root);
            if (found == slots.end())
                found = slots.emplace(change.root, slotsUnder(_document, change.root)).first;
            if (change.ordinal >= found->second.size())
                continue;
            const Slot& slot = found->second[change.ordinal];
            const uint8_t* rgba = forward ? change.after : change.before;
            std::copy(rgba, rgba + 4, slot.rgba);
            *slot.attributes = forward ? change.attributesAfter : change.attributesBefore;
        }
    }

    const Document* _document;
    std::vector<Change> _changes;
    std::string _name;
};

Change changeOf(Element* root, size_t ordinal, const Slot& slot, const uint8_t* rgba, const std::string& link)
{
    Change change;
    change.root = root;
    change.ordinal = ordinal;
    std::copy(slot.rgba, slot.rgba + 4, change.before);
    std::copy(rgba, rgba + 4, change.after);
    change.attributesBefore = *slot.attributes;
    change.attributesAfter = withAttribute(*slot.attributes, kLink, link);
    return change;
}

CommandPtr commandOf(const Document* document, std::vector<Change> changes, const std::string& name)
{
    if (changes.empty())
        return nullptr;
    return CommandPtr(new ChangesCommand(document, std::move(changes), name));
}

} // namespace

const Palette* Palettes::activePalette() const
{
    for (const Palette& palette : list)
        if (palette.name == active)
            return &palette;
    return list.empty() ? nullptr : &list.front();
}

Palette* Palettes::activePalette()
{
    return const_cast<Palette*>(static_cast<const Palettes*>(this)->activePalette());
}

Palettes load(const Document& document)
{
    // active~palette;name=RRGGBBAA;name=RRGGBBAA~palette;...
    Palettes palettes;
    const std::vector<std::string> parts = split(attribute(document.extraAttributes, kKey), '~');
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i == 0)
        {
            palettes.active = unescaped(parts[0]);
            continue;
        }
        const std::vector<std::string> fields = split(parts[i], ';');
        if (fields.empty())
            continue;
        Palette palette;
        palette.name = unescaped(fields[0]);
        for (size_t f = 1; f < fields.size(); ++f)
        {
            const size_t equals = fields[f].find('=');
            if (equals == std::string::npos)
                continue;
            Color color;
            color.name = unescaped(fields[f].substr(0, equals));
            fromHex(fields[f].substr(equals + 1), color.rgba);
            palette.colors.push_back(color);
        }
        palettes.list.push_back(palette);
    }
    return palettes;
}

std::vector<std::pair<std::string, std::string>> saved(const Document& document, const Palettes& palettes)
{
    std::string text;
    if (!palettes.list.empty())
    {
        text = escaped(palettes.active);
        for (const Palette& palette : palettes.list)
        {
            text += "~" + escaped(palette.name);
            for (const Color& color : palette.colors)
                text += ";" + escaped(color.name) + "=" + hex(color.rgba);
        }
    }
    return withAttribute(document.extraAttributes, kKey, text);
}

std::vector<std::pair<std::string, int>> usage(const Document& document)
{
    std::map<std::string, int> counts;
    everySlot(document, [&counts](const Slot& slot) {
        const std::string link = attribute(*slot.attributes, kLink);
        if (!link.empty())
            ++counts[link];
    });
    return std::vector<std::pair<std::string, int>>(counts.begin(), counts.end());
}

CommandPtr recolor(Document& document, const Palettes& palettes)
{
    const Palette* palette = palettes.activePalette();
    if (!palette)
        return nullptr;
    std::map<std::string, const Color*> byName;
    for (const Color& color : palette->colors)
        byName[color.name] = &color;
    std::vector<Change> changes;
    size_t ordinal = 0;
    everySlot(document, [&](const Slot& slot) {
        const size_t at = ordinal++;
        const auto found = byName.find(attribute(*slot.attributes, kLink));
        if (found == byName.end() || std::equal(slot.rgba, slot.rgba + 4, found->second->rgba))
            return;
        changes.push_back(changeOf(nullptr, at, slot, found->second->rgba, found->first));
    });
    return commandOf(&document, std::move(changes), "Recolor from Palette");
}

CommandPtr linkMatching(Document& document, const Color& color)
{
    std::vector<Change> changes;
    size_t ordinal = 0;
    everySlot(document, [&](const Slot& slot) {
        const size_t at = ordinal++;
        if (slot.rgba[0] != color.rgba[0] || slot.rgba[1] != color.rgba[1] || slot.rgba[2] != color.rgba[2])
            return;
        if (attribute(*slot.attributes, kLink) == color.name)
            return;
        // The color stays as it is; only the link is made.
        changes.push_back(changeOf(nullptr, at, slot, slot.rgba, color.name));
    });
    return commandOf(&document, std::move(changes), "Link to Palette");
}

CommandPtr paint(const std::vector<Element*>& elements, const Color& color, bool fills, bool strokes)
{
    std::vector<Change> changes;
    for (Element* element : elements)
    {
        size_t ordinal = 0;
        slotsOf(element, [&](const Slot& slot) {
            const size_t at = ordinal++;
            if ((slot.stroke && strokes) || (!slot.stroke && fills))
                changes.push_back(changeOf(element, at, slot, color.rgba, color.name));
        });
    }
    return commandOf(nullptr, std::move(changes), "Paint from Palette");
}

CommandPtr unlink(const std::vector<Element*>& elements)
{
    std::vector<Change> changes;
    for (Element* element : elements)
    {
        size_t ordinal = 0;
        slotsOf(element, [&](const Slot& slot) {
            const size_t at = ordinal++;
            if (!attribute(*slot.attributes, kLink).empty())
                changes.push_back(changeOf(element, at, slot, slot.rgba, std::string()));
        });
    }
    return commandOf(nullptr, std::move(changes), "Unlink from Palette");
}

} // namespace palettes

} // namespace fla
