#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/data/shape.h"
#include "../src/play/display/display_object.h"
#include "../src/play/display/stage.h"
#include "../src/play/movie_runtime.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

/// A movie built in memory: a root timeline, and symbols placed on it.
struct Movie
{
    std::unique_ptr<fla::Timeline> root{new fla::Timeline(nullptr)};
    std::vector<std::unique_ptr<fla::Symbol>> symbols;

    fla::Layer* layer(fla::Timeline& timeline, const std::string& name)
    {
        fla::Layer* made = new fla::Layer(&timeline);
        made->name = name;
        timeline.layers.push_back(made);
        return made;
    }

    fla::Frame* frame(fla::Layer& on, int index, int duration, const std::string& script = std::string())
    {
        fla::Frame* made = new fla::Frame(&on);
        made->index = index;
        made->duration = duration;
        if (!script.empty())
        {
            made->actionScript = new fla::ActionScript(made);
            made->actionScript->code = script;
        }
        on.frames.push_back(made);
        return made;
    }

    /// A symbol of \a frames frames, one empty layer.
    fla::Symbol* symbol(const std::string& name, fla::SymbolType type, int frames)
    {
        auto made = std::make_unique<fla::Symbol>(nullptr);
        made->name = name;
        made->symbolType = type;
        fla::Timeline* timeline = new fla::Timeline(made.get());
        timeline->name = name;
        made->timelines.push_back(timeline);
        fla::Layer* content = layer(*timeline, "Layer 1");
        for (int i = 0; i < frames; ++i)
            frame(*content, i, 1);
        symbols.push_back(std::move(made));
        return symbols.back().get();
    }

    fla::SymbolInstance* place(fla::Frame& on, fla::Symbol& symbol, const std::string& name, const std::string& script = {})
    {
        fla::SymbolInstance* made = new fla::SymbolInstance(&on);
        made->symbol = &symbol;
        made->libraryItemName = symbol.name;
        made->symbolType = symbol.symbolType;
        made->name = name;
        if (!script.empty())
            made->extraChildren.push_back("<Actionscript><script><![CDATA[" + script + "]]></script></Actionscript>");
        on.elements.push_back(made);
        return made;
    }
};

} // namespace

TEST(movie_runtime_runs_frame_scripts_and_plays_on)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1, "count = 1;");
    movie.frame(*actions, 1, 1, "count = count + 1;\ntrace('second', count)");
    movie.frame(*actions, 2, 1, "stop();");
    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    std::vector<std::string> traced;
    std::vector<std::string> errors;
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    CHECK(runtime.root().frame == 0);
    runtime.step();
    CHECK(runtime.root().frame == 1);
    CHECK(traced.size() == 1 && traced[0] == "second 2");
    runtime.step();
    runtime.step();
    // Stopped on the third frame.
    CHECK(runtime.root().frame == 2 && !runtime.root().playing);
    CHECK(errors.empty());
}

TEST(movie_runtime_runs_a_dress_up_game)
{
    // As Freya Fairy does it: clothes as named movie clips that a frame
    // script hides, and buttons whose release handlers show them again.
    Movie movie;
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 3);
    fla::Symbol* button = movie.symbol("Button", fla::SymbolType::Button, 4);
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1, "ifFrameLoaded (2) {\n\tgotoAndPlay(2);\n}\n");
    movie.frame(*actions, 1, 1,
        "setProperty(\"hat\", _visible, \"0\");\nhat.gotoAndStop(3);\nstop();\n");
    fla::Layer* clothes = movie.layer(*movie.root, "clothes");
    movie.frame(*clothes, 0, 1);
    fla::Frame* dressed = movie.frame(*clothes, 1, 1);
    movie.place(*dressed, *hat, "hat");
    fla::Layer* buttons = movie.layer(*movie.root, "buttons");
    movie.frame(*buttons, 0, 1);
    fla::Frame* panel = movie.frame(*buttons, 1, 1);
    movie.place(*panel, *button, "",
        "on (release) {\n"
        "\tif (getProperty(\"hat\", _visible) == \"0\") {\n"
        "\t\tsetProperty(\"hat\", _visible, \"1\");\n"
        "\t\tx = getProperty(\"hat\", _x);\n"
        "\t\tsetProperty(\"hat\", _x, x + 20);\n"
        "\t} else {\n"
        "\t\tsetProperty(\"hat\", _visible, \"0\");\n"
        "\t}\n"
        "}\n");

    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    std::vector<std::string> errors;
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    // ifFrameLoaded sent it on to frame 2, whose script hid the hat and
    // sent it to its own third frame, and stopped.
    CHECK(runtime.root().frame == 1 && !runtime.root().playing);
    fla::play::Clip* hatClip = runtime.root().named("hat");
    CHECK(hatClip != nullptr);
    if (!hatClip)
        return;
    CHECK(!hatClip->shown());
    CHECK(hatClip->frame == 2 && !hatClip->playing);

    // The button, found as the stage finds it, clicked.
    fla::play::Clip* buttonClip = runtime.clipOf(runtime.root(), *buttons, *static_cast<fla::SymbolInstance*>(panel->elements[0]));
    CHECK(buttonClip != nullptr && buttonClip->button);
    if (!buttonClip)
        return;
    CHECK(runtime.buttonFrame(*buttonClip) == 0);
    runtime.pointerOver(buttonClip);
    CHECK(runtime.buttonFrame(*buttonClip) == 1);
    runtime.pointerDown();
    CHECK(runtime.buttonFrame(*buttonClip) == 2);
    runtime.pointerUp();
    CHECK(hatClip->shown());
    CHECK(hatClip->scriptedX() && *hatClip->scriptedX() == 20.0);
    // Clicked again, hidden again.
    runtime.pointerDown();
    runtime.pointerUp();
    CHECK(!hatClip->shown());
    // Let go off the button: no release.
    runtime.pointerDown();
    runtime.pointerOver(nullptr);
    runtime.pointerUp();
    CHECK(!hatClip->shown());
    // Playing on: the hat keeps what the scripts set, and the root stays.
    runtime.step();
    CHECK(runtime.root().named("hat") == hatClip && !hatClip->shown());
    CHECK(errors.empty());
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
}

