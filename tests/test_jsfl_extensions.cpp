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
    CHECK(f.run(std::string(kRectangle) + "s.deleteEdge(0);\n"
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
    CHECK(f.run("var doc = fl.getDocumentDOM();\n"
                "doc.exportSWF('file:///C|/work/movie.fla', true);\n"
                "doc.exportSWF('file:///C|/work/other.swf');\n")
              .ok);
    CHECK(f.host.swfs.size() == 2);
    if (f.host.swfs.size() == 2)
    {
        // Never written over the document itself.
        CHECK(f.host.swfs[0] == "C:/work/movie.swf");
        CHECK(f.host.swfs[1] == "C:/work/other.swf");
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
