#include "test_util.h"

#include "../src/data/action_script.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/swf_movie.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/swf_import.h"
#include "../src/parser/zip_reader.h"
#include "../src/play/avm1_player.h"
#include "../src/play/movie_runtime.h"
#include "../src/util/swf_writer.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ActionScript 1 and 2 kept compiled when a SWF is opened, and run when it
// plays: a movie published from a document plays as the document does.

namespace {

std::vector<uint8_t> bytesOf(const std::string& text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

struct Parsed
{
    std::unique_ptr<ZipReader> reader;
    std::unique_ptr<fla::DOMElement> root;
    fla::Document* document = nullptr;
};

Parsed parse(const std::map<std::string, std::vector<uint8_t>>& files)
{
    Parsed parsed;
    parsed.reader.reset(new ZipReader());
    parsed.reader->openMemory(files);
    parsed.root.reset(new fla::FLADocument(nullptr));
    DocumentParser parser(parsed.reader.get());
    const auto xml = files.at("DOMDocument.xml");
    parsed.document = parser.parse(std::string(xml.begin(), xml.end()), parsed.root.get());
    static_cast<fla::FLADocument*>(parsed.root.get())->document = parsed.document;
    return parsed;
}

/// Draws every element as a square, movie clips and buttons as pieces.
class SquareDrawer : public fla::swfmovie::Drawer
{
public:
    std::vector<fla::swfmovie::Piece> layerFrame(const fla::Layer& layer, int frame, swf::Writer& writer) override
    {
        std::vector<fla::swfmovie::Piece> pieces;
        const fla::Frame* key = nullptr;
        for (const fla::Frame* f : layer.frames)
            if (frame >= f->index && frame < f->index + f->duration)
                key = f;
        if (!key)
            return pieces;
        for (size_t i = 0; i < key->elements.size(); ++i)
        {
            const fla::Element* element = key->elements[i];
            fla::swfmovie::Piece piece;
            piece.element = static_cast<int>(i);
            const fla::Transform& at = element->transform;
            piece.matrix = swf::Matrix{at.m11, at.m12, at.m21, at.m22, at.tx, at.ty};
            const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element);
            if (instance && instance->symbolType != fla::SymbolType::Graphic)
                piece.instance = instance;
            else
            {
                auto found = _shapes.find(element);
                if (found == _shapes.end())
                {
                    swf::Fill fill;
                    fill.color = swf::Color{200, 40, 40, 255};
                    found = _shapes.emplace(element, writer.defineShape(fill, {{swf::Segment::Kind::Move, 0, 0},
                        {swf::Segment::Kind::Line, 10, 0}, {swf::Segment::Kind::Line, 10, 10},
                        {swf::Segment::Kind::Line, 0, 10}, {swf::Segment::Kind::Line, 0, 0}})).first;
                }
                piece.shapes.push_back({found->second, 1.0, swf::Writer::Blend::Normal});
            }
            pieces.push_back(piece);
        }
        return pieces;
    }

private:
    std::map<const fla::Element*, int> _shapes;
};

/// The movie \a document publishes to.
std::vector<uint8_t> published(const fla::Document& document, std::vector<std::string>& problems)
{
    swf::Writer writer(static_cast<int>(document.width), static_cast<int>(document.height), document.frameRate,
        swf::Color{255, 255, 255, 255});
    SquareDrawer drawer;
    fla::swfmovie::Movie movie(document, writer, drawer);
    movie.write();
    problems = movie.problems();
    return writer.finish(true);
}

/// A document played: what it traces and its errors, over \a frames frames,
/// with the button named \a button clicked after them when there is one.
std::string play(const fla::Document& document, int frames, const std::string& button = std::string(),
    std::function<std::string(const std::string&)> mmExecute = {})
{
    std::vector<const fla::Timeline*> scenes(document.timelines.begin(), document.timelines.end());
    fla::play::MovieRuntime runtime(scenes, 12);
    std::vector<const fla::Symbol*> library;
    if (document.symbolList)
        library.assign(document.symbolList->symbols.begin(), document.symbolList->symbols.end());
    std::string out;
    runtime.trace = [&out](const std::string& line) { out += line + "\n"; };
    runtime.error = [&out](const std::string& line) { out += "error: " + line + "\n"; };
    runtime.setLibrary(library, {}, {});
    runtime.setActionScriptVersion(2);
    runtime.mmExecute = std::move(mmExecute);
    runtime.start();
    for (int i = 1; i < frames; ++i)
        runtime.step();
    if (!button.empty())
    {
        fla::play::Clip* pressed = runtime.root().named(button);
        out += pressed ? "click " + button + "\n" : "no " + button + "\n";
        runtime.pointerOver(pressed);
        runtime.pointerDown();
        runtime.pointerUp();
        runtime.step();
        runtime.step();
        runtime.keyDown(37, 0);
        runtime.keyUp(37, 0);
    }
    return out;
}

/// An AS2 document: a frame script with variables, setProperty and a
/// function; a clip with onClipEvent handlers and frame scripts of its own;
/// a button with on() handlers.
std::map<std::string, std::vector<uint8_t>> as2Package()
{
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<symbols><Include href=\"Hat.xml\"/><Include href=\"Go.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "var count = 0;\nvar words = ['a', 'b'];\nfunction twice(n) { return n * 2; }\n"
        "trace('frame 1 ' + twice(4) + ' ' + words.join('+') + ' ' + hat._x);\n"
        "setProperty('hat', _alpha, 40);\ntrace(getProperty('hat', _alpha) + ' ' + hat._name);\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "<DOMFrame index=\"1\" duration=\"3\"><Actionscript><script><![CDATA["
        "trace('frame 2 ' + count + ' ' + _root.hat._currentframe);\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"ui\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Go\" symbolType=\"button\" name=\"go\">"
        "<matrix><Matrix tx=\"150\" ty=\"10\"/></matrix>"
        "<Actionscript><script><![CDATA[on (release) {\n  trace('released');\n  _root.hat.gotoAndPlay('end');\n}\n"
        "on (keyPress \"<Left>\") {\n  trace('left key ' + count);\n}]]></script></Actionscript>"
        "</DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"hat\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"hat\"><matrix><Matrix tx=\"20\" ty=\"30\"/></matrix>"
        "<Actionscript><script><![CDATA[onClipEvent (load) {\n  trace('hat loaded ' + _name);\n}\n"
        "onClipEvent (enterFrame) {\n  _parent.count++;\n}]]></script></Actionscript>"
        "</DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    files["LIBRARY/Hat.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Hat\"><timeline><DOMTimeline name=\"Hat\">"
        "<layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"2\"><Actionscript><script><![CDATA[stop();]]></script></Actionscript>"
        "<elements/></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"1\" name=\"end\" labelType=\"name\"><Actionscript><script><![CDATA["
        "trace('hat at end ' + _parent.count);\nstop();]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements>"
        "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/></FillStyle></fills>"
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timeline></DOMSymbolItem>");
    files["LIBRARY/Go.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Go\" symbolType=\"button\"><timeline>"
        "<DOMTimeline name=\"Go\"><layers><DOMLayer name=\"face\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#00FF00\"/>"
        "</FillStyle></fills><edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges>"
        "</DOMShape></elements></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    return files;
}

/// A movie made as \a files' document publishes, then opened.
struct Reopened
{
    Parsed parsed;
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::string> problems;
    std::string error;
    bool ok = false;
};

void reopen(const fla::Document& document, Reopened& out)
{
    const std::vector<uint8_t> swf = published(document, out.problems);
    out.ok = SwfImport::convert(swf, out.files, out.error);
    if (out.ok)
        out.parsed = parse(out.files);
}

} // namespace

TEST(swf_actions_name_their_events)
{
    using fla::play::clipEventNames;
    using fla::play::buttonEventNames;
    CHECK(clipEventNames(0x1 | 0x2, 0) == std::vector<std::string>({"load", "enterFrame"}));
    CHECK(clipEventNames(1u << 17, 1) == std::vector<std::string>({"keyPress <Left>"}));
    CHECK(clipEventNames(1u << 17, 'a') == std::vector<std::string>({"keyPress a"}));
    CHECK(buttonEventNames(0x0008) == std::vector<std::string>({"release"}));
    CHECK(buttonEventNames(0x0004 | 0x0001) == std::vector<std::string>({"rollOver", "press"}));
    CHECK(buttonEventNames(13u << 9) == std::vector<std::string>({"keyPress <Enter>"}));
}

TEST(swf_actions_are_kept_on_frames_and_instances)
{
    Parsed source = parse(as2Package());
    CHECK(source.document != nullptr);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok);
    CHECK(movie.problems.empty());
    if (!movie.ok || !movie.parsed.document)
        return;
    // The frames that had scripts keep them compiled, with a comment.
    int frames = 0, clips = 0, buttons = 0;
    for (const fla::Timeline* timeline : movie.parsed.document->timelines)
        for (const fla::Layer* layer : timeline->layers)
            for (const fla::Frame* frame : layer->frames)
            {
                if (auto compiled = fla::play::compiledActionsIn(frame->extraChildren))
                {
                    ++frames;
                    CHECK(frame->actionScript && frame->actionScript->code.find("compiled") != std::string::npos);
                    CHECK(compiled->version >= 6);
                }
                for (const fla::Element* element : frame->elements)
                    if (auto compiled = fla::play::compiledActionsIn(element->extraChildren))
                        for (const auto& block : compiled->blocks)
                        {
                            clips += block.kind == fla::play::CompiledActions::Block::Kind::Clip;
                            buttons += block.kind == fla::play::CompiledActions::Block::Kind::Button;
                        }
            }
    CHECK(frames == 2);
    CHECK(clips == 2);
    CHECK(buttons == 2);
    const std::string settings(movie.files["PublishSettings.xml"].begin(), movie.files["PublishSettings.xml"].end());
    CHECK(settings.find("<ActionScriptVersion>2</ActionScriptVersion>") != std::string::npos);
}