TEST(movie_runtime_reports_script_errors_and_goes_on)
{
    Movie movie;
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1, "nothing();");
    movie.frame(*actions, 1, 1, "stop(;");
    movie.frame(*actions, 2, 1, "trace('third');");
    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    std::vector<std::string> errors;
    std::vector<std::string> traced;
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.start();
    runtime.step();
    runtime.step();
    CHECK(errors.size() == 2);
    CHECK(traced.size() == 1);
}

TEST(movie_runtime_scripts_share_globals_through_global)
{
    // ActionScript 2's _global: what one frame sets there, a later frame
    // reads by its bare name, and _global reads the built-ins too.
    Movie movie;
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1, "_global.outfit = 3;\n_global.pick = function (n) { return n * 2; };");
    movie.frame(*actions, 1, 1, "trace(outfit, pick(outfit), _global.outfit, typeof _global.Math);\nstop();");
    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    std::vector<std::string> traced;
    std::vector<std::string> errors;
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    CHECK(traced.size() == 1 && traced[0] == "3 6 3 object");
    CHECK(errors.empty());
    for (const std::string& line : traced)
        std::printf("    %s\n", line.c_str());
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
}

TEST(movie_runtime_runs_actionscript_1)
{
    // What scripts from old movies use: a variable never set is undefined,
    // the word operators, set and eval by name (slash syntax too), and call
    // running another frame's script.
    Movie movie;
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "if (MouseDown == true) { trace('no'); }\n"
        "trace(never, 'a' add 1, 'x' eq 'x', 1 and not 0);\n"
        "set('/:count', 4);\nset('total', count + 1);\n"
        "trace(_root.count, eval('total'), eval('/:count'));\n"
        "call('other');\nstop();");
    fla::Frame* other = movie.frame(*actions, 1, 1, "trace('called');");
    other->name = "other";
    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    std::vector<std::string> traced;
    std::vector<std::string> errors;
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    CHECK(traced.size() == 3);
    CHECK(traced.size() > 0 && traced[0] == "undefined a1 true true");
    CHECK(traced.size() > 1 && traced[1] == "4 5 4");
    CHECK(traced.size() > 2 && traced[2] == "called");
    CHECK(errors.empty());
    CHECK(!runtime.root().playing && runtime.root().frame == 0);
    for (const std::string& line : traced)
        std::printf("    %s\n", line.c_str());
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
}

TEST(movie_runtime_scripts_find_clips_by_path_and_dot)
{
    Movie movie;
    fla::Symbol* outer = movie.symbol("Outer", fla::SymbolType::MovieClip, 1);
    fla::Symbol* inner = movie.symbol("Inner", fla::SymbolType::MovieClip, 5);
    movie.place(*outer->timelines[0]->layers[0]->frames[0], *inner, "inner");
    fla::Layer* stage = movie.layer(*movie.root, "stage");
    movie.place(*movie.frame(*stage, 0, 1), *outer, "outer");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "outer.inner.gotoAndStop(4);\n"
        "setProperty(\"/outer/inner\", _alpha, 50);\n"
        "outer._x = 30;\n"
        "trace(getProperty(\"_root.outer.inner\", _currentframe), outer.inner._alpha, outer._x);\n"
        "stop();\n");
    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    std::vector<std::string> traced;
    std::vector<std::string> errors;
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    CHECK(traced.size() == 1 && traced[0] == "4 50 30");
    fla::play::Clip* clip = runtime.root().named("outer");
    CHECK(clip && clip->named("inner") && clip->named("inner")->opacity() == 0.5);
    CHECK(errors.empty());
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
}

