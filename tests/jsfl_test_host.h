#pragma once

// A JSFL host that keeps everything in memory, for tests of the object
// model: documents, output in a string, dialogs answered as the command
// line answers them, SWF exports and publishes recorded.

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/drawing_style.h"
#include "../src/edit/new_document.h"
#include "../src/edit/selection.h"
#include "../src/jsfl/engine.h"
#include "../src/parser/fla_parser.h"
#include "../src/writer/fla_writer.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace jsfl_test {

class Host : public jsfl::Host
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
    fla::DrawingStyle style;
    /// What exportSWF and publish were asked for.
    std::vector<std::string> swfs;
    std::vector<std::string> published;

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
        reply = initial;
        return true;
    }
    std::string configDirectory() override
    {
        return (std::filesystem::temp_directory_path() / "phoenix_jsfl_test_config").u8string();
    }
    std::string version() override { return "0.7.0"; }

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
    jsfl::Rect bounds(fla::FLADocument*, const fla::Element* element) override
    {
        const fla::Transform& m = element->transform;
        return {m.tx, m.ty, m.tx + 10, m.ty + 10};
    }
    bool exportPNG(fla::FLADocument*, const std::string&, bool, std::string& error) override
    {
        error = "no drawing in the tests";
        return false;
    }
    bool exportSWF(fla::FLADocument*, const std::string& path, std::string&) override
    {
        swfs.push_back(path);
        return true;
    }
    bool publish(fla::FLADocument* document, std::vector<std::string>& written, std::string& error) override
    {
        if (state(document).path.empty())
        {
            error = "Save the document first.";
            return false;
        }
        published.push_back(state(document).path);
        written.push_back("movie.swf");
        return true;
    }
};

/// A host with one new document: a scene of one layer of 1 frame.
struct Fixture
{
    Host host;
    jsfl::Engine engine{host};
    fla::FLADocument* document;
    Fixture() { document = host.add(fla::createEmptyDocument(550, 400, 24.0)); }
    fla::Document& data() { return *document->document; }
    fla::Timeline& scene() { return *data().currentScene(); }
    jsfl::RunResult run(const std::string& source, bool mayFail = false)
    {
        jsfl::RunOptions options;
        options.undoName = "Test Script";
        options.timeLimit = 20.0;
        const jsfl::RunResult result = engine.runString(source, "test.jsfl", options);
        if (!result.ok && !mayFail)
            std::printf("    script error: %s\n", result.report().c_str());
        return result;
    }
    /// What the script traced, cleared for the next run.
    std::string take()
    {
        std::string out = host.output;
        host.output.clear();
        return out;
    }
};

inline std::string scratchFolder(const char* name)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_jsfl_extension_tests" / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder.u8string();
}

} // namespace jsfl_test
