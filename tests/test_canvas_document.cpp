#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/canvas_document.h"
#include "../src/edit/new_document.h"
#include "../src/edit/publish_settings.h"
#include "../src/parser/fla_parser.h"
#include "../src/play/movie_runtime.h"
#include "../src/writer/fla_writer.h"
#include "../src/writer/xfl_folder_writer.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// The HTML5 Canvas document kept with the tests, as an XFL folder.
const fs::path kCanvasGame = fs::path(__FILE__).parent_path() / "data" / "canvas" / "canvas_game";

/// An empty folder of its own under the temporary folder, gone after.
struct Scratch
{
    fs::path path;
    explicit Scratch(const char* name)
        : path(fs::temp_directory_path() / name)
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
        fs::create_directories(path, ignored);
    }
    ~Scratch()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string publishSettingsOf(const fla::Document& document)
{
    for (const auto& [path, bytes] : document.extraFiles)
        if (path == "PublishSettings.xml")
            return std::string(bytes.get().begin(), bytes.get().end());
    return std::string();
}

std::string attributeOf(const fla::DOMElement& element, const std::string& key)
{
    for (const auto& [name, value] : element.extraAttributes)
        if (name == key)
            return value;
    return std::string();
}

std::string firstScript(const fla::Timeline& timeline)
{
    for (const fla::Layer* layer : timeline.layers)
        for (const fla::Frame* frame : layer->frames)
            if (frame->actionScript && !frame->actionScript->code.empty())
                return frame->actionScript->code;
    return std::string();
}

/// What a document's movie prints in its first \a frames frames, played
/// as Test Movie plays it, and its errors.
std::string play(const fla::Document& document, int frames, std::vector<std::string>& errors)
{
    std::vector<const fla::Timeline*> scenes(document.timelines.begin(), document.timelines.end());
    fla::play::MovieRuntime movie(scenes, document.frameRate);
    std::string output;
    movie.trace = [&output](const std::string& line) { output += line + "\n"; };
    movie.error = [&errors](const std::string& line) { errors.push_back(line); };
    std::vector<const fla::Symbol*> symbols;
    if (document.symbolList)
        symbols.assign(document.symbolList->symbols.begin(), document.symbolList->symbols.end());
    movie.setLibrary(symbols, {}, {});
    movie.setJavaScript(fla::canvas::isCanvasDocument(document));
    movie.start();
    for (int i = 0; i < frames; ++i)
        movie.step();
    return output;
}

} // namespace

TEST(canvas_document_is_recognized_and_plays_its_javascript)
{
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> read(parser.parse(kCanvasGame.string()));
    CHECK(read && read->document);
    if (!read || !read->document)
        return;
    const fla::Document& document = *read->document;
    CHECK(fla::canvas::isCanvasDocument(document));
    CHECK(fla::canvas::documentType(document) == "HTML5 Canvas");
    CHECK(firstScript(*document.timelines[0]).rfind("this.stop();", 0) == 0);
    std::vector<std::string> errors;
    const std::string output = play(document, 12, errors);
    CHECK(errors.empty());
    CHECK(output == "Canvas ready 1 2 0\nball at 192\n");
    if (output != "Canvas ready 1 2 0\nball at 192\n" || !errors.empty())
    {
        std::printf("    got:\n%s", output.c_str());
        for (const std::string& error : errors)
            std::printf("    error: %s\n", error.c_str());
    }
}

TEST(canvas_document_round_trips_through_fla_and_xfl)
{
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> read(parser.parse(kCanvasGame.string()));
    CHECK(read && read->document);
    if (!read || !read->document)
        return;
    const std::string settings = publishSettingsOf(*read->document);
    const std::string script = firstScript(*read->document->timelines[0]);
    Scratch scratch("phoenix_test_canvas_document");

    const std::string fla = (scratch.path / "canvas_game.fla").string();
    fla::FLAWriter writer;
    CHECK(writer.write(*read->document, fla));
    fla::XFLFolderWriter folderWriter;
    const fs::path folder = scratch.path / "xfl";
    CHECK(folderWriter.write(*read->document, folder.string(), "canvas_game"));

    for (const std::string& path : {fla, folder.string()})
    {
        FLAParser reparser;
        std::unique_ptr<fla::FLADocument> again(reparser.parse(path));
        CHECK(again && again->document);
        if (!again || !again->document)
            continue;
        const fla::Document& document = *again->document;
        // Still a Canvas document, its publish settings as they were, its
        // scripts and what Phoenix does not know of kept.
        CHECK(fla::canvas::isCanvasDocument(document));
        CHECK(publishSettingsOf(document) == settings);
        CHECK(firstScript(*document.timelines[0]) == script);
        CHECK(attributeOf(document, "canvasTestUnknown") == "kept");
        CHECK(document.symbolList && document.symbolList->symbols.size() == 1 &&
            attributeOf(*document.symbolList->symbols[0], "linkageClassName") == "Ball");
    }
}

TEST(canvas_new_document_publishes_canvas)
{
    std::unique_ptr<fla::FLADocument> made(fla::createEmptyDocument(640, 480, 30.0));
    fla::Document& document = *made->document;
    CHECK(!fla::canvas::isCanvasDocument(document));
    CHECK(fla::canvas::documentType(document) == "ActionScript");
    fla::canvas::makeCanvasDocument(document, "Untitled");
    CHECK(fla::canvas::isCanvasDocument(document));
    CHECK(fla::canvas::documentType(document) == "HTML5 Canvas");
    // Its profile publishes no SWF, and the Publish Settings dialog keeps
    // the Canvas format's settings when it saves its own.
    const std::string settings = publishSettingsOf(document);
    const fla::publish::Settings read = fla::publish::read(settings, "Untitled");
    CHECK(!read.flash && !read.html);
    CHECK(fla::canvas::publishesCanvas(fla::publish::write(settings, read, "Untitled")));
    // An ActionScript 3 profile is not one.
    CHECK(!fla::canvas::publishesCanvas("<flash_profiles><flash_profile current=\"true\"><PublishFlashProperties>"
                                        "<ActionScriptVersion>3</ActionScriptVersion></PublishFlashProperties>"
                                        "</flash_profile></flash_profiles>"));

    Scratch scratch("phoenix_test_canvas_new");
    const std::string fla = (scratch.path / "new.fla").string();
    fla::FLAWriter writer;
    CHECK(writer.write(document, fla));
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> again(parser.parse(fla));
    CHECK(again && again->document && fla::canvas::isCanvasDocument(*again->document));
}