TEST(movie_runtime_plays_scenes_in_turn_and_goes_to_them)
{
    Movie movie;
    movie.root->name = "Intro";
    fla::Layer* introLayer = movie.layer(*movie.root, "actions");
    movie.frame(*introLayer, 0, 2);
    fla::Timeline second(nullptr);
    second.name = "Story";
    fla::Layer* storyLayer = movie.layer(second, "actions");
    movie.frame(*storyLayer, 0, 1, "trace('story');");
    movie.frame(*storyLayer, 1, 1)->name = "ending";
    movie.frame(*storyLayer, 2, 1, "stop();");

    std::vector<std::string> traced;
    fla::play::MovieRuntime runtime(std::vector<const fla::Timeline*>{movie.root.get(), &second}, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    CHECK(runtime.sceneCount() == 2 && runtime.scene() == 0);
    runtime.step();
    CHECK(runtime.scene() == 0 && runtime.root().frame == 1);
    // Past the first scene's end, the next scene's first frame and its script.
    runtime.step();
    CHECK(runtime.scene() == 1 && runtime.root().frame == 0 && traced.size() == 1 && traced[0] == "story");
    runtime.step();
    runtime.step();
    CHECK(runtime.scene() == 1 && runtime.root().frame == 2 && !runtime.root().playing);

    // A script that names a scene, or a label in another scene, goes there.
    fla::Layer* buttons = movie.layer(*movie.root, "nav");
    (void)buttons;
    movie.root->layers[0]->frames[0]->actionScript = new fla::ActionScript(movie.root->layers[0]->frames[0]);
    movie.root->layers[0]->frames[0]->actionScript->code = "gotoAndStop('ending');";
    runtime.start();
    CHECK(runtime.scene() == 1 && runtime.root().frame == 1 && !runtime.root().playing);
    movie.root->layers[0]->frames[0]->actionScript->code = "gotoAndPlay('Story', 3);";
    runtime.start();
    CHECK(runtime.scene() == 1 && runtime.root().frame == 2);
}

TEST(movie_runtime_runs_actionscript_3_listeners)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* button = movie.symbol("Button", fla::SymbolType::MovieClip, 2);
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 3);
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* first = movie.frame(*art, 0, 1);
    movie.place(*first, *button, "btn");
    movie.place(*first, *hat, "hat");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "import flash.events.MouseEvent;\n"
        "import flash.events.KeyboardEvent;\n"
        "var clicks:int = 0;\n"
        "function onClick(e:MouseEvent):void {\n"
        "    clicks++;\n"
        "    trace('clicked ' + e.currentTarget.name + ' ' + clicks);\n"
        "    hat.visible = false;\n"
        "    hat.gotoAndStop(3);\n"
        "}\n"
        "btn.addEventListener(MouseEvent.CLICK, onClick);\n"
        "btn.buttonMode = true;\n"
        "stage.addEventListener(KeyboardEvent.KEY_DOWN, function(e:KeyboardEvent):void {\n"
        "    trace('key ' + e.keyCode);\n"
        "    hat.alpha = 0.5;\n"
        "});\n"
        "stop();\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    CHECK(errors.empty());
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    fla::play::Clip* btn = runtime.root().named("btn");
    fla::play::Clip* hatClip = runtime.root().named("hat");
    CHECK(btn && hatClip && btn->wantsMouse() && !hatClip->wantsMouse());
    if (!btn || !hatClip)
        return;

    runtime.pointerOver(btn);
    runtime.pointerDown();
    runtime.pointerUp();
    CHECK(traced.size() == 1 && traced[0] == "clicked btn 1");
    CHECK(!hatClip->shown() && hatClip->frame == 2 && !hatClip->playing);
    // A press that ends elsewhere is no click.
    runtime.pointerDown();
    runtime.pointerOver(nullptr);
    runtime.pointerUp();
    CHECK(traced.size() == 1);

    runtime.keyDown(37, 0);
    runtime.keyUp(37, 0);
    CHECK(traced.size() == 2 && traced[1] == "key 37" && hatClip->opacity() == 0.5);
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
}

TEST(movie_runtime_hears_keys_in_actionscript_2)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* button = movie.symbol("Button", fla::SymbolType::Button, 4);
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* first = movie.frame(*art, 0, 3);
    movie.place(*first, *button, "", "on (keyPress \"<Right>\") {\n    nextFrame();\n}\non (keyPress \"a\") {\n    trace('a');\n}");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 3,
        "stop();\n"
        "var listener = new Object();\n"
        "listener.onKeyDown = function() { trace('down ' + Key.getCode() + ' ' + Key.isDown(Key.RIGHT)); };\n"
        "Key.addListener(listener);\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.keyDown(39, 0);
    CHECK(runtime.root().frame == 1);
    CHECK(traced.size() == 1 && traced[0] == "down 39 true");
    runtime.keyUp(39, 0);
    runtime.keyDown(65, 'a');
    CHECK(traced.size() == 3 && traced[1] == "a" && traced[2] == "down 65 false");
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
}

