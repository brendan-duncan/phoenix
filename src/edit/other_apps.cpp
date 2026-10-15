#include "other_apps.h"

#include "adjustments.h"
#include "behaviors.h"
#include "library_commands.h"
#include "templates.h"

#include "../data/document.h"
#include "../data/filter.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace fla::otherapps {

namespace {

std::vector<Timeline*> allTimelines(Document& document)
{
    std::vector<Timeline*> out(document.timelines.begin(), document.timelines.end());
    if (document.symbolList)
        for (Symbol* symbol : document.symbolList->symbols)
            for (Timeline* timeline : symbol ? symbol->timelines : std::vector<Timeline*>())
                out.push_back(timeline);
    out.erase(std::remove(out.begin(), out.end(), nullptr), out.end());
    return out;
}

void eachElement(Element* element, const std::function<void(Element*)>& visit)
{
    if (!element)
        return;
    visit(element);
    if (element->elementType() == Element::Type::Group)
        for (Element* member : static_cast<Group*>(element)->members)
            eachElement(member, visit);
}

void eachElement(Timeline& timeline, const std::function<void(Element*)>& visit)
{
    for (Layer* layer : timeline.layers)
        for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            for (Element* element : frame ? frame->elements : std::vector<Element*>())
                eachElement(element, visit);
}

std::string uniqueName(const Document& document, const std::string& base)
{
    std::string name = base;
    for (int n = 2; document.symbolList && document.symbolList->symbolMap.count(name); ++n)
        name = base + " " + std::to_string(n);
    return name;
}

void addSymbol(Document& document, Symbol* symbol)
{
    if (!document.symbolList)
        document.symbolList = new SymbolList(&document);
    symbol->parent = document.symbolList;
    document.symbolList->symbols.push_back(symbol);
    document.symbolList->link(symbol);
}

// Templates -----------------------------------------------------------------

struct TemplateBaker
{
    Document& document;
    std::map<std::string, Symbol*> made;

