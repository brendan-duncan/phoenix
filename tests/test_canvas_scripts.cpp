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

    /// The display objects scripts added to the main timeline, in order.
    std::vector<fla::display::DisplayObject*> made()
    {
        std::vector<fla::display::DisplayObject*> out;
        auto& root = static_cast<fla::display::DisplayObjectContainer&>(*runtime.root().display);
        for (const auto& child : root.children())
            if (child && !child->timeline && !child->clip)
                out.push_back(child.get());
        return out;
    }
};

const fla::display::Graphics* graphicsOf(const fla::display::DisplayObject* object)
{
    if (!object || object->kind() != fla::display::ObjectKind::Sprite)
        return nullptr;
    return &static_cast<const fla::display::Sprite*>(object)->graphics;
}

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

TEST(canvas_instances_are_there_before_their_frame_and_kept_between)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Symbol* button = movie.symbol("Button", 2);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 2,
        "if (this.seen) return; this.seen = true;\n"
        "var late = this.late; this.kept = late;\n"
        "console.log(late !== undefined, late.parent, late.currentFrame, late.totalFrames);\n"
        "late.on('click', function () { console.log('click', late.currentFrame); });\n"
        "late.marked = 'yes';\n");
    movie.frame(*actions, 2, 2, "console.log('placed', this.late === this.kept, this.late.parent === this);");
    movie.frame(*actions, 4, 1, "console.log('off', this.late === this.kept, this.late.parent);");
    movie.frame(*actions, 5, 1, "this.stop(); console.log('back', this.late === this.kept, this.late.marked);");
    movie.frame(*art, 0, 2);
    movie.place(*movie.frame(*art, 2, 2), *button, "late", 100, 100);
    movie.frame(*art, 4, 1);
    movie.place(*movie.frame(*art, 5, 1), *button, "late", 100, 100);
    Played played(movie);
    for (int i = 0; i < 5; ++i)
        played.runtime.step();
    CHECK(played.runtime.root().frame == 5);
    // The same clip, its listener with it, started over from its first frame
    // (it was on its second when the frame took it off).
    played.click(110, 110);
    played.expect("true null 0 2\nplaced true true\noff true null\nback true yes\nclick 0\n");
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
    movie.frame(*actions, 3, 1, "new createjs.DOMElement(document.getElementById('form'));");
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
        CHECK(played.errors[3].find("new createjs.DOMElement() (HTML elements over the canvas) is not available") !=
            std::string::npos);
    }
    if (played.errors.size() != 4)
        for (const std::string& error : played.errors)
            std::printf("    (told) %s\n", error.c_str());
    // Each error knows its keyframe, for the Actions panel.
    CHECK(played.located.size() == 4 && played.located[0].source == movie.root->layers[0]->frames[0] &&
        played.located[0].line == 2);
}

TEST(canvas_shapes_draw_with_graphics)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var s = new createjs.Shape();\n"
        "s.graphics.beginFill('#ff0000').drawRect(10, 20, 30, 40);\n"
        "s.graphics.setStrokeStyle(2).beginStroke('rgba(0,0,255,0.5)').moveTo(0, 0).lineTo(100, 0);\n"
        "this.addChild(s);\n"
        "var c = new createjs.Shape();\n"
        "this.circle = c.graphics.f('green').dc(0, 0, 10).command;\n"
        "c.x = 200; c.y = 200;\n"
        "this.addChild(c);\n"
        "console.log(s.parent === this, this.getChildIndex(c) - this.getChildIndex(s), s instanceof createjs.DisplayObject,\n"
        "  this.circle instanceof createjs.Graphics.Circle, c.graphics.isEmpty());\n"
        "var b = s.getBounds(); console.log(b.width > 99, Math.round(b.y + b.height));\n"
        "c.on('click', function (e) { console.log('click', e.target === c, Math.round(e.localX)); });\n"
        "var p = new createjs.Shape(new createjs.Graphics().f('#00f').p('AAAAAIBkAAIAABkg'));\n"
        "console.log(p.graphics.instructions.length, p.getBounds().width);\n");
    Played played(movie);
    std::vector<fla::display::DisplayObject*> made = played.made();
    CHECK(made.size() == 2);
    const fla::display::Graphics* drawn = made.empty() ? nullptr : graphicsOf(made[0]);
    // The rectangle's fill; the line's path is filled too (with no area), as
    // the canvas fills it, then stroked.
    CHECK(drawn && drawn->paths().size() == 3);
    if (drawn && drawn->paths().size() == 3)
    {
        const fla::display::GraphicsPath& fill = drawn->paths()[0];
        CHECK(fill.kind == fla::display::GraphicsPath::Kind::Fill && fill.fill.color == 0xff0000 && fill.nonZero);
        const fla::display::GraphicsPath& line = drawn->paths()[2];
        CHECK(line.kind == fla::display::GraphicsPath::Kind::Line && line.line.thickness == 2.0 &&
            line.line.fill.color == 0x0000ff && std::abs(line.line.fill.alpha - 0.5) < 1e-9 &&
            line.line.caps == fla::display::LineStyle::Caps::None);
    }
    const fla::display::Graphics* circle = made.size() > 1 ? graphicsOf(made[1]) : nullptr;
    CHECK(circle && std::abs(circle->bounds(false).width - 20.0) < 0.5);
    played.click(205, 200);
    played.click(250, 250);
    played.expect("true 1 true true false\ntrue 60\n6 10\nclick true 5\n");
}