TEST(movie_runtime_errors_name_their_script_and_line)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* button = movie.symbol("Button", fla::SymbolType::Button, 4);
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* first = movie.frame(*art, 0, 1);
    fla::SymbolInstance* placed = movie.place(*first, *button, "go", "on (release) {\n    trace('ok');\n    missing();\n}");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    fla::Frame* scripted = movie.frame(*actions, 0, 1, "stop();\nvar a = 1;\nnothing();\n");

    std::vector<fla::play::MovieRuntime::ScriptError> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.scriptError = [&errors](const fla::play::MovieRuntime::ScriptError& error) { errors.push_back(error); };
    runtime.start();
    CHECK(errors.size() == 1 && errors[0].source == scripted && errors[0].line == 3);
    fla::play::Clip* go = runtime.root().named("go");
    CHECK(go != nullptr);
    if (!go)
        return;
    runtime.pointerOver(go);
    runtime.pointerDown();
    runtime.pointerUp();
    CHECK(errors.size() == 2 && errors[1].source == placed && errors[1].line == 3);
}

TEST(movie_runtime_sets_text_fields_and_shows_variables)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* first = movie.frame(*art, 0, 1);
    auto* score = new fla::StaticText(first);
    score->textType = fla::StaticText::TextType::Dynamic;
    score->instanceName = "score_txt";
    score->runs.push_back(fla::TextRun());
    score->runs.back().text = "0";
    first->elements.push_back(score);
    auto* lives = new fla::StaticText(first);
    lives->textType = fla::StaticText::TextType::Dynamic;
    lives->extraAttributes.emplace_back("variableName", "lives");
    first->elements.push_back(lives);
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "stop();\ntrace(score_txt.text);\nscore_txt.text = 'Score: ' + (2 + 3);\nvar lives = 3;\n");

    std::vector<std::string> traced;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.start();
    CHECK(traced.size() == 1 && traced[0] == "0");
    CHECK(runtime.textOf(runtime.root(), *score).value_or("") == "Score: 5");
    CHECK(runtime.textOf(runtime.root(), *lives).value_or("") == "3");
}

TEST(movie_runtime_makes_clips_from_the_library)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 2);
    hat->extraAttributes.emplace_back("linkageIdentifier", "hat");
    fla::Symbol* star = movie.symbol("Star", fla::SymbolType::MovieClip, 1);
    star->extraAttributes.emplace_back("linkageClassName", "Star");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "stop();\n"
        "var h = this.attachMovie('hat', 'hat1', 1, {_x: 50});\n"
        "trace(h._name + ' ' + h._x);\n"
        "var s = new Star();\n"
        "addChild(s);\n"
        "s.x = 10;\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.setLibrary({hat, star}, {}, {});
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
    CHECK(traced.size() == 1 && traced[0] == "hat1 50");
    std::vector<fla::play::Clip*> made = runtime.madeChildren(runtime.root());
    CHECK(made.size() == 2);
    if (made.size() != 2)
        return;
    CHECK(made[0]->name == "hat1" && made[0]->symbol == hat && made[1]->symbol == star && made[1]->scriptedX().value_or(0) == 10);
    CHECK(runtime.clipOfMade(*made[0]->made) == made[0]);
    // They stay, and play, as the frames go on.
    runtime.step();
    made = runtime.madeChildren(runtime.root());
    CHECK(made.size() == 2 && made[0]->frame == 1);
}

TEST(movie_runtime_drags_and_finds_the_drop_target)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* piece = movie.symbol("Piece", fla::SymbolType::MovieClip, 1);
    fla::Symbol* bin = movie.symbol("Bin", fla::SymbolType::MovieClip, 1);
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* first = movie.frame(*art, 0, 1);
    movie.place(*first, *piece, "piece")->transform.tx = 100;
    movie.place(*first, *bin, "bin");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "stop();\n"
        "piece.startDrag();\n"
        "var keys = new Object();\n"
        "keys.onKeyDown = function() {\n"
        "    stopDrag();\n"
        "    trace(piece._droptarget + ' ' + piece.hitTest(bin) + ' ' + bin.hitTest(205, 210, true));\n"
        "};\n"
        "Key.addListener(keys);\n");

    std::vector<std::string> traced;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    // Where the drawing would have put them: the bin from (200, 200) to
    // (300, 300) on the stage.
    runtime.pointerAt(0, 0);
    runtime.start();
    fla::play::Clip* pieceClip = runtime.root().named("piece");
    fla::play::Clip* binClip = runtime.root().named("bin");
    CHECK(pieceClip && binClip);
    if (!pieceClip || !binClip)
        return;
    binClip->placement.known = true;
    binClip->placement.left = binClip->placement.top = 200;
    binClip->placement.right = binClip->placement.bottom = 300;
    runtime.pointerAt(150, 210);
    CHECK(runtime.dragging() && pieceClip->scriptedX().value_or(0) == 250 && pieceClip->scriptedY().value_or(0) == 210);
    pieceClip->placement.known = true;
    pieceClip->placement.left = 250;
    pieceClip->placement.top = 210;
    pieceClip->placement.right = 260;
    pieceClip->placement.bottom = 220;
    runtime.pointerAt(250, 250);
    runtime.keyDown(32, ' ');
    CHECK(!runtime.dragging());
    CHECK(traced.size() == 1 && traced[0] == "/bin true true");
}

