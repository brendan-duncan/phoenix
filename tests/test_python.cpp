// Python scripts over the JSFL object model, run on small documents through
// a host that keeps everything in memory. Each test skips itself when no
// Python is installed.
#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/new_document.h"
#include "../src/edit/selection.h"
#include "../src/edit/timeline_edit.h"
#include "../src/python/python_engine.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

/// One document in memory, output in a string, dialogs answered at once.
class PythonHost : public jsfl::Host
{
public:
    std::unique_ptr<fla::FLADocument> document{fla::createEmptyDocument(550, 400, 24.0)};
    fla::CommandStack commands;
    fla::Selection chosen;
    fla::Timeline* edited = document->document->currentScene();
    int frame = 0;
    int layer = 0;
    std::string output;
    std::vector<std::string> alerts;

    void trace(const std::string& text) override { output += text + "\n"; }
    void alert(const std::string& message) override { alerts.push_back(message); }
    bool confirm(const std::string&) override { return true; }
    bool prompt(const std::string&, const std::string& initial, std::string& answer) override
    {
        answer = initial.empty() ? "typed" : initial;
        return true;
    }
    std::string configDirectory() override
    {
        return (std::filesystem::temp_directory_path() / "phoenix_python_test_config").u8string();
    }
    std::vector<fla::FLADocument*> documents() override { return {document.get()}; }
    fla::FLADocument* activeDocument() override { return document.get(); }
    void activate(fla::FLADocument*) override {}
    fla::FLADocument* open(const std::string&, std::string& error) override
    {
        error = "no opening in the tests";
        return nullptr;
    }
    fla::FLADocument* create(std::string& error) override
    {
        error = "no new documents in the tests";
        return nullptr;
    }
    bool save(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "no saving in the tests";
        return false;
    }
    bool close(fla::FLADocument*, bool) override { return false; }
    std::string path(fla::FLADocument*) override { return {}; }
    std::string name(fla::FLADocument*) override { return "Untitled"; }
    bool modified(fla::FLADocument*) override { return !commands.isClean(); }
    fla::CommandStack& history(fla::FLADocument*) override { return commands; }
    fla::Selection& selection(fla::FLADocument*) override { return chosen; }
    fla::Timeline* editedTimeline(fla::FLADocument*) override { return edited; }
    void editTimeline(fla::FLADocument*, fla::Timeline* timeline) override { edited = timeline; }
    int currentFrame(fla::FLADocument*) override { return frame; }
    void setCurrentFrame(fla::FLADocument*, int value) override { frame = value; }
    int currentLayer(fla::FLADocument*) override { return layer; }
    void setCurrentLayer(fla::FLADocument*, int value) override { layer = value; }
    jsfl::Rect bounds(fla::FLADocument*, const fla::Element*) override { return {0, 0, 10, 10}; }
    bool exportPNG(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "no drawing in the tests";
        return false;
    }
};

struct Fixture
{
    PythonHost host;
    python::Engine engine{host};
    fla::Timeline& scene() { return *host.document->document->currentScene(); }
    jsfl::RunResult run(const std::string& source, const std::string& undo = "Test Script", bool mayFail = false,
        const std::string& name = "test.py")
    {
        jsfl::RunOptions options;
        options.undoName = undo;
        options.timeLimit = 20.0;
        const jsfl::RunResult result = engine.runString(source, name, options);
        if (!result.ok && !mayFail)
            std::printf("    script error: %s\n%s\n", result.report().c_str(), result.stack.c_str());
        return result;
    }
};

/// False, saying why, on a machine with no Python.
bool havePython()
{
    std::string why;
    if (python::Engine::available(&why))
        return true;
    static bool said = false;
    if (!said)
        std::printf("    skipped (no Python): %s\n", why.c_str());
    said = true;
    return false;
}

#define CHECK_OUTPUT(f, expected)                                                                  \
    do                                                                                             \
    {                                                                                              \
        const std::string wanted = (expected);                                                     \
        CHECK((f).host.output == wanted);                                                          \
        if ((f).host.output != wanted)                                                             \
            std::printf("    got:\n%s    wanted:\n%s", (f).host.output.c_str(), wanted.c_str());   \
    } while (0)

} // namespace

