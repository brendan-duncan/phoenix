#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/timeline_edit.h"
#include "../src/edit/motion_object.h"
#include "../src/edit/frame_bake.h"
#include "../src/data/symbol_instance.h"
#include <cmath>
#include <memory>

#include <memory>
#include <vector>

namespace {

/// A layer with keyframes at 0 (5 frames, one shape) and 5 (5 frames).
std::unique_ptr<fla::Layer> twoSpans()
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = new fla::Frame(layer.get());
    a->index = 0;
    a->duration = 5;
    a->elements.push_back(new fla::Shape(a));
    fla::Frame* b = new fla::Frame(layer.get());
    b->index = 5;
    b->duration = 5;
    layer->frames = {a, b};
    layer->lastFrame = 10;
    return layer;
}

bool spans(const fla::Layer& layer, const std::vector<std::pair<int, int>>& expected)
{
    if (layer.frames.size() != expected.size())
        return false;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        if (layer.frames[i]->index != expected[i].first || layer.frames[i]->duration != expected[i].second)
            return false;
    }
    return true;
}

} // namespace

TEST(insert_frames_grows_the_span_and_moves_the_rest)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    fla::timeline::insertFrames(*layer, 2, 3);
    CHECK(spans(*layer, {{0, 8}, {8, 5}}));
    CHECK(layer->lastFrame == 13);
    // Past the end, the last span reaches out to the frame.
    fla::timeline::insertFrames(*layer, 20, 1);
    CHECK(spans(*layer, {{0, 8}, {8, 13}}));
}

TEST(insert_keyframe_copies_and_blank_keyframe_does_not)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    fla::Frame* copy = fla::timeline::insertKeyframe(*layer, 2, false);
    CHECK(spans(*layer, {{0, 2}, {2, 3}, {5, 5}}));
    CHECK(copy && copy->elements.size() == 1);
    fla::Frame* blank = fla::timeline::insertKeyframe(*layer, 12, true);
    CHECK(spans(*layer, {{0, 2}, {2, 3}, {5, 7}, {12, 1}}));
    CHECK(blank && blank->elements.empty());
    // Where a keyframe already is, nothing changes.
    CHECK(fla::timeline::insertKeyframe(*layer, 5, true) == layer->frames[2]);
    CHECK(layer->frames.size() == 4);
}

TEST(insert_keyframe_in_a_classic_tween_keeps_it_tweening)
{
    // A shape moved 0 to 100 over frames 0 to 10, eased.
    std::unique_ptr<fla::Layer> layer = twoSpans();
    fla::Frame* start = layer->frames[0];
    fla::Frame* end = layer->frames[1];
    start->duration = 10;
    end->index = 10;
    end->duration = 1;
    end->elements.push_back(new fla::Shape(end));
    end->elements[0]->transform.tx = 100.0;
    start->tweenType = fla::TweenType::Motion;
    start->motionTweenRotate = "none";
    start->hasCustomEase = true;
    start->customEase = {{0, 0}, {0.25, 0.25}, {0.75, 0.75}, {1, 1}};

    fla::Frame* key = fla::timeline::insertKeyframe(*layer, 4, false);
    CHECK(spans(*layer, {{0, 4}, {4, 6}, {10, 1}}));
    // The pose the tween showed there, and the tween going on from it.
    CHECK(key && key->elements.size() == 1);
    CHECK_NEAR(key->elements[0]->transform.tx, 40.0);
    CHECK(key->tweenType == fla::TweenType::Motion);
    CHECK(key->hasCustomEase && key->customEase.size() == 4);
    CHECK(key->motionTweenRotate == "none");
    CHECK(start->tweenType == fla::TweenType::Motion);

    // Outside a tween a keyframe copies the span's start, as before.
    start->tweenType = fla::TweenType::None;
    fla::Frame* still = fla::timeline::insertKeyframe(*layer, 2, false);
    CHECK(still && still->elements.size() == 1 && still->elements[0]->transform.tx == 0.0);
    CHECK(still->tweenType == fla::TweenType::None);
}

TEST(remove_frames_shortens_spans_and_drops_empty_ones)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    std::vector<fla::Frame*> removed;
    fla::timeline::removeFrames(*layer, 3, 4, removed);
    // Frames 3..6 go: the first span keeps 0..2, the second starts at 3.
    CHECK(spans(*layer, {{0, 3}, {3, 3}}));
    CHECK(removed.empty());

    fla::timeline::removeFrames(*layer, 3, 3, removed);
    CHECK(spans(*layer, {{0, 3}}));
    CHECK(removed.size() == 1);
    for (fla::Frame* frame : removed)
        delete frame;
}

