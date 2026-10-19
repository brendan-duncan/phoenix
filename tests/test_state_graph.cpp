// Interactive states: the graph kept on a timeline, and the ActionScript it
// is turned into.
#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/state_graph.h"

#include <memory>
#include <string>

namespace {

namespace st = fla::states;

/// A timeline of 30 frames: labels idle (1-10), eat (11-20) and sleep
/// (21-30) on a layer of their own, art under them.
std::unique_ptr<fla::Timeline> timeline()
{
    auto made = std::make_unique<fla::Timeline>(nullptr);
    made->name = "Scene 1";
    fla::Layer* labels = new fla::Layer(made.get());
    labels->name = "labels";
    made->layers.push_back(labels);
    int at = 0;
    for (const char* name : {"idle", "eat", "sleep"})
    {
        fla::Frame* frame = new fla::Frame(labels);
        frame->index = at;
        frame->duration = 10;
        frame->name = name;
        frame->labelType = "name";
        labels->frames.push_back(frame);
        at += 10;
    }
    fla::Layer* art = new fla::Layer(made.get());
    art->name = "art";
    made->layers.push_back(art);
    fla::Frame* frame = new fla::Frame(art);
    frame->duration = 30;
    art->frames.push_back(frame);
    return made;
}

/// The graph the tests turn into scripts.
st::Graph graph()
{
    st::Graph graph;
    graph.states = {{"idle", "", st::Play::Loop, 0, 0}, {"eat", "", st::Play::Once, 200, 0},
        {"sleep", "", st::Play::Hold, 100, 150}};
    graph.inputs = {{"score", st::InputKind::Number, 0}, {"poke", st::InputKind::Trigger, 0},
        {"awake", st::InputKind::Switch, 1}};
    st::Transition click;
    click.from = 0;
    click.to = 1;
    click.when = st::When::Click;
    click.target = "feedBtn";
    st::Transition finished;
    finished.from = 1;
    finished.to = 0;
    finished.when = st::When::Finished;
    st::Transition space;
    space.from = 0;
    space.to = 2;
    space.when = st::When::Key;
    space.target = "Space";
    st::Transition score;
    score.from = 2;
    score.to = 0;
    score.when = st::When::Condition;
    score.condition = {"score", st::Compare::More, 2};
    st::Transition poke;
    poke.from = -1;
    poke.to = 2;
    poke.when = st::When::Condition;
    poke.condition = {"poke", st::Compare::Fired, 0};
    st::Transition timer;
    timer.from = 2;
    timer.to = 1;
    timer.when = st::When::Timer;
    timer.seconds = 2;
    graph.transitions = {click, finished, space, score, poke, timer};
    graph.listeners = {{st::When::Key, "A", "score", st::Change::Add, 1}, {st::When::Key, "P", "poke", st::Change::Fire, 0}};
    return graph;
}

bool has(const std::string& text, const std::string& part)
{
    const bool found = text.find(part) != std::string::npos;
    if (!found)
        std::printf("    not there: %s\n", part.c_str());
    return found;
}

} // namespace

TEST(state_graph_round_trips_through_its_attribute)
{
    const st::Graph made = graph();
    st::Graph read;
    CHECK(st::decode(st::encode(made), read));
    CHECK(read == made);
    CHECK(read.states.size() == 3 && read.states[1].play == st::Play::Once && read.states[2].x == 100.0);
    CHECK(read.inputs.size() == 3 && read.inputs[2].kind == st::InputKind::Switch && read.inputs[2].value == 1.0);
    CHECK(read.transitions.size() == 6 && read.transitions[4].from == -1 &&
        read.transitions[4].condition.compare == st::Compare::Fired && read.transitions[5].seconds == 2.0);
    CHECK(read.listeners.size() == 2 && read.listeners[0].change == st::Change::Add && read.listeners[0].target == "A");
    CHECK(!st::decode("<graph/>", read));

    // Kept on the timeline beside its other attributes, and taken off.
    std::unique_ptr<fla::Timeline> scene = timeline();
    scene->extraAttributes = {{"phoenixSomething", "1"}};
    scene->extraAttributes = st::withGraph(*scene, made);
    CHECK(scene->extraAttributes.size() == 2 && st::of(*scene) == made);
    scene->extraAttributes = st::withGraph(*scene, st::Graph());
    CHECK(scene->extraAttributes.size() == 1 && st::of(*scene).empty());

    // Taking a state out takes its transitions and renumbers the rest.
    st::Graph fewer = made;
    fewer.removeState(0);
    CHECK(fewer.states.size() == 2 && fewer.states[0].label == "eat");
    CHECK(fewer.transitions.size() == 2);
    for (const st::Transition& each : fewer.transitions)
        CHECK(each.to >= 0 && each.to < 2 && each.from < 2);
    fewer.removeInput("score");
    CHECK(fewer.inputs.size() == 2 && fewer.listeners.size() == 1);
}

