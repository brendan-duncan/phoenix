#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/play/canvas/scripts.h"
#include "../src/play/display/backend.h"
#include "../src/play/display/display_object.h"
#include "../src/play/display/stage.h"
#include "../src/play/display/text_field.h"
#include "../src/play/movie_runtime.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

/// An HTML5 Canvas movie built in memory: a main timeline, and symbols
/// placed on it, their scripts JavaScript.
struct CanvasMovie
{
    std::unique_ptr<fla::Timeline> root{new fla::Timeline(nullptr)};
    std::vector<std::unique_ptr<fla::Symbol>> symbols;

    CanvasMovie() { root->name = "Scene 1"; }

    fla::Layer* layer(fla::Timeline& timeline, const std::string& name)
    {
        fla::Layer* made = new fla::Layer(&timeline);
        made->name = name;
        timeline.layers.push_back(made);
        return made;
    }

    fla::Frame* frame(fla::Layer& on, int index, int duration, const std::string& script = std::string(),
        const std::string& label = std::string())
    {
        fla::Frame* made = new fla::Frame(&on);
        made->index = index;
        made->duration = duration;
        made->name = label;
        if (!script.empty())
        {
            made->actionScript = new fla::ActionScript(made);
            made->actionScript->code = script;
        }
        on.frames.push_back(made);
        return made;
    }

    /// A movie clip of \a frames frames, a shape on each (so the pointer
    /// finds it).
    fla::Symbol* symbol(const std::string& name, int frames)
    {
        auto made = std::make_unique<fla::Symbol>(nullptr);
        made->name = name;
        made->symbolType = fla::SymbolType::MovieClip;
        fla::Timeline* timeline = new fla::Timeline(made.get());
        timeline->name = name;
        made->timelines.push_back(timeline);
        fla::Layer* content = layer(*timeline, "Layer_1");
        for (int i = 0; i < frames; ++i)
            frame(*content, i, 1)->elements.push_back(new fla::Shape(content->frames.back()));
        symbols.push_back(std::move(made));
        return symbols.back().get();
    }

    fla::SymbolInstance* place(fla::Frame& on, fla::Symbol& symbol, const std::string& name, double x = 0.0,
        double y = 0.0)
    {
        fla::SymbolInstance* made = new fla::SymbolInstance(&on);
        made->symbol = &symbol;
        made->libraryItemName = symbol.name;
        made->symbolType = symbol.symbolType;
        made->name = name;
        made->transform.tx = x;
        made->transform.ty = y;
        on.elements.push_back(made);
        return made;
    }
};

/// Timeline art 40 by 40 from its origin, for the pointer to find.
class BoxBackend : public fla::display::Backend
{
public:
    void draw(fla::display::BitmapData&, const fla::display::DisplayObject&, const fla::display::Matrix&,
        const fla::display::ColorTransform*, fla::display::BlendMode, const fla::display::Rect*, bool) override
    {}
    bool applyFilter(fla::display::BitmapData&, const fla::display::BitmapData&, const fla::display::Rect&,
        const fla::display::Point&, const fla::display::BitmapFilter&) override
    {
        return false;
    }
    fla::display::Rect artBounds(const fla::display::TimelineArt&, bool) override { return {0, 0, 40, 40}; }
    bool artHits(const fla::display::TimelineArt&, double x, double y) override
    {
        return x >= 0 && y >= 0 && x < 40 && y < 40;
    }
    std::vector<fla::display::TextLine> layoutText(const fla::display::TextField&) override { return {}; }
    int textIndexAt(const fla::display::TextField&, double, double) override { return 0; }
    fla::display::Rect textCharBox(const fla::display::TextField&, int) override { return {}; }
};

/// The backend for as long as a test needs it.
struct UsingBackend
{
    BoxBackend backend;
    fla::display::Backend* was = fla::display::backend();
    UsingBackend() { fla::display::setBackend(&backend); }
    ~UsingBackend() { fla::display::setBackend(was); }
};

struct Played
{
    fla::play::MovieRuntime runtime;
    std::vector<std::string> traced;
    std::vector<std::string> errors;
    std::vector<fla::play::MovieRuntime::ScriptError> located;

