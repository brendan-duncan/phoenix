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

TEST(sound_envelopes_and_split_audio)
{
    fla::Layer layer(nullptr);
    fla::Frame* frame = new fla::Frame(&layer);
    frame->duration = 48;
    frame->extraAttributes.emplace_back("soundName", "voice.wav");
    frame->extraAttributes.emplace_back("soundSync", "stream");
    layer.frames.push_back(frame);
    CHECK(fla::frame_sound::envelope(*frame).empty());

    // A hand-made envelope and trim, read back.
    std::vector<fla::frame_sound::EnvelopePoint> points = {{0, 0, 0}, {22050, 32768, 16384}, {88200, 32768, 32768}};
    fla::frame_sound::setEnvelope(points, 4410, 132300, frame->extraAttributes, frame->extraChildren);
    CHECK(fla::frame_sound::envelope(*frame) == points);
    CHECK(fla::frame_sound::read(*frame).effect == "custom");
    size_t in = 0, out = 0;
    fla::frame_sound::trim(*frame, in, out);
    CHECK(in == 4410 && out == 132300);

    // Cut a second in, at 24 fps: the second keyframe starts the sound a
    // second further on, with the envelope still.
    CHECK(fla::frame_sound::splitAudio(layer, 24, 24.0));
    CHECK(layer.frames.size() == 2 && layer.frames[1]->index == 24);
    size_t secondIn = 0, secondOut = 0;
    fla::frame_sound::trim(*layer.frames[1], secondIn, secondOut);
    CHECK(secondIn == 4410 + 44100 && secondOut == 132300);
    fla::frame_sound::trim(*layer.frames[0], in, out);
    CHECK(out == 4410 + 44100);
    CHECK(fla::frame_sound::read(*layer.frames[1]).name == "voice.wav");
    CHECK(fla::frame_sound::envelope(*layer.frames[1]) == points);
    // Not on a keyframe, nor on an event sound.
    CHECK(!fla::frame_sound::splitAudio(layer, 24, 24.0));
    fla::frame_sound::setEnvelope({}, 0, 0, frame->extraAttributes, frame->extraChildren);
    CHECK(fla::frame_sound::envelope(*frame).empty() && fla::frame_sound::read(*frame).effect == "none");
}