TEST(movie_runtime_plays_library_sounds)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "stop();\nvar s = new Sound();\ns.attachSound('ding');\ns.setVolume(50);\ns.start(1.5, 2);\n"
        "var chime = new Chime();\nvar channel = chime.play();\nchannel.stop();\n");

    std::vector<std::string> played;
    std::vector<int> stopped;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.setLibrary({}, {"ding"}, {"Chime"});
    runtime.playSound = [&played](const std::string& linkage, double offset, int loops, double volume) {
        played.push_back(linkage + " " + std::to_string(static_cast<int>(offset * 10)) + " " + std::to_string(loops) + " " +
            std::to_string(static_cast<int>(volume * 100)));
        return static_cast<int>(played.size());
    };
    runtime.stopSound = [&stopped](int channel) { stopped.push_back(channel); };
    runtime.start();
    CHECK(played.size() == 2 && played[0] == "ding 15 2 50" && played[1] == "Chime 0 1 100");
    CHECK(stopped.size() == 1 && stopped[0] == 2);
}

TEST(movie_runtime_types_into_input_fields)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* first = movie.frame(*art, 0, 1);
    auto* name = new fla::StaticText(first);
    name->textType = fla::StaticText::TextType::Input;
    name->instanceName = "name_txt";
    first->elements.push_back(name);
    auto* answer = new fla::StaticText(first);
    answer->textType = fla::StaticText::TextType::Input;
    answer->extraAttributes.emplace_back("variableName", "answer");
    first->elements.push_back(answer);
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1, "stop();\nSelection.setFocus('name_txt');\n");

    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.start();
    CHECK(runtime.focused(runtime.root(), *name));
    runtime.keyDown('H', 'H');
    runtime.keyDown('I', 'i');
    runtime.keyDown('X', 'x');
    runtime.keyDown(8, 0);
    CHECK(runtime.textOf(runtime.root(), *name).value_or("") == "Hi");
    // A click on the other field moves the typing there, into its variable.
    runtime.focusText(&runtime.root(), answer);
    runtime.keyDown('4', '4');
    runtime.keyDown('2', '2');
    CHECK(runtime.textOf(runtime.root(), *answer).value_or("") == "42");
    CHECK(runtime.textOf(runtime.root(), *name).value_or("") == "Hi");
}

TEST(movie_runtime_tells_of_frame_sounds)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Layer* sound = movie.layer(*movie.root, "sound");
    movie.frame(*sound, 0, 2)->extraAttributes.emplace_back("soundName", "ding.wav");
    movie.frame(*sound, 2, 1);
    std::vector<std::string> heard;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.frameSound = [&heard](const fla::Frame& frame) { heard.push_back(std::to_string(frame.index)); };
    runtime.start();
    runtime.step();
    runtime.step();
    // Once on its keyframe, not again on the frame after; again as it loops.
    CHECK(heard.size() == 1 && heard[0] == "0");
    runtime.step();
    CHECK(heard.size() == 2);
}

TEST(movie_runtime_plays_button_state_sounds)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* button = movie.symbol("Button", fla::SymbolType::Button, 4);
    fla::Layer* states = button->timelines[0]->layers[0];
    const char* names[4] = {"up.wav", "over.wav", "down.wav", "hit.wav"};
    for (int i = 0; i < 4; ++i)
        states->frames[static_cast<size_t>(i)]->extraAttributes.emplace_back("soundName", names[i]);
    fla::Layer* layer = movie.layer(*movie.root, "buttons");
    fla::Frame* frame = movie.frame(*layer, 0, 1, "stop();");
    movie.place(*frame, *button, "");
    std::vector<int> heard;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.frameSound = [&heard](const fla::Frame& f) { heard.push_back(f.index); };
    runtime.start();
    // Placing the button plays nothing.
    CHECK(heard.empty());
    fla::play::Clip* clip = runtime.clipOf(runtime.root(), *layer, *static_cast<fla::SymbolInstance*>(frame->elements[0]));
    CHECK(clip != nullptr);
    if (!clip)
        return;
    runtime.pointerOver(clip);
    runtime.pointerDown();
    runtime.pointerUp();
    runtime.pointerOver(nullptr);
    // Over, Down, Hit (released on it), then Up on rolling out.
    CHECK(heard == (std::vector<int>{1, 2, 3, 0}));
    // Pressed and dragged off: no Up sound and no Hit sound.
    heard.clear();
    runtime.pointerOver(clip);
    runtime.pointerDown();
    runtime.pointerOver(nullptr);
    runtime.pointerUp();
    CHECK(heard == (std::vector<int>{1, 2}));
}