TEST(clear_keyframe_hands_its_frames_to_the_span_before)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    std::vector<fla::Frame*> removed;
    CHECK(!fla::timeline::clearKeyframe(*layer, 0, removed));
    CHECK(fla::timeline::clearKeyframe(*layer, 5, removed));
    CHECK(spans(*layer, {{0, 10}}));
    CHECK(removed.size() == 1);
    for (fla::Frame* frame : removed)
        delete frame;
}

TEST(move_keyframe_stays_between_its_neighbors)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    CHECK(fla::timeline::moveKeyframe(*layer, 5, 3));
    CHECK(spans(*layer, {{0, 3}, {3, 5}}));
    // The last keyframe keeps its length when moved later.
    CHECK(fla::timeline::moveKeyframe(*layer, 3, 8));
    CHECK(spans(*layer, {{0, 8}, {8, 5}}));
    // It cannot pass the keyframe before it.
    CHECK(fla::timeline::moveKeyframe(*layer, 8, 0));
    CHECK(spans(*layer, {{0, 1}, {1, 5}}));
}

TEST(classic_tween_needs_a_keyframe_to_tween_to)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    CHECK(fla::timeline::setTween(*layer, 2, true));
    CHECK(layer->frames[0]->tweenType == fla::TweenType::Motion);
    CHECK(!fla::timeline::setTween(*layer, 7, true));
    CHECK(fla::timeline::setTween(*layer, 2, false));
    CHECK(layer->frames[0]->tweenType == fla::TweenType::None);
}

TEST(edit_frames_command_undoes_and_redoes_and_keeps_removed_frames)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    fla::Layer* target = layer.get();
    fla::Frame* second = layer->frames[1];
    fla::CommandStack stack;
    fla::EditFramesCommand* command = new fla::EditFramesCommand({target}, "Remove Frames",
        [target](std::vector<fla::Frame*>& removed) { fla::timeline::removeFrames(*target, 5, 5, removed); });
    stack.push(fla::CommandPtr(command));
    CHECK(command->changedAnything());
    CHECK(spans(*layer, {{0, 5}}));

    stack.undo();
    CHECK(spans(*layer, {{0, 5}, {5, 5}}));
    CHECK(layer->frames[1] == second);

    stack.redo();
    CHECK(spans(*layer, {{0, 5}}));

    // A keyframe made by an edit goes with an undo and comes back with a redo.
    stack.push(fla::CommandPtr(new fla::EditFramesCommand({target}, "Insert Blank Keyframe",
        [target](std::vector<fla::Frame*>&) { fla::timeline::insertKeyframe(*target, 3, true); })));
    CHECK(spans(*layer, {{0, 3}, {3, 2}}));
    stack.undo();
    CHECK(spans(*layer, {{0, 5}}));
    stack.redo();
    CHECK(spans(*layer, {{0, 3}, {3, 2}}));
}

TEST(copy_frames_cuts_spans_to_the_range)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    std::vector<fla::Frame*> copies = fla::timeline::copyFrames(*layer, 3, 6);
    // Frames 3-4 of the first span and 5-6 of the second, numbered from 0.
    CHECK(copies.size() == 2);
    if (copies.size() == 2)
    {
        CHECK(copies[0]->index == 0 && copies[0]->duration == 2 && copies[0]->elements.size() == 1);
        CHECK(copies[1]->index == 2 && copies[1]->duration == 2);
    }
    for (fla::Frame* frame : copies)
        delete frame;

    // Past the end of the layer a blank span covers the rest.
    copies = fla::timeline::copyFrames(*layer, 8, 12);
    CHECK(copies.size() == 2);
    if (copies.size() == 2)
        CHECK(copies[1]->index == 2 && copies[1]->duration == 3 && copies[1]->elements.empty());
    for (fla::Frame* frame : copies)
        delete frame;
}

