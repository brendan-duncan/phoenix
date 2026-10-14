// The JSFL engine and object model, run on small documents through a host
// that keeps everything in memory.
#include "test_util.h"

#include "../src/data/bitmap_instance.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/drawing_style.h"
#include "../src/edit/new_document.h"
#include "../src/edit/selection.h"
#include "../src/edit/timeline_edit.h"
#include "../src/jsfl/engine.h"
#include "../src/jsfl/flfile.h"
#include "../src/parser/fla_parser.h"
#include "../src/writer/fla_writer.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace {

/// Documents in memory, output in a string, dialogs answered as the
/// command line answers them.
class TestHost : public jsfl::Host
{
public:
    struct State
    {
        std::unique_ptr<fla::FLADocument> document;
        fla::CommandStack history;
        fla::Selection selection;
        fla::Timeline* edited = nullptr;
        int frame = 0;
        int layer = 0;
        std::string path;
    };

    std::vector<std::unique_ptr<State>> states;
    fla::FLADocument* active = nullptr;
    std::string output;
    std::vector<std::string> alerts;
    std::string answer = "typed";
    bool cancelPrompt = false;
    fla::DrawingStyle style;

    fla::DrawingStyle* drawingStyle() override { return &style; }

    fla::FLADocument* add(fla::FLADocument* document, const std::string& path = std::string())
    {
        auto state = std::make_unique<State>();
        state->document.reset(document);
        state->edited = document->document->currentScene();
        state->path = path;
        states.push_back(std::move(state));
        active = document;
        return document;
    }

    State& state(fla::FLADocument* document)
    {
        for (auto& state : states)
        {
            if (state->document.get() == document)
                return *state;
        }
        throw std::runtime_error("not open");
    }

    void trace(const std::string& text) override { output += text + "\n"; }
    void clearOutput() override { output.clear(); }
    std::string outputText() override { return output; }
    void alert(const std::string& message) override { alerts.push_back(message); }
    bool confirm(const std::string&) override { return true; }
    bool prompt(const std::string&, const std::string& initial, std::string& reply) override
    {
        reply = answer.empty() ? initial : answer;
        return !cancelPrompt;
    }
    std::string configDirectory() override
    {
        return (std::filesystem::temp_directory_path() / "phoenix_jsfl_test_config").u8string();
    }
    std::string version() override { return "0.6.0"; }

    std::vector<fla::FLADocument*> documents() override
    {
        std::vector<fla::FLADocument*> list;
        for (auto& state : states)
            list.push_back(state->document.get());
        return list;
    }
    fla::FLADocument* activeDocument() override { return active; }
    void activate(fla::FLADocument* document) override { active = document; }
    fla::FLADocument* open(const std::string& path, std::string& error) override
    {
        for (auto& state : states)
        {
            if (state->path == path)
                return active = state->document.get();
        }
        FLAParser parser;
        fla::FLADocument* document = parser.parse(path);
        if (!document)
        {
            error = parser.errorString();
            return nullptr;
        }
        return add(document, path);
    }
    fla::FLADocument* create(std::string&) override { return add(fla::createEmptyDocument(550, 400, 24.0)); }
    bool save(fla::FLADocument* document, const std::string& path, bool copy, std::string& error) override
    {
        State& here = state(document);
        const std::string target = path.empty() ? here.path : path;
        fla::FLAWriter writer;
        if (!writer.write(*document->document, target))
        {
            error = writer.errorString();
            return false;
        }
        if (!copy)
        {
            here.path = target;
            here.history.setClean();
        }
        return true;
    }
    bool close(fla::FLADocument* document, bool) override
    {
        for (size_t i = 0; i < states.size(); ++i)
        {
            if (states[i]->document.get() == document)
            {
                states[i]->history.clear();
                states.erase(states.begin() + static_cast<std::ptrdiff_t>(i));
                active = states.empty() ? nullptr : states.back()->document.get();
                return true;
            }
        }
        return false;
    }
    std::string path(fla::FLADocument* document) override { return state(document).path; }
    std::string name(fla::FLADocument* document) override
    {
        const std::string& path = state(document).path;
        return path.empty() ? "Untitled" : std::filesystem::u8path(path).filename().u8string();
    }
    bool modified(fla::FLADocument* document) override { return !state(document).history.isClean(); }
    fla::CommandStack& history(fla::FLADocument* document) override { return state(document).history; }
    fla::Selection& selection(fla::FLADocument* document) override { return state(document).selection; }
    fla::Timeline* editedTimeline(fla::FLADocument* document) override { return state(document).edited; }
    void editTimeline(fla::FLADocument* document, fla::Timeline* timeline) override { state(document).edited = timeline; }
    int currentFrame(fla::FLADocument* document) override { return state(document).frame; }
    void setCurrentFrame(fla::FLADocument* document, int frame) override { state(document).frame = frame; }
    int currentLayer(fla::FLADocument* document) override { return state(document).layer; }
    void setCurrentLayer(fla::FLADocument* document, int layer) override { state(document).layer = layer; }
    /// Every element is a 10 by 10 square from its registration point.
    jsfl::Rect bounds(fla::FLADocument*, const fla::Element* element) override
    {
        const fla::Transform& m = element->transform;
        double xs[4];
        double ys[4];
        const double cx[4] = {0, 10, 10, 0};
        const double cy[4] = {0, 0, 10, 10};
        for (int i = 0; i < 4; ++i)
        {
            xs[i] = m.m11 * cx[i] + m.m21 * cy[i] + m.tx;
            ys[i] = m.m12 * cx[i] + m.m22 * cy[i] + m.ty;
        }
        return {*std::min_element(xs, xs + 4), *std::min_element(ys, ys + 4), *std::max_element(xs, xs + 4),
            *std::max_element(ys, ys + 4)};
    }
    bool exportPNG(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "no drawing in the tests";
        return false;
    }
    /// Every bitmap is 4 by 2 pixels, red on the left and blue on the right.
    bool bitmapPixels(fla::FLADocument*, const std::string&, int& width, int& height, std::vector<uint8_t>& rgba) override
    {
        width = 4;
        height = 2;
        rgba.clear();
        for (int i = 0; i < width * height; ++i)
        {
            const bool red = i % width < 2;
            rgba.insert(rgba.end(), {static_cast<uint8_t>(red ? 255 : 0), 0, static_cast<uint8_t>(red ? 0 : 255), 255});
        }
        return true;
    }
};

