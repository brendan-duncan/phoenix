#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/frame_actions.h"

using fla::frameactions::Action;
using fla::frameactions::Call;
using fla::frameactions::parse;

namespace {

/// A keyframe at \a index lasting \a duration, with a script and a label
/// when given.
fla::Frame* addFrame(fla::Layer& layer, int index, int duration, const std::string& script,
    const std::string& label = std::string(), const std::string& labelType = std::string())
{
    fla::Frame* frame = new fla::Frame(&layer);
    frame->index = index;
    frame->duration = duration;
    frame->name = label;
    frame->labelType = labelType;
    if (!script.empty())
    {
        frame->actionScript = new fla::ActionScript(frame);
        frame->actionScript->code = script;
    }
    layer.frames.push_back(frame);
    return frame;
}

fla::Layer* addLayer(fla::Timeline& timeline, fla::Layer::Type type = fla::Layer::Type::Normal)
{
    fla::Layer* layer = new fla::Layer(&timeline);
    layer->layerType = type;
    timeline.layers.push_back(layer);
    return layer;
}

} // namespace

TEST(frame_actions_parse_the_simple_calls)
{
    std::vector<Action> actions = parse("stop();");
    CHECK(actions.size() == 1 && actions[0].call == Call::Stop);
    actions = parse("this.play()");
    CHECK(actions.size() == 1 && actions[0].call == Call::Play);
    actions = parse("gotoAndPlay(5);\n");
    CHECK(actions.size() == 1 && actions[0].call == Call::GotoAndPlay && actions[0].frame == 4 && actions[0].label.empty());
    actions = parse("this.gotoAndStop(\"walk\");");
    CHECK(actions.size() == 1 && actions[0].call == Call::GotoAndStop && actions[0].frame == -1 && actions[0].label == "walk");
    actions = parse("gotoAndStop('idle')");
    CHECK(actions.size() == 1 && actions[0].label == "idle");
    actions = parse("nextFrame(); prevFrame();");
    CHECK(actions.size() == 2 && actions[0].call == Call::NextFrame && actions[1].call == Call::PrevFrame);
    // Statements on their own lines, without semicolons.
    actions = parse("stop()\nplay()\n");
    CHECK(actions.size() == 2 && actions[0].call == Call::Stop && actions[1].call == Call::Play);
    CHECK(parse("").empty());
    CHECK(parse("var x = 3;\ntrace(x);").empty());
}

TEST(frame_actions_skip_comments_and_strings)
{
    CHECK(parse("// stop();").empty());
    CHECK(parse("/* stop();\n gotoAndPlay(3); */").empty());
    CHECK(parse("trace(\"stop();\");").empty());
    std::vector<Action> actions = parse("/* hold */ stop(); // here\n");
    CHECK(actions.size() == 1 && actions[0].call == Call::Stop);
    // A label with an escaped quote.
    actions = parse("gotoAndPlay(\"a\\\"b\");");
    CHECK(actions.size() == 1 && actions[0].label == "a\"b");
}

TEST(frame_actions_take_only_top_level_statements)
{
    // In a block, a function or after a condition: left out.
    CHECK(parse("if (done) { stop(); }").empty());
    CHECK(parse("if (done) stop();").empty());
    CHECK(parse("if (a) play(); else stop();").empty());
    CHECK(parse("function f() { stop(); }").empty());
    CHECK(parse("addEventListener(\"click\", function(e) { gotoAndPlay(2); });").empty());
    CHECK(parse("while (x) stop();").empty());
    CHECK(parse("for (var i = 0; i < 3; i++) stop();").empty());
    // After the condition's statement or block, a call counts again.
    std::vector<Action> actions = parse("if (a) { play(); }\nstop();");
    CHECK(actions.size() == 1 && actions[0].call == Call::Stop);
    actions = parse("if (a) trace(1); stop();");
    CHECK(actions.size() == 1 && actions[0].call == Call::Stop);
    // Another timeline's calls, or a call within an expression: left out.
    CHECK(parse("clip.stop();").empty());
    CHECK(parse("parent.gotoAndPlay(3);").empty());
    CHECK(parse("MovieClip(root).stop();").empty());
    CHECK(parse("this.clip.stop();").empty());
    CHECK(parse("x = stop();").empty());
    CHECK(parse("stop() || play();").empty());
    CHECK(parse("stopAll();").empty());
    // Targets it cannot be sure of: an expression, a scene, frame 0.
    CHECK(parse("gotoAndPlay(currentFrame + 1);").empty());
    CHECK(parse("gotoAndPlay(n);").empty());
    CHECK(parse("gotoAndPlay(1, \"Scene 2\");").empty());
    CHECK(parse("gotoAndStop(0);").empty());
    CHECK(parse("gotoAndStop();").empty());
    CHECK(parse("stop(1);").empty());
}

