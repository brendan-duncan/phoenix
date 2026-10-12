#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include <memory>
#include <string>
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

namespace {

/// A timeline of layers named by the letters given, top first, none linked.
std::unique_ptr<fla::Timeline> lettered(const char* names)
{
    std::unique_ptr<fla::Timeline> timeline(new fla::Timeline(nullptr));
    for (const char* c = names; *c; ++c)
    {
        fla::Layer* layer = new fla::Layer(timeline.get());
        layer->name = std::string(1, *c);
        timeline->layers.push_back(layer);
    }
    return timeline;
}

std::string order(const fla::Timeline& timeline)
{
    std::string names;
    for (const fla::Layer* layer : timeline.layers)
        names += layer->name;
    return names;
}

fla::Layer* named(const fla::Timeline& timeline, char name)
{
    for (fla::Layer* layer : timeline.layers)
    {
        if (layer->name[0] == name)
            return layer;
    }
    return nullptr;
}

} // namespace

TEST(moving_a_folder_takes_its_layers_and_undoes_in_one_step)
{
    std::unique_ptr<fla::Timeline> timeline = lettered("FabC");
    fla::Layer* folder = named(*timeline, 'F');
    folder->layerType = fla::Layer::Type::Folder;
    named(*timeline, 'a')->parentLayer = folder;
    named(*timeline, 'b')->parentLayer = folder;

    fla::LayerSnapshot after = fla::snapshotLayers(*timeline);
    // To the bottom, under nothing: its layers go with it.
    CHECK(fla::layers::moveLayer(after, folder, nullptr, nullptr));
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::LayerEditCommand(timeline.get(), fla::snapshotLayers(*timeline), after, "Move Layer")));
    CHECK(order(*timeline) == "CFab");
    CHECK(named(*timeline, 'a')->parentLayerIndex == 1);
    stack.undo();
    CHECK(order(*timeline) == "FabC");
    CHECK(named(*timeline, 'b')->parentLayerIndex == 0);

    // A layer into a folder, and a folder never into itself.
    after = fla::snapshotLayers(*timeline);
    CHECK(fla::layers::moveLayer(after, named(*timeline, 'C'), named(*timeline, 'a'), folder));
    CHECK(!fla::layers::moveLayer(after, folder, nullptr, named(*timeline, 'a')));
    fla::LayerEditCommand::apply(*timeline, after);
    CHECK(order(*timeline) == "FCab" && named(*timeline, 'C')->parentLayer == folder);
}

TEST(a_layer_becomes_masked_under_the_mask_above_and_a_mask_lets_go)
{
    std::unique_ptr<fla::Timeline> timeline = lettered("MxY");
    fla::Layer* mask = named(*timeline, 'M');
    fla::LayerSnapshot after = fla::snapshotLayers(*timeline);
    // No mask above yet.
    CHECK(!fla::layers::setKind(after, named(*timeline, 'Y'), fla::layers::Kind::Masked));
    CHECK(fla::layers::setKind(after, mask, fla::layers::Kind::Mask));
    CHECK(fla::layers::setKind(after, named(*timeline, 'Y'), fla::layers::Kind::Masked));
    fla::LayerEditCommand::apply(*timeline, after);
    // Y moves up to sit right under the mask, ahead of the plain layer x.
    CHECK(order(*timeline) == "MYx");
    CHECK(named(*timeline, 'Y')->layerType == fla::Layer::Type::Masked);
    CHECK(named(*timeline, 'Y')->parentLayer == mask);
    CHECK(fla::layers::kindOf(after, named(*timeline, 'Y')) == fla::layers::Kind::Masked);

    // The mask goes back to normal and its layer with it.
    after = fla::snapshotLayers(*timeline);
    CHECK(fla::layers::setKind(after, mask, fla::layers::Kind::Normal));
    fla::LayerEditCommand::apply(*timeline, after);
    CHECK(named(*timeline, 'Y')->parentLayer == nullptr);
    CHECK(named(*timeline, 'Y')->layerType == fla::Layer::Type::Normal);
}

#include "../src/edit/camera.h"

