#include "layer_commands.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "timeline_edit.h"

#include <map>
#include <set>
#include <string>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace fla {

namespace {

/// Animate's layer outline colors, handed out in turn as layers are added.
const uint8_t kLayerColors[][3] = {
    {0x4F, 0xFF, 0x4F}, {0xFF, 0x4F, 0xFF}, {0x4F, 0xFF, 0xFF}, {0xFF, 0xFF, 0x4F},
    {0x4F, 0x4F, 0xFF}, {0xFF, 0x4F, 0x4F}, {0xFF, 0x99, 0x33}, {0x99, 0x33, 0xFF},
    {0x33, 0xFF, 0x99}, {0xFF, 0x33, 0x99},
};

int longestLayer(const Timeline& timeline)
{
    int frames = 1;
    for (const Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        for (const Frame* frame : layer->frames)
        {
            if (frame)
                frames = std::max(frames, frame->index + std::max(frame->duration, 1));
        }
    }
    return frames;
}

/// The next free "Layer N" or "Folder N": one more than the highest number
/// any layer of that kind already carries, and at least the count plus one.
std::string nextName(const Timeline& timeline, bool folder)
{
    // Animate names them Layer_1, Layer_2... (and folders Folder_1...).
    const std::string prefix = folder ? "Folder_" : "Layer_";
    int highest = 0;
    int count = 0;
    for (const Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        if ((layer->layerType == Layer::Type::Folder) != folder)
            continue;
        ++count;
        if (layer->name.compare(0, prefix.size(), prefix) == 0)
        {
            int number = 0;
            if (std::sscanf(layer->name.c_str() + prefix.size(), "%d", &number) == 1)
                highest = std::max(highest, number);
        }
    }
    return prefix + std::to_string(std::max(highest, count) + 1);
}

void renumberParents(Timeline& timeline)
{
    // The index of each layer's folder is read back from the folder pointer,
    // which survives any reshuffle.
    for (Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        if (!layer->parentLayer)
        {
            layer->parentLayerIndex = -1;
            continue;
        }
        auto at = std::find(timeline.layers.begin(), timeline.layers.end(), layer->parentLayer);
        layer->parentLayerIndex = at == timeline.layers.end() ? -1
            : static_cast<int>(at - timeline.layers.begin());
    }
}

void makeCurrent(Timeline& timeline, const Layer* current)
{
    for (Layer* layer : timeline.layers)
    {
        if (layer)
            layer->selected = layer == current;
    }
}

std::vector<bool> selectedFlags(const Timeline& timeline)
{
    std::vector<bool> flags;
    for (const Layer* layer : timeline.layers)
        flags.push_back(layer && layer->selected);
    return flags;
}

void restoreSelected(Timeline& timeline, const std::vector<bool>& flags)
{
    for (size_t i = 0; i < timeline.layers.size() && i < flags.size(); ++i)
    {
        if (timeline.layers[i])
            timeline.layers[i]->selected = flags[i];
    }
}

} // namespace

Layer* newLayerFor(const Timeline& timeline, bool folder)
{
    Layer* layer = new Layer(const_cast<Timeline*>(&timeline));
    layer->name = nextName(timeline, folder);
    layer->autoNamed = true;
    layer->layerType = folder ? Layer::Type::Folder : Layer::Type::Normal;
    const uint8_t* color = kLayerColors[timeline.layers.size() % (sizeof(kLayerColors) / sizeof(kLayerColors[0]))];
    layer->color[0] = color[0];
    layer->color[1] = color[1];
    layer->color[2] = color[2];
    layer->color[3] = 255;
    if (!folder)
    {
        Frame* frame = new Frame(layer);
        frame->index = 0;
        frame->duration = longestLayer(timeline);
        frame->keyMode = "9728";
        layer->frames.push_back(frame);
        layer->firstFrame = 0;
        layer->lastFrame = frame->duration;
    }
    return layer;
}

