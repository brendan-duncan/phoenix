// The HTML5 runtime (resources/html5) run in QuickJS, without a browser:
// what does not need a canvas (events, geometry, timelines, frame order,
// text, the CreateJS layer).
#include "test_util.h"

#include <quickjs.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

const fs::path kRuntime = fs::path(__FILE__).parent_path().parent_path() / "resources" / "html5";

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// What a browser gives that the runtime asks for: the console (into
/// __out), and timers that never fire (the tests step frames themselves).
const char* const kHost = R"JS(
var __out = [];
var console = {log: function () { __out.push(Array.prototype.join.call(arguments, " ")); }};
console.info = console.warn = console.error = console.log;
var __timers = 0;
function setTimeout() { return ++__timers; }
function setInterval() { return ++__timers; }
function clearTimeout() {}
function clearInterval() {}
)JS";

/// The runtime's files in the order Publish joins them.
const char* const kFiles[] = {"core", "geom", "events", "display", "graphics", "filters", "render", "bitmap", "timeline",
    "text", "media", "utils", "player"};

/// A QuickJS context with the runtime loaded.
class Runtime
{
public:
    explicit Runtime(bool createJs = false)
    {
        _rt = JS_NewRuntime();
        _ctx = JS_NewContext(_rt);
        ok = eval(kHost, "<host>");
        for (const char* file : kFiles)
            ok = ok && eval(readFile(kRuntime / (std::string(file) + ".js")), file);
        if (createJs)
            ok = ok && eval(readFile(kRuntime / "createjs.js"), "createjs");
    }
    ~Runtime()
    {
        JS_FreeContext(_ctx);
        JS_FreeRuntime(_rt);
    }

    bool ok = false;
    std::string error;

    bool eval(const std::string& code, const std::string& where)
    {
        JSValue result = JS_Eval(_ctx, code.c_str(), code.size(), where.c_str(), JS_EVAL_TYPE_GLOBAL);
        const bool good = !JS_IsException(result);
        if (!good)
            error = exception();
        JS_FreeValue(_ctx, result);
        settle();
        return good;
    }
    /// Runs \a code, then what promises queued, and returns what the code
    /// left in __result (as a string).
    std::string run(const std::string& code)
    {
        if (!eval("var __result = '';\n" + code, "<test>"))
            return "threw: " + error;
        return global("__result");
    }
    std::string output() { return evaluate("__out.join('\\n')"); }

private:
    void settle()
    {
        JSContext* pending = nullptr;
        for (int i = 0; i < 10000 && JS_ExecutePendingJob(_rt, &pending) > 0; ++i) {}
    }
    std::string exception()
    {
        JSValue thrown = JS_GetException(_ctx);
        const char* text = JS_ToCString(_ctx, thrown);
        std::string out = text ? text : "?";
        JS_FreeCString(_ctx, text);
        JSValue stack = JS_GetPropertyStr(_ctx, thrown, "stack");
        if (!JS_IsUndefined(stack))
        {
            const char* where = JS_ToCString(_ctx, stack);
            out += std::string("\n") + (where ? where : "");
            JS_FreeCString(_ctx, where);
        }
        JS_FreeValue(_ctx, stack);
        JS_FreeValue(_ctx, thrown);
        return out;
    }
    std::string global(const char* name)
    {
        JSValue object = JS_GetGlobalObject(_ctx);
        JSValue value = JS_GetPropertyStr(_ctx, object, name);
        const char* text = JS_ToCString(_ctx, value);
        std::string out = text ? text : "";
        JS_FreeCString(_ctx, text);
        JS_FreeValue(_ctx, value);
        JS_FreeValue(_ctx, object);
        return out;
    }
    std::string evaluate(const std::string& expression)
    {
        eval("var __value = String(" + expression + ");", "<value>");
        return global("__value");
    }

    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;
};

bool same(const std::string& got, const std::string& expected)
{
    if (got != expected)
        std::printf("    got:      %s\n    expected: %s\n", got.c_str(), expected.c_str());
    return got == expected;
}