/// A host with one new document: a scene of one layer of 1 frame.
struct Fixture
{
    TestHost host;
    jsfl::Engine engine{host};
    fla::FLADocument* document;
    Fixture() { document = host.add(fla::createEmptyDocument(550, 400, 24.0)); }
    fla::Document& data() { return *document->document; }
    fla::Timeline& scene() { return *data().currentScene(); }
    jsfl::RunResult run(const std::string& source, const std::string& undo = "Test Script", bool mayFail = false)
    {
        jsfl::RunOptions options;
        options.undoName = undo;
        options.timeLimit = 20.0;
        const jsfl::RunResult result = engine.runString(source, "test.jsfl", options);
        if (!result.ok && !mayFail)
            std::printf("    script error: %s\n", result.report().c_str());
        return result;
    }
};

/// Checks the output, showing it when it differs.
#define CHECK_OUTPUT(f, expected)                                                                  \
    do                                                                                             \
    {                                                                                              \
        const std::string wanted = (expected);                                                     \
        CHECK((f).host.output == wanted);                                                          \
        if ((f).host.output != wanted)                                                             \
            std::printf("    got:\n%s    wanted:\n%s", (f).host.output.c_str(), wanted.c_str());   \
    } while (0)

std::string scratchFolder(const char* name)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_jsfl_tests" / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder.u8string();
}

} // namespace

TEST(jsfl_runs_a_script_and_traces)
{
    Fixture f;
    CHECK(f.run("fl.trace('hello ' + (6 * 7)); fl.outputPanel.trace('panel');").ok);
    CHECK_OUTPUT(f, "hello 42\npanel\n");
    CHECK(f.run("fl.outputPanel.clear(); fl.trace([1, 2, 3]);").ok);
    CHECK_OUTPUT(f, "1,2,3\n");
}

TEST(jsfl_dialogs_answer_without_blocking)
{
    Fixture f;
    f.host.answer.clear();
    CHECK(f.run("alert('hi'); fl.trace(confirm('sure?')); fl.trace(prompt('name?', 'Bob'));").ok);
    CHECK(f.host.alerts.size() == 1 && f.host.alerts[0] == "hi");
    CHECK_OUTPUT(f, "true\nBob\n");
    f.host.output.clear();
    f.host.cancelPrompt = true;
    CHECK(f.run("fl.trace(prompt('name?'));").ok);
    CHECK_OUTPUT(f, "null\n");
}

TEST(jsfl_errors_name_the_line)
{
    Fixture f;
    const jsfl::RunResult thrown = f.engine.runString("var a = 1;\n\nnull.x;\n", "broken.jsfl");
    CHECK(!thrown.ok);
    CHECK(thrown.file == "broken.jsfl");
    CHECK(thrown.line == 3);
    CHECK(thrown.message.find("TypeError") != std::string::npos);

    const jsfl::RunResult syntax = f.engine.runString("var a = 1;\nvar = ;\n", "syntax.jsfl");
    CHECK(!syntax.ok);
    CHECK(syntax.line == 2);
    CHECK(syntax.message.find("SyntaxError") != std::string::npos);

    // An error from Phoenix's side points at the script's line too.
    const jsfl::RunResult model = f.engine.runString("\nfl.getDocumentDOM().getTimeline().deleteLayer(7);\n", "model.jsfl");
    CHECK(!model.ok);
    CHECK(model.line == 2);
    CHECK(model.message.find("no layer 7") != std::string::npos);
    CHECK(model.report() == "model.jsfl:2: " + model.message);
}