// ---------------------------------------------------------------------------

AddLayerCommand::AddLayerCommand(Timeline* timeline, int index, Layer* layer, const std::string& name)
    : _timeline(timeline)
    , _index(index)
    , _layer(layer)
    , _name(name)
{
}

AddLayerCommand::~AddLayerCommand()
{
    if (_owned)
        delete _layer;
}

void AddLayerCommand::redo()
{
    if (!_timeline || !_layer)
        return;
    _selectedBefore = selectedFlags(*_timeline);
    const int at = std::clamp(_index, 0, static_cast<int>(_timeline->layers.size()));
    _timeline->layers.insert(_timeline->layers.begin() + at, _layer);
    _layer->parent = _timeline;
    renumberParents(*_timeline);
    // A new layer becomes the one drawings go to; a folder cannot.
    if (_layer->layerType != Layer::Type::Folder)
        makeCurrent(*_timeline, _layer);
    _owned = false;
}

void AddLayerCommand::undo()
{
    if (!_timeline || !_layer)
        return;
    auto at = std::find(_timeline->layers.begin(), _timeline->layers.end(), _layer);
    if (at != _timeline->layers.end())
        _timeline->layers.erase(at);
    renumberParents(*_timeline);
    restoreSelected(*_timeline, _selectedBefore);
    _owned = true;
}

// ---------------------------------------------------------------------------

RemoveLayerCommand::RemoveLayerCommand(Timeline* timeline, int index, const std::string& name)
    : _timeline(timeline)
    , _index(index)
    , _name(name)
{
}

RemoveLayerCommand::~RemoveLayerCommand()
{
    if (_owned)
    {
        for (const auto& entry : _removed)
            delete entry.second;
    }
}