    explicit Played(CanvasMovie& movie)
        : runtime(*movie.root, 24.0)
    {
        std::vector<const fla::Symbol*> library;
        for (const auto& symbol : movie.symbols)
            library.push_back(symbol.get());
        runtime.setLibrary(library, {}, {});
        runtime.setJavaScript(true);
        runtime.setStagePointer(true);
        runtime.trace = [this](const std::string& line) { traced.push_back(line); };
        runtime.error = [this](const std::string& line) { errors.push_back(line); };
        runtime.scriptError = [this](const fla::play::MovieRuntime::ScriptError& error) { located.push_back(error); };
        runtime.start();
    }

    std::string output() const
    {
        std::string all;
        for (const std::string& line : traced)
            all += line + "\n";
        return all;
    }

    void click(double x, double y)
    {
        fla::display::Stage& stage = runtime.stage();
        stage.pointerMove(x, y);
        stage.pointerDown(x, y);
        stage.pointerUp(x, y);
        runtime.afterEvents();
    }

    /// The output is \a expected, and nothing went wrong; what came is
    /// shown when it is not.
    void expect(const std::string& expected) const
    {
        CHECK(errors.empty());
        CHECK(output() == expected);
        if (output() != expected || !errors.empty())
        {
            std::printf("    got:\n%s", output().c_str());
            for (const std::string& error : errors)
                std::printf("    error: %s\n", error.c_str());
        }
    }
};

} // namespace

TEST(canvas_frame_scripts_drive_the_timeline_from_frame_zero)
{
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "console.log('start', this.currentFrame, this.totalFrames, this === exportRoot, this instanceof createjs.MovieClip);\n"
        "var local = 1;\n"
        "this.count = 1;");
    movie.frame(*actions, 1, 1, "this.count++; console.log('second', this.currentFrame, typeof local);");
    movie.frame(*actions, 2, 1, "this.gotoAndStop('end');", "loop");
    movie.frame(*actions, 3, 1, "alert('at ' + this.currentLabel + ' ' + this.currentFrame + ' ' + this.paused);", "end");
    movie.frame(*actions, 4, 1);
    Played played(movie);
    CHECK(played.runtime.javaScript() && !played.runtime.actionScript3());
    for (int i = 0; i < 6; ++i)
        played.runtime.step();
    // Stopped where gotoAndStop sent it: frame 3 counting from 0.
    CHECK(played.runtime.root().frame == 3 && !played.runtime.root().playing);
    played.expect("start 0 5 true true\nsecond 1 undefined\nat end 3 true\n");
}

TEST(canvas_scripts_reach_instances_by_name_and_set_their_properties)
{
    CanvasMovie movie;
    fla::Symbol* ball = movie.symbol("Ball", 3);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var b = this.ball;\n"
        "console.log(b === this.ball, b.parent === this, this.parent === stage, b.stage === stage, b.name);\n"
        "console.log(b.x, b.y, b.currentFrame, b.totalFrames, this.nothing);\n"
        "b.x = 120; b.rotation = 90; b.alpha = 0.5; b.scaleX = 2; b.visible = false;\n"
        "console.log(b.x, b.rotation, b.alpha, b.scaleX, b.visible);\n"
        "b.gotoAndStop(2); console.log(b.currentFrame, b.paused);\n"
        "this.score = 3; console.log(this.score, this.children.length, this.numChildren > 0);\n"
        "var p = b.localToGlobal(0, 0); console.log(p.x, p.y);\n");
    fla::Frame* placed = movie.frame(*art, 0, 1);
    movie.place(*placed, *ball, "ball", 10, 20);
    Played played(movie);
    played.expect("true true true true ball\n10 20 0 3 undefined\n120 90 0.5 2 false\n2 true\n3 1 true\n120 20\n");
    fla::play::Clip* clip = played.runtime.root().named("ball");
    CHECK(clip && clip->frame == 2 && !clip->display->visible());
}