TEST(python_runs_a_script_and_prints)
{
    if (!havePython())
        return;
    Fixture f;
    CHECK(f.run("print('hello', 6 * 7)\nfl.trace('traced')\nfl.outputPanel.trace('panel')\n"
                "print('no newline', end='')\n")
              .ok);
    CHECK_OUTPUT(f, "hello 42\ntraced\npanel\nno newline\n");

    // The dialogs, as JSFL scripts have them, and in the phoenix module.
    f.host.output.clear();
    CHECK(f.run("alert('hi')\nprint(confirm('sure?'), prompt('name?', 'Bob'))\n"
                "import phoenix\nprint(phoenix.fl == fl, phoenix.prompt('again?'))\n")
              .ok);
    CHECK(f.host.alerts.size() == 1 && f.host.alerts[0] == "hi");
    CHECK_OUTPUT(f, "True Bob\nTrue typed\n");
}

TEST(python_walks_and_edits_a_document_as_one_undo_step)
{
    if (!havePython())
        return;
    Fixture f;
    const size_t before = f.host.commands.count();
    CHECK(f.run("doc = fl.getDocumentDOM()\n"
                "tl = doc.getTimeline()\n"
                "tl.addNewLayer('Ink')\n"
                "tl.add_new_layer('Paint')\n"
                "tl.layers[0].name = 'Top'\n"
                "tl.insertFrames(9, True, 0)\n"
                "for layer in tl.layers:\n"
                "    print(layer.name, layer.frameCount, type(layer.frame_count).__name__)\n"
                "print(doc.width, doc.height, doc.get_timeline() == tl, fl.documents[0] == doc)\n"
                "print(len(tl.layers), tl.layer_count, isinstance(tl, __import__('phoenix').Object))\n",
        "Make Layers")
              .ok);
    CHECK_OUTPUT(f, "Top 10 int\nInk 10 int\nLayer_1 10 int\n550 400 True True\n3 3 True\n");
    CHECK(f.scene().layers.size() == 3);
    CHECK(f.scene().layers[0]->name == "Top");
    CHECK(fla::timeline::layerEnd(*f.scene().layers[2]) == 10);

    // All of it is one step, named after the script.
    CHECK(f.host.commands.count() == before + 1);
    CHECK(f.host.commands.undoName() == "Make Layers");
    CHECK(!f.host.commands.isInMacro());
    f.host.commands.undo();
    CHECK(f.scene().layers.size() == 1);
    CHECK(fla::timeline::layerEnd(*f.scene().layers[0]) == 1);

    // A script that changes nothing leaves no step.
    CHECK(f.run("print(fl.getDocumentDOM().width)").ok);
    CHECK(f.host.commands.count() == before + 1);
    CHECK(f.host.commands.canRedo());
}

TEST(python_errors_name_the_line)
{
    if (!havePython())
        return;
    Fixture f;
    const jsfl::RunResult thrown = f.run("a = 1\n\nNone.x\n", "Test Script", true, "broken.py");
    CHECK(!thrown.ok);
    CHECK(thrown.file == "broken.py");
    CHECK(thrown.line == 3);
    CHECK(thrown.message.rfind("AttributeError:", 0) == 0);
    CHECK(thrown.stack.find("Traceback") != std::string::npos);

    const jsfl::RunResult syntax = f.run("a = 1\nb = = 2\n", "Test Script", true, "syntax.py");
    CHECK(!syntax.ok);
    CHECK(syntax.line == 2);
    CHECK(syntax.message.rfind("SyntaxError:", 0) == 0);

    // An error from Phoenix's side points at the script's line, also from
    // inside a function, and can be caught as phoenix.Error.
    const jsfl::RunResult model = f.run("def go():\n    fl.getDocumentDOM().getTimeline().deleteLayer(7)\n\ngo()\n",
        "Test Script", true, "model.py");
    CHECK(!model.ok);
    CHECK(model.line == 2);
    CHECK(model.message.rfind("Error: ", 0) == 0);
    CHECK(model.message.find("no layer 7") != std::string::npos);
    CHECK(model.report() == "model.py:2: " + model.message);
    f.host.output.clear();
    CHECK(f.run("import phoenix\ntry:\n    fl.getDocumentDOM().getTimeline().deleteLayer(7)\n"
                "except phoenix.Error as error:\n    print('caught')\n")
              .ok);
    CHECK_OUTPUT(f, "caught\n");

    // Misspelled names are errors, not new properties.
    const jsfl::RunResult typo = f.run("fl.getDocumentDOM().widht = 3\n", "Test Script", true);
    CHECK(!typo.ok);
    CHECK(typo.message.rfind("AttributeError:", 0) == 0);
}