std::vector<int> RemoveLayerCommand::affectedIndices(const Timeline& timeline, int index)
{
    std::vector<int> indices;
    if (index < 0 || index >= static_cast<int>(timeline.layers.size()))
        return indices;
    indices.push_back(index);
    // Anything whose folder chain reaches the layer goes with it.
    for (int i = 0; i < static_cast<int>(timeline.layers.size()); ++i)
    {
        if (i == index)
            continue;
        const Layer* walk = timeline.layers[i];
        bool inside = false;
        for (int guard = 0; walk && guard < 64 && !inside; ++guard)
        {
            if (walk->parentLayerIndex == index)
                inside = true;
            walk = walk->parentLayerIndex >= 0 && walk->parentLayerIndex < static_cast<int>(timeline.layers.size())
                ? timeline.layers[walk->parentLayerIndex] : nullptr;
        }
        if (inside)
            indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

void RemoveLayerCommand::redo()
{
    if (!_timeline)
        return;

    _parentsBefore.clear();
    for (const Layer* layer : _timeline->layers)
        _parentsBefore.push_back(layer ? layer->parentLayerIndex : -1);
    _selectedBefore = selectedFlags(*_timeline);

    if (_removed.empty())
    {
        for (int index : affectedIndices(*_timeline, _index))
            _removed.push_back({index, _timeline->layers[index]});
    }

    bool currentWent = false;
    for (auto it = _removed.rbegin(); it != _removed.rend(); ++it)
    {
        auto at = std::find(_timeline->layers.begin(), _timeline->layers.end(), it->second);
        if (at != _timeline->layers.end())
        {
            if ((*at)->selected)
                currentWent = true;
            _timeline->layers.erase(at);
        }
    }
    renumberParents(*_timeline);

    if (currentWent && !_timeline->layers.empty())
    {
        // The layer that took the slot, else the one above.
        const int slot = std::min(_index, static_cast<int>(_timeline->layers.size()) - 1);
        Layer* next = _timeline->layers[slot];
        for (int i = slot; i >= 0 && next && next->layerType == Layer::Type::Folder; --i)
            next = _timeline->layers[i];
        makeCurrent(*_timeline, next);
    }
    _owned = true;
}

void RemoveLayerCommand::undo()
{
    if (!_timeline)
        return;
    for (const auto& entry : _removed)
    {
        const int at = std::clamp(entry.first, 0, static_cast<int>(_timeline->layers.size()));
        _timeline->layers.insert(_timeline->layers.begin() + at, entry.second);
    }
    for (size_t i = 0; i < _timeline->layers.size() && i < _parentsBefore.size(); ++i)
    {
        Layer* layer = _timeline->layers[i];
        if (!layer)
            continue;
        layer->parentLayerIndex = _parentsBefore[i];
        layer->parentLayer = layer->parentLayerIndex >= 0 &&
                layer->parentLayerIndex < static_cast<int>(_timeline->layers.size())
            ? _timeline->layers[layer->parentLayerIndex] : nullptr;
    }
    restoreSelected(*_timeline, _selectedBefore);
    _owned = false;
}

// ---------------------------------------------------------------------------

LayerSnapshot snapshotLayers(const Timeline& timeline)
{
    LayerSnapshot snapshot;
    for (Layer* layer : timeline.layers)
    {
        if (!layer)
            continue;
        LayerState state;
        state.layer = layer;
        state.parent = layer->parentLayer;
        state.type = layer->layerType;
        state.otherType = layer->otherLayerType;
        state.name = layer->name;
        state.autoNamed = layer->autoNamed;
        for (int i = 0; i < 4; ++i)
            state.color[i] = layer->color[i];
        state.visible = layer->visible;
        state.locked = layer->locked;
        state.outline = layer->outline;
        state.extraAttributes = layer->extraAttributes;
        snapshot.push_back(state);
    }
    return snapshot;
}

LayerEditCommand::LayerEditCommand(Timeline* timeline, LayerSnapshot before, LayerSnapshot after,
    const std::string& name)
    : _timeline(timeline)
    , _before(std::move(before))
    , _after(std::move(after))
    , _name(name)
{
}

void LayerEditCommand::apply(Timeline& timeline, const LayerSnapshot& snapshot)
{
    timeline.layers.clear();
    for (const LayerState& state : snapshot)
    {
        Layer* layer = state.layer;
        timeline.layers.push_back(layer);
        layer->parentLayer = state.parent;
        layer->layerType = state.type;
        layer->otherLayerType = state.otherType;
        layer->name = state.name;
        layer->autoNamed = state.autoNamed;
        for (int i = 0; i < 4; ++i)
            layer->color[i] = state.color[i];
        layer->visible = state.visible;
        layer->locked = state.locked;
        layer->outline = state.outline;
        layer->extraAttributes = state.extraAttributes;
    }
    renumberParents(timeline);
}

void LayerEditCommand::redo()
{
    if (_timeline)
        apply(*_timeline, _after);
}

void LayerEditCommand::undo()
{
    if (_timeline)
        apply(*_timeline, _before);
}

namespace layers {

namespace {

int indexIn(const LayerSnapshot& snapshot, const Layer* layer)
{
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        if (snapshot[i].layer == layer)
            return static_cast<int>(i);
    }
    return -1;
}

LayerState* stateOf(LayerSnapshot& snapshot, const Layer* layer)
{
    const int index = indexIn(snapshot, layer);
    return index < 0 ? nullptr : &snapshot[static_cast<size_t>(index)];
}

/// Whether \a layer is \a ancestor or sits inside it, however deep.
bool within(const LayerSnapshot& snapshot, const Layer* layer, const Layer* ancestor)
{
    for (int guard = 0; layer && guard < 256; ++guard)
    {
        if (layer == ancestor)
            return true;
        const int index = indexIn(snapshot, layer);
        layer = index < 0 ? nullptr : snapshot[static_cast<size_t>(index)].parent;
    }
    return false;
}

Layer::Type typeIn(const LayerSnapshot& snapshot, const Layer* layer)
{
    const int index = indexIn(snapshot, layer);
    return index < 0 ? Layer::Type::Normal : snapshot[static_cast<size_t>(index)].type;
}

/// The folder a layer is in, looking through the mask or guide it is under.
Layer* folderOf(const LayerSnapshot& snapshot, const Layer* layer)
{
    // A step limit, as within() has, so a loop of parents cannot hang.
    for (int guard = 0; guard < 256; ++guard)
    {
        const int index = indexIn(snapshot, layer);
        Layer* parent = index < 0 ? nullptr : snapshot[static_cast<size_t>(index)].parent;
        if (!parent || typeIn(snapshot, parent) == Layer::Type::Folder)
            return parent;
        layer = parent;
    }
    return nullptr;
}

/// The layer just below everything \a layer holds: where a layer goes to be
/// the last one inside it. Null at the bottom.
Layer* afterBlock(const LayerSnapshot& snapshot, const Layer* layer)
{
    const int index = indexIn(snapshot, layer);
    for (size_t i = static_cast<size_t>(index) + 1; i < snapshot.size(); ++i)
    {
        if (!within(snapshot, snapshot[i].layer, layer))
            return snapshot[i].layer;
    }
    return nullptr;
}

/// The layers a mask or guide holds go back to its folder as plain layers.
void release(LayerSnapshot& snapshot, const Layer* layer)
{
    Layer* folder = folderOf(snapshot, layer);
    for (LayerState& state : snapshot)
    {
        if (state.parent != layer)
            continue;
        state.parent = folder;
        if (state.type == Layer::Type::Masked)
            state.type = Layer::Type::Normal;
    }
}

} // namespace

Kind kindOf(const LayerSnapshot& snapshot, const Layer* layer)
{
    const int index = indexIn(snapshot, layer);
    if (index < 0)
        return Kind::Normal;
    const LayerState& state = snapshot[static_cast<size_t>(index)];
    switch (state.type)
    {
    case Layer::Type::Folder: return Kind::Folder;
    case Layer::Type::Mask: return Kind::Mask;
    case Layer::Type::Guide: return Kind::Guide;
    case Layer::Type::Masked: return Kind::Masked;
    case Layer::Type::Normal: break;
    }
    if (state.parent && typeIn(snapshot, state.parent) == Layer::Type::Mask)
        return Kind::Masked;
    if (state.parent && typeIn(snapshot, state.parent) == Layer::Type::Guide)
        return Kind::Guided;
    return Kind::Normal;
}

bool moveLayer(LayerSnapshot& snapshot, Layer* layer, Layer* before, Layer* parent)
{
    const int from = indexIn(snapshot, layer);
    if (from < 0 || (before && indexIn(snapshot, before) < 0) || (parent && indexIn(snapshot, parent) < 0))
        return false;
    if (parent && within(snapshot, parent, layer))
        return false;
    if (before && within(snapshot, before, layer))
        return false;
    const Layer::Type parentType = parent ? typeIn(snapshot, parent) : Layer::Type::Folder;
    if (parentType == Layer::Type::Normal || parentType == Layer::Type::Masked)
        return false;
    // A folder, mask or guide does not go under a mask or guide.
    const Layer::Type type = snapshot[static_cast<size_t>(from)].type;
    if (parent && parentType != Layer::Type::Folder &&
        (type == Layer::Type::Folder || type == Layer::Type::Mask || type == Layer::Type::Guide))
        return false;

    // The layer and everything in it, taken out in order.
    LayerSnapshot block;
    LayerSnapshot rest;
    for (const LayerState& state : snapshot)
        (within(snapshot, state.layer, layer) ? block : rest).push_back(state);
    LayerState& moved = block.front().layer == layer ? block.front() : *stateOf(block, layer);
    moved.parent = parent;
    if (parentType == Layer::Type::Mask)
        moved.type = Layer::Type::Masked;
    else if (moved.type == Layer::Type::Masked)
        moved.type = Layer::Type::Normal;

    const int at = before ? indexIn(rest, before) : static_cast<int>(rest.size());
    rest.insert(rest.begin() + at, block.begin(), block.end());
    snapshot = rest;
    return true;
}

bool setKind(LayerSnapshot& snapshot, Layer* layer, Kind kind)
{
    LayerState* state = stateOf(snapshot, layer);
    if (!state)
        return false;
    const Kind now = kindOf(snapshot, layer);
    if (now == kind)
        return true;
    if (now == Kind::Folder || kind == Kind::Folder)
        return false;

    if (kind == Kind::Masked || kind == Kind::Guided)
    {
        // Under the nearest mask (or guide) above, in the same folder.
        const Layer::Type wanted = kind == Kind::Masked ? Layer::Type::Mask : Layer::Type::Guide;
        Layer* folder = folderOf(snapshot, layer);
        Layer* owner = nullptr;
        for (int i = indexIn(snapshot, layer) - 1; i >= 0 && !owner; --i)
        {
            const LayerState& above = snapshot[static_cast<size_t>(i)];
            if (above.type == wanted && above.parent == folder && above.layer != layer)
                owner = above.layer;
        }
        if (!owner)
            return false;
        if (now == Kind::Mask || now == Kind::Guide)
        {
            release(snapshot, layer);
            stateOf(snapshot, layer)->type = Layer::Type::Normal;
        }
        Layer* before = afterBlock(snapshot, owner);
        if (before == layer)
            before = afterBlock(snapshot, layer);
        return moveLayer(snapshot, layer, before, owner);
    }

    // A mask or guide stops holding its layers once it is something else.
    if (now == Kind::Mask || now == Kind::Guide)
        release(snapshot, layer);
    state = stateOf(snapshot, layer);
    // Out from under a mask or guide, into its folder.
    if (now == Kind::Masked || now == Kind::Guided)
        state->parent = folderOf(snapshot, layer);
    state->type = kind == Kind::Mask ? Layer::Type::Mask
        : kind == Kind::Guide ? Layer::Type::Guide : Layer::Type::Normal;
    return true;
}

int transparencyOf(const Layer& layer)
{
    for (const auto& [key, value] : layer.extraAttributes)
        if (key == "phoenixTransparency")
            return std::clamp(std::atoi(value.c_str()), 0, 100);
    return 0;
}

std::vector<std::pair<std::string, std::string>> withTransparency(const Layer& layer, int percent)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : layer.extraAttributes)
        if (attribute.first != "phoenixTransparency")
            attributes.push_back(attribute);
    if (percent > 0 && percent < 100)
        attributes.emplace_back("phoenixTransparency", std::to_string(percent));
    return attributes;
}