TEST(canvas_graphics_commands_animate_and_objects_order)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var c = new createjs.Shape();\n"
        "var circle = c.graphics.f('#00ff00').dc(0, 0, 10).command;\n"
        "this.addChild(c);\n"
        "createjs.Ticker.on('tick', function () { circle.radius = 30; });\n"
        "var box = new createjs.Container(), a = new createjs.Shape(), b = new createjs.Shape();\n"
        "box.addChild(a, b); box.setChildIndex(b, 0);\n"
        "console.log(box.getChildIndex(a), box.getChildIndex(b), box.numChildren);\n"
        "box.removeChildAt(0); console.log(box.numChildren, b.parent);\n"
        "stage.addChild(box); box.name = 'box';\n"
        "console.log(box.parent === stage, box.stage === stage, stage.numChildren, stage.getChildByName('box') === box,\n"
        "  stage.getChildAt(0) === exportRoot);\n");
    Played played(movie);
    played.runtime.step();
    const std::vector<fla::display::DisplayObject*> made = played.made();
    const fla::display::Graphics* circle = made.empty() ? nullptr : graphicsOf(made[0]);
    CHECK(circle && std::abs(circle->bounds(false).width - 60.0) < 0.5);
    played.expect("1 0 2\n1 null\ntrue true 2 true true\n");
}

TEST(canvas_text_is_set_out_as_createjs_draws_it)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var t = new createjs.Text('Hello', 'bold 20px Arial', '#ff0000');\n"
        "t.textAlign = 'center'; t.x = 100;\n"
        "this.addChild(t);\n"
        "console.log(typeof t.getMeasuredWidth(), t.getMeasuredLineHeight() > 0, t.text, t instanceof createjs.Text);\n"
        "this.t = t;\n");
    Played played(movie);
    const std::vector<fla::display::DisplayObject*> made = played.made();
    CHECK(made.size() == 1 && made[0]->kind() == fla::display::ObjectKind::TextField);
    if (made.size() == 1 && made[0]->kind() == fla::display::ObjectKind::TextField)
    {
        const auto& field = static_cast<const fla::display::TextField&>(*made[0]);
        const fla::display::TextFormat format = field.getTextFormat();
        CHECK(field.text() == "Hello" && format.size == 20.0 && format.bold == true && format.font == std::string("Arial"));
        CHECK(format.color == 0xff0000u && format.align == std::string("center"));
        // Centered on its origin.
        CHECK(field.box().x < 0.0 && std::abs(field.box().x + field.box().width / 2.0) < 0.01);
        CHECK(made[0]->x() == 100.0);
    }
    played.expect("number true Hello true\n");
}

TEST(canvas_skew_and_registration_point_transform)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Symbol* ball = movie.symbol("Ball", 1);
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    fla::Layer* art = movie.layer(*movie.root, "Art");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var s = new createjs.Shape();\n"
        "s.setTransform(100, 50, 2, 1, 0, 0, 0, 10, 5);\n"
        "this.addChild(s);\n"
        "var p = s.localToGlobal(10, 5); console.log(p.x, p.y, s.regX, s.scaleX);\n"
        "s.skewX = 30; s.rotation = 45;\n"
        "p = s.localToGlobal(10, 5); console.log(Math.round(p.x), Math.round(p.y), s.skewX, s.rotation);\n"
        "var q = s.localToGlobal(11, 5); console.log(Math.round((q.x - p.x) * 1000) / 1000);\n"
        "this.ball.regX = 20; this.ball.x = 100;\n"
        "p = this.ball.localToGlobal(20, 0); console.log(p.x, p.y, this.ball.x);\n"
        "var m = s.getMatrix(); console.log(Math.round(m.tx), Math.round(m.ty));\n");
    movie.place(*movie.frame(*art, 0, 1), *ball, "ball", 10, 30);
    Played played(movie);
    // The registration point stays where x and y put it, skewed or not.
    played.expect("100 50 10 2\n100 50 30 45\n0.707\n100 30 100\n98 35\n");
}

