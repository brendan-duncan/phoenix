// The JSFL object model's shape geometry, persistent data, whole-layer
// edits and publishing, run through a host that keeps everything in memory.
#include "test_util.h"

#include "jsfl_test_host.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/rectangle_primitive.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/timeline.h"
#include "../src/jsfl/tool_script.h"
#include "../src/phx/phx_document.h"
#include "../src/writer/xfl_folder_writer.h"

#include <filesystem>
#include <memory>
#include <string>

using jsfl_test::Fixture;

namespace {

/// Checks what a run traced, showing it when it differs.
void expectOutput(Fixture& f, const std::string& wanted)
{
    const std::string got = f.take();
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    got:\n%s    wanted:\n%s", got.c_str(), wanted.c_str());
}

void redFillBlackStroke(Fixture& f)
{
    f.host.style.hasFill = true;
    f.host.style.fillColor[0] = 255;
    f.host.style.fillColor[1] = 0;
    f.host.style.fillColor[2] = 0;
    f.host.style.fillColor[3] = 255;
    f.host.style.hasStroke = true;
}

const char* const kRectangle =
    "var doc = fl.getDocumentDOM();\n"
    "doc.addNewRectangle({left: 0, top: 0, right: 100, bottom: 50}, 0);\n"
    "var s = doc.getTimeline().layers[0].frames[0].elements[0];\n";

} // namespace

TEST(jsfl_shape_reads_vertices_edges_and_contours)
{
    Fixture f;
    redFillBlackStroke(f);
    CHECK(f.run(std::string(kRectangle) +
                "fl.trace(s.elementType + ' ' + s.vertices.length + ' ' + s.edges.length + ' ' + s.contours.length);\n"
                "var c = s.contours;\n"
                "fl.trace(c[0].interior + ' ' + c[0].fill.style + ' ' + c[1].interior + ' ' + c[1].fill.style + ' ' + c[1].fill.color);\n"
                "fl.trace(c[1].orientation + ' ' + c[0].orientation);\n"
                "var he = c[1].getHalfEdge(), start = he.id, n = 0, xs = 0, ys = 0;\n"
                "do { var v = he.getVertex(); xs += v.x; ys += v.y; he = he.getNext(); n++; } while (he.id != start && n < 10);\n"
                "fl.trace(n + ' ' + xs + ' ' + ys);\n"
                "var e = s.edges[0];\n"
                "fl.trace(e.isLine + ' ' + e.stroke.style + ' ' + e.stroke.color + ' ' + e.stroke.thickness);\n"
                "fl.trace((e.getHalfEdge(0).getOppositeHalfEdge().id == e.getHalfEdge(1).id) + ' ' + (e.getHalfEdge(1).getEdge().id == e.id));\n"
                "fl.trace((he.getNext().getPrev().id == he.id) + ' ' + (s.edges[0] === s.edges[0]));\n"
                "var a = e.getControl(0), b = e.getControl(2), m = e.getControl(1);\n"
                "fl.trace((m.x == (a.x + b.x) / 2) + ' ' + (m.y == (a.y + b.y) / 2));\n"
                "var p = s.getCubicSegmentPoints(e.cubicSegmentIndex);\n"
                "fl.trace(p.length + ' ' + s.numCubicSegments);\n"
                "var v0 = s.vertices[0]; fl.trace(v0.getHalfEdge().getVertex().x == v0.x);\n"
                "fl.trace(s.isGroup + ' ' + s.isDrawingObject + ' ' + s.isRectangleObject);\n")
              .ok);
    expectOutput(f,
        "shape 4 4 2\n"
        "false noFill true solid #FF0000\n"
        "1 -1\n"
        "4 200 100\n"
        "true solid #000000 1\n"
        "true true\n"
        "true true\n"
        "true true\n"
        "4 4\n"
        "true\n"
        "false false false\n");
}

TEST(jsfl_shape_reads_curves_as_quadratics)
{
    Fixture f;
    redFillBlackStroke(f);
    CHECK(f.run("var doc = fl.getDocumentDOM();\n"
                "doc.addNewOval({left: 0, top: 0, right: 100, bottom: 100});\n"
                "var s = doc.getTimeline().layers[0].frames[0].elements[0];\n"
                "var curved = 0;\n"
                "for (var i = 0; i < s.edges.length; i++) if (!s.edges[i].isLine) curved++;\n"
                "fl.trace((s.edges.length >= 4) + ' ' + (curved == s.edges.length) + ' ' + s.contours.length);\n"
                "var e = s.edges[0], c = e.getControl(1), p = s.getCubicSegmentPoints(e.cubicSegmentIndex);\n"
                "fl.trace((Math.abs(c.x - 50) > 1 || Math.abs(c.y - 50) > 1) + ' ' + p.length);\n")
              .ok);
    expectOutput(f, "true true 2\n"
                    "true 4\n");
}