namespace {

const char* const kParentScale = "phoenixParentScale";
const char* const kParentSkew = "phoenixParentSkew";
const char* const kParentFlip = "phoenixParentFlip";

} // namespace

ParentPropagation propagationOf(const Layer& layer)
{
    ParentPropagation propagation;
    for (const auto& [key, value] : layer.extraAttributes)
    {
        if (key == kParentScale)
            propagation.scale = value != "false";
        else if (key == kParentSkew)
            propagation.skew = value != "false";
        else if (key == kParentFlip)
            propagation.flip = value != "false";
    }
    return propagation;
}

std::vector<std::pair<std::string, std::string>> withPropagation(
    const std::vector<std::pair<std::string, std::string>>& from, ParentPropagation propagation)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : from)
        if (attribute.first != kParentScale && attribute.first != kParentSkew && attribute.first != kParentFlip)
            attributes.push_back(attribute);
    if (!propagation.scale)
        attributes.emplace_back(kParentScale, "false");
    if (!propagation.skew)
        attributes.emplace_back(kParentSkew, "false");
    if (!propagation.flip)
        attributes.emplace_back(kParentFlip, "false");
    return attributes;
}

Transform propagatedTransform(const Transform& parent, ParentPropagation propagation)
{
    if (propagation.all())
        return parent;
    // The x axis's image is (a, b), the y axis's (c, d). A flip, a mirror
    // across the parent's y axis, is taken off first, leaving no mirror.
    double a = parent.m11;
    double b = parent.m12;
    const double c = parent.m21;
    const double d = parent.m22;
    const bool flipped = a * d - b * c < 0.0;
    if (flipped)
    {
        a = -a;
        b = -b;
    }
    const double scaleX = std::hypot(a, b);
    if (scaleX < 1e-12)
        return parent;
    // The rotation turns the x axis to (a, b); the y axis, turned back by
    // it, is the skew's slant along x and the y scale.
    const double cosine = a / scaleX;
    const double sine = b / scaleX;
    const double along = c * cosine + d * sine;
    double scaleY = d * cosine - c * sine;
    double slant = std::abs(scaleY) < 1e-12 ? 0.0 : along / scaleY;
    double sx = scaleX;
    if (!propagation.scale)
    {
        sx = 1.0;
        scaleY = 1.0;
    }
    if (!propagation.skew)
        slant = 0.0;

    Transform result(parent);
    result.m11 = sx * cosine;
    result.m12 = sx * sine;
    result.m21 = slant * scaleY * cosine - scaleY * sine;
    result.m22 = slant * scaleY * sine + scaleY * cosine;
    if (flipped && propagation.flip)
    {
        result.m11 = -result.m11;
        result.m12 = -result.m12;
    }
    return result;
}