TEST(state_graph_finds_label_ranges_and_describes_itself)
{
    std::unique_ptr<fla::Timeline> scene = timeline();
    CHECK(st::labels(*scene).size() == 3 && st::length(*scene) == 30);
    int first = 0, last = 0;
    CHECK(st::range(*scene, {"eat", "", st::Play::Once, 0, 0}, first, last) && first == 10 && last == 19);
    CHECK(st::range(*scene, {"sleep", "", st::Play::Once, 0, 0}, first, last) && first == 20 && last == 29);
    CHECK(st::range(*scene, {"idle", "eat", st::Play::Once, 0, 0}, first, last) && first == 0 && last == 19);
    CHECK(!st::range(*scene, {"gone", "", st::Play::Once, 0, 0}, first, last));
    const st::Graph made = graph();
    CHECK(st::describe(made, made.transitions[0]) == "feedBtn is clicked");
    CHECK(st::describe(made, made.transitions[3]) == "score > 2");
    CHECK(st::describe(made, made.transitions[5]) == "after 2 s");
    CHECK(st::describe(made.listeners[0]) == "When A is pressed: add 1 to score");
}

TEST(state_graph_compiles_to_actionscript_3)
{
    std::unique_ptr<fla::Timeline> scene = timeline();
    const std::string code = st::compile(graph(), *scene, 3, 12.0);
    CHECK(code.rfind(st::kBegin, 0) == 0);
    CHECK(has(code, std::string(st::kEnd) + "\n"));
    CHECK(has(code, "function phxGo(state:String):void {"));
    CHECK(has(code, "\t\tgotoAndPlay(\"idle\");"));
    CHECK(has(code, "\t\tgotoAndStop(\"sleep\");\n\t\tphxDone = true;"));
    // The clip's clicks, keys and frames listened to.
    CHECK(has(code, "o = getChildByName(\"feedBtn\");\n\tif (o != null) {\n\t\to.addEventListener(\"click\", phxOn0);"));
    CHECK(has(code, "stage.addEventListener(\"keyDown\", phxKey);"));
    CHECK(has(code, "addEventListener(\"enterFrame\", phxFrame);"));
    // The loop goes round, the one played once stops at its end.
    CHECK(has(code, "if (f < 1 || f > 10 || f < phxAt) {"));
    CHECK(has(code, "if (f == 20) {\n\t\t\tstop();\n\t\t\tphxDone = true;"));
    // Transitions and listeners.
    CHECK(has(code, "if (target == \"feedBtn\" && what == \"click\") {\n\t\t\t\tphxTo(\"eat\", false);"));
    CHECK(has(code, "if (target == \"key\" && what == \"32\") {"));
    CHECK(has(code, "phxInputs[\"score\"] = phxInputs[\"score\"] + 1;"));
    CHECK(has(code, "if (phxInputs[\"score\"] > 2) {"));
    CHECK(has(code, "if (phxTicks >= 24) {"));
    CHECK(has(code, "if (phxInputs[\"poke\"] == true) {\n\t\t\tphxInputs[\"poke\"] = false;"));
    CHECK(has(code, "phxInputs[\"awake\"] = true;"));
    CHECK(has(code, "phxGo(\"idle\");\n}"));
    // Nothing ActionScript 1 and 2 would have.
    CHECK(code.find("phxT.") == std::string::npos);
}

