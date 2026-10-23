#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/symbol_instance.h"
#include "../src/edit/artist_edit.h"
#include "../src/edit/timeline_edit.h"

using namespace fla;

TEST(artist_new_frame_goes_after_the_drawing_and_moves_the_rest_along)
{
    // A drawing held five frames, then another held three.
    Layer layer(nullptr);
    timeline::insertFrames(layer, 4, 1);
    timeline::insertKeyframe(layer, 5, true);
    timeline::insertFrames(layer, 5, 2);
    CHECK(layer.frames.size() == 2 && layer.frames[1]->index == 5 && layer.frames[1]->duration == 3);
    layer.frames[0]->elements.push_back(new Shape(layer.frames[0]));

    // A blank frame from inside the first drawing: right after its hold,
    // the second drawing a frame later.
    CHECK(artist::addFrameAfter(layer, 2, false) == 5);
    CHECK(layer.frames.size() == 3);
    CHECK(layer.frames[0]->duration == 5);
    CHECK(layer.frames[1]->index == 5 && layer.frames[1]->duration == 1 && layer.frames[1]->elements.empty());
    CHECK(layer.frames[2]->index == 6 && layer.frames[2]->duration == 3);

    // A copy keeps the drawing.
    CHECK(artist::addFrameAfter(layer, 0, true) == 5);
    CHECK(layer.frames.size() == 4);
    CHECK(layer.frames[1]->index == 5 && layer.frames[1]->duration == 1 && layer.frames[1]->elements.size() == 1);
    CHECK(layer.frames[2]->index == 6 && layer.frames[2]->elements.empty());
    CHECK(layer.frames[3]->index == 7 && layer.frames[3]->duration == 3);

    // Past the end: right after the last span.
    CHECK(timeline::layerEnd(layer) == 10);
    CHECK(artist::addFrameAfter(layer, 30, false) == 10);
    CHECK(layer.frames.size() == 5 && layer.frames[3]->duration == 3 && layer.frames[4]->index == 10);

    // An empty layer gets its first frame.
    Layer empty(nullptr);
    CHECK(artist::addFrameAfter(empty, 3, false) == 0);
    CHECK(empty.frames.size() == 1 && empty.frames[0]->index == 0);
}

TEST(artist_key_for_move_keys_the_frame_and_tweens_the_span_before)
{
    Layer layer(nullptr);
    timeline::insertFrames(layer, 11, 1);
    Frame* first = layer.frames[0];
    first->elements.push_back(new SymbolInstance(first));
    CHECK(artist::tweenable(*first));

    CHECK(!artist::insideSpan(layer, 0));
    CHECK(artist::insideSpan(layer, 7));
    CHECK(!artist::insideSpan(layer, 12));
    CHECK(artist::keyForMove(layer, 0) == nullptr);
    CHECK(artist::keyForMove(layer, 12) == nullptr);

    Frame* key = artist::keyForMove(layer, 7);
    CHECK(key && key->index == 7 && key->duration == 5 && key->elements.size() == 1);
    CHECK(first->duration == 7 && first->tweenType == TweenType::Motion);
    CHECK(artist::easeOf(layer, 3) == "even");

    // A shape on a keyframe is not tweenable as it is.
    key->elements.push_back(new Shape(key));
    CHECK(!artist::tweenable(*key));
}

TEST(artist_eases_are_named_and_jump_takes_the_tween_away)
{
    Layer layer(nullptr);
    timeline::insertFrames(layer, 11, 1);
    timeline::insertKeyframe(layer, 6, false);
    Frame* first = layer.frames[0];
    CHECK(artist::easeOf(layer, 2) == "jump");

    CHECK(artist::setEase(layer, 2, "in"));
    CHECK(first->tweenType == TweenType::Motion && first->easeMethod == "quadIn");
    CHECK(artist::easeOf(layer, 2) == "in");
    CHECK(!artist::setEase(layer, 2, "in"));

    CHECK(artist::setEase(layer, 2, "both"));
    CHECK(first->easeMethod == "quadInOut" && artist::easeOf(layer, 2) == "both");
    bool named = false;
    for (const auto& attribute : first->extraAttributes)
        named = named || (attribute.first == "easeMethodName" && attribute.second == "quadInOut");
    CHECK(named);

    CHECK(artist::setEase(layer, 2, "even"));
    CHECK(first->easeMethod.empty() && artist::easeOf(layer, 2) == "even");

    CHECK(artist::setEase(layer, 2, "jump"));
    CHECK(first->tweenType == TweenType::None && artist::easeOf(layer, 2) == "jump");
    CHECK(!artist::setEase(layer, 2, "jump"));

    CHECK(artist::setEase(layer, 2, "out"));
    CHECK(first->tweenType == TweenType::Motion && artist::easeOf(layer, 2) == "out");

    // The Ease value alone names an ease too.
    first->easeMethod.clear();
    first->acceleration = -50;
    CHECK(artist::easeOf(layer, 2) == "in");

    // The last span has nothing to tween to, and an unknown name is refused.
    CHECK(!artist::setEase(layer, 8, "even"));
    CHECK(artist::easeOf(layer, 8) == "jump");
    CHECK(!artist::setEase(layer, 2, "bouncy"));
}