std::vector<int> withContents(const Timeline& timeline, std::vector<int> indices)
{
    std::set<int> all;
    for (int index : indices)
        for (int inside : RemoveLayerCommand::affectedIndices(timeline, index))
            all.insert(inside);
    return std::vector<int>(all.begin(), all.end());
}

std::vector<Layer*> cloneLayers(const std::vector<const Layer*>& layers, const Timeline& into)
{
    // New rig numbers start past every one the timeline has.
    int nextRig = 0;
    for (const Layer* layer : into.layers)
        if (layer)
            for (const auto& [key, value] : layer->extraAttributes)
                if (key == "layerRiggingIndex")
                    nextRig = std::max(nextRig, std::atoi(value.c_str()) + 1);
    std::map<std::string, std::string> rigs;
    for (const Layer* layer : layers)
        for (const auto& [key, value] : layer->extraAttributes)
            if (key == "layerRiggingIndex")
                rigs[value] = std::to_string(nextRig++);

    std::map<const Layer*, Layer*> copies;
    std::vector<Layer*> made;
    for (const Layer* layer : layers)
    {
        Layer* copy = new Layer(nullptr);
        copy->name = layer->name;
        std::copy(std::begin(layer->color), std::end(layer->color), std::begin(copy->color));
        copy->layerType = layer->layerType;
        copy->otherLayerType = layer->otherLayerType;
        copy->autoNamed = layer->autoNamed;
        copy->locked = layer->locked;
        copy->outline = layer->outline;
        copy->visible = layer->visible;
        copy->extraChildren = layer->extraChildren;
        copy->presentAttributes = layer->presentAttributes;
        for (const auto& [key, value] : layer->extraAttributes)
            copy->extraAttributes.emplace_back(key, key == "layerRiggingIndex" ? rigs[value] : value);
        for (const Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            Frame* frameCopy = timeline::cloneFrame(*frame, copy);
            // A child whose parent came along follows the copy.
            for (auto& [key, value] : frameCopy->extraAttributes)
                if (key == "parentLayerIndex" && rigs.count(value))
                    value = rigs[value];
            copy->frames.push_back(frameCopy);
        }
        const auto parent = copies.find(layer->parentLayer);
        copy->parentLayer = parent == copies.end() ? nullptr : parent->second;
        copies[layer] = copy;
        made.push_back(copy);
    }
    return made;
}

} // namespace layers

} // namespace fla