TEST(jsfl_shape_edits_delete_edges_and_move_vertices)
{
    Fixture f;
    redFillBlackStroke(f);
    CHECK(f.run(kRectangle).ok);
    CHECK(f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                "s.deleteEdge(0);\n"
                "fl.trace(s.edges.length + ' ' + s.contours.length + ' ' + s.contours[0].interior);\n")
              .ok);
    // The rectangle opened up: its fill has nowhere to be.
    expectOutput(f, "3 1 false\n");
    auto* shape = dynamic_cast<fla::Shape*>(f.scene().layers[0]->frames[0]->elements[0]);
    CHECK(shape != nullptr);
    // One step undoes the script.
    f.host.state(f.document).history.undo();
    CHECK(f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                "fl.trace(s.edges.length + ' ' + s.contours.length);\n")
              .ok);
    expectOutput(f, "4 2\n");

    // Moving a corner drags its two edges along, and the fill with them.
    CHECK(f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                "s.beginEdit();\n"
                "for (var i = 0; i < s.vertices.length; i++) {\n"
                "  var v = s.vertices[i];\n"
                "  if (v.x == 100 && v.y == 50) v.setLocation(120, 60);\n"
                "}\n"
                "s.endEdit();\n"
                "var found = 0;\n"
                "for (var i = 0; i < s.vertices.length; i++) if (s.vertices[i].x == 120 && s.vertices[i].y == 60) found++;\n"
                "fl.trace(found + ' ' + s.edges.length + ' ' + s.contours[1].fill.color);\n"
                "s.edges[0].splitEdge(0.5);\n"
                "fl.trace(s.edges.length + ' ' + s.vertices.length);\n")
              .ok);
    expectOutput(f, "1 4 #FF0000\n"
                    "5 5\n");

    // Parts read before an edit no longer name the shape's parts.
    const jsfl::RunResult stale = f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                                        "var e = s.edges[0]; s.deleteEdge(1); e.splitEdge(0.5);\n",
        true);
    CHECK(!stale.ok);
    CHECK(stale.message.find("changed") != std::string::npos);

    // An edit a script began and never ended goes with its run: the next
    // run's changes are its own, made at once.
    CHECK(f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                "s.beginEdit(); s.deleteEdge(0);\n")
              .ok);
    CHECK(f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                "var before = s.edges.length; s.deleteEdge(0);\n"
                "fl.trace((before - s.edges.length) + '');\n")
              .ok);
    expectOutput(f, "1\n");
}

TEST(jsfl_shape_reads_rectangle_objects)
{
    Fixture f;
    fla::Frame* frame = f.scene().layers[0]->frames[0];
    auto* rectangle = new fla::RectanglePrimitive(frame);
    rectangle->rect = fla::Rect(fla::Point(0, 0), fla::Point(40, 20));
    auto* fill = new fla::SolidColor(rectangle);
    fill->color[1] = 128;
    rectangle->fillStyle = fill;
    frame->elements.push_back(rectangle);
    CHECK(f.run("var s = fl.getDocumentDOM().getTimeline().layers[0].frames[0].elements[0];\n"
                "fl.trace(s.isRectangleObject + ' ' + s.vertices.length + ' ' + s.contours.length + ' ' + s.contours[1].fill.color);\n"
                "fl.trace(s.edges[0].stroke.style);\n")
              .ok);
    expectOutput(f, "true 4 2 #008000\n"
                    "noStroke\n");
}

