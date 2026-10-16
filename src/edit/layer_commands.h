#pragma once

#include "command.h"
#include "../data/layer.h"
#include "../data/transform.h"

#include <cstdint>

#include <string>
#include <utility>
#include <vector>

namespace fla {

class Element;
class Selection;
class SymbolList;
class Timeline;

/// A new, empty layer or folder for \a timeline, named "Layer N" (or
/// "Folder N") with the next free number and the next of the layer
/// colors. A layer gets one keyframe spanning as many frames as the
/// timeline's longest layer. The caller owns it
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

/// What a layer is in a timeline's structure, and how it looks: its place
/// is its place in the snapshot's list.
struct LayerState
{
    Layer* layer = nullptr;
    /// The folder it sits in, or the mask or guide it is under.
    Layer* parent = nullptr;
    Layer::Type type = Layer::Type::Normal;
    std::string otherType;
    std::string name;
    bool autoNamed = false;
    uint8_t color[4] = {0, 0, 0, 255};
    bool visible = true;
    bool locked = false;
    bool outline = false;
    /// Settings kept for the file or for Phoenix: parenting rigs, layer
    /// transparency, which parts of a parent's transform it takes on.
    std::vector<std::pair<std::string, std::string>> extraAttributes;
};

/// A timeline's layers, top first, as an edit leaves them.
using LayerSnapshot = std::vector<LayerState>;

LayerSnapshot snapshotLayers(const Timeline& timeline);

/// Puts a timeline's layers into the order, links and settings of a
/// snapshot: moving layers, changing their kind, renaming them, all as one
/// undo step. The snapshots hold the same layers, in any order.
class LayerEditCommand : public Command
{
public:
    LayerEditCommand(Timeline* timeline, LayerSnapshot before, LayerSnapshot after, const std::string& name);

    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

    /// Applies a snapshot to its timeline.
    static void apply(Timeline& timeline, const LayerSnapshot& snapshot);

private:
    Timeline* _timeline;
    LayerSnapshot _before;
    LayerSnapshot _after;
    std::string _name;
};

namespace layers {

/// What a layer is, as Layer Properties offers it: a guided layer
/// is a normal one under a guide; a masked one, one under a mask.
enum class Kind
{
    Normal,
    Guide,
    Guided,
    Mask,
    Masked,
    Folder
};

Kind kindOf(const LayerSnapshot& snapshot, const Layer* layer);

/// Moves \a layer, with the layers inside it, to just above \a before (to
/// the bottom for null), into \a parent: a folder, a mask (making it
/// masked), a guide (making it guided), or null for the top level. False
/// when that cannot be: a layer into itself, or under a plain layer.
bool moveLayer(LayerSnapshot& snapshot, Layer* layer, Layer* before, Layer* parent);

/// Makes \a layer the kind asked for. A masked or guided layer goes under
/// the nearest mask or guide above it in the same folder, and is moved to
/// sit just below what that mask or guide already holds; a mask or guide
/// that stops being one lets its layers go. False when that cannot be: no
/// mask or guide above, or a change to or from a folder.
bool setKind(LayerSnapshot& snapshot, Layer* layer, Kind kind);

/// Layer transparency: a visible layer shown faded while editing, as see
/// through as \a percent of it is shown (100 or 0 when it is not faded).
/// Kept on the layer for Phoenix; exports draw the layer in full.
int transparencyOf(const Layer& layer);
std::vector<std::pair<std::string, std::string>> withTransparency(const Layer& layer, int percent);

/// Layer parenting: which parts of its parent's transform a parented layer
/// takes on. Position and rotation always carry over; scale, skew and flip
/// each can be left behind. Kept on the child layer for Phoenix as
/// phoenixParentScale, phoenixParentSkew and phoenixParentFlip, written
/// only as "false"; the file format has no setting for this, so a file
/// saved with all three on reads the same anywhere.
struct ParentPropagation
{
    bool scale = true;
    bool skew = true;
    bool flip = true;

    bool all() const { return scale && skew && flip; }
};

ParentPropagation propagationOf(const Layer& layer);
/// A layer's \a attributes with \a propagation in place of what they said.
std::vector<std::pair<std::string, std::string>> withPropagation(
    const std::vector<std::pair<std::string, std::string>>& attributes, ParentPropagation propagation);

/// \a parent, a parent's transform, with the parts \a propagation leaves
/// behind taken out. It is read as translate * rotate * skew * scale * flip,
/// the skew a slant of the parent's y axis along its x axis and the flip a
/// mirror across its y axis (a parent flipped top to bottom is one flipped
/// side to side and turned half round, so a child that leaves flips behind
/// still turns with it). With everything taken on it is \a parent exactly.
Transform propagatedTransform(const Transform& parent, ParentPropagation propagation);

/// The layers at \a indices with everything inside them, in timeline order:
/// a selection of layers grown to whole folders, masks and guides.
std::vector<int> withContents(const Timeline& timeline, std::vector<int> indices);

/// Deep copies of \a layers (top first, as withContents gives them): their
/// frames, settings and folder links among them; a link to a layer not
/// copied is dropped. Layer parenting rigs are renumbered after the ones
/// \a into already has, so a copied child follows its copied parent and
/// one copied alone still follows the original. The caller owns them; they
/// belong to no timeline yet.
std::vector<Layer*> cloneLayers(const std::vector<const Layer*>& layers, const Timeline& into);

/// Distribute to Layers: each of \a elements lying on a frame of
/// \a timeline moves to a layer of its own, the topmost to a new layer just
/// under layer \a current, the next under that; a symbol's layer is named
/// for it. The commands that do it, in order (a removal and a new layer
/// for each); empty when none of them is on \a timeline.
std::vector<CommandPtr> distributeToLayers(Timeline& timeline, int current, std::vector<Element*> elements,
    const SymbolList* library, Selection* selection);

} // namespace layers

} // namespace fla