TEST(jsfl_scripts_can_be_stopped)
{
    Fixture f;
    jsfl::RunOptions options;
    options.timeLimit = 0.3;
    const jsfl::RunResult slow = f.engine.runString("while (true) {}", "loop.jsfl", options);
    CHECK(!slow.ok);
    CHECK(slow.interrupted);

    // try/catch in the script cannot hold off a stop.
    jsfl::RunOptions stopping;
    int polls = 0;
    stopping.poll = [&]() {
        if (++polls == 3)
            f.engine.stop();
    };
    const jsfl::RunResult stopped = f.engine.runString("for (;;) { try { while (true) {} } catch (e) {} }", "loop.jsfl", stopping);
    CHECK(!stopped.ok);
    CHECK(stopped.interrupted);
    CHECK(stopped.message == "The script was stopped.");
    CHECK(f.run("fl.trace('again');").ok);
}

TEST(jsfl_memory_is_limited)
{
    Fixture f;
    jsfl::RunOptions options;
    options.memoryLimit = 16 * 1024 * 1024;
    const jsfl::RunResult result =
        f.engine.runString("var a = []; while (true) a.push(new Array(100000).join('x') + a.length);", "hog.jsfl", options);
    CHECK(!result.ok);
    CHECK(result.interrupted);
    CHECK(result.message.find("memory") != std::string::npos);
}

TEST(jsfl_a_run_is_one_undo_step)
{
    Fixture f;
    const size_t before = f.host.history(f.document).count();
    CHECK(f.run("var tl = fl.getDocumentDOM().getTimeline();\n"
                "tl.addNewLayer('A'); tl.addNewLayer('B'); tl.layers[0].name = 'Top';\n"
                "tl.insertFrames(9, true, 0);",
        "Make Layers")
              .ok);
    fla::CommandStack& history = f.host.history(f.document);
    CHECK(history.count() == before + 1);
    CHECK(history.undoName() == "Make Layers");
    CHECK(f.scene().layers.size() == 3);
    CHECK(f.scene().layers[0]->name == "Top");
    CHECK(fla::timeline::layerEnd(*f.scene().layers[2]) == 10);
    history.undo();
    CHECK(f.scene().layers.size() == 1);
    CHECK(fla::timeline::layerEnd(*f.scene().layers[0]) == 1);

    // A script that changes nothing leaves no step.
    CHECK(f.run("fl.trace(fl.getDocumentDOM().width);").ok);
    CHECK(history.count() == before + 1);
    CHECK(!history.canUndo() || history.undoName() != "Test Script");

    // A failed script keeps what it did before failing, as one step.
    const jsfl::RunResult failed = f.run("fl.getDocumentDOM().getTimeline().addNewLayer('X'); null.y;", "Half", true);
    CHECK(!failed.ok);
    CHECK(history.undoName() == "Half");
    CHECK(!history.isInMacro());
}

TEST(jsfl_documents)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "fl.trace(d.width + 'x' + d.height + ' ' + d.frameRate + ' ' + d.backgroundColor + ' ' + d.name);\n"
                "d.width = 640; d.frameRate = 30; d.backgroundColor = '#336699';\n"
                "fl.trace(fl.documents.length + ' ' + (fl.documents[0] == d) + ' ' + d.timelines.length + ' ' + d.currentTimeline);\n"
                "fl.trace(d.path === undefined);")
              .ok);
    CHECK_OUTPUT(f, "550x400 24 #FFFFFF Untitled\n1 true 1 0\ntrue\n");
    CHECK(f.data().width == 640);
    CHECK(f.data().frameRate == 30.0);
    CHECK(f.data().backgroundColor[0] == 0x33 && f.data().backgroundColor[2] == 0x99);

    // No document: null, not an error.
    TestHost empty;
    jsfl::Engine engine(empty);
    CHECK(engine.runString("fl.trace(fl.getDocumentDOM() === null);", "none.jsfl").ok);
    CHECK(empty.output == "true\n");
}

TEST(jsfl_document_files)
{
    Fixture f;
    const std::string folder = scratchFolder("documents");
    const std::string uri = jsfl::uri::fromPath(folder + "/made.fla");
    CHECK(f.run("var d = fl.createDocument(); d.getTimeline().addNewLayer('Made');\n"
                "d.addNewRectangle({left: 0, top: 0, right: 40, bottom: 20}, 0);\n"
                "fl.saveDocument(d, '" + uri + "');\n"
                "fl.trace(d.name + ' ' + (d.pathURI == '" + uri + "'));\n"
                "fl.closeDocument(d, false);\n"
                "var again = fl.openDocument('" + uri + "');\n"
                "fl.trace(again.getTimeline().layers[0].name + ' ' + again.getTimeline().layers[0].frames[0].elements.length);\n"
                "fl.trace(fl.documents.length);")
              .ok);
    CHECK_OUTPUT(f, "made.fla true\nMade 1\n2\n");
}

TEST(jsfl_stale_objects_fail_clearly)
{
    Fixture f;
    const jsfl::RunResult result = f.run("var tl = fl.getDocumentDOM().getTimeline();\n"
                                         "tl.addNewLayer('Doomed');\n"
                                         "var layer = tl.layers[0];\n"
                                         "tl.deleteLayer(0);\n"
                                         "layer.name;\n",
        "Test Script", true);
    CHECK(!result.ok);
    CHECK(result.line == 5);
    CHECK(result.message.find("no longer in the document") != std::string::npos);

    const jsfl::RunResult closed = f.run("var d = fl.getDocumentDOM(); fl.closeDocument(d, false); d.width;", "Test Script", true);
    CHECK(!closed.ok);
    CHECK(closed.message.find("closed") != std::string::npos);
}