TEST(jsfl_persistent_data_round_trips)
{
    const std::string folder = jsfl_test::scratchFolder("persistent");
    const std::string fla = folder + "/data.fla";
    const std::string xfl = folder + "/data_xfl";
    const std::string phx = folder + "/data.phx";
    {
        Fixture f;
        redFillBlackStroke(f);
        CHECK(f.run(std::string(kRectangle) +
                    "s.setPersistentData('count', 'integer', 42.7);\n"
                    "s.setPersistentData('note', 'string', 'a \"quoted\" <tag> & more\\nline');\n"
                    "s.setPersistentData('scale', 'doubleArray', [1.5, 2.25]);\n"
                    "s.setPersistentData('gone', 'integer', 1);\n"
                    "s.removePersistentData('gone');\n"
                    "doc.addDataToDocument('sourceChars', 'string', 'Phoenix');\n"
                    "doc.addDataToDocument('ids', 'integerArray', [3, 4, 5]);\n"
                    "doc.library.addNewItem('graphic', 'Armature');\n"
                    "var item = doc.library.items[0];\n"
                    "item.addData('armature', 'string', '<armature name=\"a\"/>');\n"
                    "fl.trace(s.getPersistentData('count') + ' ' + s.hasPersistentData('gone') + ' ' + s.getPersistentData('scale'));\n"
                    "fl.trace(doc.documentHasData('sourceChars') + ' ' + item.hasData('armature') + ' ' + item.getData('nothing'));\n")
                  .ok);
        expectOutput(f, "42 false 1.5,2.25\n"
                        "true true undefined\n");
        fla::FLAWriter writer;
        CHECK(writer.write(f.data(), fla));
        fla::XFLFolderWriter folderWriter;
        CHECK(folderWriter.write(f.data(), xfl));
        std::string error;
        CHECK(phx::save(f.data(), phx, phx::SaveOptions(), error));
    }
    const std::string read =
        "var doc = fl.getDocumentDOM();\n"
        "var s = doc.getTimeline().layers[0].frames[0].elements[0];\n"
        "fl.trace(s.getPersistentData('count') + '|' + s.getPersistentData('note') + '|' + s.getPersistentData('scale').join(';'));\n"
        "fl.trace(doc.getDataFromDocument('sourceChars') + '|' + doc.getDataFromDocument('ids').length + '|' + s.hasPersistentData('gone'));\n"
        "fl.trace(doc.library.items[0].getData('armature'));\n";
    const std::string expected = "42|a \"quoted\" <tag> & more\nline|1.5;2.25\n"
                                 "Phoenix|3|false\n"
                                 "<armature name=\"a\"/>\n";
    for (const std::string& path : {fla, xfl})
    {
        Fixture f;
        FLAParser parser;
        fla::FLADocument* document = parser.parse(path);
        CHECK(document != nullptr);
        if (!document)
            continue;
        f.host.add(document, path);
        CHECK(f.run(read).ok);
        expectOutput(f, expected);
    }
    {
        Fixture f;
        std::string error;
        std::string warning;
        bool readOnly = false;
        fla::FLADocument* document = phx::load(phx, error, warning, readOnly);
        CHECK(document != nullptr);
        if (document)
        {
            f.host.add(document, phx);
            CHECK(f.run(read).ok);
            expectOutput(f, expected);
        }
    }
}

TEST(jsfl_layers_copy_paste_cut_and_duplicate)
{
    Fixture f;
    CHECK(f.run("var tl = fl.getDocumentDOM().getTimeline();\n"
                "tl.layers[0].name = 'Base';\n"
                "tl.addNewLayer('Top');\n"
                "function names() { var n = []; for (var i = 0; i < tl.layerCount; i++) n.push(tl.layers[i].name); return n.join(','); }\n"
                "fl.trace(names());\n"
                "tl.copyLayers(1);\n"
                "fl.trace(tl.pasteLayers(0) + ' ' + names());\n"
                "tl.duplicateLayers(0);\n"
                "fl.trace(names());\n"
                "tl.cutLayers(0, 1);\n"
                "fl.trace(names());\n"
                "fl.trace(tl.pasteLayers(1) + ' ' + names());\n")
              .ok);
    expectOutput(f, "Top,Base\n"
                    "0 Base,Top,Base\n"
                    "Base copy,Base,Top,Base\n"
                    "Top,Base\n"
                    "1 Top,Base copy,Base,Base\n");
    // The run is one step to undo.
    f.host.state(f.document).history.undo();
    CHECK(f.scene().layers.size() == 1);
}

TEST(jsfl_layers_move_into_folders)
{
    Fixture f;
    CHECK(f.run("var tl = fl.getDocumentDOM().getTimeline();\n"
                "tl.layers[0].name = 'Art';\n"
                "var folder = tl.addNewLayer('Group', 'folder');\n"
                "var art = tl.layers[tl.findLayerIndex('Art')[0]];\n"
                "art.parentLayer = tl.layers[folder];\n"
                "fl.trace(art.parentLayer.name + ' ' + tl.findLayerIndex('Art')[0]);\n"
                "art.parentLayer = null;\n"
                "fl.trace(art.parentLayer + ' ' + tl.layerCount);\n")
              .ok);
    expectOutput(f, "Group 1\n"
                    "null 2\n");
}

