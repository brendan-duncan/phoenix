#include "layer_commands.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/timeline.h"

#include <algorithm>
#include <cstdio>

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

} // namespace fla