TEST(canvas_listeners_hear_clicks_and_the_stage)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Symbol* button = movie.symbol("Button", 1);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var self = this;\n"
        "this.go.addEventListener('click', function (e) {\n"
        "  console.log('click', e.type, e.target === self.go, e.currentTarget === self.go, e.stageX, e.stageY, e.localX);\n"
        "});\n"
        "this.go.on('mousedown', function (e, data) { console.log('down', this === self, data); }, this, false, 'hat');\n"
        "this.on('click', function (e) { console.log('bubbled', e.target === self.go, e.currentTarget === this); });\n"
        "this.handler = function () { console.log('bound', this === self); }.bind(this);\n"
        "this.go.addEventListener('pressup', this.handler);\n"
        "stage.on('stagemousedown', function (e) { console.log('stage down', e.stageX); });\n"
        "var once = this.go.on('click', function () { console.log('once'); }, null, true);\n");
    movie.place(*movie.frame(*art, 0, 1), *button, "go", 100, 100);
    Played played(movie);
    played.click(110, 120);
    played.click(110, 120);
    // Off the button: only the stage hears it.
    played.click(10, 10);
    // As CreateJS orders them: the stage's first, and a release's pressup
    // after its click.
    played.expect("stage down 110\ndown true hat\n"
                  "click click true true 110 120 10\nonce\nbubbled true true\nbound true\n"
                  "stage down 110\ndown true hat\n"
                  "click click true true 110 120 10\nbubbled true true\nbound true\n"
                  "stage down 10\n");
}

TEST(canvas_listeners_come_off_and_events_stop)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Symbol* button = movie.symbol("Button", 1);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "this.heard = function (e) { console.log('heard'); e.stopPropagation(); };\n"
        "this.go.addEventListener('click', this.heard);\n"
        "this.addEventListener('click', function () { console.log('parent'); });\n"
        "this.go.addEventListener('mouseover', function () { console.log('over'); });\n"
        "this.go.addEventListener('mouseout', function () { console.log('out'); });\n"
        "this.go.dispatchEvent('custom');\n"
        "this.go.on('custom', function (e) { console.log('custom', e.type, e.target === this); });\n"
        "this.go.dispatchEvent('custom');\n");
    movie.frame(*actions, 1, 1, "this.stop(); this.go.removeEventListener('click', this.heard);");
    fla::Frame* placed = movie.frame(*art, 0, 2);
    movie.place(*placed, *button, "go", 100, 100);
    Played played(movie);
    // The click stops at the button; off it, the pointer goes out.
    played.click(110, 120);
    played.runtime.stage().pointerMove(10, 10);
    played.runtime.afterEvents();
    // Its listener taken off, the click goes on to the parent.
    played.runtime.seek(1);
    played.click(110, 120);
    played.expect("custom custom true\nover\nheard\nout\nover\nparent\n");
}

TEST(canvas_ticker_tween_and_timers_follow_the_frames)
{
    CanvasMovie movie;
    fla::Symbol* ball = movie.symbol("Ball", 1);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var ticks = 0;\n"
        "createjs.Ticker.addEventListener('tick', function (e) { ticks++; });\n"
        "this.ball.addEventListener('tick', function () { if (ticks === 2) console.log('ball tick'); });\n"
        "console.log(createjs.Ticker.framerate);\n"
        "createjs.Tween.get(this.ball).to({x: 200}, 1000).call(function () { console.log('done', this.x, ticks); });\n"
        "setTimeout(function () { console.log('timeout', ticks); }, 500);\n"
        "var n = 0; var id = setInterval(function () { if (++n === 3) { clearInterval(id); console.log('interval', ticks); } }, 100);\n");
    movie.place(*movie.frame(*art, 0, 1), *ball, "ball");
    Played played(movie);
    for (int i = 0; i < 30; ++i)
        played.runtime.step();
    // At 24 frames a second: the interval's third at 300ms (frame 8), the
    // timeout at 500 (12), the tween's end at 1000 (24).
    played.expect("24\nball tick\ninterval 7\ntimeout 11\ndone 200 23\n");
    fla::play::Clip* clip = played.runtime.root().named("ball");
    CHECK(clip && clip->display->x() == 200.0);
}

TEST(canvas_tween_eases_and_loops)
{
    CanvasMovie movie;
    fla::Symbol* ball = movie.symbol("Ball", 1);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "createjs.Tween.get(this.ball, {loop: true}).to({alpha: 0}, 500, createjs.Ease.quadIn).to({alpha: 1}, 500);\n"
        "console.log(createjs.Ease.quadOut(0.5), createjs.Ease.linear(0.25), createjs.Ease.bounceOut(1));\n");
    movie.place(*movie.frame(*art, 0, 1), *ball, "ball");
    Played played(movie);
    for (int i = 0; i < 6; ++i)
        played.runtime.step();
    fla::play::Clip* clip = played.runtime.root().named("ball");
    // 250ms in, quadIn: a quarter of the way.
    CHECK(clip && std::abs(clip->display->alpha() - 0.75) < 0.01);
    for (int i = 0; i < 24; ++i)
        played.runtime.step();
    // Round again: 250ms into the second time.
    CHECK(clip && std::abs(clip->display->alpha() - 0.75) < 0.01);
    played.expect("0.75 0.25 1\n");
}