TEST(jsfl_timeline_layers_and_frames)
{
    Fixture f;
    CHECK(f.run("var tl = fl.getDocumentDOM().getTimeline();\n"
                "var i = tl.addNewLayer('Guide', 'guide');\n"
                "fl.trace(i + ' ' + tl.layerCount + ' ' + tl.currentLayer + ' ' + tl.layers[0].layerType);\n"
                "tl.currentLayer = 1;\n"
                "tl.insertFrames(11);\n"
                "tl.insertKeyframe(5); tl.insertBlankKeyframe(8);\n"
                "var layer = tl.layers[1];\n"
                "fl.trace(tl.frameCount + ' ' + layer.frameCount + ' ' + layer.frames.length);\n"
                "fl.trace(layer.frames[6].startFrame + ' ' + layer.frames[6].duration + ' ' + (layer.frames[3].startFrame === layer.frames[4].startFrame));\n"
                "layer.frames[5].name = 'walk'; layer.frames[5].labelType = 'comment';\n"
                "tl.setSelectedFrames(0, 1);\n"
                "tl.createMotionTween();\n"
                "fl.trace(layer.frames[0].tweenType + ' ' + layer.frames[5].labelType + ' ' + tl.getSelectedFrames());\n"
                "tl.setFrameProperty('tweenEasing', -50, 0, 1);\n"
                "fl.trace(layer.frames[0].tweenEasing);\n"
                "tl.removeFrames(10, 12);\n"
                "fl.trace(layer.frameCount + ' ' + tl.findLayerIndex('Guide'));\n"
                "tl.setLayerProperty('locked', true, 'all');\n"
                "fl.trace(tl.layers[0].locked + ' ' + tl.layers[1].locked);\n"
                "layer.color = '#FF0000'; layer.visible = false;\n"
                "fl.trace(layer.color + ' ' + layer.visible);\n"
                "tl.convertToKeyframes(1, 3); tl.clearKeyframes(1, 2);\n"
                "fl.trace(layer.frames[2].startFrame);\n"
                "tl.copyFrames(0, 1); tl.pasteFrames(8, 9);\n"
                "fl.trace(layer.frames[8].tweenType);\n")
              .ok);
    CHECK_OUTPUT(f, "0 2 0 guide\n"
                           "12 12 12\n"
                           "5 3 true\n"
                           "motion comment 1,0,1\n"
                           "-50\n"
                           "10 0\n"
                           "true true\n"
                           "#FF0000 false\n"
                           "2\n"
                           "motion\n");
    fla::Layer* layer = f.scene().layers[1];
    CHECK(layer->frames[0]->tweenType == fla::TweenType::Motion);
}

TEST(jsfl_elements_and_selection)
{
    Fixture f;
    // A symbol with an instance on the stage, and a text.
    fla::Document& data = f.data();
    data.symbolList = data.symbolList ? data.symbolList : new fla::SymbolList(&data);
    auto* ball = new fla::Symbol(data.symbolList);
    ball->name = "Ball";
    ball->symbolType = fla::SymbolType::MovieClip;
    auto* inner = new fla::Timeline(ball);
    auto* innerLayer = new fla::Layer(inner);
    innerLayer->frames.push_back(new fla::Frame(innerLayer));
    inner->layers.push_back(innerLayer);
    ball->timelines.push_back(inner);
    data.symbolList->symbols.push_back(ball);
    data.symbolList->symbolMap["Ball"] = ball;
    fla::Frame* frame = f.scene().layers[0]->frames[0];
    auto* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = "Ball";
    instance->symbol = ball;
    instance->symbolType = fla::SymbolType::MovieClip;
    instance->transform.tx = 100;
    instance->transform.ty = 50;
    frame->elements.push_back(instance);
    auto* text = new fla::StaticText(frame);
    fla::TextRun run;
    run.text = "Hello";
    run.face = "Arial";
    run.size = 12;
    text->runs.push_back(run);
    frame->elements.push_back(text);

    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "var els = d.getTimeline().layers[0].frames[0].elements;\n"
                "var mc = els[0], t = els[1];\n"
                "fl.trace(mc.elementType + ' ' + mc.instanceType + ' ' + mc.symbolType + ' ' + mc.libraryItem.name + ' ' + mc.x + ',' + mc.y);\n"
                "mc.x = 120; mc.rotation = 90; mc.scaleX = 2; mc.name = 'ball_mc';\n"
                "fl.trace(Math.round(mc.rotation) + ' ' + mc.scaleX.toFixed(3) + ' ' + mc.name + ' ' + mc.matrix.tx);\n"
                "mc.colorAlphaPercent = 50; mc.loop = 'single frame'; mc.firstFrame = 3;\n"
                "fl.trace(mc.colorMode + ' ' + mc.colorAlphaPercent + ' ' + mc.loop + ' ' + mc.firstFrame);\n"
                "mc.filters = [{name: 'blurFilter', blurX: 4, blurY: 6, quality: 'high'}];\n"
                "fl.trace(mc.filters.length + ' ' + mc.filters[0].name + ' ' + mc.filters[0].blurY + ' ' + mc.filters[0].quality);\n"
                "fl.trace(t.elementType + ' ' + t.textType + ' ' + t.getTextString() + ' ' + t.getTextString(1, 3));\n"
                "t.setTextString('J', 0, 1); t.setTextAttr('size', 20);\n"
                "fl.trace(t.getTextString() + ' ' + t.getTextAttr('size') + ' ' + t.textRuns[0].textAttrs.face);\n"
                "d.selection = [mc];\n"
                "fl.trace(d.selection.length + ' ' + mc.selected);\n"
                "d.moveSelectionBy({x: 5, y: -5});\n"
                "fl.trace(mc.x + ',' + mc.y);\n"
                "d.selectAll(); fl.trace(d.selection.length);\n"
                "d.arrange('back'); fl.trace(els[0].depth + ' ' + d.getTimeline().layers[0].frames[0].elements[0].elementType);\n"
                "d.selectNone(); fl.trace(d.selection.length);\n")
              .ok);
    CHECK_OUTPUT(f, "instance symbol movie clip Ball 100,50\n"
                           "90 2.000 ball_mc 120\n"
                           "alpha 50 single frame 3\n"
                           "1 blurFilter 6 high\n"
                           "text static Hello el\n"
                           "Jello 20 Arial\n"
                           "1 true\n"
                           "125,45\n"
                           "2\n"
                           "0 instance\n"
                           "0\n");
    CHECK(instance->name == "ball_mc");
    CHECK(instance->colorTransform.alphaMultiplier == 0.5);
    CHECK(instance->filters.size() == 1 && instance->filters[0].blurY == 6.0);
    CHECK(text->runs[0].text == "Jello");
    // All of it undoes together.
    f.host.history(f.document).undo();
    CHECK(instance->transform.tx == 100);
    CHECK(instance->name.empty());
    CHECK(text->runs[0].text == "Hello");
    CHECK(frame->elements[0] == instance);
}