/// A small movie: a main timeline of four frames with a label, placing a
/// clip (a two-frame symbol linked as com.studio.Hat) that a classic tween
/// moves, and a graphic that plays along.
const char* const kMovie = R"JS(
var movie = {
    width: 200, height: 100, frameRate: 10, background: 0xffffff,
    art: [{o: [{p: "M0 0L10 0L10 10L0 10Z", f: {c: 0xff0000, a: 1}}], b: [0, 0, 10, 10]}],
    texts: [], images: [], sounds: [],
    symbols: [
        {name: "Hat", type: "mc", linkage: "com.studio.Hat",
         timeline: {n: 2, states: [[{k: "a", t: "a", id: 0}], [{k: "a", t: "a", id: 0, m: [2, 0, 0, 2, 0, 0]}]], map: [0, 1]}},
        {name: "Spin", type: "graphic",
         timeline: {n: 3, states: [[{k: "a", t: "a", id: 0}], [{k: "a", t: "a", id: 0, m: [1, 0, 0, 1, 1, 0]}],
            [{k: "a", t: "a", id: 0, m: [1, 0, 0, 1, 2, 0]}]], map: [0, 1, 2]}}
    ],
    root: {n: 4, labels: [[2, "end"]], scenes: [{name: "Scene 1", at: 0, n: 4}],
        states: [
            [{k: "h", d: 2, t: "s", id: 0, n: "hat", m: [1, 0, 0, 1, 0, 0]}, {k: "g", d: 1, t: "s", id: 1}],
            [{k: "h", d: 2, t: "s", id: 0, n: "hat", m: [1, 0, 0, 1, 10, 0]}, {k: "g", d: 1, t: "s", id: 1, gf: 1}],
            [{k: "h", d: 2, t: "s", id: 0, n: "hat", m: [1, 0, 0, 1, 20, 0]}, {k: "g", d: 1, t: "s", id: 1, gf: 2}],
            []],
        map: [0, 1, 2, 3]}
};
)JS";

} // namespace

TEST(html5_runtime_loads)
{
    Runtime js;
    CHECK(js.ok);
    if (!js.ok)
        std::printf("    %s\n", js.error.c_str());
    CHECK(same(js.run("__result = [typeof flash.display.MovieClip, typeof flash.events.EventDispatcher, "
                      "flash.utils.getQualifiedClassName(new flash.display.Sprite())].join(' ');"),
        "function function flash.display::Sprite"));
}

TEST(html5_runtime_events_go_through_their_phases)
{
    Runtime js;
    CHECK(same(js.run(R"JS(
var Sprite = flash.display.Sprite, Event = flash.events.Event;
var a = new Sprite(), b = new Sprite(), c = new Sprite();
a.addChild(b); b.addChild(c);
var seen = [];
a.addEventListener("x", function (e) { seen.push("a-capture" + e.eventPhase); }, true);
a.addEventListener("x", function (e) { seen.push("a" + e.eventPhase + (e.currentTarget === a) + (e.target === c)); });
c.addEventListener("x", function (e) { seen.push("c-low"); }, false, -1);
c.addEventListener("x", function (e) { seen.push("c-high" + e.eventPhase); }, false, 5);
b.addEventListener("x", function (e) { seen.push("b"); e.stopPropagation(); });
c.dispatchEvent(new Event("x", true));
// Not bubbling: the target's own listeners only.
c.dispatchEvent(new Event("x", false));
__result = seen.join(",");
)JS"),
        "a-capture1,c-high2,c-low,b,a-capture1,c-high2,c-low"));
}

TEST(html5_runtime_display_objects_keep_their_parts)
{
    Runtime js;
    CHECK(same(js.run(R"JS(
var s = new flash.display.Sprite();
s.rotation = 90;
s.scaleX = 2;
s.x = 5;
var m = s.transform.matrix;
var r = function (v) { return Math.round(v * 1000) / 1000; };
var parts = [r(s.rotation), r(s.scaleX), r(s.scaleY), r(m.a), r(m.b), r(m.c), r(m.d), m.tx];
var shape = new flash.display.Shape();
shape.graphics.beginFill(0xff0000);
shape.graphics.drawRect(10, 20, 30, 40);
s.addChild(shape);
var bounds = shape.getBounds(shape);
var global = shape.localToGlobal(new flash.geom.Point(10, 20));
parts.push(bounds.x, bounds.y, bounds.width, bounds.height, r(global.x), r(global.y), s.numChildren);
s.removeChild(shape);
parts.push(s.numChildren, shape.parent === null);
var threw = "";
try { s.removeChild(shape); } catch (e) { threw = e.errorID; }
parts.push(threw);
__result = parts.join(" ");
)JS"),
        "90 2 1 0 2 -1 0 5 10 20 30 40 -15 20 1 0 true 2025"));
}