    /// \a symbol with \a values written in: a copy when any of its fields
    /// (or those of the symbols inside it) has a value, else itself.
    Symbol* baked(Symbol* symbol, const templates::Values& values, int depth)
    {
        if (!symbol || depth > 16 || values.empty())
            return symbol;
        bool touched = false;
        for (const templates::Field& field : templates::fieldsOf(*symbol))
            touched = touched || values.count(field.name);
        if (!touched)
            return symbol;
        const std::string key = symbol->name + '\x1f' + templates::encode(values);
        const auto found = made.find(key);
        if (found != made.end())
            return found->second;
        // Named after the first words it shows, so the library says which.
        std::string label;
        for (const auto& [name, value] : values)
            if (label.empty() && !value.empty() && value[0] != '#')
                label = value.substr(0, 24);
        Symbol* copy = library::cloneSymbol(*symbol, uniqueName(document, symbol->name + " (" + (label.empty() ? "baked" : label) + ")"));
        addSymbol(document, copy);
        made[key] = copy;
        for (Timeline* timeline : copy->timelines)
        {
            if (!timeline)
                continue;
            eachElement(*timeline, [&](Element* element) {
                if (element->elementType() == Element::Type::StaticText)
                {
                    auto* text = static_cast<StaticText*>(element);
                    const auto value = values.find(text->instanceName);
                    if (text->textType != StaticText::TextType::Static && !text->instanceName.empty() && value != values.end())
                    {
                        TextRun run = text->runs.empty() ? TextRun() : text->runs.front();
                        run.text = value->second;
                        text->runs = {run};
                    }
                    return;
                }
                if (element->elementType() != Element::Type::SymbolInstance)
                    return;
                auto* instance = static_cast<SymbolInstance*>(element);
                unsigned char rgb[3];
                const auto value = instance->name.empty() ? values.end() : values.find(instance->name);
                if (value != values.end() && templates::colorOf(value->second, rgb))
                {
                    instance->colorTransform.tintMultiplier = 1.0;
                    for (int c = 0; c < 3; ++c)
                        instance->colorTransform.tintColor[c] = rgb[c];
                }
                // Its own values under the ones from outside, which win.
                templates::Values merged = templates::valuesOf(*instance);
                for (const auto& [name, each] : values)
                    merged[name] = each;
                instance->extraAttributes = templates::withValues(*instance, {});
                Symbol* inner = baked(instance->symbol, merged, depth + 1);
                if (inner && inner != instance->symbol)
                {
                    instance->symbol = inner;
                    instance->libraryItemName = inner->name;
                }
            });
        }
        return copy;
    }
};

int bakeTemplates(Document& document)
{
    TemplateBaker baker{document, {}};
    int count = 0;
    for (Timeline* timeline : allTimelines(document))
        eachElement(*timeline, [&](Element* element) {
            if (element->elementType() != Element::Type::SymbolInstance)
                return;
            auto* instance = static_cast<SymbolInstance*>(element);
            if (!templates::any(*instance))
                return;
            const templates::Values values = templates::valuesOf(*instance);
            instance->extraAttributes = templates::withValues(*instance, {});
            Symbol* symbol = baker.baked(instance->symbol, values, 0);
            if (symbol && symbol != instance->symbol)
            {
                instance->symbol = symbol;
                instance->libraryItemName = symbol->name;
                ++count;
            }
        });
    return count;
}

// Track mattes ----------------------------------------------------------------

void removeAttribute(DOMElement& element, const char* name)
{
    element.extraAttributes.erase(std::remove_if(element.extraAttributes.begin(), element.extraAttributes.end(),
                                      [name](const auto& each) { return each.first == name; }),
        element.extraAttributes.end());
}

void bakeMattes(Timeline& timeline, std::vector<std::string>& notes)
{
    for (size_t i = 0; i < timeline.layers.size(); ++i)
    {
        Layer* layer = timeline.layers[i];
        if (!layer)
            continue;
        const compositing::Matte matte = compositing::matteOf(*layer);
        if (matte == compositing::Matte::None)
            continue;
        removeAttribute(*layer, compositing::kMatte);
        if (matte == compositing::Matte::AlphaInverted || matte == compositing::Matte::LumaInverted)
        {
            notes.push_back("Layer " + layer->name + ": an inverted track matte cannot be carried; its matte layer is drawn plainly.");
            continue;
        }
        // The nearest layer above that is not a folder holds the matte.
        int source = -1;
        for (int j = static_cast<int>(i) - 1; j >= 0 && source < 0; --j)
            if (timeline.layers[static_cast<size_t>(j)] && timeline.layers[static_cast<size_t>(j)]->layerType != Layer::Type::Folder)
                source = j;
        Layer* mask = source >= 0 ? timeline.layers[static_cast<size_t>(source)] : nullptr;
        if (!mask || layer->layerType != Layer::Type::Normal ||
            (mask->layerType != Layer::Type::Normal && mask->layerType != Layer::Type::Mask))
            continue;
        mask->layerType = Layer::Type::Mask;
        layer->layerType = Layer::Type::Masked;
        layer->parentLayer = mask;
        layer->parentLayerIndex = source;
        notes.push_back("Layer " + layer->name + ": its track matte became a mask, hard-edged" +
            (matte == compositing::Matte::Luma ? std::string(", by the matte's shape rather than its light.") : std::string(".")));
    }
}

// Adjustment layers -------------------------------------------------------------

double number(const behaviors::Applied& applied, const char* name, int frame, double fallback)
{
    const std::string text = applied.valueAt(name, frame, std::string());
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return text.empty() || end == text.c_str() ? fallback : value;
}

/// The filters and color effect \a effects make on \a frame.
void effectsAt(const std::vector<behaviors::Applied>& effects, int frame, std::vector<Filter>& filters, ColorTransform& color)
{
    for (const behaviors::Applied& applied : effects)
    {
        if (!applied.enabled)
            continue;
        const double amount = std::clamp(number(applied, "amount", frame, 100.0) / 100.0, 0.0, 1.0);
        if (applied.type == "blur")
        {
            Filter blur;
            blur.type = Filter::Type::Blur;
            blur.blurX = blur.blurY = number(applied, "radius", frame, 6.0) * 2.0 * amount;
            blur.quality = 3;
            filters.push_back(blur);
        }
        else if (applied.type == "color")
        {
            Filter adjust;
            adjust.type = Filter::Type::AdjustColor;
            adjust.brightness = number(applied, "brightness", frame, 0.0) * amount;
            adjust.contrast = number(applied, "contrast", frame, 0.0) * amount;
            adjust.saturation = number(applied, "saturation", frame, 0.0) * amount;
            adjust.hue = number(applied, "hue", frame, 0.0) * amount;
            filters.push_back(adjust);
        }
        else if (applied.type == "tint")
        {
            std::string text = applied.valueAt("color", frame, "#3050A0");
            unsigned char rgb[3] = {0x30, 0x50, 0xA0};
            templates::colorOf(text, rgb);
            color.tintMultiplier = amount;
            for (int c = 0; c < 3; ++c)
                color.tintColor[c] = rgb[c];
        }
    }
}

void bakeAdjustments(Document& document, Timeline& timeline, std::vector<std::string>& notes)
{
    for (size_t a = 0; a < timeline.layers.size(); ++a)
    {
        Layer* adjustment = timeline.layers[a];
        if (!adjustment || !compositing::isAdjustment(*adjustment))
            continue;
        const std::vector<behaviors::Applied> effects = compositing::effectsOf(*adjustment);
        removeAttribute(*adjustment, compositing::kAdjustmentLayer);
        removeAttribute(*adjustment, compositing::kAdjustments);
        if (a + 1 >= timeline.layers.size())
            continue;

        // The layers below, into a movie clip of their own.
        auto* clip = new Symbol(nullptr);
        clip->name = uniqueName(document, "Adjusted by " + (adjustment->name.empty() ? std::string("a layer") : adjustment->name));
        clip->href = clip->name + ".xml";
        clip->symbolType = SymbolType::MovieClip;
        auto* inside = new Timeline(clip);
        inside->name = clip->name;
        clip->timelines.push_back(inside);
        const int moved = static_cast<int>(a + 1);
        int length = 1;
        for (size_t i = a + 1; i < timeline.layers.size(); ++i)
        {
            Layer* layer = timeline.layers[i];
            if (!layer)
                continue;
            layer->parent = inside;
            if (layer->parentLayerIndex >= 0)
            {
                layer->parentLayerIndex -= moved;
                if (layer->parentLayerIndex < 0)
                {
                    layer->parentLayerIndex = -1;
                    layer->parentLayer = nullptr;
                }
            }
            for (const Frame* frame : layer->frames)
                if (frame)
                    length = std::max(length, frame->index + std::max(1, frame->duration));
            inside->layers.push_back(layer);
        }
        timeline.layers.resize(a + 1);
        addSymbol(document, clip);
        // An adjustment layer further down now sits inside the clip.
        bakeAdjustments(document, *inside, notes);

        // The adjustment layer shows the clip, with the effects as filters:
        // a keyframe a frame where a setting is keyed, one otherwise.
        bool keyed = false;
        bool hadArt = false;
        for (const behaviors::Applied& applied : effects)
            for (const auto& [name, value] : applied.params)
                keyed = keyed || applied.keyed(name);
        for (Frame* frame : adjustment->frames)
        {
            hadArt = hadArt || (frame && !frame->elements.empty());
            delete frame;
        }
        adjustment->frames.clear();
        adjustment->layerType = Layer::Type::Normal;
        for (int f = 0; f < (keyed ? length : 1); ++f)
        {
            auto* frame = new Frame(adjustment);
            frame->index = f;
            frame->duration = keyed ? 1 : length;
            auto* instance = new SymbolInstance(frame);
            instance->libraryItemName = clip->name;
            instance->symbol = clip;
            instance->symbolType = SymbolType::MovieClip;
            effectsAt(effects, f, instance->filters, instance->colorTransform);
            frame->elements.push_back(instance);
            adjustment->frames.push_back(frame);
        }
        notes.push_back("Adjustment layer " + adjustment->name + ": the layers below it are in the movie clip \"" + clip->name +
            "\", which plays on its own in other apps" + (hadArt ? "; its art no longer limits where the effects reach." : "."));
        return bakeAdjustments(document, timeline, notes);
    }
}

} // namespace

bool needsBaking(const Document& document)
{
    Document& editable = const_cast<Document&>(document);
    for (Timeline* timeline : allTimelines(editable))
    {
        for (const Layer* layer : timeline->layers)
            if (layer && (compositing::isAdjustment(*layer) || compositing::matteOf(*layer) != compositing::Matte::None))
                return true;
        bool templated = false;
        eachElement(*timeline, [&](Element* element) {
            templated = templated || (element->elementType() == Element::Type::SymbolInstance &&
                                         templates::any(*static_cast<SymbolInstance*>(element)));
        });
        if (templated)
            return true;
    }
    return false;
}

void bakeCompositing(Document& document, std::vector<std::string>& notes)
{
    const int templated = bakeTemplates(document);
    if (templated > 0)
        notes.push_back(std::to_string(templated) + " template instance" + (templated == 1 ? "" : "s") +
            " now show symbols with their words and colors written in.");
    for (Timeline* timeline : allTimelines(document))
        bakeMattes(*timeline, notes);
    for (Timeline* timeline : allTimelines(document))
        bakeAdjustments(document, *timeline, notes);
}

} // namespace fla::otherapps