TEST(jsfl_document_edits)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "d.addNewRectangle({left: 0, top: 0, right: 100, bottom: 50}, 0);\n"
                "var tl = d.getTimeline();\n"
                "fl.trace(tl.layers[0].frames[0].elements.length + ' ' + tl.layers[0].frames[0].elements[0].elementType);\n"
                "d.selectAll();\n"
                "var sym = d.convertToSymbol('graphic', 'Box', 'top left');\n"
                "fl.trace(sym.name + ' ' + sym.itemType + ' ' + d.library.itemExists('Box') + ' ' + d.selection[0].libraryItem.name);\n"
                "d.duplicateSelection(); d.selectAll(); fl.trace(d.selection.length);\n"
                "d.group(); fl.trace(d.selection.length + ' ' + tl.layers[0].frames[0].elements[0].isGroup);\n"
                "d.selectAll(); d.unGroup(); d.selectAll(); fl.trace(d.selection.length);\n"
                "d.align('left'); fl.trace(d.selection[0].x == d.selection[1].x);\n"
                "d.deleteSelection(); fl.trace(tl.layers[0].frames[0].elements.length);\n")
              .ok);
    CHECK_OUTPUT(f, "1 shape\nBox graphic true Box\n2\n1 true\n2\ntrue\n0\n");
    // The symbol stays in the library; undo takes it all back.
    f.host.history(f.document).undo();
    CHECK(f.scene().layers[0]->frames[0]->elements.empty());
    CHECK(!f.data().symbolList || f.data().symbolList->symbolMap.count("Box") == 0);
}

TEST(jsfl_trace_bitmap)
{
    Fixture f;
    fla::Frame* frame = f.scene().layers[0]->frames[0];
    fla::BitmapInstance* picture = new fla::BitmapInstance(frame);
    picture->libraryItemName = "picture.png";
    frame->elements.push_back(picture);
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "d.selectAll(); d.traceBitmap(100, 1, 'pixels');\n"
                "var e = d.getTimeline().layers[0].frames[0].elements;\n"
                "fl.trace(e.length + ' ' + e[0].elementType + ' ' + d.selection.length);\n"
                "try { d.traceBitmap(100, 1, 'wobbly'); } catch (x) { fl.trace('refused'); }\n")
              .ok);
    // Two areas, red and blue, as one shape in the bitmap's place.
    CHECK_OUTPUT(f, "1 shape 1\nrefused\n");
    const auto* shape = dynamic_cast<const fla::Shape*>(frame->elements[0]);
    CHECK(shape && shape->fills.size() == 2);
    f.host.history(f.document).undo();
    CHECK(frame->elements.size() == 1 && frame->elements[0]->elementType() == fla::Element::Type::BitmapInstance);
}

TEST(jsfl_clipboard)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM(); d.addNewRectangle({left: 0, top: 0, right: 10, bottom: 10}, 0);\n"
                "d.selectAll(); d.clipCopy(); d.getTimeline().addNewLayer('Copy'); d.clipPaste(true);\n"
                "var tl = d.getTimeline();\n"
                "fl.trace(tl.layers[0].name + ' ' + tl.layers[0].frames[0].elements.length + ' ' + d.selection.length);\n"
                "d.clipCut(); fl.trace(tl.layers[0].frames[0].elements.length);\n")
              .ok);
    CHECK_OUTPUT(f, "Copy 1 1\n0\n");
}