TEST(paste_frames_replaces_what_was_there)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    std::vector<fla::Frame*> copies = fla::timeline::copyFrames(*layer, 0, 1);
    std::vector<const fla::Frame*> clip(copies.begin(), copies.end());
    std::vector<fla::Frame*> removed;
    // Two frames of the first span pasted over frames 6-7 of the second:
    // the second keeps 5 and 8-9.
    fla::timeline::pasteFrames(*layer, 6, clip, removed);
    CHECK(spans(*layer, {{0, 5}, {5, 1}, {6, 2}, {8, 2}}));
    CHECK(layer->frames[2]->elements.size() == 1);
    CHECK(removed.empty());

    // Past the end, the last span reaches out to the paste.
    fla::timeline::pasteFrames(*layer, 14, clip, removed);
    CHECK(spans(*layer, {{0, 5}, {5, 1}, {6, 2}, {8, 6}, {14, 2}}));
    for (fla::Frame* frame : copies)
        delete frame;
    for (fla::Frame* frame : removed)
        delete frame;
}

TEST(dragging_a_block_of_frames_moves_or_copies_it)
{
    // Moved along its own layer: the block lands at frame 10, and where it
    // was is blank. Nothing after it moves.
    std::unique_ptr<fla::Layer> layer = twoSpans();
    std::vector<fla::Frame*> removed;
    CHECK(fla::timeline::moveFrames({layer.get()}, {layer.get()}, 0, 4, 10, false, removed));
    CHECK(spans(*layer, {{0, 5}, {5, 5}, {10, 5}}));
    CHECK(layer->frames[0]->elements.empty());
    CHECK(layer->frames[2]->elements.size() == 1);

    // Moved onto part of itself: it carries what it held.
    layer = twoSpans();
    CHECK(fla::timeline::moveFrames({layer.get()}, {layer.get()}, 0, 4, 2, false, removed));
    CHECK(spans(*layer, {{0, 2}, {2, 5}, {7, 3}}));
    CHECK(layer->frames[0]->elements.empty());
    CHECK(layer->frames[1]->elements.size() == 1);

    // Alt-dragged to the layer below: copied, the original left as it was.
    layer = twoSpans();
    std::unique_ptr<fla::Layer> below(new fla::Layer(nullptr));
    fla::Frame* empty = new fla::Frame(below.get());
    empty->index = 0;
    empty->duration = 10;
    below->frames = {empty};
    CHECK(fla::timeline::moveFrames({layer.get()}, {below.get()}, 0, 4, 2, true, removed));
    CHECK(spans(*layer, {{0, 5}, {5, 5}}));
    CHECK(layer->frames[0]->elements.size() == 1);
    CHECK(spans(*below, {{0, 2}, {2, 5}, {7, 3}}));
    CHECK(below->frames[1]->elements.size() == 1);

    // Nowhere to go, or no layers to pair up: nothing happens.
    CHECK(!fla::timeline::moveFrames({layer.get()}, {layer.get()}, 0, 4, 0, false, removed));
    CHECK(!fla::timeline::moveFrames({layer.get()}, {}, 0, 4, 3, false, removed));
    for (fla::Frame* frame : removed)
        delete frame;
}

namespace {

/// A layer with one ten-frame object motion tween moving x from 0 to 100,
/// at 24 frames a second (1000 TimeScale units a frame), then a keyframe.
std::unique_ptr<fla::Layer> motionLayer()
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* span = new fla::Frame(layer.get());
    span->index = 0;
    span->duration = 10;
    span->otherTweenType = "motion object";
    span->extraAttributes.emplace_back("isMotionObject", "true");
    span->extraChildren.push_back(
        "<motionObjectXML><AnimationCore TimeScale=\"24000\" Version=\"1\" duration=\"10000\">"
        "<TimeMap strength=\"0\" type=\"Quadratic\"/><PropertyContainer id=\"headContainer\">"
        "<PropertyContainer id=\"Basic_Motion\"><Property enabled=\"1\" id=\"Motion_X\">"
        "<Keyframe anchor=\"0,0\" next=\"0,0\" previous=\"0,0\" roving=\"0\" timevalue=\"0\"/>"
        "<Keyframe anchor=\"0,100\" next=\"0,100\" previous=\"0,100\" roving=\"0\" timevalue=\"10000\"/>"
        "</Property></PropertyContainer></PropertyContainer></AnimationCore></motionObjectXML>");
    fla::Frame* end = new fla::Frame(layer.get());
    end->index = 10;
    end->duration = 1;
    layer->frames = {span, end};
    return layer;
}