TEST(state_graph_compiles_to_actionscript_2)
{
    std::unique_ptr<fla::Timeline> scene = timeline();
    const std::string code = st::compile(graph(), *scene, 2, 12.0);
    CHECK(has(code, "var phxT = this;\nif (!phxT.phxReady) {"));
    CHECK(has(code, "phxT.gotoAndPlay(\"idle\");"));
    CHECK(has(code, "o = phxT[\"feedBtn\"];\n\t\tif (o != undefined) {\n\t\t\to.onRelease = phxT.phxOn0;"));
    CHECK(has(code, "phxT.createEmptyMovieClip(\"phxTicker\", 1048000);"));
    CHECK(has(code, "Key.addListener(phxKeys);"));
    CHECK(has(code, "var f = phxT._currentframe;"));
    CHECK(has(code, "phxT.phxGo(\"idle\");\n}"));
    // Nothing of ActionScript 3's: no types, no listeners.
    CHECK(code.find(":void") == std::string::npos && code.find("addEventListener") == std::string::npos);
    CHECK(st::compile(st::Graph(), *scene, 2, 12.0).empty());
}

TEST(state_graph_block_keeps_the_scripts_own_lines)
{
    const std::string block = std::string(st::kBegin) + "\nphxGo();\n" + st::kEnd + "\n";
    const std::string own = "// mine\nvar lives = 3;\n";
    const std::string with = st::withBlock(own, block);
    CHECK(with == own + "\n" + block);
    CHECK(st::blockOf(with) == block);
    // Made again: only the block changes.
    const std::string again = std::string(st::kBegin) + "\nphxGo(2);\n" + st::kEnd + "\n";
    const std::string after = st::withBlock(with + "trace(lives);\n", again);
    CHECK(after == own + "\n" + again + "trace(lives);\n");
    // Taken out: the script as it was.
    CHECK(st::withBlock(with, std::string()) == own);
    CHECK(st::withBlock(block, std::string()).empty());
    CHECK(st::withBlock(std::string(), block) == block);
}

TEST(state_graph_apply_is_one_undo_step)
{
    std::unique_ptr<fla::Timeline> scene = timeline();
    fla::Frame* own = scene->layers[0]->frames[0];
    fla::CommandStack stack;
    st::Graph made = graph();
    made.states.push_back({"wave", "", st::Play::Once, 0, 300});
    st::Edit edit;
    edit.newLabels = {{"wave", 30}};
    edit.renames = {{"sleep", "nap"}};
    made.states[2].label = "nap";
    st::apply(stack, *scene, made, edit, 3, 12.0, "Add State");
    CHECK(stack.count() == 1 && stack.nameAt(0) == "Add State");
    fla::Layer* layer = st::layerOf(*scene);
    CHECK(layer != nullptr && scene->layers[0] == layer);
    if (!layer)
        return;
    CHECK(st::of(*scene) == made);
    // The new label at frame 31, the renamed one, the script on frame 1.
    int first = 0, last = 0;
    CHECK(st::range(*scene, made.states[3], first, last) && first == 30);
    CHECK(st::range(*scene, made.states[2], first, last) && first == 20 && last == 29);
    CHECK(layer->frames[0]->actionScript && has(layer->frames[0]->actionScript->code, "gotoAndStop(\"nap\")"));
    CHECK(!st::stale(*scene, 3, 12.0));
    CHECK(st::stale(*scene, 2, 12.0));

    stack.undo();
    CHECK(st::layerOf(*scene) == nullptr && scene->layers.size() == 2);
    CHECK(st::of(*scene).empty() && scene->layers[0]->frames[2]->name == "sleep");
    CHECK(scene->layers[0]->frames[0] == own);
    stack.redo();
    CHECK(st::layerOf(*scene) == layer && st::of(*scene) == made);

    // The user's own script on that frame stays through an edit and the
    // graph's removal.
    layer->frames[0]->actionScript->code = "trace('mine');\n\n" + st::blockOf(layer->frames[0]->actionScript->code);
    st::apply(stack, *scene, st::Graph(), st::Edit(), 3, 12.0, "Delete States");
    CHECK(layer->frames[0]->actionScript && layer->frames[0]->actionScript->code == "trace('mine');\n");
    CHECK(st::of(*scene).empty());
}
