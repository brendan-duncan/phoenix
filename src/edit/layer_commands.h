#pragma once

#include "command.h"

#include <string>
#include <utility>
#include <vector>

namespace fla {

class Layer;
class Timeline;

/// A new, empty layer or folder for \a timeline, named "Layer N" (or
/// "Folder N") with the next free number and the next of Animate's layer
/// colors. A layer gets one keyframe spanning as many frames as the
/// timeline's longest layer, as Animate's New Layer does. The caller owns it
/// until an AddLayerCommand takes it.
Layer* newLayerFor(const Timeline& timeline, bool folder);

/// Puts a layer into a timeline at an index (0 is the top of the list),
/// making it the current layer. The layers below move down a slot, and the
/// folder links of the layers that follow are renumbered to match.
class AddLayerCommand : public Command
{
public:
    /// Takes ownership of \a layer.
    AddLayerCommand(Timeline* timeline, int index, Layer* layer, const std::string& name);
    ~AddLayerCommand() override;

    AddLayerCommand(const AddLayerCommand&) = delete;
    AddLayerCommand& operator=(const AddLayerCommand&) = delete;

    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

    Layer* layer() const { return _layer; }

private:
    Timeline* _timeline;
    int _index;
    Layer* _layer;
    bool _owned = true;
    std::string _name;
    std::vector<bool> _selectedBefore;
};

/// Takes a layer out of a timeline, and with a folder everything inside it.
/// The folder links of the layers that remain are renumbered, and if the
/// current layer went, the nearest one left becomes current.
class RemoveLayerCommand : public Command
{
public:
    RemoveLayerCommand(Timeline* timeline, int index, const std::string& name);
    ~RemoveLayerCommand() override;

    RemoveLayerCommand(const RemoveLayerCommand&) = delete;
    RemoveLayerCommand& operator=(const RemoveLayerCommand&) = delete;

    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

    /// The indices that go, the layer itself first.
    static std::vector<int> affectedIndices(const Timeline& timeline, int index);

private:
    Timeline* _timeline;
    int _index;
    std::string _name;
    /// The removed layers with the indices they had, top first.
    std::vector<std::pair<int, Layer*>> _removed;
    bool _owned = false;
    /// Every layer's folder link and current flag before the removal.
    std::vector<int> _parentsBefore;
    std::vector<bool> _selectedBefore;
};

} // namespace fla