double motionX(fla::Frame& span, double frame)
{
    fla::MotionObject motion;
    std::string* xml = fla::timeline::motionOf(span);
    if (!xml || !motion.parse(*xml))
        return -1.0;
    return motion.at(frame, 24.0).x;
}

} // namespace

TEST(frame_commands_keep_an_object_motion_tween_whole)
{
    // F5 in the span: twice as long, the motion stretched over it.
    std::unique_ptr<fla::Layer> layer = motionLayer();
    fla::Layer* l = layer.get();
    {
        fla::EditFramesCommand stretch({l}, "Insert Frame", [l](std::vector<fla::Frame*>&) {
            fla::timeline::insertFrames(*l, 3, 10);
        });
        stretch.redo();
        CHECK(l->frames[0]->duration == 20);
        CHECK_NEAR(motionX(*l->frames[0], 10.0), 50.0);
        CHECK_NEAR(motionX(*l->frames[0], 20.0), 100.0);
        // Undo puts the motion back as it was, keys and all.
        stretch.undo();
        CHECK(l->frames[0]->duration == 10);
        CHECK_NEAR(motionX(*l->frames[0], 10.0), 100.0);
    }

    // F6 in the span: keyframes for the properties, the span and the motion
    // as they were.
    fla::timeline::insertKeyframe(*l, 4, false);
    CHECK(l->frames.size() == 2 && l->frames[0]->duration == 10);
    CHECK(fla::timeline::motionOf(*l->frames[0])->find("timevalue=\"4000\"") != std::string::npos);
    CHECK_NEAR(motionX(*l->frames[0], 4.0), 40.0);
    CHECK_NEAR(motionX(*l->frames[0], 10.0), 100.0);

    // F7 in the span: the motion stops where the blank keyframe starts.
    fla::timeline::insertKeyframe(*l, 5, true);
    fla::timeline::fitMotionSpans(*l);
    CHECK(l->frames[0]->duration == 5);
    CHECK_NEAR(motionX(*l->frames[0], 5.0), 50.0);

    // Remove Tween: static frames, the motion gone.
    CHECK(fla::timeline::setTween(*l, 0, false));
    CHECK(l->frames[0]->otherTweenType.empty());
    CHECK(fla::timeline::motionOf(*l->frames[0]) == nullptr);
}

namespace {

/// A classic tween: an instance at x 0 on frame 0 (10 frames) moving to
/// x 100 on frame 10 (1 frame).
std::unique_ptr<fla::Layer> classicTween()
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = new fla::Frame(layer.get());
    a->index = 0;
    a->duration = 10;
    a->tweenType = fla::TweenType::Motion;
    a->acceleration = 50;
    a->elements.push_back(new fla::SymbolInstance(a));
    fla::Frame* b = new fla::Frame(layer.get());
    b->index = 10;
    b->duration = 1;
    auto* moved = new fla::SymbolInstance(b);
    moved->transform.tx = 100.0;
    b->elements.push_back(moved);
    layer->frames = {a, b};
    layer->lastFrame = 11;
    return layer;
}

} // namespace

TEST(convert_to_frame_by_frame_bakes_a_tween_into_keyframes)
{
    std::unique_ptr<fla::Layer> layer = classicTween();
    layer->frames[0]->acceleration = 0;
    fla::frames::convertToFrameByFrame(*layer, 0, 9, 24.0);
    CHECK(layer->frames.size() == 11);
    for (int i = 0; i < 10; ++i)
    {
        const fla::Frame* frame = layer->frames[static_cast<size_t>(i)];
        CHECK(frame->index == i && frame->duration == 1 && frame->tweenType == fla::TweenType::None);
        CHECK(frame->elements.size() == 1 && std::fabs(frame->elements[0]->transform.tx - 10.0 * i) < 1e-9);
    }
    for (fla::Frame* frame : layer->frames)
    {
        for (fla::Element* element : frame->elements)
            CHECK(element->parent == frame);
    }
}

TEST(reverse_frames_plays_a_tween_backward)
{
    std::unique_ptr<fla::Layer> layer = classicTween();
    std::vector<fla::Frame*> removed;
    fla::frames::reverseFrames(*layer, 0, 10, removed);
    // The end keyframe comes first and tweens back to the start, eased the
    // other way.
    CHECK(spans(*layer, {{0, 10}, {10, 1}}));
    CHECK(layer->frames[0]->elements[0]->transform.tx == 100.0);
    CHECK(layer->frames[0]->tweenType == fla::TweenType::Motion && layer->frames[0]->acceleration == -50);
    CHECK(layer->frames[1]->elements[0]->transform.tx == 0.0);
    CHECK(layer->frames[1]->tweenType == fla::TweenType::None);
}