TEST(jsfl_layer_filters_at_frames)
{
    Fixture f;
    CHECK(f.run("var layer = fl.getDocumentDOM().getTimeline().layers[0];\n"
                "fl.trace(layer.getFiltersAtFrame(0).length);\n"
                "layer.setFiltersAtFrame(0, [{name: 'blurFilter', blurX: 8, blurY: 4, quality: 'high'},\n"
                "  {name: 'dropShadowFilter', color: '#FF000080', distance: 3}]);\n"
                "var got = layer.getFiltersAtFrame(0);\n"
                "fl.trace(got.length + ' ' + got[0].name + ' ' + got[0].blurX + ' ' + got[0].blurY + ' ' + got[0].quality);\n"
                "fl.trace(got[1].name + ' ' + got[1].color + ' ' + got[1].distance);\n"
                "layer.setFiltersAtFrame(0, []);\n"
                "fl.trace(layer.getFiltersAtFrame(0).length);\n")
              .ok);
    expectOutput(f, "0\n"
                    "2 blurFilter 8 4 high\n"
                    "dropShadowFilter #FF000080 3\n"
                    "0\n");
    CHECK(f.scene().layers[0]->frames[0]->extraChildren.empty());
}

TEST(jsfl_publishing_goes_to_the_host)
{
    Fixture f;
#ifdef _WIN32
    const std::string folder = "file:///C|/work/";
    const std::string path = "C:/work/";
#else
    const std::string folder = "file:///tmp/work/";
    const std::string path = "/tmp/work/";
#endif
    CHECK(f.run("var doc = fl.getDocumentDOM();\n"
                "doc.exportSWF('" + folder + "movie.fla', true);\n"
                "doc.exportSWF('" + folder + "other.swf');\n")
              .ok);
    CHECK(f.host.swfs.size() == 2);
    if (f.host.swfs.size() == 2)
    {
        // Never written over the document itself.
        CHECK(f.host.swfs[0] == path + "movie.swf");
        CHECK(f.host.swfs[1] == path + "other.swf");
    }
    const jsfl::RunResult unsaved = f.run("fl.getDocumentDOM().publish();", true);
    CHECK(!unsaved.ok);
    f.host.state(f.document).path = "C:/work/movie.fla";
    CHECK(f.run("fl.trace(fl.getDocumentDOM().publish());").ok);
    expectOutput(f, "movie.swf\n");
    CHECK(f.host.published.size() == 1);
}

TEST(jsfl_run_command_line_gives_the_exit_code)
{
    Fixture f;
    CHECK(f.run("fl.trace(FLfile.runCommandLine('exit 3'));").ok);
    expectOutput(f, "3\n");
}

