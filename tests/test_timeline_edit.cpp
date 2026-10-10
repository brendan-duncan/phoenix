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