TEST(reverse_frames_reverses_held_drawings)
{
    // Drawing A for 1 frame, then B held for 5: reversed, B for 5 then A.
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = new fla::Frame(layer.get());
    a->index = 0;
    a->duration = 1;
    a->elements.push_back(new fla::Shape(a));
    fla::Frame* b = new fla::Frame(layer.get());
    b->index = 1;
    b->duration = 5;
    b->elements.push_back(new fla::Shape(b));
    b->elements.push_back(new fla::Shape(b));
    layer->frames = {a, b};
    std::vector<fla::Frame*> removed;
    fla::frames::reverseFrames(*layer, 0, 5, removed);
    CHECK(layer->frames.size() == 3);
    // A has one shape, B two.
    CHECK(layer->frames.back()->index == 5 && layer->frames.back()->elements.size() == 1);
    for (size_t i = 0; i + 1 < layer->frames.size(); ++i)
        CHECK(layer->frames[i]->elements.size() == 2);
    int covered = 0;
    for (const fla::Frame* frame : layer->frames)
        covered += frame->duration;
    CHECK(covered == 6);
}

TEST(paste_frames_inserts_and_moves_the_rest_along)
{
    std::unique_ptr<fla::Layer> layer = twoSpans();
    std::vector<fla::Frame*> copies = fla::timeline::copyFrames(*layer, 0, 1);
    std::vector<const fla::Frame*> copied(copies.begin(), copies.end());
    std::vector<fla::Frame*> removed;
    fla::frames::insertFrames(*layer, 5, copied, removed);
    // The pasted two frames sit at 5 and the span that was there starts at 7.
    CHECK(layer->frames.size() == 3);
    CHECK(layer->frames[1]->index == 5 && layer->frames[1]->duration == 2);
    CHECK(layer->frames[2]->index == 7 && layer->frames[2]->duration == 5);
    for (fla::Frame* frame : copies)
        delete frame;
    for (fla::Frame* frame : removed)
        delete frame;
}

TEST(create_motion_tween_on_a_symbol_instance)
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Layer* l = layer.get();
    fla::Frame* frame = new fla::Frame(l);
    frame->index = 0;
    frame->duration = 1;
    l->frames = {frame};
    auto* instance = new fla::SymbolInstance(frame);
    instance->transform = fla::Transform(2.0, 0.0, 0.0, 2.0, 30.0, 40.0);
    frame->elements.push_back(instance);

    fla::EditFramesCommand create({l}, "Create Motion Tween", [l](std::vector<fla::Frame*>&) {
        fla::timeline::createMotionTween(*l, 0, 24.0);
    });
    create.redo();
    // A lone frame grows to a second, the object held as it was.
    CHECK(l->frames.size() == 1 && l->frames[0]->duration == 24);
    CHECK(l->frames[0]->otherTweenType == "motion object" && l->frames[0]->keyMode == "8195");
    fla::MotionObject motion;
    CHECK(fla::timeline::motionOf(*l->frames[0]) && motion.parse(*fla::timeline::motionOf(*l->frames[0])));
    CHECK_NEAR(motion.at(12.0, 24.0).scaleX, 200.0);
    CHECK(!fla::timeline::createMotionTween(*l, 0, 24.0));

    create.undo();
    CHECK(l->frames[0]->duration == 1 && l->frames[0]->otherTweenType.empty() && l->frames[0]->keyMode.empty());
    CHECK(fla::timeline::motionOf(*l->frames[0]) == nullptr);
}