TEST(jsfl_toolbar_style_and_text)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "d.setFillColor('#ff0000'); d.setStrokeSize(3);\n"
                "var fill = d.getCustomFill('toolbar'), stroke = d.getCustomStroke();\n"
                "fl.trace(fill.style + ' ' + fill.color + ' ' + stroke.thickness);\n"
                "d.swapStrokeAndFill(); fl.trace(d.getCustomFill().color);\n"
                "d.addNewText({left: 10, top: 20, right: 110, bottom: 40}, 'Hi');\n"
                "var t = d.selection[0]; fl.trace(t.elementType + ' ' + t.getTextString() + ' ' + t.x);\n"
                "var trace = fl.trace; trace('detached');\n")
              .ok);
    CHECK_OUTPUT(f, "solid #FF0000 3\n#000000\ntext Hi 10\ndetached\n");
}

TEST(jsfl_library)
{
    Fixture f;
    CHECK(f.run("var lib = fl.getDocumentDOM().library;\n"
                "lib.addNewItem('movie clip', 'Hero');\n"
                "lib.newFolder('Characters');\n"
                "fl.trace(lib.items.length + ' ' + lib.findItemIndex('Hero') + ' [' + lib.findItemIndex('Nobody') + ']');\n"
                "lib.moveToFolder('Characters', 'Hero');\n"
                "fl.trace(lib.itemExists('Characters/Hero') + ' ' + lib.itemExists('Hero'));\n"
                "var hero = lib.items[lib.findItemIndex('Characters/Hero')];\n"
                "hero.linkageExportForAS = true; hero.linkageClassName = 'Hero';\n"
                "fl.trace(hero.itemType + ' ' + hero.linkageClassName + ' ' + hero.linkageExportForAS + ' ' + hero.timeline.layerCount);\n"
                "lib.selectItem('Characters/Hero'); lib.duplicateItem();\n"
                "fl.trace(lib.getSelectedItems()[0].name);\n"
                "lib.selectItem('Characters/Hero'); lib.renameItem('Villain');\n"
                "fl.trace(lib.itemExists('Characters/Villain') + ' ' + hero.name);\n"
                "lib.addItemToDocument({x: 50, y: 60}, 'Characters/Villain');\n"
                "var placed = fl.getDocumentDOM().selection[0];\n"
                "fl.trace(placed.libraryItem.name + ' ' + placed.x + ',' + placed.y);\n"
                "fl.trace(lib.editItem('Characters/Villain') + ' ' + fl.getDocumentDOM().getTimeline().name);\n"
                "fl.getDocumentDOM().exitEditMode();\n"
                "lib.deleteItem('Characters/Hero copy');\n"
                "fl.trace(lib.unusedItems().length + ' ' + lib.items.length);\n")
              .ok);
    CHECK_OUTPUT(f, "2 1 []\n"
                           "true false\n"
                           "movie clip Hero true 1\n"
                           "Characters/Hero copy\n"
                           "true Characters/Villain\n"
                           "Characters/Villain 45,55\n"
                           "true Villain\n"
                           "0 2\n");
}

TEST(jsfl_flfile)
{
    Fixture f;
    const std::string folder = scratchFolder("flfile");
    const std::string base = jsfl::uri::fromPath(folder);
    CHECK(base.compare(0, 8, "file:///") == 0);
    CHECK(f.run("var base = '" + base + "/';\n"
                "fl.trace(FLfile.createFolder(base + 'sub') + ' ' + FLfile.exists(base + 'sub'));\n"
                "FLfile.write(base + 'a.txt', 'one'); FLfile.write(base + 'a.txt', ' two', 'append');\n"
                "fl.trace(FLfile.read(base + 'a.txt') + ' ' + FLfile.getSize(base + 'a.txt'));\n"
                "fl.trace(FLfile.copy(base + 'a.txt', base + 'sub/b.txt') + ' ' + FLfile.copy(base + 'a.txt', base + 'sub/b.txt'));\n"
                "fl.trace(FLfile.listFolder(base) + ' | ' + FLfile.listFolder(base + '*.txt', 'files') + ' | ' + FLfile.listFolder(base, 'directories'));\n"
                "fl.trace(FLfile.getAttributes(base + 'sub').indexOf('D') >= 0);\n"
                "fl.trace(FLfile.remove(base + 'sub') + ' ' + FLfile.exists(base + 'sub/b.txt') + ' ' + FLfile.read(base + 'nothing.txt'));\n"
                "fl.trace(FLfile.platformPathToURI(FLfile.uriToPlatformPath(base + 'a.txt')) == base + 'a.txt');\n")
              .ok);
    CHECK_OUTPUT(f, "true true\none two 7\ntrue false\na.txt,sub | a.txt | sub\ntrue\ntrue false null\ntrue\n");
#ifdef _WIN32
    CHECK(jsfl::uri::toPath("file:///C|/Work/a%20b.fla") == "C:/Work/a b.fla");
    CHECK(jsfl::uri::toPath("file:///C:/Work/a b.fla") == "C:/Work/a b.fla");
    CHECK(jsfl::uri::fromPath("C:\\Work\\a b.fla") == "file:///C|/Work/a%20b.fla");
#else
    CHECK(jsfl::uri::toPath("file:///Users/me/a%20b.fla") == "/Users/me/a b.fla");
    CHECK(jsfl::uri::fromPath("/Users/me/a b.fla") == "file:///Users/me/a%20b.fla");
#endif
}