TEST(movie_runtime_runs_a_document_class_and_symbol_classes)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* button = movie.symbol("Button", fla::SymbolType::MovieClip, 1);
    fla::Symbol* star = movie.symbol("Star", fla::SymbolType::MovieClip, 1);
    star->extraAttributes.emplace_back("linkageClassName", "Star");
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 1), *button, "btn");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1, "trace('frame 1, clicks ' + clicks);\n");

    std::map<std::string, std::string> classes;
    classes["Main"] =
        "package {\n"
        "    import flash.display.MovieClip;\n"
        "    import flash.events.MouseEvent;\n"
        "    public class Main extends MovieClip {\n"
        "        public var clicks:int = 0;\n"
        "        public function Main() {\n"
        "            stop();\n"
        "            btn.addEventListener(MouseEvent.CLICK, onClick);\n"
        "            var made = new Star();\n"
        "            addChild(made);\n"
        "            trace('main ' + made.rotation);\n"
        "        }\n"
        "        private function onClick(event:MouseEvent):void {\n"
        "            clicks++;\n"
        "            trace('clicked ' + clicks);\n"
        "        }\n"
        "    }\n"
        "}\n";
    classes["Star"] =
        "package {\n"
        "    import flash.display.MovieClip;\n"
        "    public class Star extends MovieClip {\n"
        "        public function Star() {\n"
        "            rotation = 45;\n"
        "        }\n"
        "    }\n"
        "}\n";

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.setLibrary({button, star}, {}, {});
    runtime.setClasses(classes, "Main");
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
    // The constructor first, then the first frame's script, which sees its field.
    CHECK(traced.size() == 2 && traced[0] == "main 45" && traced[1] == "frame 1, clicks 0");
    CHECK(!runtime.root().playing && runtime.madeChildren(runtime.root()).size() == 1);
    fla::play::Clip* btn = runtime.root().named("btn");
    CHECK(btn != nullptr);
    if (!btn)
        return;
    runtime.pointerOver(btn);
    runtime.pointerDown();
    runtime.pointerUp();
    CHECK(traced.size() == 3 && traced[2] == "clicked 1" && errors.empty());
}

TEST(movie_runtime_finds_class_files_beside_the_document)
{
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "phoenix_class_files";
    fs::remove_all(folder);
    fs::create_directories(folder / "src" / "game");
    std::ofstream(folder / "Main.as") << "package { public class Main {} }";
    std::ofstream(folder / "src" / "game" / "Star.as") << "package game { public class Star {} }";

    fla::Document document(nullptr);
    const std::string settings =
        "<flash_profiles><flash_profile><PublishFlashProperties><DocumentClass>Main</DocumentClass>"
        "<AS3PackagePaths>.;src</AS3PackagePaths></PublishFlashProperties></flash_profile></flash_profiles>";
    document.extraFiles.emplace_back("PublishSettings.xml", fla::LazyBytes(std::vector<uint8_t>(settings.begin(), settings.end())));
    document.symbolList = new fla::SymbolList(&document);
    auto* star = new fla::Symbol(document.symbolList);
    star->name = "Star";
    star->extraAttributes.emplace_back("linkageClassName", "game.Star");
    document.symbolList->symbols.push_back(star);

    std::string documentClass;
    const std::map<std::string, std::string> classes = fla::play::classFiles(document, folder.string(), documentClass);
    CHECK(documentClass == "Main");
    CHECK(classes.size() == 2 && classes.count("Main") && classes.count("Star"));
    CHECK(classes.count("Star") && classes.at("Star").find("package game") != std::string::npos);
    fs::remove_all(folder);
}

namespace {

/// Gives frame \a index of \a symbol's first layer a script.
void symbolScript(fla::Symbol& symbol, int index, const std::string& code)
{
    fla::Frame* frame = symbol.timelines[0]->layers[0]->frames[static_cast<size_t>(index)];
    frame->actionScript = new fla::ActionScript(frame);
    frame->actionScript->code = code;
}

} // namespace

TEST(movie_runtime_objects_of_freed_clips_do_nothing)
{
    // A script keeps a clip and its methods past the frame that drops it.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 2);
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 1), *hat, "hat");
    movie.frame(*art, 1, 2);
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "var keep = hat;\n"
        "var go = hat.gotoAndStop;\n"
        "var listen = hat.addEventListener;\n"
        "var hit = hat.hitTest;\n");
    movie.frame(*actions, 1, 1);
    movie.frame(*actions, 2, 1,
        "stop();\n"
        "trace(keep._x);\n"
        "go(2);\n"
        "listen('enterFrame', function() {});\n"
        "trace(hit(0, 0));\n"
        "keep.score = 3;\n"
        "trace(keep.score);\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    CHECK(!runtime.root().named("hat"));
    runtime.step();
    runtime.step();
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
    CHECK(traced.size() == 3 && traced[0] == "undefined" && traced[1] == "false" && traced[2] == "3");
}

