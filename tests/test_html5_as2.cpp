// HTML5 publishing of ActionScript 1 and 2: the scripts gathered with the
// content, translated by libas3, and run on the runtime's ActionScript 2
// layer (resources/html5/as2.js) in QuickJS.
#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/element.h"
#include "../src/data/fla_document.h"
#include "../src/edit/html5_actionscript2.h"
#include "../src/edit/html5_export.h"
#include "../src/parser/fla_parser.h"

#include <quickjs.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

const fs::path kData = fs::path(__FILE__).parent_path() / "data";
const fs::path kRuntime = fs::path(__FILE__).parent_path().parent_path() / "resources" / "html5";

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// The same small shape for every piece of art.
class FakeArt : public fla::html5::ArtSource
{
public:
    std::string art(const fla::Element&) override
    {
        return "{\"o\":[{\"p\":\"M0 0L10 0L10 10L0 10Z\",\"f\":{\"c\":0,\"a\":1}}],\"b\":[0,0,10,10]}";
    }
    int bitmap(const fla::Bitmap&) override { return -1; }
    int sound(const std::string&) override { return -1; }
    std::string images() override { return "[]"; }
    std::string sounds() override { return "[]"; }
};

bool has(const std::string& text, const std::string& part)
{
    const bool found = text.find(part) != std::string::npos;
    if (!found)
        std::printf("    not there: %s\n", part.c_str());
    return found;
}

/// The test movie (tests/data/html5/as2), its content and its scripts
/// translated, its classes found beside it.
struct Published
{
    std::unique_ptr<fla::FLADocument> file;
    std::string content;
    fla::html5::ActionScript2 scripts;
    fla::html5as2::Result translated;
    std::vector<std::string> notes;
};

std::unique_ptr<Published> publish(const char* movie = "as2")
{
    auto out = std::make_unique<Published>();
    FLAParser parser;
    out->file.reset(parser.parse((kData / "html5" / movie).string()));
    if (!out->file || !out->file->document)
        return nullptr;
    FakeArt art;
    fla::html5::Options options;
    options.actionScript2 = &out->scripts;
    out->content = fla::html5::content(*out->file->document, art, options, &out->notes);
    const fs::path folder = kData / "html5" / movie;
    out->translated = fla::html5as2::translate(*out->file->document, out->scripts,
        [folder](const std::string& name) -> std::optional<std::string> {
            const fs::path path = folder / (name + ".as");
            if (!fs::exists(path))
                return std::nullopt;
            return readFile(path);
        },
        &out->notes);
    return out;
}

/// QuickJS with the runtime, avm1.js and the ActionScript 2 layer.
class Runtime
{
public:
    Runtime()
    {
        _rt = JS_NewRuntime();
        _ctx = JS_NewContext(_rt);
        ok = eval("var __out = [];\n"
                  "var console = {log: function () { __out.push(Array.prototype.join.call(arguments, ' ')); }};\n"
                  "console.info = console.warn = console.error = console.log;\n"
                  "function setTimeout() { return 1; } function setInterval() { return 1; }\n"
                  "function clearTimeout() {} function clearInterval() {}\n",
            "<host>");
        static const char* const files[] = {"core", "geom", "events", "display", "graphics", "filters", "render",
            "bitmap", "timeline", "text", "media", "utils", "player"};
        for (const char* file : files)
            ok = ok && eval(readFile(kRuntime / (std::string(file) + ".js")), file);
        ok = ok && eval(fla::html5as2::avm1Runtime(), "avm1.js") && eval(readFile(kRuntime / "as2.js"), "as2.js") &&
            eval(readFile(kRuntime / "as2components.js"), "as2components.js");
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
        {
            JSValue thrown = JS_GetException(_ctx);
            const char* text = JS_ToCString(_ctx, thrown);
            error = text ? text : "?";
            JS_FreeCString(_ctx, text);
            JS_FreeValue(_ctx, thrown);
        }
        JS_FreeValue(_ctx, result);
        JSContext* pending = nullptr;
        for (int i = 0; i < 10000 && JS_ExecutePendingJob(_rt, &pending) > 0; ++i) {}
        return good;
    }
    std::string output()
    {
        eval("var __text = __out.join('\\n');", "<out>");
        JSValue object = JS_GetGlobalObject(_ctx);
        JSValue value = JS_GetPropertyStr(_ctx, object, "__text");
        const char* text = JS_ToCString(_ctx, value);
        std::string out = text ? text : "";
        JS_FreeCString(_ctx, text);
        JS_FreeValue(_ctx, value);
        JS_FreeValue(_ctx, object);
        return out;
    }

private:
    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;
};

} // namespace