TEST(multiplane_camera_pans_near_layers_more_than_far)
{
    fla::Timeline timeline(nullptr);
    fla::Layer* camera = fla::camera::newCameraLayer(timeline, 10, 400.0, 300.0);
    timeline.layers.push_back(camera);
    CHECK(fla::camera::isCamera(*camera) && fla::camera::cameraOf(timeline) == camera);
    CHECK(camera->layerType == fla::Layer::Type::Guide);

    // At rest every depth shows as drawn.
    fla::camera::State state = fla::camera::stateAt(timeline, 3, 400.0, 300.0);
    CHECK(state.x == 200.0 && state.y == 150.0 && state.zoom == 100.0);
    fla::Transform t;
    CHECK(fla::camera::view(state, 3000.0, 400.0, 300.0, t));
    CHECK(std::fabs(t.m11 - 1.0) < 1e-9 && std::fabs(t.tx) < 1e-9 && std::fabs(t.ty) < 1e-9);

    // Panning 100 px right moves the stage plane 100 px left, a layer a
    // focal length behind half that.
    state.x += 100.0;
    CHECK(fla::camera::view(state, 0.0, 400.0, 300.0, t) && std::fabs(t.tx + 100.0) < 1e-9);
    CHECK(fla::camera::view(state, fla::camera::kFocal, 400.0, 300.0, t) && std::fabs(t.tx + 50.0) < 1e-9);

    // At 200% the stage plane doubles; a far layer grows less.
    state.x = 200.0;
    state.zoom = 200.0;
    CHECK(fla::camera::view(state, 0.0, 400.0, 300.0, t) && std::fabs(t.m11 - 2.0) < 1e-9);
    CHECK(fla::camera::view(state, fla::camera::kFocal, 400.0, 300.0, t) && t.m11 > 1.0 && t.m11 < 2.0);
    // A layer the camera has moved past is not seen.
    CHECK(!fla::camera::view(state, -600.0, 400.0, 300.0, t));

    // Keyframes tween.
    fla::Frame* first = camera->frames.front();
    first->duration = 5;
    first->tweenType = fla::TweenType::Motion;
    fla::Frame* second = new fla::Frame(camera);
    second->index = 5;
    second->duration = 5;
    fla::camera::State moved;
    moved.x = 300.0;
    moved.y = 150.0;
    moved.zoom = 400.0;
    fla::camera::set(second->extraAttributes, moved);
    camera->frames.push_back(second);
    const fla::camera::State half = fla::camera::stateAt(timeline, 2, 400.0, 300.0);
    CHECK(half.x > 200.0 && half.x < 300.0);
    CHECK(std::fabs(fla::camera::stateAt(timeline, 7, 400.0, 300.0).zoom - 400.0) < 1e-9);

    fla::Layer layer(nullptr);
    fla::camera::setDepth(layer.extraAttributes, 250.0);
    CHECK(fla::camera::depthOf(layer) == 250.0);
    fla::camera::setDepth(layer.extraAttributes, 0.0);
    CHECK(layer.extraAttributes.empty());
}

TEST(layers_copy_with_their_folders_and_rigs_and_fade)
{
    std::unique_ptr<fla::Timeline> timeline = timelineWithFolder();
    // The folder picked takes its layer along.
    CHECK((fla::layers::withContents(*timeline, {0}) == std::vector<int>{0, 1}));
    CHECK((fla::layers::withContents(*timeline, {2, 1}) == std::vector<int>{1, 2}));

    // A parenting rig: Layer_2 follows Layer_1.
    timeline->layers[1]->extraAttributes.emplace_back("layerRiggingIndex", "0");
    timeline->layers[2]->extraAttributes.emplace_back("layerRiggingIndex", "1");
    timeline->layers[2]->frames[0]->extraAttributes.emplace_back("parentLayerIndex", "0");

    std::vector<fla::Layer*> copies = fla::layers::cloneLayers(
        {timeline->layers[0], timeline->layers[1], timeline->layers[2]}, *timeline);
    CHECK(copies.size() == 3);
    CHECK(copies[1]->parentLayer == copies[0] && copies[2]->parentLayer == nullptr);
    CHECK(copies[1]->frames.size() == 1 && copies[1]->frames[0] != timeline->layers[1]->frames[0]);
    CHECK(copies[1]->frames[0]->duration == 12 && copies[1]->frames[0]->parent == copies[1]);
    // New rigs, after the timeline's, and the child follows the copied parent.
    CHECK(copies[1]->extraAttributes.back().second == "2" && copies[2]->extraAttributes.back().second == "3");
    CHECK(copies[2]->frames[0]->extraAttributes.back().second == "2");
    for (fla::Layer* copy : copies)
        delete copy;

    // Copied alone, the child still follows the original parent.
    std::vector<fla::Layer*> alone = fla::layers::cloneLayers({timeline->layers[2]}, *timeline);
    CHECK(alone[0]->frames[0]->extraAttributes.back().second == "0");
    delete alone[0];

    fla::Layer& layer = *timeline->layers[2];
    CHECK(fla::layers::transparencyOf(layer) == 0);
    layer.extraAttributes = fla::layers::withTransparency(layer, 40);
    CHECK(fla::layers::transparencyOf(layer) == 40);
    layer.extraAttributes = fla::layers::withTransparency(layer, 0);
    CHECK(fla::layers::transparencyOf(layer) == 0 && layer.extraAttributes.size() == 1);
}
