#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/timeline_edit.h"

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