TEST(jsfl_tools_preview_while_dragging_and_draw_on_release)
{
    // A tool as the PolyStar JSFL tool is written: an options file, a preview
    // on the drawing layer while the mouse is down, a path made into a
    // shape when it comes up.
    const std::filesystem::path folder = jsfl_test::scratchFolder("tools");
    {
        std::FILE* script = std::fopen((folder / "Box.jsfl").u8string().c_str(), "wb");
        std::fputs(
            "var sides = 0;\n"
            "function configureTool() {\n"
            "  var t = fl.tools.activeTool;\n"
            "  t.setToolName('box'); t.setMenuString('Box Tool'); t.setToolTip('Box Tool');\n"
            "  t.setIcon('Box.png'); t.setOptionsFile('Box.xml'); t.setPI('shape');\n"
            "}\n"
            "function notifySettingsChanged() { sides = fl.tools.activeTool.nsides; }\n"
            "function setCursor() { fl.tools.setCursor(0); }\n"
            "function mouseDown() { fl.drawingLayer.beginDraw(); }\n"
            "function mouseMove() {\n"
            "  if (!fl.tools.mouseIsDown) return;\n"
            "  var a = fl.tools.penDownLoc, b = fl.tools.penLoc;\n"
            "  fl.drawingLayer.beginFrame();\n"
            "  fl.drawingLayer.frameRect({left: a.x, top: a.y, right: b.x, bottom: b.y});\n"
            "  fl.drawingLayer.endFrame();\n"
            "}\n"
            "function mouseUp() {\n"
            "  fl.drawingLayer.endDraw();\n"
            "  var a = fl.tools.penDownLoc, b = fl.tools.penLoc;\n"
            "  var path = fl.drawingLayer.newPath();\n"
            "  path.addPoint(a.x, a.y); path.addPoint(b.x, a.y); path.addPoint(b.x, b.y); path.addPoint(a.x, b.y);\n"
            "  path.close();\n"
            "  path.makeShape();\n"
            "}\n",
            script);
        std::fclose(script);
        std::FILE* options = std::fopen((folder / "Box.xml").u8string().c_str(), "wb");
        std::fputs("<properties><property name=\"Number of Sides\" variable=\"nsides\" min=\"3\" max=\"32\" "
                   "defaultValue=\"5\" type=\"Number\"/><property name=\"Style\" variable=\"style\" "
                   "list=\"polygon,star\" defaultValue=\"0\" type=\"Strings\"/></properties>",
            options);
        std::fclose(options);
    }
    Fixture f;
    redFillBlackStroke(f);
    jsfl::ToolScript tool(f.host, (folder / "Box.jsfl").u8string());
    std::string error;
    CHECK(tool.load(error));
    if (!error.empty())
        std::printf("    %s\n", error.c_str());
    CHECK(tool.toolName() == "box" && tool.menuString() == "Box Tool");
    CHECK(tool.iconPath().find("Box.png") != std::string::npos);
    // The options start at their defaults, and the tool hears of them.
    const auto options = tool.options();
    CHECK(options.size() == 2 && options[0].variable == "nsides" && options[0].value == "5");
    CHECK(options.size() == 2 && options[1].list.size() == 2 && options[1].value == "0");
    CHECK(tool.setOptions({{"nsides", "7"}}).empty());
    CHECK(tool.options()[0].value == "7");

    jsfl::ToolScript::State state;
    state.penX = state.downX = 10;
    state.penY = state.downY = 20;
    state.mouseDown = true;
    CHECK(tool.call("mouseDown", state).empty());
    state.penX = 110;
    state.penY = 70;
    CHECK(tool.call("mouseMove", state).empty());
    const auto preview = tool.preview();
    CHECK(preview.size() == 1 && preview[0].pieces.size() == 5 && preview[0].pieces[0].kind == 'M');
    // Nothing is in the document until the mouse comes up.
    fla::Frame* frame = f.scene().layers[0]->frames[0];
    CHECK(frame->elements.empty());
    state.mouseDown = false;
    CHECK(tool.call("mouseUp", state).empty());
    CHECK(tool.preview().empty());
    CHECK(frame->elements.size() == 1);
    // One undo step, named after the tool.
    fla::CommandStack& history = f.host.history(f.document);
    CHECK(history.undoName() == "box");
    history.undo();
    CHECK(frame->elements.empty());
    // A handler the tool does not have is no error.
    CHECK(tool.call("keyDown", state).empty());
}

TEST(jsfl_listeners_hear_events_after_their_script_ends)
{
    Fixture f;
    // A script that listens stays loaded once it ends.
    CHECK(f.run("var saves = 0;\n"
                "var id = fl.addEventListener('documentSaved', function () {\n"
                "  saves++; fl.trace('saved ' + saves);\n"
                "});\n"
                "fl.addEventListener('frameChanged', function () {\n"
                "  fl.getDocumentDOM().addNewRectangle({left: 0, top: 0, right: 10, bottom: 10}, 0);\n"
                "});\n"
                "fl.addEventListener('selectionChanged', function () { throw new Error('broken'); });\n")
              .ok);
    CHECK(f.engine.listeningScripts() == 1);
    f.engine.dispatch("documentSaved");
    f.engine.dispatch("documentSaved");
    f.engine.dispatch("documentOpened");
    CHECK(f.take() == "saved 1\nsaved 2\n");
    // What a listener changes is one step to undo, named after its script.
    redFillBlackStroke(f);
    f.engine.dispatch("frameChanged");
    CHECK(f.scene().layers[0]->frames[0]->elements.size() == 1);
    CHECK(f.host.history(f.document).undoName() == "Test Script");
    // A listener's error goes to the Output panel; the others still hear.
    f.engine.dispatch("selectionChanged");
    CHECK(f.take().find("broken") != std::string::npos);
    // A script with no listener left is let go.
    CHECK(f.run("fl.trace(typeof fl.addEventListener);").ok);
    CHECK(f.take() == "function\n");
    CHECK(f.engine.listeningScripts() == 1);
    f.engine.forgetListeners();
    CHECK(f.engine.listeningScripts() == 0);
    CHECK(f.run("var id = fl.addEventListener('documentSaved', function () {});\n"
                "fl.removeEventListener('documentSaved', id);").ok);
    CHECK(f.engine.listeningScripts() == 0);
}
