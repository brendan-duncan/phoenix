#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/frame_commands.h"
#include "../src/edit/frame_sound.h"

#include <memory>

namespace {

/// A layer with one keyframe spanning frames 0 to 9 holding one shape.
std::unique_ptr<fla::Layer> layerWithSpan()
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* frame = new fla::Frame(layer.get());
    frame->index = 0;
    frame->duration = 10;
    frame->keyMode = "9728";
    frame->elements.push_back(new fla::Shape(frame));
    layer->frames.push_back(frame);
    layer->firstFrame = 0;
    layer->lastFrame = 10;
    return layer;
}

} // namespace

TEST(inserting_a_keyframe_cuts_the_span_and_copies_its_contents)
{
    std::unique_ptr<fla::Layer> layer = layerWithSpan();
    fla::CommandStack stack;

    fla::InsertKeyframeCommand* command = new fla::InsertKeyframeCommand(layer.get(), 4, "Insert Keyframe");
    stack.push(fla::CommandPtr(command));

    CHECK(layer->frames.size() == 2);
    if (layer->frames.size() != 2)
        return;
    CHECK(layer->frames[0]->index == 0 && layer->frames[0]->duration == 4);
    CHECK(layer->frames[1]->index == 4 && layer->frames[1]->duration == 6);
    CHECK(layer->frames[1] == command->frame());
    CHECK(layer->frames[1]->keyMode == "9728");
    // A copy of the shape, not the same one.
    CHECK(layer->frames[1]->elements.size() == 1);
    CHECK(layer->frames[1]->elements[0] != layer->frames[0]->elements[0]);
    CHECK(layer->lastFrame == 10);

    stack.undo();
    CHECK(layer->frames.size() == 1);
    CHECK(layer->frames[0]->duration == 10);

    stack.redo();
    CHECK(layer->frames.size() == 2);
    CHECK(layer->frames[0]->duration == 4);
}

TEST(inserting_a_keyframe_past_the_end_stretches_the_last_span)
{
    std::unique_ptr<fla::Layer> layer = layerWithSpan();
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::InsertKeyframeCommand(layer.get(), 15, "Insert Keyframe")));

    CHECK(layer->frames.size() == 2);
    if (layer->frames.size() != 2)
        return;
    CHECK(layer->frames[0]->duration == 15);
    CHECK(layer->frames[1]->index == 15 && layer->frames[1]->duration == 1);
    CHECK(layer->frames[1]->elements.size() == 1);
    CHECK(layer->lastFrame == 16);

    stack.undo();
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 10);
    CHECK(layer->lastFrame == 10);
}

TEST(inserting_where_a_keyframe_already_is_changes_nothing)
{
    std::unique_ptr<fla::Layer> layer = layerWithSpan();
    fla::CommandStack stack;
    fla::InsertKeyframeCommand* command = new fla::InsertKeyframeCommand(layer.get(), 0, "Insert Keyframe");
    stack.push(fla::CommandPtr(command));
    CHECK(layer->frames.size() == 1);
    CHECK(command->frame() == layer->frames[0]);
    stack.undo();
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 10);
}

TEST(a_frame_sound_is_written_as_animate_keeps_it)
{
    fla::Frame frame(nullptr);
    frame.extraAttributes.emplace_back("keepMe", "1");
    fla::frame_sound::Settings settings;
    settings.name = "beat.wav";
    settings.sync = "stream";
    settings.effect = "fade in";
    settings.loops = 3;
    fla::frame_sound::write(settings, 4000, frame.extraAttributes, frame.extraChildren);

    const fla::frame_sound::Settings read = fla::frame_sound::read(frame);
    CHECK(read.name == "beat.wav" && read.sync == "stream" && read.effect == "fade in");
    CHECK(read.loops == 3 && !read.loop);
    CHECK(frame.extraAttributes.front().first == "keepMe");
    CHECK(frame.extraChildren.size() == 1);
    CHECK(frame.extraChildren.front().find("mark44=\"1000\"") != std::string::npos);

    // An event sound played once writes nothing but its name.
    settings.sync = "event";
    settings.effect = "none";
    settings.loops = 1;
    fla::frame_sound::write(settings, 4000, frame.extraAttributes, frame.extraChildren);
    CHECK(frame.extraAttributes.size() == 2);
    CHECK(frame.extraChildren.empty());

    // No name takes the sound off.
    settings.name.clear();
    fla::frame_sound::write(settings, 4000, frame.extraAttributes, frame.extraChildren);
    CHECK(frame.extraAttributes.size() == 1);
}
