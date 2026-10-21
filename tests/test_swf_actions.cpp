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
#include <filesystem>
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
    std::function<std::string(const std::string&)> mmExecute = {}, const std::string& sharedObjects = std::string())
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
    runtime.sharedObjectFolder = sharedObjects;
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

TEST(swf_actions_have_the_players_geometry_classes)
{
    // ActionScript 2's flash.geom, as a published movie's actions find it.
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "var Point = _global.flash.geom.Point;\nvar Rectangle = _global.flash.geom.Rectangle;\n"
        "var Matrix = _global.flash.geom.Matrix;\nvar ColorTransform = _global.flash.geom.ColorTransform;\n"
        "var p = new Point(3, 4);\ntrace(p + ' ' + p.length);\n"
        "var r = new Rectangle(1, 2, 3, 4);\ntrace(r.union(new Rectangle(0, 0, 2, 2)) + ' ' + r.bottomRight);\n"
        "var m = new Matrix();\nm.translate(5, 6);\nm.scale(2, 2);\ntrace(m.transformPoint(new Point(1, 1)));\n"
        "var c = new ColorTransform(0.5, 1, 1, 1, 10, 0, 0, 0);\ntrace(c.redMultiplier + ' ' + c.rgb);\n"
        "var keys = '';\nfor (var k in new Point(1, 2)) keys += k + ' ';\ntrace(keys);\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok && movie.parsed.document);
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 1, std::string());
    const std::string wanted = "(x=3, y=4) 5\n(x=0, y=0, w=4, h=6) (x=4, y=6)\n(x=12, y=14)\n0.5 655360\n"
                               "toString offset normalize clone equals subtract add length y x \n";
    CHECK(got == wanted);
    if (got != wanted)
    {
        std::printf("    got:\n%s", got.c_str());
        for (const std::string& problem : movie.problems)
            std::printf("    problem: %s\n", problem.c_str());
    }
}

TEST(swf_actions_have_the_players_bitmaps)
{
    // ActionScript 2's flash.display.BitmapData: pixels, and the numbers
    // its methods give rather than errors.
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "var BitmapData = _global.flash.display.BitmapData;\n"
        "var b = new BitmapData(4, 3, true, 0x80FF0000);\n"
        "trace(b.width + ' ' + b.height + ' ' + b.transparent + ' ' + b.rectangle);\n"
        "trace(b.getPixel32(1, 1) + ' ' + b.getPixel(1, 1) + ' ' + b.getPixel() + ' ' + new BitmapData(1));\n"
        "trace(b.setPixel32(0, 0, 0xFF00FF00) + ' ' + (b.getPixel32(0, 0) >>> 0).toString(16));\n"
        "var c = b.clone();\ntrace((c instanceof BitmapData) + ' ' + b.compare(c));\n"
        "b.dispose();\ntrace(b.width + ' ' + b.getPixel(0, 0) + ' ' + c.width);\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok && movie.parsed.document);
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 1, std::string());
    const std::string wanted = "4 3 true (x=0, y=0, w=4, h=3)\n-2130771968 16711680 -1 undefined\n0 ff00ff00\n"
                               "true 0\n-1 -1 4\n";
    CHECK(got == wanted);
    if (got != wanted)
    {
        std::printf("    got:\n%s", got.c_str());
        for (const std::string& problem : movie.problems)
            std::printf("    problem: %s\n", problem.c_str());
    }
}

TEST(swf_actions_put_clips_at_depths_and_draw_in_them)
{
    // ActionScript 2's depths, drawing API and coordinates, as a published
    // movie's actions find them on MovieClip.prototype.
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "var a = this.createEmptyMovieClip('one', 5);\nvar b = this.createEmptyMovieClip('two', 2);\n"
        "trace(a.getDepth() + ' ' + b.getDepth() + ' ' + this.getNextHighestDepth());\n"
        "a.swapDepths(b);\ntrace(a.getDepth() + ' ' + b.getDepth() + ' ' + this.getInstanceAtDepth(5)._name);\n"
        "this.createEmptyMovieClip('c', 5);\ntrace(typeof this.two + ' ' + this.c.getDepth());\n"
        "a.lineStyle(2, 0xFF0000);\na.beginFill(0x00FF00, 50);\na.moveTo(0, 0);\na.lineTo(10, 0);\n"
        "a.lineTo(10, 20);\na.lineTo(0, 20);\na.endFill();\na._x = 100;\n"
        "var box = a.getBounds(this);\ntrace(box.xMin + ' ' + box.xMax + ' ' + box.yMin + ' ' + box.yMax);\n"
        "var p = {x: 5, y: 5};\na.localToGlobal(p);\ntrace(p.x + ' ' + p.y);\n"
        "var keys = '';\nfor (var k in MovieClip.prototype) keys += k;\ntrace('[' + keys + ']');\n"
        "a.removeMovieClip();\ntrace(typeof this.one);\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok && movie.parsed.document);
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 1, std::string());
    const std::string wanted = "5 2 6\n2 5 two\nundefined 5\n99 111 -1 21\n105 5\n[]\nundefined\n";
    CHECK(got == wanted);
    if (got != wanted)
    {
        std::printf("    got:\n%s", got.c_str());
        for (const std::string& problem : movie.problems)
            std::printf("    problem: %s\n", problem.c_str());
    }
}