TEST(jsfl_run_script_includes_share_the_global)
{
    Fixture f;
    const std::string folder = scratchFolder("include");
    {
        std::ofstream lib(std::filesystem::u8path(folder + "/lib.jsfl"));
        lib << "var libraryLoaded = true;\nfunction double(x) { return x * 2; }\n"
               "function where() { return fl.scriptURI.replace(/^.*\\//, ''); }\nfl.trace('in ' + where());\n";
    }
    {
        std::ofstream main(std::filesystem::u8path(folder + "/main.jsfl"));
        main << "fl.runScript(fl.scriptURI.replace(/[^\\/]*$/, '') + 'lib.jsfl');\n"
                "fl.trace(libraryLoaded + ' ' + double(21) + ' ' + fl.scriptURI.replace(/^.*\\//, ''));\n"
                "fl.trace(fl.runScript(fl.scriptURI.replace(/[^\\/]*$/, '') + 'lib.jsfl', 'double', 5));\n"
                "var o = {n: 1}; function bump(x) { x.n++; return x; }\n"
                "fl.trace(fl.runScript(fl.scriptURI.replace(/[^\\/]*$/, '') + 'lib.jsfl', 'where') + ' ' + o.n);\n";
    }
    const jsfl::RunResult result = f.engine.runFile(folder + "/main.jsfl");
    CHECK(result.ok);
    if (!result.ok)
        std::printf("    %s\n", result.report().c_str());
    CHECK_OUTPUT(f, "in lib.jsfl\ntrue 42 main.jsfl\nin lib.jsfl\n10\nin lib.jsfl\nlib.jsfl 1\n");
}

TEST(jsfl_run_script_survives_classes_first_made_inside_it)
{
    // An included script that is the first to reach the document, timeline,
    // layers, frames and library makes their prototypes while runScript is
    // still running.
    Fixture f;
    const std::string folder = scratchFolder("include_first_use");
    {
        std::ofstream lib(std::filesystem::u8path(folder + "/lib.jsfl"));
        lib << "var d = fl.getDocumentDOM(), tl = d.getTimeline();\n"
               "var seen = tl.layers[0].frames.length + ' ' + d.library.items.length;\n";
    }
    {
        std::ofstream main(std::filesystem::u8path(folder + "/main.jsfl"));
        main << "fl.runScript(fl.scriptURI.replace(/[^\\/]*$/, '') + 'lib.jsfl');\n"
                "fl.trace(seen + ' ' + fl.scriptURI.replace(/^.*\\//, ''));\n";
    }
    const jsfl::RunResult result = f.engine.runFile(folder + "/main.jsfl");
    CHECK(result.ok);
    if (!result.ok)
        std::printf("    %s\n", result.report().c_str());
    CHECK_OUTPUT(f, "1 0 main.jsfl\n");
}

TEST(jsfl_corpus_clear_extra_blank_keyframes)
{
    // A published command (tests/data/jsfl/SOURCES.txt): blank keyframes
    // right after blank keyframes go.
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM(), tl = d.getTimeline();\n"
                "d.addNewRectangle({left: 0, top: 0, right: 10, bottom: 10}, 0);\n"
                "tl.insertFrames(11); tl.insertBlankKeyframe(4); tl.insertBlankKeyframe(8);\n")
              .ok);
    CHECK(f.scene().layers[0]->frames.size() == 3);
    const jsfl::RunResult result = f.engine.runFile(std::string(PHOENIX_TEST_DATA) + "/jsfl/clear_extra_blank_keyframes.jsfl");
    CHECK(result.ok);
    if (!result.ok)
        std::printf("    %s\n", result.report().c_str());
    CHECK(f.host.alerts.size() == 1 && f.host.alerts[0] == "Cleared 1 extra blank keyframe");
    CHECK(f.scene().layers[0]->frames.size() == 2);
    CHECK(f.scene().layers[0]->visible);
}

TEST(jsfl_corpus_setters_without_a_parameter)
{
    // Flash2Svg's wrappers declare setters with no parameter, which only the
    // old engine took.
    std::string source;
    {
        std::ifstream in(std::filesystem::u8path(std::string(PHOENIX_TEST_DATA) + "/jsfl/flash2svg_vertex.jsfl"));
        std::ostringstream text;
        text << in.rdbuf();
        source = text.str();
    }
    CHECK(!source.empty());
    Fixture f;
    const std::string stub = "var extensible = {Object: function () { this.extend = function () {}; }, Point: function () {},\n"
                             "    extend: function (o) { this.Vertex = o.Vertex; }};\n"
                             "function Vertex() {} function Shape() {}\n";
    CHECK(f.run(stub + source + ";\nvar v = new extensible.Vertex(); fl.trace(typeof v.setLocation);").ok);
    CHECK_OUTPUT(f, "function\n");
}