TEST(canvas_filters_shadow_and_composite_apply)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "var s = new createjs.Shape(); s.graphics.f('red').dr(0, 0, 10, 10); this.addChild(s); this.s = s;\n"
        "var cm = new createjs.ColorMatrix().adjustBrightness(20);\n"
        "console.log(cm.toArray()[4], new createjs.ColorMatrix(0, 0, -100, 0).toArray()[0].toFixed(4));\n"
        "s.filters = [new createjs.BlurFilter(4, 4, 1), new createjs.ColorMatrixFilter(cm)];\n");
    movie.frame(*actions, 1, 1, "this.s.cache(0, 0, 10, 10);");
    movie.frame(*actions, 2, 1,
        "this.s.shadow = new createjs.Shadow('#000000', 3, 4, 5);\n"
        "this.s.compositeOperation = 'multiply';\n"
        "var m = new createjs.Shape(); m.graphics.f('#000').dr(0, 0, 5, 5); this.s.mask = m;\n");
    movie.frame(*actions, 3, 1,
        "this.stop(); this.s.uncache(); this.s.compositeOperation = 'xor'; this.s.compositeOperation = 'xor';\n"
        "console.log(this.s.cacheCanvas);\n");
    Played played(movie);
    const std::vector<fla::display::DisplayObject*> made = played.made();
    CHECK(made.size() == 1);
    if (made.size() != 1)
        return;
    fla::display::DisplayObject& s = *made[0];
    // Not cached: no filters, as CreateJS draws it.
    CHECK(s.filters().empty());
    played.runtime.step();
    CHECK(s.filters().size() == 2 && s.filters()[0].type == fla::display::BitmapFilter::Type::Blur &&
        s.filters()[0].blurX == 8.0 && s.filters()[1].type == fla::display::BitmapFilter::Type::ColorMatrix &&
        s.filters()[1].matrix.size() == 20 && s.filters()[1].matrix[4] == 20.0);
    played.runtime.step();
    CHECK(s.filters().size() == 3 && s.filters()[2].type == fla::display::BitmapFilter::Type::DropShadow &&
        std::abs(s.filters()[2].distance - 5.0) < 1e-9);
    CHECK(s.blendMode() == fla::display::BlendMode::Multiply);
    CHECK(s.mask() != nullptr && s.mask()->kind() == fla::display::ObjectKind::Shape);
    played.runtime.step();
    // Uncached, the shadow stays; a mode Phoenix does not have is told once.
    CHECK(s.filters().size() == 1 && s.blendMode() == fla::display::BlendMode::Normal);
    played.expect("20 0.3086\nWarning: compositeOperation \"xor\" is not available in Test Movie\nnull\n");
}

TEST(canvas_script_made_movie_clips_play_their_timeline)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var mc = new createjs.MovieClip();\n"
        "var a = new createjs.Shape(); a.graphics.f('red').dr(0, 0, 10, 10);\n"
        "var b = new createjs.Shape(); b.graphics.f('blue').dr(0, 0, 10, 10);\n"
        "mc.timeline.addTween(createjs.Tween.get(a).to({x: 100}, 4).wait(1));\n"
        "mc.timeline.addTween(createjs.Tween.get(b).wait(2).to({_off: true}, 0).wait(3));\n"
        "mc.frame_0 = function () { console.log('frame 0', a.parent === mc, b.parent === mc, mc.getChildIndex(a)); };\n"
        "mc.frame_3 = function () { console.log('frame 3', this.currentFrame, a.x, b.parent); };\n"
        "mc.timeline.addTween(createjs.Tween.get(mc).call(mc.frame_0).wait(3).call(mc.frame_3).wait(2));\n"
        "this.addChild(mc);\n"
        "console.log(mc.totalFrames, mc.currentFrame, mc instanceof createjs.MovieClip);\n"
        "var Ball = function () { this.initialize(); this.shape = new createjs.Shape(); };\n"
        "Ball.prototype = new createjs.MovieClip();\n"
        "var ball = new Ball(); console.log(ball.shape instanceof createjs.Shape, ball.id !== Ball.prototype.id);\n");
    Played played(movie);
    for (int i = 0; i < 6; ++i)
        played.runtime.step();
    played.expect("5 0 true\ntrue true\nframe 0 true true 1\nframe 3 3 75 null\nframe 0 true true 1\n");
}

TEST(canvas_sprites_play_their_animations)
{
    UsingBackend using_;
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1,
        "this.stop();\n"
        "var sheet = new createjs.SpriteSheet({images: [], frames: [[0, 0, 10, 10], [10, 0, 10, 10], [20, 0, 10, 10],\n"
        "  [30, 0, 10, 10]], animations: {run: [0, 2, 'idle'], idle: 3}});\n"
        "var sp = new createjs.Sprite(sheet, 'run'); this.addChild(sp);\n"
        "sp.on('animationend', function (e) { console.log('end', e.name, e.next); });\n"
        "console.log(sheet.getNumFrames(), sheet.getNumFrames('run'), sp.currentFrame, sp.currentAnimation);\n"
        "var n = 0; createjs.Ticker.on('tick', function () { if (++n === 5) console.log(sp.currentFrame, sp.paused); });\n");
    Played played(movie);
    for (int i = 0; i < 6; ++i)
        played.runtime.step();
    played.expect("4 3 0 run\nend run idle\nend idle null\n3 true\n");
}

TEST(canvas_unknown_apis_say_so)
{
    CanvasMovie movie;
    fla::Layer* actions = movie.layer(*movie.root, "Actions");
    movie.frame(*actions, 0, 1, "this.stop(); new createjs.Shape().graphics.draw({});");
    Played played(movie);
    CHECK(played.errors.size() == 1 &&
        played.errors[0].find("Graphics.draw() (drawing to a canvas context) is not available in Test Movie") !=
            std::string::npos);
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
