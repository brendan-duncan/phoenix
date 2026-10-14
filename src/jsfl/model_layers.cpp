// Whole layers from scripts: copying, cutting, pasting and duplicating them
// with what they hold, putting a layer into a folder, mask or guide, and the
// filters kept on a layer's frames.

#include "extensions.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/filter.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "../edit/layer_commands.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"
#include "../edit/timeline_edit.h"
#include "../third_party/tinyxml2/tinyxml2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace jsfl {

namespace {

bool isEdited(const Timeline& t)
{
    return t.session.host().editedTimeline(t.document) == t.timeline;
}

int currentLayerOf(const Timeline& t)
{
    const int count = static_cast<int>(t.get().layers.size());
    const int layer = isEdited(t) ? t.session.host().currentLayer(t.document) : t.session.position(t.timeline).layer;
    return std::clamp(layer, 0, std::max(0, count - 1));
}

void makeCurrent(Timeline& t, int index)
{
    if (isEdited(t))
        t.session.host().setCurrentLayer(t.document, index);
    else
        t.session.position(t.timeline).layer = index;
    t.session.layerSelection(t.document, t.timeline) = {index};
}

int checkedIndex(Timeline& t, const Value& value)
{
    const int index = value.toInt();
    const int count = static_cast<int>(t.get().layers.size());
    if (index < 0 || index >= count)
        throw Error("There is no layer " + std::to_string(index) + "; the timeline has " + std::to_string(count) + ".");
    return index;
}

/// The layers copyLayers and its kin work on: \a first to \a last (both
/// included) when given, else the selected layers, else the current one;
/// with whatever folders, masks and guides among them hold.
std::vector<int> pickedLayers(Timeline& t, const Args& args)
{
    std::vector<int> picked;
    if (given(args, 0))
    {
        const int first = checkedIndex(t, arg(args, 0));
        const int last = given(args, 1) ? checkedIndex(t, arg(args, 1)) : first;
        for (int i = std::min(first, last); i <= std::max(first, last); ++i)
            picked.push_back(i);
    }
    else
    {
        for (int index : t.session.layerSelection(t.document, t.timeline))
        {
            if (index >= 0 && index < static_cast<int>(t.get().layers.size()))
                picked.push_back(index);
        }
        if (picked.empty() && !t.get().layers.empty())
            picked.push_back(currentLayerOf(t));
    }
    std::sort(picked.begin(), picked.end());
    picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
    return fla::layers::withContents(t.get(), picked);
}

/// Layers copied by copyLayers or cutLayers, kept for pasting (between runs
/// too, as a clipboard is), with the document they came from.
struct LayerClipboard
{
    std::vector<std::unique_ptr<fla::Layer>> layers;
    const fla::FLADocument* from = nullptr;
};

LayerClipboard& clipboard()
{
    static LayerClipboard c;
    return c;
}

void copyToClipboard(Timeline& t, const std::vector<int>& picked)
{
    if (picked.empty())
        throw Error("There are no layers to copy.");
    std::vector<const fla::Layer*> source;
    for (int index : picked)
        source.push_back(t.get().layers[static_cast<size_t>(index)]);
    LayerClipboard& c = clipboard();
    c.layers.clear();
    for (fla::Layer* copy : fla::layers::cloneLayers(source, t.get()))
        c.layers.emplace_back(copy);
    c.from = t.document;
}

/// Copies of \a source go in above layer \a index, in its folder. Returns
/// where the first went.
int insertCopies(Timeline& t, const std::vector<const fla::Layer*>& source, int index, const char* name, bool renamed)
{
    fla::Timeline& timeline = t.get();
    std::vector<fla::Layer*> copies = fla::layers::cloneLayers(source, timeline);
    fla::Layer* folder = nullptr;
    if (index >= 0 && index < static_cast<int>(timeline.layers.size()))
    {
        fla::Layer* at = timeline.layers[static_cast<size_t>(index)];
        if (at && at->parentLayer && at->parentLayer->layerType == fla::Layer::Type::Folder)
            folder = at->parentLayer;
    }
    const int first = std::clamp(index, 0, static_cast<int>(timeline.layers.size()));
    for (size_t i = 0; i < copies.size(); ++i)
    {
        const bool top = !copies[i]->parentLayer;
        if (top)
            copies[i]->parentLayer = folder;
        if (renamed && top)
        {
            copies[i]->name += " copy";
            copies[i]->autoNamed = false;
        }
        t.push(fla::CommandPtr(new fla::AddLayerCommand(&timeline, first + static_cast<int>(i), copies[i], name)));
    }
    makeCurrent(t, first);
    return first;
}

/// Takes out the layers at \a picked (with what they hold), keeping one
/// layer that can hold frames.
void removeLayers(Timeline& t, const std::vector<int>& picked)
{
    fla::Timeline& timeline = t.get();
    int remaining = 0;
    for (size_t i = 0; i < timeline.layers.size(); ++i)
    {
        const fla::Layer* layer = timeline.layers[i];
        if (layer && layer->layerType != fla::Layer::Type::Folder &&
            std::find(picked.begin(), picked.end(), static_cast<int>(i)) == picked.end())
            ++remaining;
    }
    if (remaining == 0)
        throw Error("A timeline keeps at least one layer.");
    std::vector<int> roots;
    for (int index : picked)
    {
        const fla::Layer* layer = timeline.layers[static_cast<size_t>(index)];
        const auto parent = std::find(timeline.layers.begin(), timeline.layers.end(), layer->parentLayer);
        const int parentIndex = parent == timeline.layers.end() ? -1 : static_cast<int>(parent - timeline.layers.begin());
        if (std::find(picked.begin(), picked.end(), parentIndex) == picked.end())
            roots.push_back(index);
    }
    t.session.host().selection(t.document).clear();
    // From the bottom up, so the indices above stay put.
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        t.push(fla::CommandPtr(new fla::RemoveLayerCommand(&timeline, *it, "Cut Layers")));
    t.session.layerSelection(t.document, t.timeline).clear();
    makeCurrent(t, std::clamp(roots.empty() ? 0 : roots.front(), 0, static_cast<int>(timeline.layers.size()) - 1));
}

// Folders, masks and guides.

const fla::LayerState* stateIn(const fla::LayerSnapshot& snapshot, const fla::Layer* layer)
{
    for (const fla::LayerState& state : snapshot)
    {
        if (state.layer == layer)
            return &state;
    }
    return nullptr;
}

/// Whether \a layer is \a ancestor or sits inside it, at any depth.
bool inside(const fla::LayerSnapshot& snapshot, const fla::Layer* layer, const fla::Layer* ancestor)
{
    for (int guard = 0; layer && guard < 1000; ++guard)
    {
        if (layer == ancestor)
            return true;
        const fla::LayerState* state = stateIn(snapshot, layer);
        layer = state ? state->parent : nullptr;
    }
    return false;
}

/// The first layer after \a block and what it holds, leaving \a moving
/// out of the count; null at the bottom.
fla::Layer* after(const fla::LayerSnapshot& snapshot, const fla::Layer* block, const fla::Layer* moving)
{
    bool past = false;
    for (const fla::LayerState& state : snapshot)
    {
        if (state.layer == block)
        {
            past = true;
            continue;
        }
        if (!past || inside(snapshot, state.layer, block) || inside(snapshot, state.layer, moving))
            continue;
        return state.layer;
    }
    return nullptr;
}

// Filters on frames, kept on the keyframe as the file keeps them: a
// filters element holding one element per filter.

bool isFilters(const std::string& xml)
{
    const size_t start = xml.find_first_not_of(" \t\r\n");
    return start != std::string::npos && xml.compare(start, 9, "<filters>") == 0;
}

std::string hex(const uint8_t color[4])
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color[0], color[1], color[2]);
    return buffer;
}