TEST(html5_as2_scripts_go_with_the_content)
{
    const std::unique_ptr<Published> published = publish();
    CHECK(published != nullptr);
    if (!published)
        return;
    const fla::Document& document = *published->file->document;
    CHECK(fla::html5as2::usesActionScript2(document));
    CHECK(fla::html5as2::swfVersion(document) == 8);
    // The main timeline's two scripts and the Box's; the box's and the
    // button's handlers, named by their entries.
    CHECK(published->scripts.frames.size() == 3);
    CHECK(published->scripts.instances.size() == 2);
    if (published->scripts.frames.size() == 3)
    {
        CHECK(published->scripts.frames[0].timeline.empty());
        CHECK(published->scripts.frames[0].scene == "Scene 1");
        CHECK(published->scripts.frames[1].frame == 2);
    }
    CHECK(has(published->content, "\"n\":\"box\",\"h\":0"));
    CHECK(has(published->content, "\"n\":\"btn\",\"h\":1"));
    // The symbol attachMovie names, exported though no frame shows it.
    CHECK(has(published->content, "{\"name\":\"Ball\",\"type\":\"mc\",\"linkage\":\"Ball\",\"ident\":\"ball\""));
    CHECK(has(published->content, "\"var\":\"score\""));
    const std::string& code = published->translated.code;
    CHECK(has(code, "avm1.movie(function ($, movie) {"));
    CHECK(has(code, "$.version = 8;"));
    CHECK(has(code, "\"Ball\": [[\"MovieClip\"], function ($s) {"));
    CHECK(has(code, "movie.register(\"ball\", \"Ball\");"));
    CHECK(has(code, "movie.frame(\"Box\", 0, function ($s) {"));
    CHECK(has(code, "movie.handlers(1, ["));
    CHECK(has(code, "{events: [\"release\"], clip: false, run: function ($s) {"));
    CHECK(published->translated.classes.size() == 1);
    CHECK(published->notes.empty());
    for (const std::string& note : published->notes)
        std::printf("    note: %s\n", note.c_str());
}