TEST(swf_actions_play_as_the_document_does)
{
    Parsed source = parse(as2Package());
    if (!source.document)
        return;
    const std::string wanted = play(*source.document, 3, "go");
    CHECK(wanted.find("error") == std::string::npos);
    CHECK(wanted.find("frame 1 8 a+b 20") != std::string::npos);
    CHECK(wanted.find("hat at end") != std::string::npos);
    Reopened movie;
    reopen(*source.document, movie);
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 3, "go");
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    the document:\n%s    the movie:\n%s", wanted.c_str(), got.c_str());
}

TEST(swf_actions_reach_the_editor_through_mmexecute)
{
    // A panel written in ActionScript 2, played from its source and as the
    // movie it publishes to: MMExecute gives the editor its JSFL either way.
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "trace('got ' + MMExecute('fl.getDocumentDOM().name'));\nMMEndCommand(true, '');\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    std::vector<std::string> asked;
    const auto editor = [&asked](const std::string& code) {
        asked.push_back(code);
        return std::string("shot.fla");
    };
    const std::string fromSource = play(*source.document, 1, std::string(), editor);
    CHECK(fromSource == "got shot.fla\n");
    if (fromSource != "got shot.fla\n")
        std::printf("    from the source:\n%s", fromSource.c_str());
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok && movie.parsed.document);
    if (!movie.ok || !movie.parsed.document)
        return;
    CHECK(play(*movie.parsed.document, 1, std::string(), editor) == "got shot.fla\n");
    CHECK(asked.size() == 2 && asked[1] == "fl.getDocumentDOM().name");
}