std::string number(double value)
{
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

void readColor(const tinyxml2::XMLElement* node, const char* name, const char* alphaName, uint8_t out[4])
{
    const char* text = node->Attribute(name);
    if (text && text[0] == '#' && std::strlen(text) >= 7)
    {
        const unsigned long rgb = std::strtoul(text + 1, nullptr, 16);
        out[0] = static_cast<uint8_t>((rgb >> 16) & 0xFF);
        out[1] = static_cast<uint8_t>((rgb >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>(rgb & 0xFF);
    }
    out[3] = static_cast<uint8_t>(std::lround(std::clamp(node->DoubleAttribute(alphaName, 1.0), 0.0, 1.0) * 255.0));
}

std::string writeFilters(const std::vector<fla::Filter>& filters)
{
    std::string xml = "<filters>";
    for (const fla::Filter& f : filters)
    {
        xml += std::string("<") + f.elementName();
        const auto attribute = [&xml](const char* name, const std::string& value) { xml += std::string(" ") + name + "=\"" + value + "\""; };
        if (!f.enabled)
            attribute("enabled", "false");
        if (f.type == fla::Filter::Type::AdjustColor)
        {
            attribute("brightness", number(f.brightness));
            attribute("contrast", number(f.contrast));
            attribute("saturation", number(f.saturation));
            attribute("hue", number(f.hue));
        }
        else
        {
            attribute("blurX", number(f.blurX));
            attribute("blurY", number(f.blurY));
            attribute("quality", std::to_string(f.quality));
            if (f.type != fla::Filter::Type::Blur)
            {
                attribute("strength", number(f.strength));
                if (f.type != fla::Filter::Type::Glow)
                {
                    attribute("angle", number(f.angle));
                    attribute("distance", number(f.distance));
                }
                if (f.type == fla::Filter::Type::DropShadow || f.type == fla::Filter::Type::Glow)
                {
                    attribute("color", hex(f.color));
                    attribute("alpha", number(f.color[3] / 255.0));
                    attribute("inner", f.inner ? "true" : "false");
                    attribute("hideObject", f.hideObject ? "true" : "false");
                }
                if (f.type == fla::Filter::Type::Bevel)
                {
                    attribute("highlightColor", hex(f.highlightColor));
                    attribute("highlightAlpha", number(f.highlightColor[3] / 255.0));
                    attribute("shadowColor", hex(f.shadowColor));
                    attribute("shadowAlpha", number(f.shadowColor[3] / 255.0));
                }
                if (!f.bevelType.empty())
                    attribute("type", f.bevelType);
                attribute("knockout", f.knockout ? "true" : "false");
            }
        }
        if (f.entries.empty())
        {
            xml += "/>";
            continue;
        }
        xml += ">";
        for (const fla::GradientEntry& entry : f.entries)
            xml += "<GradientEntry color=\"" + hex(entry.color) + "\" alpha=\"" + number(entry.color[3] / 255.0) + "\" ratio=\"" +
                   number(entry.ratio) + "\"/>";
        xml += std::string("</") + f.elementName() + ">";
    }
    return xml + "</filters>";
}

std::vector<fla::Filter> readFilters(const std::string& xml)
{
    std::vector<fla::Filter> filters;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !document.RootElement())
        return filters;
    for (const tinyxml2::XMLElement* node = document.RootElement()->FirstChildElement(); node; node = node->NextSiblingElement())
    {
        fla::Filter f;
        bool known = false;
        for (fla::Filter::Type type : {fla::Filter::Type::Blur, fla::Filter::Type::DropShadow, fla::Filter::Type::Glow,
                 fla::Filter::Type::Bevel, fla::Filter::Type::GradientGlow, fla::Filter::Type::GradientBevel,
                 fla::Filter::Type::AdjustColor})
        {
            f.type = type;
            if (std::strcmp(node->Name(), f.elementName()) == 0)
            {
                known = true;
                break;
            }
        }
        if (!known)
            continue;
        f.enabled = node->BoolAttribute("enabled", true);
        f.blurX = node->DoubleAttribute("blurX", 5.0);
        f.blurY = node->DoubleAttribute("blurY", 5.0);
        f.quality = node->IntAttribute("quality", 1);
        f.angle = node->DoubleAttribute("angle", 45.0);
        f.distance = node->DoubleAttribute("distance", 5.0);
        f.strength = node->DoubleAttribute("strength", 1.0);
        f.inner = node->BoolAttribute("inner", false);
        f.knockout = node->BoolAttribute("knockout", false);
        f.hideObject = node->BoolAttribute("hideObject", false);
        f.bevelType = node->Attribute("type") ? node->Attribute("type") : "";
        f.brightness = node->DoubleAttribute("brightness", 0.0);
        f.contrast = node->DoubleAttribute("contrast", 0.0);
        f.saturation = node->DoubleAttribute("saturation", 0.0);
        f.hue = node->DoubleAttribute("hue", 0.0);
        readColor(node, "color", "alpha", f.color);
        readColor(node, "highlightColor", "highlightAlpha", f.highlightColor);
        readColor(node, "shadowColor", "shadowAlpha", f.shadowColor);
        for (const tinyxml2::XMLElement* entry = node->FirstChildElement("GradientEntry"); entry;
             entry = entry->NextSiblingElement("GradientEntry"))
        {
            fla::GradientEntry e{};
            e.ratio = entry->DoubleAttribute("ratio", 0.0);
            readColor(entry, "color", "alpha", e.color);
            f.entries.push_back(e);
        }
        filters.push_back(f);
    }
    return filters;
}

fla::Frame& keyframeAt(Layer& l, const Value& index)
{
    fla::Layer& layer = l.get();
    const int at = index.toInt();
    fla::Frame* frame = at >= 0 ? fla::timeline::spanAt(layer, at) : nullptr;
    if (!frame)
        throw Error("Layer " + layer.name + " has no frame " + std::to_string(at) + ".");
    return *frame;
}

} // namespace

void setParentLayer(Layer& l, const Value& value)
{
    fla::Layer& layer = l.get();
    auto* timeline = dynamic_cast<fla::Timeline*>(layer.parent);
    if (!timeline)
        throw Error("That layer is not on a timeline.");
    fla::Layer* parent = nullptr;
    if (!value.isNullish())
    {
        auto wrapper = value.as<Layer>();
        if (!wrapper)
            throw Error("A layer's parent is a layer (a folder, mask or guide) or null.");
        parent = &wrapper->get();
        if (parent->parent != timeline)
            throw Error("A layer's parent has to be on the same timeline.");
    }
    if (parent == layer.parentLayer)
        return;
    const fla::LayerSnapshot before = fla::snapshotLayers(*timeline);
    fla::LayerSnapshot snapshot = before;
    // Into a folder, mask or guide it goes last among what that holds; out
    // to the top level, just below the folder it was in.
    const fla::Layer* anchor = parent;
    if (!anchor)
    {
        anchor = &layer;
        for (const fla::LayerState* state = stateIn(snapshot, anchor); state && state->parent; state = stateIn(snapshot, anchor))
            anchor = state->parent;
    }
    fla::Layer* below = after(snapshot, anchor, &layer);
    if (!fla::layers::moveLayer(snapshot, &layer, below, parent))
        throw Error("Layer " + layer.name + " cannot go into " + (parent ? parent->name : std::string("the top level")) + ".");
    l.push(fla::CommandPtr(new fla::LayerEditCommand(timeline, before, snapshot, "Layer Properties")), false);
}

void describeTimelineLayers(ClassDef<Timeline>& def)
{
    def.method("copyLayers", 2, [](Timeline& t, const Args& args) {
        copyToClipboard(t, pickedLayers(t, args));
        return Value();
    });
    def.method("cutLayers", 2, [](Timeline& t, const Args& args) {
        const std::vector<int> picked = pickedLayers(t, args);
        copyToClipboard(t, picked);
        removeLayers(t, picked);
        return Value();
    });
    def.method("pasteLayers", 1, [](Timeline& t, const Args& args) {
        const LayerClipboard& c = clipboard();
        if (c.layers.empty())
            throw Error("No layers have been copied.");
        if (c.from != t.document)
            throw Error("Layers paste into the document they were copied from.");
        const int index = given(args, 0) ? checkedIndex(t, arg(args, 0)) : t.get().layers.empty() ? 0 : currentLayerOf(t);
        std::vector<const fla::Layer*> copied;
        for (const auto& layer : c.layers)
            copied.push_back(layer.get());
        return Value(insertCopies(t, copied, index, "Paste Layers", false));
    });
    def.method("duplicateLayers", 1, [](Timeline& t, const Args& args) {
        const std::vector<int> picked = pickedLayers(t, args);
        if (picked.empty())
            throw Error("There are no layers to duplicate.");
        std::vector<const fla::Layer*> source;
        for (int index : picked)
            source.push_back(t.get().layers[static_cast<size_t>(index)]);
        return Value(insertCopies(t, source, picked.front(), "Duplicate Layers", true));
    });
}

void describeLayerExtras(ClassDef<Layer>& def)
{
    def.method("getFiltersAtFrame", 1, [](Layer& l, const Args& args) {
        const fla::Frame& frame = keyframeAt(l, arg(args, 0));
        Value list = Value::array();
        for (const std::string& xml : frame.extraChildren)
        {
            if (!isFilters(xml))
                continue;
            for (const fla::Filter& filter : readFilters(xml))
                list.items.push_back(filterToValue(filter));
        }
        return list;
    });
    def.method("setFiltersAtFrame", 2, [](Layer& l, const Args& args) {
        fla::Frame& frame = keyframeAt(l, arg(args, 0));
        std::vector<fla::Filter> filters;
        for (const Value& item : arg(args, 1).items)
            filters.push_back(filterFromValue(item));
        std::vector<std::string> children = frame.extraChildren;
        children.erase(std::remove_if(children.begin(), children.end(), isFilters), children.end());
        if (!filters.empty())
            children.push_back(writeFilters(filters));
        if (children != frame.extraChildren)
            l.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(frame.extraChildren, frame.extraChildren,
                       children, "Filters")),
                false);
        return Value();
    });
}

} // namespace jsfl
