#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/play/movie_runtime.h"

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
    CHECK(hatClip->x && *hatClip->x == 20.0);
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