TEST(html5_runtime_plays_timelines_in_frame_order)
{
    Runtime js;
    CHECK(same(js.run(std::string(kMovie) + R"JS(
var log = [];
phoenix.content(movie);
phoenix.actionscript(function (as3) {
    var Hat = as3.classByName("com.studio.Hat");
    class MyHat extends Hat {
        constructor() {
            super();
            log.push("hat made " + this.name + " children " + this.numChildren);
            this.addEventListener("enterFrame", function (e) { log.push("hat enter " + e.target.currentFrame); });
        }
    }
    as3.linkClass("com.studio.Hat", MyHat);
    as3.frameScripts("", {
        0: function () { log.push("root 1 hat at " + this.hat.x + " " + (this.hat instanceof MyHat)); },
        2: function () { log.push("root 3 " + this.currentLabel + " hat at " + this.hat.x); this.stop(); }
    });
});
phoenix.start({}).then(function (player) {
    player.pause();
    var root = player.root;
    root.addEventListener("exitFrame", function () { log.push("exit " + root.currentFrame); });
    for (var i = 0; i < 4; ++i)
        player.tick();
    var graphic = root.numChildren > 1 ? root.getChildAt(0) : null;
    log.push("frame " + root.currentFrame + " playing " + root.isPlaying);
    __result = log.join("|");
});
)JS"),
        "hat made hat children 1|root 1 hat at 0 true|hat enter 2|exit 2|hat enter 1|root 3 end hat at 20|exit 3|"
        "hat enter 2|exit 3|hat enter 1|exit 3|frame 3 playing false"));
}

TEST(html5_runtime_scripts_win_over_the_timeline)
{
    Runtime js;
    CHECK(same(js.run(std::string(kMovie) + R"JS(
phoenix.content(movie);
phoenix.start({}).then(function (player) {
    player.pause();
    var root = player.root;
    var hat = root.hat;
    var parts = [hat.x];
    player.tick();
    parts.push(hat.x);
    // Moved by a script: the tween leaves it there.
    hat.x = 100;
    player.tick();
    parts.push(hat.x, root.hat === hat);
    // Back to the start: the same hat, still where the script put it.
    root.gotoAndStop(1);
    parts.push(root.currentFrame, root.hat === hat, hat.x);
    root.gotoAndStop("end");
    parts.push(root.currentFrame, root.currentFrameLabel);
    // The graphic shows the frame its parent gives it.
    var spin = root.getChildAt(0);
    parts.push(spin instanceof phoenix.Graphic, spin._frame);
    root.gotoAndStop(4);
    parts.push(root.numChildren, root.hat === null);
    __result = parts.join(" ");
});
)JS"),
        "0 10 100 true 1 true 100 3 end true 2 0 true"));
}

TEST(html5_runtime_text_fields_read_html)
{
    Runtime js;
    CHECK(same(js.run(R"JS(
var t = new flash.text.TextField();
t.htmlText = "<p align='center'><b>Hi</b> <font color='#ff0000' size='20'>there</font></p><p>two</p>";
var f0 = t.getTextFormat(0, 1), f3 = t.getTextFormat(3, 4);
var parts = [JSON.stringify(t.text), f0.bold, f0.align, f3.color.toString(16), f3.size, t.numLines];
t.appendText("!");
t.text = "a\nb";
parts.push(JSON.stringify(t.text), t.length, t.numLines);
__result = parts.join(" ");
)JS"),
        "\"Hi there\\rtwo\" true center ff0000 20 2 \"a\\rb\" 3 2"));
}

TEST(html5_runtime_createjs_layer_counts_frames_from_zero)
{
    Runtime js(true);
    CHECK(js.ok);
    CHECK(same(js.run(std::string(kMovie) + R"JS(
movie.javaScript = true;
movie.root.js = [[0, "this.stop(); var self = this; createjs.Tween.get(this.hat).to({x: 100}, 500);"
    + "this.on('tick', function () { if (createjs.Ticker.getTicks() === 3) console.log('tick', Math.round(self.hat.x), self.currentFrame); });"]];
movie.symbols[0].timeline.js = [[1, "console.log('hat frame', this.currentFrame);"]];
phoenix.content(movie);
phoenix.start({}).then(function (player) {
    player.pause();
    var root = player.root;
    for (var i = 0; i < 3; ++i)
        player.tick();
    root.gotoAndStop("end");
    var parts = [root.currentFrame, root.paused, typeof exportRoot, exportRoot === root];
    root.hat.gotoAndStop(0);
    parts.push(root.hat.currentFrame);
    __result = parts.join(" ");
});
)JS"),
        "2 true object true 0"));
    CHECK(same(js.output(), "hat frame 1\ntick 40 0\nhat frame 1"));
}
