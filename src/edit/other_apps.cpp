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

std::string uniqueName(const Document& document, const std::string& wanted)
{
    // A symbol's name is also its file's in the library folder: no
    // characters a file name cannot have, nor a slash, which makes a folder.
    std::string base = wanted;
    for (char& c : base)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = ' ';
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
                // A slider: a graphic held on its frame, which other apps
                // show without a script.
                int frame = 0;
                if (value != values.end() && templates::frameOf(value->second, frame) &&
                    instance->symbolType == SymbolType::MovieClip)
                {
                    instance->symbolType = SymbolType::Graphic;
                    instance->loopType = LoopType::SingleFrame;
                    instance->firstFrame = frame - 1;
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

/// The frames \a layers run to.
int lengthOf(const std::vector<Layer*>& layers)
{
    int length = 1;
    for (const Layer* layer : layers)
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame)
                length = std::max(length, frame->index + std::max(1, frame->duration));
    return length;
}

/// Moves \a count layers of \a timeline from \a first into a new movie clip
/// named after \a name, and puts a layer in their place showing it for as
/// long as they ran. Returns the instance shown. The layers keep their
/// parents among themselves; one below that followed one of them follows
/// the new layer.
SymbolInstance* intoClip(Document& document, Timeline& timeline, size_t first, size_t count, const std::string& name)
{
    auto* clip = new Symbol(nullptr);
    clip->name = uniqueName(document, name);
    clip->href = clip->name + ".xml";
    clip->symbolType = SymbolType::MovieClip;
    auto* inside = new Timeline(clip);
    inside->name = clip->name;
    clip->timelines.push_back(inside);
    const std::vector<Layer*> moved(timeline.layers.begin() + static_cast<std::ptrdiff_t>(first),
        timeline.layers.begin() + static_cast<std::ptrdiff_t>(first + count));
    const int length = lengthOf(moved);
    for (Layer* layer : moved)
    {
        if (!layer)
            continue;
        layer->parent = inside;
        if (layer->parentLayerIndex >= 0)
        {
            const int index = layer->parentLayerIndex - static_cast<int>(first);
            if (index >= 0 && index < static_cast<int>(count))
                layer->parentLayerIndex = index;
            else
            {
                layer->parentLayerIndex = -1;
                layer->parentLayer = nullptr;
            }
        }
        inside->layers.push_back(layer);
    }
    auto* holder = new Layer(&timeline);
    holder->name = clip->name;
    auto* frame = new Frame(holder);
    frame->index = 0;
    frame->duration = length;
    auto* instance = new SymbolInstance(frame);
    instance->libraryItemName = clip->name;
    instance->symbol = clip;
    instance->symbolType = SymbolType::MovieClip;
    frame->elements.push_back(instance);
    holder->frames.push_back(frame);
    timeline.layers.erase(timeline.layers.begin() + static_cast<std::ptrdiff_t>(first),
        timeline.layers.begin() + static_cast<std::ptrdiff_t>(first + count));
    timeline.layers.insert(timeline.layers.begin() + static_cast<std::ptrdiff_t>(first), holder);
    // The layers further down count from the new layer.
    for (size_t i = first + 1; i < timeline.layers.size(); ++i)
    {
        Layer* layer = timeline.layers[i];
        if (!layer || layer->parentLayerIndex < static_cast<int>(first))
            continue;
        if (layer->parentLayerIndex < static_cast<int>(first + count))
        {
            layer->parentLayerIndex = static_cast<int>(first);
            layer->parentLayer = holder;
        }
        else
            layer->parentLayerIndex -= static_cast<int>(count) - 1;
    }
    addSymbol(document, clip);
    return instance;
}

void bakeMattes(Document& document, Timeline& timeline, std::vector<std::string>& notes)
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
        // The nearest layer above that is not a folder holds the matte.
        int source = -1;
        for (int j = static_cast<int>(i) - 1; j >= 0 && source < 0; --j)
            if (timeline.layers[static_cast<size_t>(j)] && timeline.layers[static_cast<size_t>(j)]->layerType != Layer::Type::Folder)
                source = j;
        if (matte == compositing::Matte::AlphaInverted || matte == compositing::Matte::LumaInverted)
        {
            // Cut out of the picture: the matte and the layer it cuts in a
            // movie clip with Layer blending, the matte's art over the layer
            // in a clip of its own that erases what it covers.
            Layer* cutter = source == static_cast<int>(i) - 1 ? timeline.layers[static_cast<size_t>(source)] : nullptr;
            if (!cutter || cutter->layerType != Layer::Type::Normal || layer->layerType != Layer::Type::Normal)
            {
                notes.push_back("Layer " + layer->name + ": an inverted track matte with no plain layer just above it to cut by is left out; the layer is drawn plainly.");
                continue;
            }
            const std::string name = layer->name;
            SymbolInstance* both = intoClip(document, timeline, static_cast<size_t>(source), 2, name + " cut by its matte");
            both->blendMode = "layer";
            Timeline* inside = both->symbol->timelines.front();
            SymbolInstance* art = intoClip(document, *inside, 0, 1, name + " matte");
            art->blendMode = "erase";
            notes.push_back("Layer " + name + ": its inverted track matte became the movie clip \"" + both->symbol->name +
                "\", the matte erasing the layer with Layer and Erase blending, which plays on its own in other apps" +
                (matte == compositing::Matte::LumaInverted ? std::string("; it cuts by the matte's shape rather than its light.") : std::string(".")));
            i = static_cast<size_t>(source);
            continue;
        }
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
        for (const behaviors::Applied& applied : effects)
            if (applied.enabled && (applied.type == "turbulent" || applied.type == "wave"))
                notes.push_back("Adjustment layer " + adjustment->name + ": " +
                    (applied.type == "turbulent" ? std::string("Turbulent Displace") : std::string("Wave Displace")) +
                    " has no filter in other apps and is left out.");
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
            hadArt = hadArt || (frame && !frame->elements.empty());
        // Art on it limits where the effects reach: it becomes a mask over
        // the adjusted clip, with the clip as it is under both.
        Layer* art = nullptr;
        if (hadArt)
        {
            art = new Layer(&timeline);
            art->name = adjustment->name + " art";
            art->layerType = Layer::Type::Mask;
            art->frames = adjustment->frames;
            for (Frame* frame : art->frames)
                if (frame)
                    frame->parent = art;
        }
        else
            for (Frame* frame : adjustment->frames)
                delete frame;
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
        if (art)
        {
            auto* plain = new Layer(&timeline);
            plain->name = clip->name;
            auto* frame = new Frame(plain);
            frame->index = 0;
            frame->duration = length;
            auto* instance = new SymbolInstance(frame);
            instance->libraryItemName = clip->name;
            instance->symbol = clip;
            instance->symbolType = SymbolType::MovieClip;
            frame->elements.push_back(instance);
            plain->frames.push_back(frame);
            timeline.layers.insert(timeline.layers.begin() + static_cast<std::ptrdiff_t>(a), art);
            timeline.layers.insert(timeline.layers.begin() + static_cast<std::ptrdiff_t>(a) + 2, plain);
            adjustment->layerType = Layer::Type::Masked;
            adjustment->parentLayer = art;
            adjustment->parentLayerIndex = static_cast<int>(a);
            // Layers above that counted past it count two more.
            for (size_t i = 0; i < a; ++i)
                if (timeline.layers[i] && timeline.layers[i]->parentLayerIndex >= static_cast<int>(a))
                    timeline.layers[i]->parentLayerIndex += 2;
        }
        notes.push_back("Adjustment layer " + adjustment->name + ": the layers below it are in the movie clip \"" + clip->name +
            "\", which plays on its own in other apps" +
            (hadArt ? "; its art became a mask over the adjusted copy, so the effects reach only where it is, hard-edged." : "."));
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
        bakeMattes(document, *timeline, notes);
    for (Timeline* timeline : allTimelines(document))
        bakeAdjustments(document, *timeline, notes);
}

} // namespace fla::otherapps