TEST(movie_runtime_passes_over_clips_a_frame_took_off)
{
    // The root is sent where the clip is not, then the clip is sent on: the
    // clip is gone by its turn, and its frame's script does not run.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* kid = movie.symbol("Kid", fla::SymbolType::MovieClip, 2);
    symbolScript(*kid, 0, "stop();");
    symbolScript(*kid, 1, "trace('kid two');");
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 2), *kid, "kid");
    movie.frame(*art, 2, 1);
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1);
    movie.frame(*actions, 1, 1, "stop();\ngotoAndStop(3);\nkid.gotoAndStop(2);\n");
    movie.frame(*actions, 2, 1);

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    CHECK(runtime.root().frame == 2 && !runtime.root().named("kid"));
    CHECK(traced.empty() && errors.empty());
}

TEST(movie_runtime_children_made_and_taken_off_as_clips_play)
{
    // Each clip's frame script adds to its parent while the parent's
    // children play on, and the next time round replaces what it added.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 1);
    hat->extraAttributes.emplace_back("linkageIdentifier", "hat");
    fla::Symbol* spawner = movie.symbol("Spawner", fla::SymbolType::MovieClip, 2);
    symbolScript(*spawner, 1, "_parent.attachMovie('hat', 'from_' + _name, 1);");
    fla::Layer* art = movie.layer(*movie.root, "art");
    fla::Frame* frame = movie.frame(*art, 0, 1);
    for (int i = 0; i < 20; ++i)
        movie.place(*frame, *spawner, "s" + std::to_string(i));

    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.setLibrary({hat}, {}, {});
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    CHECK(runtime.madeChildren(runtime.root()).size() == 20);
    runtime.step();
    runtime.step();
    CHECK(runtime.madeChildren(runtime.root()).size() == 20);
    CHECK(runtime.root().children.size() == 40);
    CHECK(errors.empty());
}

TEST(movie_runtime_add_child_refuses_a_cycle)
{
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* star = movie.symbol("Star", fla::SymbolType::MovieClip, 1);
    star->extraAttributes.emplace_back("linkageClassName", "Star");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "stop();\n"
        "var a = new Star();\n"
        "var b = new Star();\n"
        "addChild(a);\n"
        "a.addChild(b);\n"
        "b.addChild(a);\n"
        "a.addChild(a);\n"
        "trace(a.parent == this, b.parent == a);\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.setLibrary({star}, {}, {});
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
    CHECK(traced.size() == 1 && traced[0] == "true true");
    const std::vector<fla::play::Clip*> made = runtime.madeChildren(runtime.root());
    CHECK(made.size() == 1 && made[0]->children.size() == 1 && made[0]->children[0]->children.empty());
    runtime.step();
}

TEST(movie_runtime_clip_event_gathering_outlives_its_clip)
{
    // __onClipEvent stays about after the clip whose handlers it gathered
    // is freed: called again it changes nothing.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* mc = movie.symbol("Mc", fla::SymbolType::MovieClip, 1);
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 1), *mc, "mc", "onClipEvent (load) {\n\ttrace('loaded');\n}\n");
    movie.frame(*art, 1, 2);
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 2);
    movie.frame(*actions, 2, 1, "stop();\n__onClipEvent(['load'], function() {});\ntrace('ok');\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    runtime.step();
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
    CHECK(traced.size() == 2 && traced[0] == "loaded" && traced[1] == "ok");
}

TEST(movie_runtime_runs_a_clips_on_handlers_on_the_clip)
{
    // A movie clip's on() and onClipEvent() handlers run on the clip, as a
    // player runs a clip's actions: its names, its timeline, this.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* mc = movie.symbol("Mc", fla::SymbolType::MovieClip, 3);
    fla::Layer* art = movie.layer(*movie.root, "art");
    movie.place(*movie.frame(*art, 0, 2), *mc, "mc",
        "onClipEvent (load) {\n\tsize = 5;\n\tstop();\n}\n"
        "on (press) {\n\ttrace('pressed ' + _name + ' ' + size + ' ' + (this == _root.mc));\n\tgotoAndStop(3);\n}\n");
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1);
    movie.frame(*actions, 1, 1, "stop();\ntrace(mc.size + ' ' + _root.size + ' ' + mc._currentframe);\n");

    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    runtime.step();
    fla::play::Clip* clip = runtime.root().child("mc");
    CHECK(clip != nullptr && clip->wantsMouse());
    if (clip)
    {
        runtime.pointerOver(clip);
        runtime.pointerDown();
        runtime.pointerUp();
        CHECK(clip->frame == 2);
    }
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
    CHECK(traced.size() == 2 && traced[0] == "5 undefined 1" && traced[1] == "pressed mc 5 true");
}