TEST(frame_actions_follow_stops_gotos_and_labels)
{
    fla::Timeline timeline(nullptr);
    fla::Layer* art = addLayer(timeline);
    addFrame(*art, 0, 20, std::string());
    fla::Layer* labels = addLayer(timeline);
    addFrame(*labels, 0, 5, std::string(), "intro");
    addFrame(*labels, 5, 5, std::string(), "loop");
    addFrame(*labels, 10, 10, std::string(), "note", "comment");
    fla::Layer* actions = addLayer(timeline);
    addFrame(*actions, 0, 4, std::string());
    addFrame(*actions, 4, 5, "stop();");
    addFrame(*actions, 9, 3, "gotoAndPlay(\"loop\");");
    addFrame(*actions, 12, 2, "gotoAndStop(3);");
    addFrame(*actions, 14, 1, "gotoAndPlay(\"note\");");
    addFrame(*actions, 15, 1, "gotoAndStop(99);");
    // On a guide layer: not part of the movie.
    fla::Layer* guide = addLayer(timeline, fla::Layer::Type::Guide);
    addFrame(*guide, 0, 1, "stop();");

    const fla::frameactions::TimelineActions frames(timeline);
    CHECK(!frames.empty());
    CHECK(frames.length() == 20);
    CHECK(frames.labelFrame("loop") == 5);
    CHECK(frames.labelFrame("note") == -1);

    fla::frameactions::State state = frames.enter(0);
    CHECK(state.frame == 0 && state.playing);
    state = frames.enter(2);
    CHECK(state.frame == 2 && state.playing);
    state = frames.enter(4);
    CHECK(state.frame == 4 && !state.playing);
    state = frames.enter(9);
    CHECK(state.frame == 5 && state.playing);
    state = frames.enter(12);
    CHECK(state.frame == 2 && !state.playing);
    // A comment is not a label to go to: playback carries on.
    state = frames.enter(14);
    CHECK(state.frame == 14 && state.playing);
    // Past the end: the last frame.
    state = frames.enter(15);
    CHECK(state.frame == 19 && !state.playing);

    fla::Timeline empty(nullptr);
    addFrame(*addLayer(empty), 0, 10, "trace(1);");
    CHECK(fla::frameactions::TimelineActions(empty).empty());
}

TEST(frame_actions_chain_gotos_and_stop_the_loop_round)
{
    // Frame 1 goes to 3, whose stop holds it there.
    fla::Timeline timeline(nullptr);
    fla::Layer* layer = addLayer(timeline);
    addFrame(*layer, 0, 1, "gotoAndPlay(3);");
    addFrame(*layer, 1, 1, std::string());
    addFrame(*layer, 2, 1, "stop();");
    fla::frameactions::State state = fla::frameactions::TimelineActions(timeline).enter(0);
    CHECK(state.frame == 2 && !state.playing);

    // Gotos that go round each other end without hanging.
    fla::Timeline round(nullptr);
    fla::Layer* both = addLayer(round);
    addFrame(*both, 0, 1, "gotoAndPlay(2);");
    addFrame(*both, 1, 1, "gotoAndPlay(1);");
    state = fla::frameactions::TimelineActions(round).enter(0);
    CHECK(state.frame == 0 || state.frame == 1);
}

TEST(frame_actions_sequence_a_clip)
{
    // No actions: round and round, as a clip plays.
    fla::Timeline plain(nullptr);
    addFrame(*addLayer(plain), 0, 4, std::string());
    fla::frameactions::TimelineActions::Sequence sequence = fla::frameactions::TimelineActions(plain).sequence();
    CHECK(sequence.frameAt(0) == 0 && sequence.frameAt(3) == 3 && sequence.frameAt(4) == 0 && sequence.frameAt(9) == 1);

    // A stop on frame 1 holds the first frame.
    fla::Timeline held(nullptr);
    fla::Layer* layer = addLayer(held);
    addFrame(*layer, 0, 1, "stop();");
    addFrame(*layer, 1, 5, std::string());
    sequence = fla::frameactions::TimelineActions(held).sequence();
    CHECK(sequence.loopFrom == -1);
    CHECK(sequence.frameAt(0) == 0 && sequence.frameAt(100) == 0);

    // An intro, then a loop between frames 3 and 6.
    fla::Timeline looped(nullptr);
    fla::Layer* frames = addLayer(looped);
    addFrame(*frames, 0, 2, std::string());
    addFrame(*frames, 2, 4, std::string(), "cycle");
    addFrame(*addLayer(looped), 5, 1, "gotoAndPlay(\"cycle\");");
    sequence = fla::frameactions::TimelineActions(looped).sequence();
    // 0 1 2 3 4 (5 goes to 2) 2 3 4 2 ...
    CHECK(sequence.frameAt(0) == 0 && sequence.frameAt(1) == 1 && sequence.frameAt(4) == 4);
    CHECK(sequence.frameAt(5) == 2 && sequence.frameAt(6) == 3 && sequence.frameAt(8) == 2 && sequence.frameAt(1000) == 4);

    // Plays to a stop at the end.
    fla::Timeline once(nullptr);
    fla::Layer* onceLayer = addLayer(once);
    addFrame(*onceLayer, 0, 3, std::string());
    addFrame(*onceLayer, 3, 1, "this.stop();");
    sequence = fla::frameactions::TimelineActions(once).sequence();
    CHECK(sequence.frameAt(2) == 2 && sequence.frameAt(3) == 3 && sequence.frameAt(50) == 3);
}