TEST(python_scripts_can_be_stopped)
{
    if (!havePython())
        return;
    Fixture f;
    jsfl::RunOptions options;
    options.timeLimit = 0.3;
    const jsfl::RunResult slow = f.engine.runString("while True:\n    pass\n", "loop.py", options);
    CHECK(!slow.ok);
    CHECK(slow.interrupted);
    CHECK(slow.message.find("too long") != std::string::npos);

    // Stop, as the window's Stop button does, from its poll.
    jsfl::RunOptions stoppable;
    int polls = 0;
    stoppable.poll = [&f, &polls]() {
        if (++polls == 3)
            f.engine.stop();
    };
    const jsfl::RunResult stopped = f.engine.runString("while True:\n    pass\n", "loop.py", stoppable);
    CHECK(!stopped.ok);
    CHECK(stopped.interrupted);
    CHECK(stopped.message == "The script was stopped.");
    CHECK(!f.engine.running());

    // The next run goes on as usual.
    CHECK(f.run("print('again')").ok);
}

TEST(python_objects_belong_to_their_run)
{
    if (!havePython())
        return;
    Fixture f;
    CHECK(f.run("import phoenix\nphoenix.kept = fl.getDocumentDOM()\nfl.trace(repr(phoenix.kept))\n").ok);
    CHECK_OUTPUT(f, "<phoenix.Document 'Untitled'>\n");
    const jsfl::RunResult later = f.run("import phoenix\nprint(phoenix.kept.width)\n", "Test Script", true);
    CHECK(!later.ok);
    CHECK(later.message.find("run that has ended") != std::string::npos);
    // Globals do not carry over either.
    CHECK(f.run("x = 1").ok);
    CHECK(!f.run("print(x)", "Test Script", true).ok);
}

TEST(python_values_convert_both_ways)
{
    if (!havePython())
        return;
    Fixture f;
    CHECK(f.run("doc = fl.getDocumentDOM()\n"
                "tl = doc.getTimeline()\n"
                "tl.addNewLayer('B')\n"
                "tl.setSelectedLayers(1)\n"
                "print(tl.getSelectedLayers(), type(doc.frameRate).__name__)\n"
                "tl.setSelectedFrames([0, 0, 1])\n"
                "print(tl.getSelectedFrames())\n"
                "print(tl.findLayerIndex('B'), tl.findLayerIndex('nothing'))\n"
                "tl.layers[0].visible = 0\n"
                "tl.layers[0].name = 7.5\n"
                "print(tl.layers[0].visible, repr(tl.layers[0].name), tl.layers[0] in tl.layers)\n")
              .ok);
    CHECK_OUTPUT(f, "[1] int\n[0, 0, 1]\n[0] None\nFalse '7.5' True\n");
}

TEST(python_run_script_calls_a_function_in_another_file)
{
    if (!havePython())
        return;
    Fixture f;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_python_tests";
    std::filesystem::create_directories(folder);
    const std::filesystem::path helper = folder / "helper.py";
    {
        std::ofstream out(helper, std::ios::binary);
        out << "def add(a, b):\n    return a + b\n\ndef name():\n    return fl.getDocumentDOM().name\n";
    }
    CHECK(f.run("uri = FLfile.platformPathToURI(" + std::string("r'") + helper.u8string() +
                "')\nprint(fl.runScript(uri, 'add', 2, 3), fl.runScript(uri, 'name'))\n")
              .ok);
    CHECK_OUTPUT(f, "5 Untitled\n");
    std::filesystem::remove_all(folder);
}
