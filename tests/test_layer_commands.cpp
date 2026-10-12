#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/layer_commands.h"

#include <memory>

namespace {

/// A timeline with a folder holding one layer, and a plain layer under
/// them: [Folder 1, Layer 1 (in folder), Layer 2].
std::unique_ptr<fla::Timeline> timelineWithFolder()
{
    std::unique_ptr<fla::Timeline> timeline(new fla::Timeline(nullptr));
    fla::Layer* folder = new fla::Layer(timeline.get());
    folder->name = "Folder_1";
    folder->layerType = fla::Layer::Type::Folder;
    fla::Layer* inside = new fla::Layer(timeline.get());
    inside->name = "Layer_1";
    inside->parentLayerIndex = 0;
    inside->parentLayer = folder;
    fla::Frame* frame = new fla::Frame(inside);
    frame->index = 0;
    frame->duration = 12;
    inside->frames.push_back(frame);
    fla::Layer* plain = new fla::Layer(timeline.get());
    plain->name = "Layer_2";
    plain->selected = true;
    fla::Frame* other = new fla::Frame(plain);
    other->index = 0;
    other->duration = 5;
    plain->frames.push_back(other);
    timeline->layers = {folder, inside, plain};
    return timeline;
}

} // namespace

TEST(new_layer_spans_the_longest_layer_and_takes_the_next_name)
{
    std::unique_ptr<fla::Timeline> timeline = timelineWithFolder();
    std::unique_ptr<fla::Layer> layer(fla::newLayerFor(*timeline, false));
    CHECK(layer->name == "Layer_3");
    CHECK(layer->frames.size() == 1);
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 12);
    std::unique_ptr<fla::Layer> folder(fla::newLayerFor(*timeline, true));
    CHECK(folder->name == "Folder_2");
    CHECK(folder->frames.empty());
}

TEST(adding_a_layer_renumbers_folder_links_and_makes_it_current)
{
    std::unique_ptr<fla::Timeline> timeline = timelineWithFolder();
    fla::CommandStack stack;
    fla::Layer* added = fla::newLayerFor(*timeline, false);
    stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline.get(), 0, added, "New Layer")));

    CHECK(timeline->layers.size() == 4);
    CHECK(timeline->layers[0] == added);
    CHECK(added->selected);
    CHECK(!timeline->layers[3]->selected);
    // The layer inside the folder still points at the folder, now at 1.
    CHECK(timeline->layers[2]->parentLayerIndex == 1);

    stack.undo();
    CHECK(timeline->layers.size() == 3);
    CHECK(timeline->layers[1]->parentLayerIndex == 0);
    CHECK(timeline->layers[2]->selected);

    stack.redo();
    CHECK(timeline->layers.size() == 4 && timeline->layers[0] == added);
}

TEST(removing_a_folder_takes_its_layers_and_undo_puts_them_back)
{
    std::unique_ptr<fla::Timeline> timeline = timelineWithFolder();
    fla::Layer* folder = timeline->layers[0];
    fla::Layer* inside = timeline->layers[1];
    fla::CommandStack stack;

    const std::vector<int> going = fla::RemoveLayerCommand::affectedIndices(*timeline, 0);
    CHECK(going.size() == 2 && going[0] == 0 && going[1] == 1);

    stack.push(fla::CommandPtr(new fla::RemoveLayerCommand(timeline.get(), 0, "Delete Layer")));
    CHECK(timeline->layers.size() == 1);
    CHECK(timeline->layers[0]->name == "Layer_2");
    CHECK(timeline->layers[0]->parentLayerIndex == -1);

    stack.undo();
    CHECK(timeline->layers.size() == 3);
    CHECK(timeline->layers[0] == folder && timeline->layers[1] == inside);
    CHECK(inside->parentLayerIndex == 0 && inside->parentLayer == folder);
}

TEST(removing_the_current_layer_makes_a_neighbor_current)
{
    std::unique_ptr<fla::Timeline> timeline = timelineWithFolder();
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::RemoveLayerCommand(timeline.get(), 2, "Delete Layer")));
    CHECK(timeline->layers.size() == 2);
    // The folder cannot be current, so the layer inside it is.
    CHECK(timeline->layers[1]->selected);
    stack.undo();
    CHECK(timeline->layers.size() == 3 && timeline->layers[2]->selected && !timeline->layers[1]->selected);
}

TEST(a_locked_folder_locks_the_layers_in_it)
{
    fla::Layer folder(nullptr);
    folder.layerType = fla::Layer::Type::Folder;
    fla::Layer inner(nullptr);
    inner.parentLayer = &folder;
    fla::Layer deeper(nullptr);
    deeper.parentLayer = &inner;
    CHECK(!deeper.isLocked());
    folder.locked = true;
    CHECK(inner.isLocked());
    // Only folders pass their lock on.
    CHECK(!deeper.isLocked());
    inner.layerType = fla::Layer::Type::Folder;
    CHECK(deeper.isLocked());
}
