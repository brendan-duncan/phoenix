#include "other_apps.h"

#include "adjustments.h"
#include "behaviors.h"
#include "frame_bake.h"
#include "layer_commands.h"
#include "library_commands.h"
#include "library_media.h"
#include "templates.h"

#include "../data/bitmap.h"
#include "../data/bitmap_decoder.h"
#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/filter.h"
#include "../data/folder.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/layer.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cmath>
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
                    // And the font given it.
                    const auto font = values.find(templates::fontKey(text->instanceName));
                    if (text->textType != StaticText::TextType::Static && !text->instanceName.empty() && font != values.end() &&
                        !font->second.empty())
                        for (TextRun& run : text->runs)
                            run.face = font->second;
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
                if (value != values.end() && instance->symbol && templates::frameIn(*instance->symbol, value->second, frame) &&
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

// Effects ----------------------------------------------------------------------

constexpr double kPi = 3.14159265358979323846;
// Several masks ---------------------------------------------------------------

/// A copy of \a source's frames at \a at in \a into, a layer of \a type of
/// its own; the layers further down that count past it count one more.
Layer* copiedLayer(const Layer& source, Timeline& into, size_t at, Layer::Type type)
{
    std::vector<Layer*> copies = layers::cloneLayers({&source}, into);
    if (copies.empty())
        return nullptr;
    Layer* copy = copies.front();
    for (size_t k = 1; k < copies.size(); ++k)
        delete copies[k];
    copy->parent = &into;
    copy->layerType = type;
    copy->parentLayer = nullptr;
    copy->parentLayerIndex = -1;
    // Its own edges and further masks are Phoenix's; the copy is plain.
    for (const char* name : {"phoenixMaskFeather", "phoenixMaskExpand", "phoenixMaskInvert", compositing::kMoreMasks})
        removeAttribute(*copy, name);
    into.layers.insert(into.layers.begin() + static_cast<std::ptrdiff_t>(at), copy);
    for (size_t k = at + 1; k < into.layers.size(); ++k)
        if (into.layers[k] && into.layers[k]->parentLayerIndex >= static_cast<int>(at))
            ++into.layers[k]->parentLayerIndex;
    return copy;
}

/// Everything in \a inside put in a movie clip of its own (named after
/// \a name), which becomes its one layer. Returns the instance showing it.
SymbolInstance* wrapAll(Document& document, Timeline& inside, const std::string& name)
{
    return intoClip(document, inside, 0, inside.layers.size(), name);
}

/// \a inside cut where the art of \a cutters is: what it holds in a movie
/// clip with Layer blending, copies of the cutters' art over it in a clip of
/// their own that erases what it covers.
void cutBy(Document& document, Timeline& inside, const std::vector<const Layer*>& cutters, const std::string& name)
{
    if (cutters.empty() || inside.layers.empty())
        return;
    SymbolInstance* both = wrapAll(document, inside, name + " cut");
    both->blendMode = "layer";
    Timeline* cut = both->symbol->timelines.front();
    for (size_t k = 0; k < cutters.size(); ++k)
        copiedLayer(*cutters[k], *cut, k, Layer::Type::Normal);
    SymbolInstance* art = intoClip(document, *cut, 0, cutters.size(), name + " cutter");
    art->blendMode = "erase";
}

void bakeMoreMasks(Document& document, Timeline& timeline, std::vector<std::string>& notes)
{
    for (size_t i = 0; i < timeline.layers.size(); ++i)
    {
        Layer* layer = timeline.layers[i];
        if (!layer)
            continue;
        const std::vector<compositing::MoreMask> more = compositing::moreMasksOf(*layer);
        if (more.empty())
            continue;
        removeAttribute(*layer, compositing::kMoreMasks);
        const int ownIndex = layer->parentLayerIndex;
        if (layer->layerType != Layer::Type::Masked || ownIndex < 0 || ownIndex >= static_cast<int>(timeline.layers.size()))
            continue;
        Layer* own = timeline.layers[static_cast<size_t>(ownIndex)];
        // The masks it names, by name; one not found is left out.
        std::vector<const Layer*> adds;
        std::vector<const Layer*> subtracts;
        std::vector<const Layer*> intersects;
        for (const compositing::MoreMask& each : more)
            for (const Layer* other : timeline.layers)
                if (other && other != own && other->layerType == Layer::Type::Mask && other->name == each.layer)
                {
                    (each.mode == compositing::MaskCombine::Add ? adds
                            : each.mode == compositing::MaskCombine::Subtract ? subtracts
                                                                              : intersects)
                        .push_back(other);
                    break;
                }
        if (adds.empty() && subtracts.empty() && intersects.empty())
            continue;
        const std::string name = layer->name;

        // The layer's art into a movie clip, its place taken by a layer
        // showing it, still masked by its own mask.
        SymbolInstance* shown = intoClip(document, timeline, i, 1, name + " masked");
        Layer* holder = timeline.layers[i];
        holder->name = name;
        holder->layerType = Layer::Type::Masked;
        holder->parentLayer = own;
        holder->parentLayerIndex = ownIndex;
        Symbol* clip = shown->symbol;
        Timeline* inside = clip->timelines.front();
        inside->layers.front()->layerType = Layer::Type::Normal;
        // Inside it: each Intersect mask over what is there, one inside the
        // other; then the Subtract masks cut it.
        for (const Layer* mask : intersects)
        {
            if (inside->layers.size() > 1)
                wrapAll(document, *inside, name + " overlapped");
            copiedLayer(*mask, *inside, 0, Layer::Type::Mask);
            inside->layers[1]->layerType = Layer::Type::Masked;
            inside->layers[1]->parentLayer = inside->layers[0];
            inside->layers[1]->parentLayerIndex = 0;
        }
        cutBy(document, *inside, subtracts, name);

        // Each Add mask: another layer showing the clip, masked by it, less
        // what its own mask and the Add masks before it show already, so
        // nothing shows twice. It goes just under the own mask's layers.
        size_t below = static_cast<size_t>(ownIndex) + 1;
        while (below < timeline.layers.size() && timeline.layers[below] && timeline.layers[below]->parentLayerIndex == ownIndex &&
            timeline.layers[below]->layerType == Layer::Type::Masked)
            ++below;
        std::vector<const Layer*> before = {own};
        int length = 1;
        for (const Frame* frame : holder->frames)
            if (frame)
                length = std::max(length, frame->index + std::max(1, frame->duration));
        for (const Layer* mask : adds)
        {
            auto* added = new Symbol(nullptr);
            added->name = uniqueName(document, name + " added by " + mask->name);
            added->href = added->name + ".xml";
            added->symbolType = SymbolType::MovieClip;
            auto* addedInside = new Timeline(added);
            addedInside->name = added->name;
            added->timelines.push_back(addedInside);
            auto* content = new Layer(addedInside);
            content->name = name;
            content->layerType = Layer::Type::Masked;
            auto* frame = new Frame(content);
            frame->index = 0;
            frame->duration = length;
            auto* instance = new SymbolInstance(frame);
            instance->libraryItemName = clip->name;
            instance->symbol = clip;
            instance->symbolType = SymbolType::MovieClip;
            frame->elements.push_back(instance);
            content->frames.push_back(frame);
            addedInside->layers.push_back(content);
            copiedLayer(*mask, *addedInside, 0, Layer::Type::Mask);
            content->parentLayer = addedInside->layers[0];
            content->parentLayerIndex = 0;
            cutBy(document, *addedInside, before, added->name);
            addSymbol(document, added);
            before.push_back(mask);

            auto* place = new Layer(&timeline);
            place->name = added->name;
            auto* placeFrame = new Frame(place);
            placeFrame->index = 0;
            placeFrame->duration = length;
            auto* placed = new SymbolInstance(placeFrame);
            placed->libraryItemName = added->name;
            placed->symbol = added;
            placed->symbolType = SymbolType::MovieClip;
            placeFrame->elements.push_back(placed);
            place->frames.push_back(placeFrame);
            timeline.layers.insert(timeline.layers.begin() + static_cast<std::ptrdiff_t>(below), place);
            for (size_t k = below + 1; k < timeline.layers.size(); ++k)
                if (timeline.layers[k] && timeline.layers[k]->parentLayerIndex >= static_cast<int>(below))
                    ++timeline.layers[k]->parentLayerIndex;
            ++below;
        }
        notes.push_back("Layer " + name + ": its other masks became the movie clip \"" + clip->name + "\"" +
            (adds.empty() ? std::string() : std::string(" and a layer showing it for each Add mask, under its mask's layers")) +
            ", which play on their own in other apps; the masks are hard-edged there.");
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

void colorOf(const behaviors::Applied& applied, const char* name, int frame, const char* fallback, uint8_t rgb[3])
{
    unsigned char parsed[3] = {0, 0, 0};
    templates::colorOf(fallback, parsed);
    templates::colorOf(applied.valueAt(name, frame, fallback), parsed);
    for (int c = 0; c < 3; ++c)
        rgb[c] = parsed[c];
}

/// Whether any setting of \a effects is keyed, so they change over time.
bool keyedAny(const std::vector<behaviors::Applied>& effects)
{
    for (const behaviors::Applied& applied : effects)
        for (const auto& [name, value] : applied.params)
            if (applied.keyed(name))
                return true;
    return false;
}

/// Whether every one of \a effects can be carried on each of \a frames
/// frames from \a first, as filters and a color effect over \a color.
bool carriedThroughout(const std::vector<behaviors::Applied>& effects, int first, int frames, const ColorTransform& color)
{
    for (int f = first; f < first + (keyedAny(effects) ? std::max(1, frames) : 1); ++f)
    {
        std::vector<Filter> filters;
        ColorTransform each = color;
        if (!carryEffects(effects, f, filters, each))
            return false;
    }
    return true;
}

/// The names of the effects that cannot be carried on any of \a frames
/// frames from \a first, for a note: "Ripple has", "Ripple and Grain have".
std::string uncarried(const std::vector<behaviors::Applied>& effects, int first, int frames, const ColorTransform& color)
{
    std::set<std::string> names;
    std::vector<std::string> ordered;
    for (int f = first; f < first + (keyedAny(effects) ? std::max(1, frames) : 1); ++f)
    {
        std::vector<Filter> filters;
        ColorTransform each = color;
        std::vector<std::string> left;
        carryEffects(effects, f, filters, each, &left);
        for (const std::string& name : left)
            if (names.insert(name).second)
                ordered.push_back(name);
    }
    std::string text;
    for (size_t i = 0; i < ordered.size(); ++i)
        text += (i == 0 ? "" : i + 1 == ordered.size() ? " and " : ", ") + ordered[i];
    return text + (ordered.size() == 1 ? " has" : " have");
}

// Bitmaps of what effects make --------------------------------------------------

/// Where a bitmap is put on a frame: the library bitmap, its top left corner
/// in the timeline's space and its pixels to the timeline's pixel. No
/// bitmap when nothing shows.
struct Placed
{
    std::string bitmap;
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

/// The pictures drawn for effects, made library bitmaps as they come (one
/// for each different picture) so only where they go is kept.
struct Bitmaps
{
    Document& document;
    std::map<uint64_t, std::string> made;

    Placed add(const Picture& picture, const std::string& name)
    {
        Placed placed;
        const int width = picture.width, height = picture.height;
        if (width <= 0 || height <= 0 || picture.pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height))
            return placed;
        // Trimmed to what shows.
        int left = width, top = height, right = -1, bottom = -1;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (picture.pixels[static_cast<size_t>(y) * width + x] >> 24)
                {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        if (right < left || bottom < top)
            return placed;
        const int w = right - left + 1, h = bottom - top + 1;
        std::vector<uint32_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y)
            std::copy_n(picture.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(top + y) * width + left), w,
                pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * w));
        const double scale = picture.scale > 0.0 ? picture.scale : 1.0;
        placed.x = picture.x + left / scale;
        placed.y = picture.y + top / scale;
        placed.scale = scale;
        // The same picture again is the same bitmap.
        uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        mix(static_cast<uint64_t>(w));
        mix(static_cast<uint64_t>(h));
        for (const uint32_t p : pixels)
            mix(p);
        const auto found = made.find(hash);
        if (found != made.end())
        {
            placed.bitmap = found->second;
            return placed;
        }
        const char* const folderName = "Baked effects";
        if (std::none_of(document.folders.begin(), document.folders.end(),
                [folderName](const Folder* folder) { return folder && folder->name == folderName; }))
        {
            auto* folder = new Folder(&document);
            folder->name = folderName;
            folder->itemId = media::newItemId();
            document.folders.push_back(folder);
        }
        std::string base = name;
        for (char& c : base)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = ' ';
        Bitmap* bitmap = media::newBitmap(document, std::string(folderName) + "/" + base, std::string(), w, h,
            encodeLosslessBitmap(w, h, pixels), false);
        bitmap->parent = &document;
        document.resources.push_back(bitmap);
        made[hash] = bitmap->name;
        placed.bitmap = bitmap->name;
        return placed;
    }
};

/// \a placed on \a frame, or nothing when nothing shows.
BitmapInstance* bitmapOn(Frame* frame, const Placed& placed)
{
    if (placed.bitmap.empty())
        return nullptr;
    auto* instance = new BitmapInstance(frame);
    instance->libraryItemName = placed.bitmap;
    instance->transform = Transform(1.0 / placed.scale, 0.0, 0.0, 1.0 / placed.scale, placed.x, placed.y);
    return instance;
}

/// What the renderer drew for the export, before anything changed: the
/// adjustment layers drawn with all below them a frame at a time, and the
/// instances drawn on their own over their keyframes' frames.
struct Drawn
{
    std::map<const Layer*, std::vector<Placed>> layers;
    std::map<const Element*, std::map<int, Placed>> instances;
};

std::string named(const std::string& name, const char* fallback)
{
    return name.empty() ? std::string(fallback) : name;
}

void drawForBitmaps(Document& document, const Renderer& render, Bitmaps& bitmaps, Drawn& drawn)
{
    for (Timeline* timeline : allTimelines(document))
    {
        const int count = static_cast<int>(timeline->layers.size());
        for (int a = 0; a < count; ++a)
        {
            Layer* layer = timeline->layers[static_cast<size_t>(a)];
            if (!layer)
                continue;
            if (compositing::isAdjustment(*layer))
            {
                const std::vector<behaviors::Applied> effects = compositing::effectsOf(*layer);
                const std::vector<Layer*> below(timeline->layers.begin() + a + 1, timeline->layers.end());
                const int length = lengthOf(below);
                if (below.empty() || carriedThroughout(effects, 0, length, ColorTransform()))
                    continue;
                // It and everything below it, a picture a frame; what is
                // below is in the pictures, so nothing more is drawn there.
                std::vector<Placed>& placed = drawn.layers[layer];
                for (int f = 0; f < length; ++f)
                {
                    Drawing drawing;
                    drawing.timeline = timeline;
                    drawing.from = a;
                    drawing.to = count;
                    drawing.frame = f;
                    drawing.margin = compositing::reach(effects, f);
                    placed.push_back(bitmaps.add(render(drawing), named(layer->name, "Adjustment") + " " + std::to_string(f + 1)));
                }
                break;
            }
            for (Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                for (Element* element : frame->elements)
                {
                    if (!element || element->elementType() != Element::Type::SymbolInstance ||
                        !compositing::hasInstanceEffects(*element))
                        continue;
                    auto* instance = static_cast<SymbolInstance*>(element);
                    const std::vector<behaviors::Applied> effects = compositing::instanceEffectsOf(*instance);
                    const int span = std::max(1, frame->duration);
                    if (carriedThroughout(effects, frame->index, span, instance->colorTransform))
                        continue;
                    std::map<int, Placed>& placed = drawn.instances[element];
                    const std::string name = named(instance->name, named(instance->libraryItemName, "Instance").c_str());
                    for (int f = frame->index; f < frame->index + span; ++f)
                    {
                        Drawing drawing;
                        drawing.timeline = timeline;
                        drawing.from = a;
                        drawing.to = a + 1;
                        drawing.element = element;
                        drawing.frame = f;
                        drawing.margin = compositing::reach(effects, f);
                        placed[f] = bitmaps.add(render(drawing), name + " " + std::to_string(f + 1));
                    }
                }
            }
        }
    }
}

// Effects on instances ------------------------------------------------------------

void bakeInstanceEffects(Document& document, Timeline& timeline, const Drawn& drawn, std::vector<std::string>& notes)
{
    for (Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        // A keyframe at a time; frames made of one are passed over.
        for (size_t k = 0; k < layer->frames.size(); ++k)
        {
            Frame* key = layer->frames[k];
            if (!key)
                continue;
            struct Target
            {
                size_t index;
                const SymbolInstance* original;
                std::vector<behaviors::Applied> effects;
                ColorTransform color;
                const std::map<int, Placed>* placed;
            };
            std::vector<Target> targets;
            bool split = false;
            for (size_t e = 0; e < key->elements.size(); ++e)
            {
                Element* element = key->elements[e];
                if (!element || element->elementType() != Element::Type::SymbolInstance)
                    continue;
                const std::vector<behaviors::Applied> effects = compositing::instanceEffectsOf(*element);
                if (effects.empty())
                    continue;
                const auto found = drawn.instances.find(element);
                const std::map<int, Placed>* placed = found == drawn.instances.end() ? nullptr : &found->second;
                targets.push_back({e, static_cast<SymbolInstance*>(element), effects,
                    static_cast<SymbolInstance*>(element)->colorTransform, placed});
                // Keyed settings or bitmaps change from frame to frame.
                split = split || placed || keyedAny(effects);
            }
            if (targets.empty())
                continue;
            const int first = key->index;
            const int span = std::max(1, key->duration);
            for (const Target& target : targets)
            {
                const std::string what = "Instance of " + named(target.original->libraryItemName, "a symbol") +
                    (target.original->name.empty() ? std::string() : " (" + target.original->name + ")") + " on layer " + layer->name;
                if (target.placed)
                    notes.push_back(what + ": " + uncarried(target.effects, first, span, target.color) +
                        " no filter in other apps, so it is drawn into a bitmap on each frame where it changes.");
                else if (!carriedThroughout(target.effects, first, span, target.color))
                    notes.push_back(what + ": " + uncarried(target.effects, first, span, target.color) +
                        " no filter in other apps and is left out.");
            }
            if (split && span > 1)
                frames::convertToFrameByFrame(*layer, first, first + span - 1, document.frameRate);
            // Each frame of the span, now a keyframe each when split.
            const int last = first + span - 1;
            for (Frame* frame : layer->frames)
            {
                if (!frame || frame->index < first || frame->index > last)
                    continue;
                for (const Target& target : targets)
                {
                    if (target.index >= frame->elements.size() ||
                        frame->elements[target.index]->elementType() != Element::Type::SymbolInstance)
                        continue;
                    auto* instance = static_cast<SymbolInstance*>(frame->elements[target.index]);
                    removeAttribute(*instance, compositing::kInstanceEffects);
                    if (target.placed)
                    {
                        // The instance as drawn, effects and all.
                        const auto at = target.placed->find(frame->index);
                        BitmapInstance* bitmap = bitmapOn(frame, at == target.placed->end() ? Placed() : at->second);
                        if (bitmap)
                            frame->elements[target.index] = bitmap;
                        else
                            frame->elements[target.index] = nullptr;
                        delete instance;
                        continue;
                    }
                    // Its effects before the filters it had.
                    std::vector<Filter> filters;
                    carryEffects(target.effects, frame->index, filters, instance->colorTransform);
                    instance->filters.insert(instance->filters.begin(), filters.begin(), filters.end());
                }
                frame->elements.erase(std::remove(frame->elements.begin(), frame->elements.end(), nullptr), frame->elements.end());
            }
            if (split && span > 1)
            {
                frames::mergeHeldFrames(*layer, first, last);
                // The frames now there are this keyframe's; on to the next.
                while (k + 1 < layer->frames.size() && layer->frames[k + 1] && layer->frames[k + 1]->index <= last)
                    ++k;
            }
        }
    }
}

/// Instances in groups, which bakeInstanceEffects passes over: their
/// effects carried as filters where they can be, on their keyframe's
/// first frame.
void bakeGroupedEffects(Timeline& timeline, std::vector<std::string>& notes)
{
    for (Layer* layer : timeline.layers)
        for (Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            for (Element* element : frame ? frame->elements : std::vector<Element*>())
                eachElement(element, [&](Element* each) {
                    if (each->elementType() != Element::Type::SymbolInstance)
                        return;
                    auto* instance = static_cast<SymbolInstance*>(each);
                    const std::vector<behaviors::Applied> effects = compositing::instanceEffectsOf(*instance);
                    if (effects.empty())
                        return;
                    removeAttribute(*instance, compositing::kInstanceEffects);
                    std::vector<Filter> filters;
                    std::vector<std::string> left;
                    if (!carryEffects(effects, frame->index, filters, instance->colorTransform, &left))
                        notes.push_back("Instance of " + named(instance->libraryItemName, "a symbol") + " in a group on layer " +
                            layer->name + ": " + uncarried(effects, frame->index, 1, ColorTransform()) +
                            " no filter in other apps and is left out.");
                    instance->filters.insert(instance->filters.begin(), filters.begin(), filters.end());
                });
}

// Adjustment layers -------------------------------------------------------------

void bakeAdjustments(Document& document, Timeline& timeline, const Drawn& drawn, std::vector<std::string>& notes)
{
    for (size_t a = 0; a < timeline.layers.size(); ++a)
    {
        Layer* adjustment = timeline.layers[a];
        if (!adjustment || !compositing::isAdjustment(*adjustment))
            continue;
        const std::vector<behaviors::Applied> effects = compositing::effectsOf(*adjustment);
        const auto found = drawn.layers.find(adjustment);
        const std::vector<Placed>* pictures = found == drawn.layers.end() ? nullptr : &found->second;
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
        bakeAdjustments(document, *inside, drawn, notes);

        if (pictures)
        {
            // Drawn: the layer shows what it and everything below it made,
            // a bitmap a frame, a keyframe only where the picture changes.
            for (Frame* frame : adjustment->frames)
                delete frame;
            adjustment->frames.clear();
            adjustment->layerType = Layer::Type::Normal;
            for (int f = 0; f < static_cast<int>(pictures->size()); ++f)
            {
                const Placed& placed = (*pictures)[static_cast<size_t>(f)];
                Frame* held = adjustment->frames.empty() ? nullptr : adjustment->frames.back();
                const Placed* before = f > 0 ? &(*pictures)[static_cast<size_t>(f - 1)] : nullptr;
                if (held && before && before->bitmap == placed.bitmap && before->x == placed.x && before->y == placed.y &&
                    before->scale == placed.scale)
                {
                    ++held->duration;
                    continue;
                }
                auto* frame = new Frame(adjustment);
                frame->index = f;
                frame->duration = 1;
                if (BitmapInstance* bitmap = bitmapOn(frame, placed))
                    frame->elements.push_back(bitmap);
                adjustment->frames.push_back(frame);
            }
            notes.push_back("Adjustment layer " + adjustment->name + ": " + uncarried(effects, 0, length, ColorTransform()) +
                " no filter in other apps, so it and the layers below it are drawn into a bitmap on each frame "
                "where the picture changes; the layers themselves are kept in the movie clip \"" + clip->name +
                "\" in the library.");
            return bakeAdjustments(document, timeline, drawn, notes);
        }
        if (!carriedThroughout(effects, 0, length, ColorTransform()))
            notes.push_back("Adjustment layer " + adjustment->name + ": " + uncarried(effects, 0, length, ColorTransform()) +
                " no filter in other apps and is left out.");

        // The adjustment layer shows the clip, with the effects as filters:
        // a keyframe a frame where a setting is keyed, one otherwise.
        const bool keyed = keyedAny(effects);
        bool hadArt = false;
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
            carryEffects(effects, f, instance->filters, instance->colorTransform);
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
        return bakeAdjustments(document, timeline, drawn, notes);
    }
}

} // namespace

bool carryEffects(const std::vector<behaviors::Applied>& effects, int frame, std::vector<Filter>& filters,
    ColorTransform& color, std::vector<std::string>* left)
{
    bool all = true;
    // The color effect holds one of them, and only when nothing else is in
    // it: the instance's own tint, say, cannot be mixed with another.
    bool colorFree = color.isIdentity();
    for (const behaviors::Applied& applied : effects)
    {
        const behaviors::Definition* definition = compositing::find(applied.type);
        if (!applied.enabled || !definition)
            continue;
        // At 0 Phoenix leaves the picture as it is.
        const double setting = number(applied, "amount", frame, 100.0);
        if (setting <= 0.0)
            continue;
        const double amount = std::clamp(setting / 100.0, 0.0, 1.0);
        const std::string& type = applied.type;
        const auto value = [&](const char* name, double fallback) { return number(applied, name, frame, fallback); };
        bool carried = true;
        if (type == "blur")
        {
            Filter blur;
            blur.type = Filter::Type::Blur;
            blur.blurX = blur.blurY = value("radius", 6.0) * 2.0 * amount;
            blur.quality = 3;
            filters.push_back(blur);
        }
        else if (type == "dirblur")
        {
            // A smear straight across or down is a Blur on that axis alone,
            // one box as Phoenix's is; a slanted one is not.
            const double length = value("length", 20.0) * amount;
            const double angle = value("angle", 0.0) * kPi / 180.0;
            const double across = std::fabs(std::cos(angle)), down = std::fabs(std::sin(angle));
            const double slant = std::sin(15.0 * kPi / 180.0);
            if (length >= 0.5 && down > slant && across > slant)
                carried = false;
            else if (length >= 0.5)
            {
                Filter blur;
                blur.type = Filter::Type::Blur;
                blur.blurX = down <= slant ? length : 0.0;
                blur.blurY = across <= slant ? length : 0.0;
                blur.quality = 1;
                filters.push_back(blur);
            }
        }
        else if (type == "lensblur")
        {
            // Without its bright spots blooming it is a plain blur.
            if (value("highlights", 50.0) > 0.0)
                carried = false;
            else if (value("radius", 8.0) >= 0.5)
            {
                Filter blur;
                blur.type = Filter::Type::Blur;
                blur.blurX = blur.blurY = value("radius", 8.0) * 2.0 * amount;
                blur.quality = 2;
                filters.push_back(blur);
            }
        }
        else if (type == "color" || type == "huesat")
        {
            const bool hueSat = type == "huesat";
            const auto truthy = [](const std::string& text) { return text == "true" || text == "1"; };
            if (hueSat && truthy(applied.valueAt("colorize", frame, "false")))
                carried = false;
            else
            {
                Filter adjust;
                adjust.type = Filter::Type::AdjustColor;
                adjust.brightness = value(hueSat ? "lightness" : "brightness", 0.0) * amount;
                adjust.contrast = hueSat ? 0.0 : value("contrast", 0.0) * amount;
                adjust.saturation = value("saturation", 0.0) * amount;
                adjust.hue = value("hue", 0.0) * amount;
                filters.push_back(adjust);
            }
        }
        else if (type == "glow" || type == "outline")
        {
            // A Glow in the color asked for: an outline is a glow strong
            // enough to be solid near the edge, as Phoenix draws it; a glow
            // spreads from the whole shape rather than its bright parts.
            const bool outline = type == "outline";
            const double reach = value(outline ? "width" : "radius", outline ? 4.0 : 12.0);
            if (reach > 0.0)
            {
                Filter glow;
                glow.type = Filter::Type::Glow;
                glow.blurX = glow.blurY = reach * 2.0;
                glow.quality = 3;
                glow.strength = outline ? 3.0 : std::max(0.0, value("intensity", 100.0) / 100.0);
                const std::string own = applied.valueAt("color", frame, outline ? "#FFFFFF" : "");
                colorOf(applied, "color", frame, own.empty() ? "#FFFFFF" : own.c_str(), glow.color);
                glow.color[3] = static_cast<uint8_t>(std::lround(255.0 * amount));
                filters.push_back(glow);
            }
        }
        else if (type == "tint" || type == "fill")
        {
            // Fill is a full tint: the color, keeping the alpha.
            if (!colorFree)
                carried = false;
            else
            {
                uint8_t rgb[3];
                colorOf(applied, "color", frame, type == "tint" ? "#3050A0" : "#FF4060", rgb);
                color.tintMultiplier = amount;
                for (int c = 0; c < 3; ++c)
                    color.tintColor[c] = rgb[c];
                colorFree = false;
            }
        }
        else if (type == "levels")
        {
            // A straight line from the input range to the output one is a
            // multiplier and an offset, so long as it does not stretch
            // (the color effect goes up to 100 percent) and has no gamma.
            const double inBlack = value("inBlack", 0.0);
            const double inWhite = std::max(inBlack + 1.0, value("inWhite", 255.0));
            const double outBlack = value("outBlack", 0.0), outWhite = value("outWhite", 255.0);
            const double slope = (outWhite - outBlack) / (inWhite - inBlack);
            const double multiplier = 1.0 + (slope - 1.0) * amount;
            const double offset = (outBlack - inBlack * slope) * amount;
            if (std::fabs(value("gamma", 1.0) - 1.0) > 0.005 || multiplier > 1.0 + 1e-6 || multiplier < 0.0 ||
                std::fabs(offset) > 255.0)
                carried = false;
            else if (std::fabs(multiplier - 1.0) > 1e-6 || std::fabs(offset) >= 0.5)
            {
                if (!colorFree)
                    carried = false;
                else
                {
                    color.redMultiplier = color.greenMultiplier = color.blueMultiplier = multiplier;
                    color.redOffset = color.greenOffset = color.blueOffset = static_cast<int>(std::lround(offset));
                    colorFree = false;
                }
            }
        }
        else if (type == "linearwipe" || type == "radialwipe" || type == "blinds" || type == "dissolve")
        {
            // Not started: nothing; finished: nothing left to see.
            const double completion = value("completion", 0.0);
            if (completion >= 100.0)
                color.alphaMultiplier = 0.0;
            else if (completion > 0.0)
                carried = false;
        }
        else if (type == "curves")
        {
            // Points on the straight line change nothing.
            const std::string points = applied.valueAt("points", frame, "0,0 255,255");
            const char* at = points.c_str();
            char* end = nullptr;
            for (;;)
            {
                const double in = std::strtod(at, &end);
                if (end == at)
                    break;
                at = end;
                while (*at == ',' || *at == ' ')
                    ++at;
                const double out = std::strtod(at, &end);
                if (end == at)
                    break;
                at = end;
                while (*at == ',' || *at == ' ')
                    ++at;
                carried = carried && std::fabs(in - out) < 0.5;
            }
        }
        // The rest change nothing at these settings and are otherwise
        // beyond a filter.
        else if (type == "grain")
            carried = value("strength", 20.0) <= 0.0;
        else if (type == "vignette")
            carried = value("darkness", 60.0) <= 0.0;
        else if (type == "chromatic")
            carried = value("distance", 4.0) == 0.0;
        else if (type == "bulge")
            carried = value("height", 50.0) == 0.0;
        else if (type == "ripple")
            carried = value("height", 6.0) == 0.0;
        else if (type == "bend")
            carried = value("strength", 30.0) == 0.0;
        else if (type == "radialblur")
            carried = value("strength", 10.0) == 0.0;
        else if (type == "displace")
            carried = applied.valueAt("layer", frame, "").empty();
        else
            carried = false;
        if (!carried)
        {
            all = false;
            if (left)
                left->push_back(definition->name);
        }
    }
    return all;
}

bool needsBaking(const Document& document)
{
    Document& editable = const_cast<Document&>(document);
    for (Timeline* timeline : allTimelines(editable))
    {
        for (const Layer* layer : timeline->layers)
            if (layer && (compositing::isAdjustment(*layer) || compositing::matteOf(*layer) != compositing::Matte::None ||
                             !compositing::moreMasksOf(*layer).empty()))
                return true;
        bool found = false;
        eachElement(*timeline, [&](Element* element) {
            found = found || (element->elementType() == Element::Type::SymbolInstance &&
                                 (templates::any(*static_cast<SymbolInstance*>(element)) ||
                                     !compositing::instanceEffectsOf(*element).empty()));
        });
        if (found)
            return true;
    }
    return false;
}

void bakeCompositing(Document& document, std::vector<std::string>& notes, const Renderer& render)
{
    // What effects with no filter make is drawn first, while the document
    // is still as Phoenix shows it.
    Bitmaps bitmaps{document, {}};
    Drawn drawn;
    if (render)
        drawForBitmaps(document, render, bitmaps, drawn);
    const int templated = bakeTemplates(document);
    if (templated > 0)
        notes.push_back(std::to_string(templated) + " template instance" + (templated == 1 ? "" : "s") +
            " now show symbols with their words and colors written in.");
    for (Timeline* timeline : allTimelines(document))
    {
        bakeInstanceEffects(document, *timeline, drawn, notes);
        bakeGroupedEffects(*timeline, notes);
    }
    for (Timeline* timeline : allTimelines(document))
        bakeMoreMasks(document, *timeline, notes);
    for (Timeline* timeline : allTimelines(document))
        bakeMattes(document, *timeline, notes);
    for (Timeline* timeline : allTimelines(document))
        bakeAdjustments(document, *timeline, drawn, notes);
}

} // namespace fla::otherapps