TEST(swf_actions_have_the_players_filters)
{
    // ActionScript 2's flash.filters: values kept as the player keeps them,
    // and a movie clip's filters copied each way.
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "var GlowFilter = _global.flash.filters.GlowFilter;\n"
        "var g = new GlowFilter(0x123456, 0.5, 8, 8, 3, 2);\n"
        "trace(g.color + ' ' + g.alpha + ' ' + g.blurX + ' ' + g.strength + ' ' + g.quality + ' ' + g.inner);\n"
        "var c = this.createEmptyMovieClip('c', 1);\nc.filters = [g, 5, new _global.flash.filters.BlurFilter(2.5)];\n"
        "var back = c.filters;\n"
        "trace(back.length + ' ' + (back[0] instanceof GlowFilter) + ' ' + back[0].alpha + ' ' + back[1].blurX);\n"
        "trace(back[0] == g);\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok && movie.parsed.document);
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 1, std::string());
    const std::string wanted = "1193046 0.498039215686275 8 3 2 false\n2 true 0.498039215686275 2.5\nfalse\n";
    CHECK(got == wanted);
    if (got != wanted)
    {
        std::printf("    got:\n%s", got.c_str());
        for (const std::string& problem : movie.problems)
            std::printf("    problem: %s\n", problem.c_str());
    }
}

TEST(swf_actions_have_the_players_xml)
{
    // ActionScript 1 and 2's XML: a tree of XMLNodes, parsed leniently, a
    // status for what went wrong.
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA["
        "var x = new XML('<?xml version=\"1.0\"?><list a=\"1\" b=\"two\"><item id=\"i\">A &amp; B</item><empty/></list>');\n"
        "trace(x.status + ' ' + x.xmlDecl + ' ' + x.firstChild.nodeName + ' ' + x.firstChild.attributes.b);\n"
        "var item = x.firstChild.firstChild;\n"
        "trace(item.firstChild.nodeValue + ' ' + item.nextSibling.nodeName + ' ' + x.idMap.i.nodeName);\n"
        "trace(x.firstChild.childNodes.length + ' ' + x);\n"
        "x.firstChild.appendChild(x.createElement('more'));\n"
        "trace(x.firstChild.lastChild.previousSibling.nodeName + ' ' + new XML('<a><b></a>').status);\nstop();\n"
        "]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    Parsed source = parse(files);
    if (!source.document)
        return;
    Reopened movie;
    reopen(*source.document, movie);
    CHECK(movie.ok && movie.parsed.document);
    if (!movie.ok || !movie.parsed.document)
        return;
    const std::string got = play(*movie.parsed.document, 1, std::string());
    const std::string wanted = "0 <?xml version=\"1.0\"?> list two\n"
                               "A & B empty item\n"
                               "2 <list a=\"1\" b=\"two\"><item id=\"i\">A &amp; B</item><empty /></list>\n"
                               "empty -10\n";
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    got:\n%s", got.c_str());
}

TEST(swf_actions_keep_shared_objects)
{
    // SharedObject in ActionScript 2: kept in a .sol file and read back the
    // next time the movie plays.
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "phoenix-test-as2-shared-objects";
    std::error_code error;
    fs::remove_all(folder, error);
    const auto run = [&](const std::string& script) {
        std::map<std::string, std::vector<uint8_t>> files;
        files["DOMDocument.xml"] = bytesOf(
            "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
            "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"actions\"><frames>"
            "<DOMFrame index=\"0\" duration=\"1\"><Actionscript><script><![CDATA[" +
            script +
            "]]></script></Actionscript><elements/></DOMFrame>"
            "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
        Parsed source = parse(files);
        if (!source.document)
            return std::string();
        Reopened movie;
        reopen(*source.document, movie);
        CHECK(movie.ok && movie.parsed.document);
        if (!movie.ok || !movie.parsed.document)
            return std::string();
        return play(*movie.parsed.document, 1, std::string(), {}, folder.u8string());
    };
    const std::string first = run(
        "var so = SharedObject.getLocal('save');\ntrace(so.getSize());\n"
        "so.data.score = 42; so.data.items = ['sword', 'shield']; so.data.when = new Date(7000);\n"
        "trace(so.getSize() > 0);\ntrace(so.flush());\n"
        "trace(SharedObject.getLocal('bad name') + ' ' + (SharedObject.getLocal('save') == so));\nstop();\n");
    const std::string firstWanted = "0\ntrue\ntrue\nnull true\n";
    CHECK(first == firstWanted);
    const std::string second = run("var so = SharedObject.getLocal('save');\n"
                                   "trace(so.data.score + ' ' + so.data.items + ' ' + so.data.when.getTime());\n"
                                   "so.clear();\ntrace(so.getSize() + ' ' + so.data.score);\nstop();\n");
    const std::string secondWanted = "42 sword,shield 7000\n0 undefined\n";
    CHECK(second == secondWanted);
    if (first != firstWanted || second != secondWanted)
        std::printf("    got:\n%s%s", first.c_str(), second.c_str());
    fs::remove_all(folder, error);
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