TEST(movie_runtime_holds_a_clip_where_its_time_remap_says)
{
    // A clip of ten frames on a scene of ten, held on the scene's frames
    // run backwards rather than playing on by itself.
    Movie movie;
    fla::Symbol* wheel = movie.symbol("Wheel", fla::SymbolType::MovieClip, 10);
    fla::Layer* layer = movie.layer(*movie.root, "art");
    fla::Frame* frame = movie.frame(*layer, 0, 10);
    movie.place(*frame, *wheel, "wheel");
    fla::play::MovieRuntime runtime(*movie.root, 24.0);
    runtime.heldFrame = [](const fla::play::Clip& clip) {
        return clip.symbol && clip.symbol->name == "Wheel" ? 9 - clip.parent->frame : -1;
    };
    runtime.start();
    fla::play::Clip* clip = runtime.root().named("wheel");
    CHECK(clip != nullptr);
    if (!clip)
        return;
    for (int step = 1; step <= 4; ++step)
    {
        runtime.step();
        CHECK(runtime.root().frame == step && clip->frame == 9 - step);
    }
    // Let go, it plays on from there.
    runtime.heldFrame = nullptr;
    runtime.step();
    CHECK(clip->frame == 6);
}

TEST(movie_runtime_puts_timeline_and_script_children_on_the_display_list)
{
    // A shape on the bottom layer for two frames, a clip over it on the
    // first only, and a sprite a script adds.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Symbol* hat = movie.symbol("Hat", fla::SymbolType::MovieClip, 1);
    fla::Layer* front = movie.layer(*movie.root, "front");
    fla::Layer* back = movie.layer(*movie.root, "back");
    movie.place(*movie.frame(*front, 0, 1), *hat, "hat");
    movie.frame(*front, 1, 1, "trace('frame 2 script');");
    fla::Frame* backFrame = movie.frame(*back, 0, 2);
    backFrame->elements.push_back(new fla::Shape(backFrame));

    std::vector<std::string> heard;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.trace = [&heard](const std::string& line) { heard.push_back(line); };
    runtime.start();
    auto* root = static_cast<fla::display::DisplayObjectContainer*>(runtime.root().display.get());
    CHECK(root && root->stage() == &runtime.stage());
    CHECK(root && root->numChildren() == 2);
    if (!root || root->numChildren() != 2)
        return;
    const std::shared_ptr<fla::display::DisplayObject> art = root->getChildAt(0);
    CHECK(art->kind() == fla::display::ObjectKind::TimelineArt);
    fla::play::Clip* hatClip = runtime.root().named("hat");
    CHECK(hatClip && root->getChildAt(1) == hatClip->display && hatClip->display->name() == "hat");
    if (!hatClip)
        return;
    CHECK(root->getChildByName("hat") == hatClip->display);

    // A script's child goes over the timeline's.
    auto sprite = std::make_shared<fla::display::Sprite>();
    root->addChild(sprite);
    CHECK(root->getChildIndex(sprite.get()) == 2);
    int removed = 0;
    hatClip->display->addEventListener(fla::display::events::kRemovedFromStage,
        [&removed](fla::display::Event&) { ++removed; });
    runtime.stage().addEventListener(fla::display::events::kEnterFrame,
        [&heard](fla::display::Event&) { heard.push_back("enterFrame"); });

    // The next frame drops the clip; enterFrame comes before its script.
    runtime.step();
    CHECK(removed == 1);
    CHECK(root->numChildren() == 2 && root->getChildAt(0) == art && root->getChildAt(1) == sprite);
    CHECK(heard.size() == 2 && heard[0] == "enterFrame" && heard[1] == "frame 2 script");

    // Taken off by a script, the shape stays off while its keyframe lasts;
    // the clip, placed again, goes on top.
    root->removeChild(art.get());
    runtime.step();
    CHECK(root->numChildren() == 2 && root->getChildAt(0) == sprite);
    fla::play::Clip* again = runtime.root().named("hat");
    CHECK(again && root->getChildAt(1) == again->display);
    // What a script sets is kept over where the timeline places it.
    if (again)
    {
        again->display->setX(42);
        CHECK(again->transform(fla::Transform()).tx == 42.0);
        CHECK(again->scriptedX().value_or(0) == 42.0);
    }
}

TEST(movie_runtime_creates_empty_clips_that_hear_each_frame)
{
    // createEmptyMovieClip: a clip with nothing in it, whose onEnterFrame
    // runs each frame however the timeline it is on goes.
    Movie movie;
    movie.root->name = "Scene 1";
    fla::Layer* actions = movie.layer(*movie.root, "actions");
    movie.frame(*actions, 0, 1,
        "var ticks = 0;\n"
        "var made = this.createEmptyMovieClip(\"ticker\", 1000);\n"
        "trace(made._name + ' ' + (ticker == made));\n"
        "ticker.onEnterFrame = function () { ticks++; trace('tick ' + ticks); };\n"
        "stop();\n");
    std::vector<std::string> traced;
    std::vector<std::string> errors;
    fla::play::MovieRuntime runtime(*movie.root, 12.0);
    runtime.setActionScriptVersion(2);
    runtime.trace = [&traced](const std::string& line) { traced.push_back(line); };
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.start();
    for (int i = 0; i < 5; ++i)
        runtime.step();
    CHECK(traced.size() == 6 && traced[0] == "ticker true" && traced.back() == "tick 5");
    CHECK(runtime.root().named("ticker") != nullptr);
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
    CHECK(errors.empty());
}