TEST(paste_motion_onto_another_instance)
{
    // Copied: a slide 100 px right while turning from 10 to 40 degrees,
    // over 10 frames at 10 fps.
    fla::MotionState start;
    start.rotation = 10.0;
    std::string copied = fla::motion::create(start, 10, 10.0);
    fla::MotionState end = start;
    end.x = 100.0;
    end.rotation = 40.0;
    CHECK(fla::motion::setKeys(copied, 27000.0, end));

    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Layer* l = layer.get();
    fla::Frame* frame = new fla::Frame(l);
    frame->duration = 3;
    l->frames = {frame};
    auto* instance = new fla::SymbolInstance(frame);
    frame->elements.push_back(instance);

    fla::EditFramesCommand paste({l}, "Paste Motion", [l, copied](std::vector<fla::Frame*>&) {
        fla::timeline::pasteMotion(*l, 0, copied, 10.0);
    });
    paste.redo();
    CHECK(paste.changedAnything());
    // The span grew to the motion's length; the turn starts from this
    // object's (none), still turning 30 degrees.
    CHECK(l->frames[0]->duration == 10);
    fla::MotionObject motion;
    CHECK(motion.parse(*fla::timeline::motionOf(*l->frames[0])));
    CHECK_NEAR(motion.at(0.0, 10.0).rotation, 0.0);
    CHECK_NEAR(motion.at(9.0, 10.0).rotation, 30.0);
    CHECK_NEAR(motion.at(9.0, 10.0).x, 100.0);
    paste.undo();
    CHECK(l->frames[0]->duration == 3 && !fla::timeline::motionOf(*l->frames[0]));

    // A bare AnimationCore, as a preset keeps it, pastes as well.
    const std::string core = copied.substr(copied.find("<AnimationCore"),
        copied.find("</motionObjectXML>") - copied.find("<AnimationCore"));
    CHECK(fla::timeline::pasteMotion(*l, 0, core, 10.0));
    CHECK(!fla::timeline::pasteMotion(*l, 0, "<nothing/>", 10.0));
}

TEST(scale_spans_keeps_their_length_in_seconds)
{
    // Keyframes at 0, 12 and 18 of a 24-frame layer: from 24 fps to 30.
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Layer* l = layer.get();
    for (const auto& span : {std::make_pair(0, 12), std::make_pair(12, 6), std::make_pair(18, 6)})
    {
        fla::Frame* frame = new fla::Frame(l);
        frame->index = span.first;
        frame->duration = span.second;
        l->frames.push_back(frame);
    }
    fla::timeline::scaleSpans(*l, 30.0 / 24.0);
    CHECK(l->frames[0]->index == 0 && l->frames[0]->duration == 15);
    CHECK(l->frames[1]->index == 15 && l->frames[1]->duration == 8);
    CHECK(l->frames[2]->index == 23 && l->frames[2]->duration == 7);

    // Squeezed hard, every keyframe still has a frame of its own.
    fla::timeline::scaleSpans(*l, 0.01);
    CHECK(l->frames[0]->index == 0 && l->frames[1]->index == 1 && l->frames[2]->index == 2);
    for (const fla::Frame* frame : l->frames)
        CHECK(frame->duration >= 1);
}

TEST(clear_frames_leaves_blank_frames_and_keeps_what_follows)
{
    // One 10-frame keyframe holding a shape; frames 3 to 5 cleared.
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Layer* l = layer.get();
    fla::Frame* frame = new fla::Frame(l);
    frame->duration = 10;
    frame->elements.push_back(new fla::Shape(frame));
    l->frames = {frame};
    fla::EditFramesCommand clear({l}, "Clear Frames", [l](std::vector<fla::Frame*>& removed) {
        fla::timeline::clearFrames(*l, 3, 5, removed);
    });
    clear.redo();
    CHECK(l->frames.size() == 3);
    CHECK(l->frames[0]->index == 0 && l->frames[0]->duration == 3 && l->frames[0]->elements.size() == 1);
    CHECK(l->frames[1]->index == 3 && l->frames[1]->duration == 3 && l->frames[1]->elements.empty());
    CHECK(l->frames[2]->index == 6 && l->frames[2]->duration == 4 && l->frames[2]->elements.size() == 1);
    clear.undo();
    CHECK(l->frames.size() == 1 && l->frames[0]->duration == 10);


    // Clearing from a keyframe replaces it with a blank one.
    std::unique_ptr<fla::Layer> other(new fla::Layer(nullptr));
    fla::Frame* keyed = new fla::Frame(other.get());
    keyed->duration = 4;
    keyed->elements.push_back(new fla::Shape(keyed));
    other->frames = {keyed};
    std::vector<fla::Frame*> removed;
    fla::timeline::clearFrames(*other, 0, 3, removed);
    CHECK(other->frames.size() == 1 && other->frames[0]->elements.empty() && other->frames[0]->duration == 4);
    CHECK(removed.size() == 1 && removed[0] == keyed);
    delete keyed;
}