TEST(jsfl_corpus_compiles)
{
    // Opt in: PHOENIX_JSFL_CORPUS names a folder of .jsfl files, each
    // compiled (never run) after the old-dialect rewrite. Lists the ones
    // that do not compile.
    const char* folder = std::getenv("PHOENIX_JSFL_CORPUS");
    if (!folder || !*folder)
    {
        std::printf("    skipped: set PHOENIX_JSFL_CORPUS to a folder of .jsfl files\n");
        return;
    }
    TestHost host;
    jsfl::Engine engine(host);
    jsfl::RunOptions options;
    options.compileOnly = true;
    int total = 0;
    int failed = 0;
    std::map<std::string, int> reasons;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(folder)))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsfl")
            continue;
        ++total;
        const jsfl::RunResult result = engine.runFile(entry.path().u8string(), options);
        if (!result.ok)
        {
            ++failed;
            ++reasons[result.message.substr(0, 60)];
            if (std::getenv("PHOENIX_JSFL_CORPUS_VERBOSE"))
                std::printf("    %s\n", result.report().c_str());
        }
    }
    std::printf("    %d scripts, %d do not compile\n", total, failed);
    for (const auto& reason : reasons)
        std::printf("    %4d  %s\n", reason.second, reason.first.c_str());
}

TEST(jsfl_corpus_runs)
{
    // Opt in: runs each .jsfl under PHOENIX_JSFL_CORPUS that neither touches
    // files nor includes others, on PHOENIX_JSFL_DOCUMENT, and tallies the
    // errors they stop on: what the object model still lacks.
    const char* folder = std::getenv("PHOENIX_JSFL_CORPUS");
    const char* document = std::getenv("PHOENIX_JSFL_DOCUMENT");
    if (!folder || !*folder || !document || !*document)
    {
        std::printf("    skipped: set PHOENIX_JSFL_CORPUS and PHOENIX_JSFL_DOCUMENT\n");
        return;
    }
    int total = 0;
    int failed = 0;
    std::map<std::string, int> reasons;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(folder)))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsfl")
            continue;
        std::ifstream in(entry.path());
        std::ostringstream text;
        text << in.rdbuf();
        const std::string source = text.str();
        if (source.find("FLfile") != std::string::npos || source.find("runScript") != std::string::npos ||
            source.find("getDocumentDOM") == std::string::npos)
            continue;
        TestHost host;
        std::string error;
        if (!host.open(document, error))
        {
            std::printf("    could not open %s\n", document);
            return;
        }
        jsfl::Engine engine(host);
        jsfl::RunOptions options;
        options.timeLimit = 5.0;
        ++total;
        const jsfl::RunResult result = engine.runString(source, entry.path().u8string(), options);
        if (!result.ok)
        {
            ++failed;
            ++reasons[result.message.substr(0, 70)];
            if (std::getenv("PHOENIX_JSFL_CORPUS_VERBOSE"))
                std::printf("    %s\n", result.report().c_str());
        }
    }
    std::printf("    %d scripts run, %d stopped on an error\n", total, failed);
    for (const auto& reason : reasons)
        std::printf("    %4d  %s\n", reason.second, reason.first.c_str());
}

TEST(jsfl_old_dialect)
{
    // for each loops, setters without a parameter, toSource.
    const std::string modern = jsfl::Engine::modernize("for each (var x in list) f(x);\n// for each (a in b)\nvar s = 'for each (a in b)';\n"
                                                      "var o = { set x() {} };\nr = a / b; q = /for each/g;");
    const std::string wanted = "for (var x of __jsflEach( list)) f(x);\n// for each (a in b)\nvar s = 'for each (a in b)';\n"
                               "var o = { set x(__jsflValue) {} };\nr = a / b; q = /for each/g;";
    CHECK(modern == wanted);
    if (modern != wanted)
        std::printf("    got:\n%s\n", modern.c_str());
    Fixture f;
    CHECK(f.run("var total = 0; for each (var n in [1, 2, 3]) total += n;\n"
                "for each (var v in {a: 'x', b: 'y'}) total += v;\n"
                "var o = { get v() { return 7; }, set v() {} };\n"
                "fl.trace(total + ' ' + o.v + ' ' + ({a: 1}).toSource() + ' ' + uneval([1]));\n"
                "fl.trace(fl.Math.concatMatrix({a: 2, b: 0, c: 0, d: 2, tx: 1, ty: 1}, {a: 1, b: 0, c: 0, d: 1, tx: 5, ty: 0}).tx);\n")
              .ok);
    CHECK_OUTPUT(f, "6xy 7 {\"a\":1} [1]\n6\n");

    // "class" as a name, and a return at the top level.
    f.host.output.clear();
    CHECK(f.run("var class = 'c'; function make(class) { return class + 1; }\nvar o = {class: 2};\n"
                "fl.trace(class + make(class) + o.class);\nif (true) return;\nfl.trace('not reached');")
              .ok);
    CHECK_OUTPUT(f, "cc12\n");
}