TEST(swf_actions_run_classes_init_actions_and_prototypes)
{
    // What only a SWF's ActionScript 1 does here: #initclip, prototypes,
    // Object.registerClass, onEnterFrame.
    std::map<std::string, std::vector<uint8_t>> files = as2Package();
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<symbols><Include href=\"Hat.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"3\"><Actionscript><script><![CDATA["
        "trace('made ' + _global.hatsMade + ' ' + hat.tip() + ' ' + (hat instanceof _global.HatClass));\n"
        "var steps = 0;\nhat.onEnterFrame = function () { steps++; if (steps == 2) trace('stepped ' + this._name); };\n"
        "var copy = hat.duplicateMovieClip('copy', 5);\ntrace(typeof copy + ' ' + copy.tip());\n"
        "stop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"hat\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"hat\"/>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    files["LIBRARY/Hat.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Hat\" linkageExportForAS=\"true\" "
        "linkageIdentifier=\"HatLink\"><timeline><DOMTimeline name=\"Hat\">"
        "<layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA[#initclip\n"
        "_global.hatsMade = 0;\n_global.HatClass = function () { _global.hatsMade++; };\n"
        "HatClass.prototype.__proto__ = MovieClip.prototype;\n"
        "HatClass.prototype.tip = function () { return 'tipped ' + this._name; };\n"
        "Object.registerClass('HatLink', HatClass);\n#endinitclip\nstop();]]></script></Actionscript>"
        "<elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"1\"><elements>"
        "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/></FillStyle></fills>"
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timeline></DOMSymbolItem>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok);
    for (const std::string& problem : movie.problems)
        std::printf("    %s\n", problem.c_str());
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 4);
    const std::string wanted = "made 1 tipped hat true\nmovieclip tipped copy\nstepped hat\n";
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    got:\n%s", got.c_str());
}