TEST(canvas_library_clips_are_made_and_added)
{
    CanvasMovie movie;
    movie.symbol("Star Shape", 2);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var s = new lib.StarShape();\n"
        "console.log(s instanceof lib.StarShape, s.parent);\n"
        "this.addChild(s); s.x = 50;\n"
        "console.log(s.parent === this, this.getChildIndex(s), this.contains(s));\n"
        "this.removeChild(s); console.log(s.parent);\n"
        "this.addChild(s); this.star = s;\n"
        "console.log(lib.properties.width, lib.properties.fps);\n");
    Played played(movie);
    played.runtime.step();
    played.expect("true null\ntrue 0 true\nnull\n550 24\n");
    const std::vector<fla::play::Clip*> made = played.runtime.madeChildren(played.runtime.root());
    CHECK(made.size() == 1 && made[0]->display->x() == 50.0);
}

TEST(canvas_text_fields_are_set_by_name)
{
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Text");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "console.log(this.score_txt.text);\n"
        "this.score_txt.text = 42;\n"
        "console.log(this.score_txt.text);\n");
    fla::Frame* frame = movie.frame(*art, 0, 1);
    auto* field = new fla::StaticText(frame);
    field->textType = fla::StaticText::TextType::Dynamic;
    field->instanceName = "score_txt";
    field->runs.emplace_back();
    field->runs.back().text = "0";
    frame->elements.push_back(field);
    Played played(movie);
    played.expect("0\n42\n");
}

TEST(canvas_errors_are_told_with_their_line_and_play_goes_on)
{
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1, "console.log('one');\nthis.noSuchMethod();\nconsole.log('not reached');");
    movie.frame(*actions, 1, 1, "createjs.Graphics2.get();");
    movie.frame(*actions, 2, 1, "var x = ;");
    movie.frame(*actions, 3, 1, "new createjs.Shape();");
    movie.frame(*actions, 4, 1, "this.gotoAndStop('nowhere'); console.log('still', this.currentFrame);");
    movie.frame(*actions, 5, 1, "this.stop(); console.log('six');");
    Played played(movie);
    for (int i = 0; i < 8; ++i)
        played.runtime.step();
    CHECK(played.runtime.root().frame == 5);
    CHECK(played.output() == "one\nWarning: gotoAndStop(\"nowhere\"): there is no such label\nstill 4\nsix\n");
    CHECK(played.errors.size() == 4);
    if (played.errors.size() == 4)
    {
        CHECK(played.errors[0].find("Scene 1, frame 1, line 2: TypeError") == 0);
        CHECK(played.errors[1].find("createjs.Graphics2 is not available in Test Movie") != std::string::npos);
        CHECK(played.errors[2].find("Scene 1, frame 3") == 0 && played.errors[2].find("SyntaxError") != std::string::npos);
        CHECK(played.errors[3].find("new createjs.Shape()") != std::string::npos);
    }
    for (const std::string& error : played.errors)
        std::printf("    (told) %s\n", error.c_str());
    // Each error knows its keyframe, for the Actions panel.
    CHECK(played.located.size() == 4 && played.located[0].source == movie.root->layers[0]->frames[0] &&
        played.located[0].line == 2);
}

TEST(canvas_syntax_check_accepts_javascript)
{
    int line = 0;
    std::string message;
    CHECK(fla::play::canvas::checkSyntax("this.stop();\nvar f = () => { return `t${1}`; };\nconst [a, b] = [1, 2];", line,
        message));
    CHECK(fla::play::canvas::checkSyntax("this.btn.addEventListener('click', fl_Click.bind(this));\n"
                                         "function fl_Click() { this.gotoAndPlay(5); }",
        line, message));
    CHECK(!fla::play::canvas::checkSyntax("var ok = 1;\nvar x = ;\n", line, message));
    CHECK(line == 2 && !message.empty());
    // ActionScript's typed declarations are not JavaScript.
    CHECK(!fla::play::canvas::checkSyntax("var n:int = 3;", line, message));
}