TEST(html5_as2_movie_runs_in_the_runtime)
{
    const std::unique_ptr<Published> published = publish();
    CHECK(published != nullptr);
    if (!published)
        return;
    Runtime js;
    CHECK(js.ok);
    if (!js.ok)
    {
        std::printf("    %s\n", js.error.c_str());
        return;
    }
    CHECK(js.eval("phoenix.content(" + published->content + ");", "content"));
    CHECK(js.eval(published->translated.code, "movie.as.js"));
    CHECK(js.eval(R"JS(
phoenix.start({}).then(function (player) {
    player.pause();
    for (var i = 0; i < 4; ++i)
        player.tick();
    var root = phoenix.as2Face(player.root);
    console.log("after: frame " + root._currentframe + ", box at " + root.box._currentframe + ", ball1 " +
        Math.round(root.ball1._x) + ", score_txt " + root.score_txt.text + ", " + (root.ball2 instanceof avm1.global.Ball));
});
)JS", "<test>"));
    if (!js.error.empty())
        std::printf("    %s\n", js.error.c_str());
    const std::string out = js.output();
    CHECK(has(out, "box load box"));
    CHECK(has(out, "start movieclip 1/3 _level0"));
    CHECK(has(out, "Ball() ball1 speed 3"));
    CHECK(has(out, "attached ball1 60 true 20 _level0.ball1"));
    CHECK(has(out, "box 2 60 /box"));
    CHECK(has(out, "xml 1 2 hi"));
    CHECK(has(out, "lv 1 two words"));
    CHECK(has(out, "sorted 1,5,10"));
    CHECK(has(out, "stage 400x300"));
    // Stopped on frame 3; the ball rolled on (3 a frame), the field shows
    // its variable, the clone is a Ball too.
    CHECK(has(out, "frame 3: 2 frames, frames 2"));
    CHECK(has(out, "after: frame 3, box at 2, ball1 72, score_txt frames 4, true"));
    if (out.find("after:") == std::string::npos)
        std::printf("%s\n", out.c_str());
}

TEST(html5_as1_movie_runs_case_blind_across_scenes)
{
    const std::unique_ptr<Published> published = publish("as1");
    CHECK(published != nullptr);
    if (!published)
        return;
    CHECK(fla::html5as2::swfVersion(*published->file->document) == 6);
    CHECK(has(published->translated.code, "$.version = 6;"));
    Runtime js;
    CHECK(js.ok);
    CHECK(js.eval("phoenix.content(" + published->content + ");", "content"));
    CHECK(js.eval(published->translated.code, "movie.as.js"));
    // getURL opens nothing here: open is a stand-in that notes the call.
    CHECK(js.eval(R"JS(
var opened = [];
globalThis.open = function (url, target) { opened.push(url + " " + target); };
phoenix.start({}).then(function (player) {
    player.pause();
    player.tick();
    var stage = player.stage;
    var KeyboardEvent = flash.events.KeyboardEvent;
    stage.dispatchEvent(new KeyboardEvent("keyDown", true, false, 32, 32));
    stage.dispatchEvent(new KeyboardEvent("keyUp", true, false, 32, 32));
    stage.dispatchEvent(new KeyboardEvent("keyDown", true, false, 0, 39));
    var root = phoenix.as2Face(player.root);
    console.log("after: frame " + root._currentframe + ", card0 " + root.card0._x + " depth " + root.card0.getDepth() +
        ", opened " + opened.join());
});
)JS", "<test>"));
    const std::string out = js.output();
    CHECK(has(out, "tally 6 total 5 0"));
    CHECK(has(out, "depths 3 1 top card0"));
    CHECK(has(out, "scene 2, frame 3 of 3"));
    // One keyPress handler call for one key.
    CHECK(out.find("space on _level0\nspace on") == std::string::npos);
    CHECK(has(out, "space on _level0"));
    CHECK(has(out, "right, card0 at 30"));
    CHECK(has(out, "after: frame 3, card0 30 depth 3, opened http://example.com/ _blank"));
    if (out.find("after:") == std::string::npos)
        std::printf("%s\n%s\n", out.c_str(), js.error.c_str());
}

TEST(html5_as2_components_work_with_their_parameters)
{
    const std::unique_ptr<Published> published = publish("as2forms");
    CHECK(published != nullptr);
    if (!published)
        return;
    // The Button's own symbol, and stand-ins for the components the library
    // has none of, each placed with its parameters.
    CHECK(has(published->content, "{\"name\":\"Button\",\"type\":\"mc\",\"linkage\":\"mx.controls.Button\""));
    CHECK(has(published->content, "{\"name\":\"ComboBox\",\"type\":\"mc\",\"linkage\":\"mx.controls.ComboBox\""));
    CHECK(has(published->content, "\"n\":\"go\",\"cp\":{\"label\":\"Go\",\"toggle\":false}"));
    CHECK(has(published->content, "\"cp\":{\"labels\":[\"Red\",\"Green\",\"Blue\"],\"data\":[\"r\",\"g\",\"b\"],"
                                   "\"editable\":false}"));
    CHECK(published->notes.empty());
    for (const std::string& note : published->notes)
        std::printf("    note: %s\n", note.c_str());
    Runtime js;
    CHECK(js.ok);
    CHECK(js.eval("phoenix.content(" + published->content + ");", "content"));
    CHECK(js.eval(published->translated.code, "movie.as.js"));
    CHECK(js.eval(R"JS(
phoenix.start({}).then(function (player) {
    player.pause();
    player.tick();
    var root = phoenix.as2Face(player.root);
    var MouseEvent = flash.events.MouseEvent;
    // The box unchecked, then clicks on the button, the big radio button
    // and the stepper's up arrow twice; the combo box's second item chosen.
    root.agree.selected = false;
    phoenix._.as2.displayOf(root.go).dispatchEvent(new MouseEvent("click", true));
    phoenix._.as2.displayOf(root.big).dispatchEvent(new MouseEvent("click", true));
    root.color.selectedIndex = 1;
    root.color.dispatchEvent({type: "change"});
    var count = phoenix._.as2.displayOf(root.count);
    count._c.arrows.dispatchEvent(new MouseEvent("click", true));
    count._c.arrows.dispatchEvent(new MouseEvent("click", true));
    console.log("after: " + root.small.selected + " " + root.big.selected + " " + root.count.value + " " +
        root.caption.text + " " + root.who.text);
});
)JS", "<test>"));
    const std::string out = js.output();
    CHECK(has(out, "start Go true 0 r Ann 3 3 12x10 false"));
    CHECK(has(out, "picks 4 Fourth"));
    CHECK(has(out, "clicked go Go false"));
    // Delegate calls the function on the root, which has no name.
    CHECK(has(out, "color Green g \ncount 4"));
    // The stepper stops at its maximum.
    CHECK(has(out, "count 4\ncount 5"));
    CHECK(has(out, "after: false true 5 Pick one Ann"));
    if (out.find("after:") == std::string::npos)
        std::printf("%s\n%s\n", out.c_str(), js.error.c_str());
}
