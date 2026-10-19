// Interactive states played: a graph turned into ActionScript 3 and 2, run
// in the movie runtime with clicks and keys, compiled for an SWF and
// translated for an HTML5 page.
#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/html5_actionscript2.h"
#include "../src/edit/html5_export.h"
#include "../src/edit/state_graph.h"
#include "../src/edit/swf_actionscript3.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/zip_reader.h"
#include "../src/play/movie_runtime.h"

#include <quickjs.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace st = fla::states;
namespace fs = std::filesystem;

const fs::path kRuntime = fs::path(__FILE__).parent_path().parent_path() / "resources" / "html5";

std::vector<uint8_t> bytesOf(const std::string& text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// A movie with the states turned into ActionScript \a version: labels
/// idle (frames 1-10), eat (11-20) and sleep (21-30), and a button,
/// feedBtn, on every frame.
struct Movie
{
    std::unique_ptr<ZipReader> reader;
    std::unique_ptr<fla::FLADocument> root;
    fla::Document* document = nullptr;
    fla::CommandStack stack;
};

std::unique_ptr<Movie> movie(int version)
{
    const std::string square = "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/></FillStyle></fills>"
                               "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|800 0!800 0|800 800!800 800|0 800!0 800|0 0\"/>"
                               "</edges></DOMShape>";
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<symbols><Include href=\"Feed.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"labels\"><frames>"
        "<DOMFrame index=\"0\" duration=\"10\" name=\"idle\" labelType=\"name\"><elements/></DOMFrame>"
        "<DOMFrame index=\"10\" duration=\"10\" name=\"eat\" labelType=\"name\"><elements/></DOMFrame>"
        "<DOMFrame index=\"20\" duration=\"10\" name=\"sleep\" labelType=\"name\"><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"30\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Feed\" name=\"feedBtn\" symbolType=\"button\">"
        "<matrix><Matrix tx=\"20\" ty=\"20\"/></matrix></DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    files["LIBRARY/Feed.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Feed\" symbolType=\"button\">"
        "<timeline><DOMTimeline name=\"Feed\"><layers><DOMLayer name=\"art\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\"><elements>" +
        square + "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    const std::string settings =
        "<flash_profiles><flash_profile name=\"Default\" current=\"true\"><PublishFlashProperties enabled=\"true\">"
        "<ActionScriptVersion>" +
        std::to_string(version) +
        "</ActionScriptVersion><Version>10</Version></PublishFlashProperties></flash_profile></flash_profiles>";

    auto made = std::make_unique<Movie>();
    made->reader.reset(new ZipReader());
    made->reader->openMemory(files);
    made->root.reset(new fla::FLADocument(nullptr));
    DocumentParser parser(made->reader.get());
    const auto xml = files.at("DOMDocument.xml");
    made->document = parser.parse(std::string(xml.begin(), xml.end()), made->root.get());
    made->root->document = made->document;
    if (!made->document || made->document->timelines.empty())
        return nullptr;
    made->document->extraFiles.emplace_back("PublishSettings.xml", fla::LazyBytes(bytesOf(settings)));

    st::Graph graph;
    graph.states = {{"idle", "", st::Play::Loop, 0, 0}, {"eat", "", st::Play::Once, 200, 0},
        {"sleep", "", st::Play::Hold, 100, 150}};
    graph.inputs = {{"score", st::InputKind::Number, 0}, {"poke", st::InputKind::Trigger, 0}};
    const auto transition = [](int from, int to, st::When when, const std::string& target) {
        st::Transition made;
        made.from = from;
        made.to = to;
        made.when = when;
        made.target = target;
        return made;
    };
    graph.transitions = {transition(0, 1, st::When::Click, "feedBtn"), transition(1, 0, st::When::Finished, ""),
        transition(0, 2, st::When::Key, "Space"), transition(2, 0, st::When::Condition, ""),
        transition(-1, 2, st::When::Condition, ""), transition(2, 1, st::When::Timer, "")};
    graph.transitions[3].condition = {"score", st::Compare::More, 2};
    graph.transitions[4].condition = {"poke", st::Compare::Fired, 0};
    graph.transitions[5].seconds = 2;
    graph.listeners = {{st::When::Key, "A", "score", st::Change::Add, 1}, {st::When::Key, "P", "poke", st::Change::Fire, 0}};
    st::apply(made->stack, *made->document->timelines[0], graph, st::Edit(), version, 12.0, "States");
    return made;
}

/// Plays the movie with its scripts in ActionScript \a version, clicking
/// and pressing keys, and checks each state is reached when it should be.
void play(int version)
{
    std::unique_ptr<Movie> made = movie(version);
    CHECK(made != nullptr);
    if (!made)
        return;
    fla::play::MovieRuntime runtime(*made->document->timelines[0], 12.0);
    runtime.setActionScriptVersion(version);
    std::vector<std::string> errors;
    runtime.error = [&errors](const std::string& line) { errors.push_back(line); };
    runtime.trace = [](const std::string& line) { std::printf("    trace: %s\n", line.c_str()); };
    runtime.start();
    fla::play::Clip& root = runtime.root();
    CHECK(root.frame == 0 && root.playing);
    // Idle loops round its ten frames.
    bool inIdle = true;
    for (int i = 0; i < 25; ++i)
    {
        runtime.step();
        inIdle = inIdle && root.frame <= 9;
    }
    CHECK(inIdle && root.playing);

    // A click on the button: eat, played once, then idle again.
    fla::play::Clip* button = root.named("feedBtn");
    CHECK(button != nullptr);
    if (!button)
        return;
    runtime.pointerOver(button);
    runtime.pointerDown();
    runtime.pointerUp();
    runtime.pointerOver(nullptr);
    runtime.afterEvents();
    CHECK(root.frame == 10);
    for (int i = 0; i < 8; ++i)
        runtime.step();
    CHECK(root.frame == 18);
    runtime.step();
    CHECK(root.frame == 0 && root.playing);

    // P fires the poke trigger: to sleep from any state, held.
    runtime.keyDown(80, 'p');
    runtime.keyUp(80, 'p');
    runtime.afterEvents();
    CHECK(root.frame == 20 && !root.playing);
    // Two seconds asleep: eat.
    for (int i = 0; i < 23; ++i)
        runtime.step();
    CHECK(root.frame == 20);
    runtime.step();
    CHECK(root.frame == 10);
    for (int i = 0; i < 9; ++i)
        runtime.step();
    CHECK(root.frame == 0);

    // Space: sleep; A three times makes the score 3, and score > 2 wakes it.
    runtime.keyDown(32, ' ');
    runtime.keyUp(32, ' ');
    runtime.afterEvents();
    CHECK(root.frame == 20);
    for (int i = 0; i < 2; ++i)
    {
        runtime.keyDown(65, 'a');
        runtime.keyUp(65, 'a');
    }
    runtime.afterEvents();
    CHECK(root.frame == 20);
    runtime.keyDown(65, 'a');
    runtime.keyUp(65, 'a');
    runtime.afterEvents();
    CHECK(root.frame == 0 && root.playing);
    CHECK(errors.empty());
    for (const std::string& error : errors)
        std::printf("    %s\n", error.c_str());
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

/// QuickJS with the HTML5 runtime and its ActionScript 2 layer.
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
        ok = ok && eval(fla::html5as2::avm1Runtime(), "avm1.js") && eval(readFile(kRuntime / "as2.js"), "as2.js");
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

TEST(state_graph_plays_as_actionscript_3)
{
    play(3);
}

TEST(state_graph_plays_as_actionscript_2)
{
    play(2);
}

TEST(state_graph_actionscript_3_compiles_and_translates)
{
    std::unique_ptr<Movie> made = movie(3);
    CHECK(made != nullptr);
    if (!made)
        return;
    fla::swfmovie::ActionScript3Options options;
    options.findClass = [](const std::string&) { return std::optional<std::string>(); };
    options.documentName = "states";
    const fla::swfmovie::ActionScript3 compiled = fla::swfmovie::compileActionScript3(*made->document, options);
    CHECK(!compiled.abc.empty() && compiled.mainClass == "states_fla.MainTimeline");
    for (const std::string& note : compiled.notes)
        std::printf("    swf: %s\n", note.c_str());
    const fla::swfmovie::JavaScript3 translated = fla::swfmovie::translateActionScript3(*made->document, options);
    CHECK(!translated.bundle.empty());
    for (const std::string& note : translated.notes)
        std::printf("    html5: %s\n", note.c_str());
    for (const char* part : {"phxGo", "phxFrame", "\"enterFrame\"", "\"keyDown\"", "addEventListener(\"click\""})
    {
        const bool found = translated.bundle.find(part) != std::string::npos;
        CHECK(found);
        if (!found)
            std::printf("    not in the bundle: %s\n", part);
    }
}

TEST(state_graph_actionscript_2_runs_on_an_html5_page)
{
    std::unique_ptr<Movie> made = movie(2);
    CHECK(made != nullptr);
    if (!made)
        return;
    CHECK(fla::html5as2::usesActionScript2(*made->document));
    FakeArt art;
    fla::html5::ActionScript2 scripts;
    fla::html5::Options options;
    options.actionScript2 = &scripts;
    std::vector<std::string> notes;
    const std::string content = fla::html5::content(*made->document, art, options, &notes);
    const fla::html5as2::Result translated = fla::html5as2::translate(*made->document, scripts,
        [](const std::string&) { return std::optional<std::string>(); }, &notes);
    for (const std::string& note : notes)
        std::printf("    note: %s\n", note.c_str());
    CHECK(notes.empty());
    CHECK(translated.code.find("onRelease") != std::string::npos);
    CHECK(translated.code.find("phxEvent") != std::string::npos);

    Runtime js;
    CHECK(js.ok);
    if (!js.ok)
    {
        std::printf("    %s\n", js.error.c_str());
        return;
    }
    CHECK(js.eval("phoenix.content(" + content + ");", "content"));
    CHECK(js.eval(translated.code, "movie.as.js"));
    CHECK(js.eval(R"JS(
phoenix.start({}).then(function (player) {
    player.pause();
    for (var i = 0; i < 12; ++i)
        player.tick();
    var root = phoenix.as2Face(player.root);
    var looped = root._currentframe;
    root.feedBtn.onRelease();
    var eating = root._currentframe;
    for (var i = 0; i < 9; ++i)
        player.tick();
    console.log("frames " + looped + " " + eating + " " + root._currentframe + " " + root.phxState);
});
)JS", "<test>"));
    if (!js.error.empty())
        std::printf("    %s\n", js.error.c_str());
    const std::string out = js.output();
    CHECK(out.find("frames 3 11 1 idle") != std::string::npos);
    if (out.find("frames 3 11 1 idle") == std::string::npos)
        std::printf("%s\n", out.c_str());
}
