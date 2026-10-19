// The JSFL objects added after the first round: the selection's stroke and
// fill, the toolbox and zoom, importing documents and movies (FLA, SWF,
// importEmbeddedSWF), and a targetlist's instances, run through a host
// that keeps everything in memory.
#include "test_util.h"

#include "jsfl_test_host.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/static_text.h"
#include "../src/data/stroke_style.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/jsfl/flfile.h"
#include "../src/util/swf_writer.h"
#include "../src/writer/fla_writer.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using jsfl_test::Fixture;

namespace {

void expectOutput(Fixture& f, const std::string& wanted)
{
    const std::string got = f.take();
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    got:\n%s    wanted:\n%s", got.c_str(), wanted.c_str());
}

const fla::Shape* firstShape(Fixture& f)
{
    for (fla::Element* element : f.scene().layers[0]->frames[0]->elements)
    {
        if (const auto* shape = dynamic_cast<const fla::Shape*>(element))
            return shape;
    }
    return nullptr;
}

/// A small movie: a red square, on two frames.
std::vector<uint8_t> squareMovie()
{
    swf::Writer writer(200, 100, 12.0, swf::Color{255, 255, 255, 255});
    swf::Fill red;
    red.color = swf::Color{255, 0, 0, 255};
    using K = swf::Segment::Kind;
    const int shape = writer.defineShape(red, {{K::Move, 10, 10}, {K::Line, 60, 10}, {K::Line, 60, 60},
                                                  {K::Line, 10, 60}, {K::Line, 10, 10}});
    writer.place(shape, 1);
    writer.showFrame();
    writer.showFrame();
    return writer.finish(false);
}

} // namespace

TEST(jsfl_document_stroke_and_fill_of_the_selection)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "d.setStrokeColor('#000000'); d.setStrokeSize(1); d.setFillColor('#00FF00');\n"
                "d.addNewRectangle({left: 0, top: 0, right: 40, bottom: 20}, 0);\n")
              .ok);
    const fla::Shape* shape = firstShape(f);
    CHECK(shape && !shape->strokes.empty());
    CHECK(f.run("var d = fl.getDocumentDOM(); d.selectAll();\n"
                "d.setStroke('#FF0000', 3, 'dashed');\n"
                "var s = d.getCustomStroke('selection'); fl.trace(s.style + ' ' + s.thickness + ' ' + s.color);\n"
                "d.setStrokeStyle('dotted'); fl.trace(d.getCustomStroke('selection').style);\n"
                "d.setStrokeSize(5); d.setStrokeColor('#0000FF');\n"
                "s = d.getCustomStroke('selection'); fl.trace(s.style + ' ' + s.thickness + ' ' + s.color);\n"
                "d.setFillColor('#123456'); fl.trace(d.getCustomFill('selection').color);\n"
                "fl.trace(d.getCustomStroke('toolbar').style + ' ' + d.getCustomStroke().thickness);\n"
                "d.setCustomStroke({style: 'solid', thickness: 2, color: '#654321', capType: 'square'});\n"
                "s = d.getCustomStroke('selection'); fl.trace(s.style + ' ' + s.thickness + ' ' + s.color + ' ' + s.capType);\n"
                "try { d.setStrokeStyle('wavy'); } catch (e) { fl.trace('no wavy'); }\n")
              .ok);
    expectOutput(f, "dashed 3 #FF0000\ndotted\ndotted 5 #0000FF\n#123456\ndotted 5\nsolid 2 #654321 square\nno wavy\n");
    // One step to undo: the shape as it was drawn.
    f.host.history(f.document).undo();
    shape = firstShape(f);
    CHECK(shape && !shape->strokes.empty());
    if (shape && !shape->strokes.empty())
    {
        const fla::StrokeStyle* stroke = shape->strokes.front();
        CHECK(stroke->style() == fla::StrokeStyle::Style::Solid && stroke->weight == 1.0);
        const auto* color = dynamic_cast<const fla::SolidColor*>(stroke->fill);
        CHECK(color && color->color[0] == 0 && color->color[1] == 0 && color->color[2] == 0);
    }
}

TEST(jsfl_tools_and_zoom)
{
    Fixture f;
    CHECK(f.run("fl.selectTool('oval');\n"
                "fl.trace(fl.tools.toolObjs.length + ' ' + (fl.tools.activeTool !== null) + ' ' + fl.tools.activeTool.position);\n"
                "fl.selectTool('Paint Bucket');\n"
                "if (!fl.selectTool('teleporter')) fl.trace('no teleporter');\n"
                "var p = fl.tools.constrainPoint({x: 0, y: 0}, {x: 10, y: 1}); fl.trace(p.x + ',' + p.y);\n"
                "var d = fl.getDocumentDOM(); fl.trace(d.zoomFactor); d.zoomFactor = 2; fl.trace(d.zoomFactor);\n"
                "try { d.zoomFactor = 0; } catch (e) { fl.trace('no zero'); }\n")
              .ok);
    expectOutput(f, "6 true 3\nno teleporter\n10,0\n1\n2\nno zero\n");
    CHECK(f.host.tool == "Paint Bucket");
    CHECK(f.host.zoomFactor == 2.0);
}

TEST(jsfl_selecting_frames_goes_to_them_and_selects_what_is_there)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM(); var tl = d.getTimeline();\n"
                "d.addNewRectangle({left: 0, top: 0, right: 20, bottom: 20}, 0);\n"
                "tl.insertFrames(19); tl.convertToKeyframes(10);\n"
                "d.selectNone(); tl.currentFrame = 0;\n"
                "tl.setSelectedFrames([0, 10, 11], true);\n"
                "fl.trace(tl.currentFrame + ' ' + d.selection.length);\n"
                "tl.addNewLayer('empty');\n"
                "tl.setSelectedFrames([0, 3, 4], true);\n"
                "fl.trace(tl.currentFrame + ' ' + d.selection.length);\n")
              .ok);
    expectOutput(f, "10 1\n3 0\n");
}

TEST(jsfl_reload_tools_view_matrix_and_test_movie)
{
    Fixture f;
    // A tool put in the Tools folder is there once the tools are read
    // again, by the name its script gives it.
    CHECK(f.run("fl.trace(fl.selectTool('Rig Tool'));\n"
                "fl.reloadTools();\n"
                "fl.trace(fl.selectTool('Rig Tool'));\n"
                "var d = fl.getDocumentDOM();\n"
                "var m = d.viewMatrix; fl.trace([m.a, m.d, m.tx, m.ty].join(','));\n"
                "d.addNewRectangle({left: 0, top: 0, right: 20, bottom: 20}, 0);\n"
                "d.selectAll(); d.convertToSymbol('graphic', 'Box', 'top left');\n"
                "d.moveSelectionBy({x: 30, y: 40});\n"
                "d.enterEditMode('inPlace');\n"
                "m = d.viewMatrix; fl.trace([m.a, m.d, m.tx, m.ty].join(','));\n"
                "fl.trace(d.getTimeline().name);\n"
                "fl.trace(d.canTestMovie());\n")
              .ok);
    expectOutput(f, "false\ntrue\n1,1,0,0\n1,1,30,40\nBox\nfalse\n");
    CHECK(f.host.tool == "Rig");
    CHECK(f.host.inPlace != nullptr);
}

TEST(jsfl_document_import_documents_and_movies)
{
    const std::string folder = jsfl_test::scratchFolder("import_documents");
    // A document to import: a ball symbol on its stage.
    {
        Fixture source;
        CHECK(source.run("var d = fl.getDocumentDOM();\n"
                         "d.addNewOval({left: 0, top: 0, right: 30, bottom: 30});\n"
                         "d.selectAll(); d.convertToSymbol('graphic', 'Ball', 'center');\n")
                  .ok);
        fla::FLAWriter writer;
        CHECK(writer.write(source.data(), folder + "/scene.fla"));
    }
    {
        const std::vector<uint8_t> movie = squareMovie();
        std::ofstream out(std::filesystem::u8path(folder + "/square.swf"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(movie.data()), static_cast<std::streamsize>(movie.size()));
    }
    Fixture f;
    const std::string base = jsfl::uri::fromPath(folder) + "/";
    CHECK(f.run("var d = fl.getDocumentDOM(), lib = d.library, base = '" + base + "';\n"
                "fl.trace(d.importFile(base + 'scene.fla'));\n"
                "fl.trace(lib.itemExists('scene') + ' ' + lib.itemExists('Ball') + ' ' + lib.getItemType('scene'));\n"
                "fl.trace(d.selection.length + ' ' + d.selection[0].libraryItem.name);\n"
                "fl.trace(d.importFile(base + 'scene.fla', true) + ' ' + lib.itemExists('scene copy') + ' ' + lib.itemExists('Ball copy'));\n"
                "fl.trace(d.getTimeline().layers[0].frames[0].elements.length);\n"
                "fl.trace(d.importFile(base + 'square.swf', true) + ' ' + lib.itemExists('square') + ' ' + lib.getItemType('square'));\n"
                "fl.trace(lib.items[lib.findItemIndex('square')].timeline.frameCount);\n")
              .ok);
    // What a movie cannot bring back is said once.
    const std::string note =
        "Imported from an SWF movie: its frames come baked, with no layer names or library folders of the original.\n";
    expectOutput(f, "true\ntrue true movie clip\n1 scene\ntrue true true\n1\n" + note + "true true movie clip\n2\n");
    // The script's one undo step takes it all away.
    f.host.history(f.document).undo();
    CHECK(!f.data().symbolList || f.data().symbolList->symbols.empty());

    // importEmbeddedSWF: the bytes handed over as an array.
    const std::vector<uint8_t> movie = squareMovie();
    std::string bytes;
    for (uint8_t b : movie)
        bytes += (bytes.empty() ? "" : ",") + std::to_string(b);
    CHECK(f.run("var d = fl.getDocumentDOM(), lib = d.library;\n"
                "d.importEmbeddedSWF('SquareClip', [" + bytes + "], 'Embedded Square');\n"
                "var item = lib.items[lib.findItemIndex('Embedded Square')];\n"
                "fl.trace(item.itemType + ' ' + item.linkageExportForAS + ' ' + item.linkageIdentifier);\n"
                "try { d.importEmbeddedSWF('x', [1, 2], 'y'); } catch (e) { fl.trace('too short'); }\n")
              .ok);
    expectOutput(f, note + "movie clip true SquareClip\ntoo short\n");
}

TEST(jsfl_xul_targetlist_lists_named_instances)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "d.addNewOval({left: 0, top: 0, right: 30, bottom: 30});\n"
                "d.selectAll(); d.convertToSymbol('movie clip', 'Wheel', 'center');\n"
                "d.selection[0].name = 'wheel';\n"
                "d.selectAll(); d.convertToSymbol('movie clip', 'Car', 'center');\n"
                "d.selection[0].name = 'car';\n")
              .ok);
    CHECK(f.run("var r = fl.xmlPanelFromString('<dialog title=\"Target\" buttons=\"accept\">' +\n"
                "  '<script>function shown() { var items = fl.xmlui.getControlItemElements(\"target\");' +\n"
                "  ' fl.trace(items.map(function (i) { return i.label; }).join(\" \")); }</script>' +\n"
                "  '<targetlist id=\"target\" value=\"this.car\" oncreate=\"shown()\"/></dialog>');\n"
                "fl.trace(r.target);\n")
              .ok);
    const std::string out = f.take();
    CHECK(out.find("this.car this.car.wheel\n") != std::string::npos);
    CHECK(out.find("this.car\n", out.find("accepted")) != std::string::npos);
}

TEST(jsfl_document_publish_profiles)
{
    Fixture f;
    const std::string folder = jsfl_test::scratchFolder("publish_profiles");
    const std::string base = jsfl::uri::fromPath(folder) + "/";
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "fl.trace(d.publishProfiles.join(',') + ' ' + d.currentPublishProfile + ' ' + d.asVersion);\n"
                "fl.trace(d.addNewPublishProfile('Web') + ' ' + d.currentPublishProfile);\n"
                "d.asVersion = 2; d.docClass = 'com.studio.Main';\n"
                "fl.trace(d.asVersion + ' ' + d.docClass);\n"
                "var xml = d.exportPublishProfileString();\n"
                "fl.trace(xml.indexOf('<flash_profiles>') >= 0 && xml.indexOf('name=\"Web\"') >= 0);\n"
                "d.exportPublishProfile('" + base + "web.xml', 'Web');\n"
                "fl.trace(d.renamePublishProfile('Web 2') + ' ' + d.publishProfiles.join(','));\n"
                "fl.trace(d.importPublishProfile('" + base + "web.xml') + ' ' + d.publishProfiles.join(',') + ' ' + d.currentPublishProfile);\n"
                "fl.trace(d.asVersion + ' ' + d.docClass);\n"
                "d.currentPublishProfile = 'Default'; fl.trace(d.asVersion + ' ' + d.docClass);\n"
                "fl.trace(d.deletePublishProfile() + ' ' + d.publishProfiles.join(','));\n"
                "fl.trace(d.importPublishProfileString('not xml'));\n"
                "try { d.currentPublishProfile = 'Nope'; } catch (e) { fl.trace('no Nope'); }\n")
              .ok);
    expectOutput(f, "Default Default 3\n1 Web\n2 com.studio.Main\ntrue\ntrue Default,Web 2\n2 Default,Web 2,Web Web\n"
                    "2 com.studio.Main\n3 \n0 Web 2,Web\n-1\nno Nope\n");
}

TEST(jsfl_document_more_members)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "d.addNewPrimitiveRectangle({left: 0, top: 0, right: 40, bottom: 20}, 5);\n"
                "fl.trace(d.selection.length + ' ' + d.selection[0].elementType + ' ' + d.selection[0].isRectangleObject);\n"
                "d.addNewPrimitiveOval({left: 50, top: 0, right: 80, bottom: 30}, false, true);\n"
                "fl.trace(d.getTimeline().layers[0].frames[0].elements.length);\n"
                "d.selectNone(); d.addNewText({left: 0, top: 50, right: 100, bottom: 70}, 'Hello');\n"
                "d.selectNone(); d.selection = [d.getTimeline().layers[0].frames[0].elements[2]];\n"
                "fl.trace(d.setTextRectangle({left: 10, top: 60, right: 210, bottom: 100}));\n"
                "var t = d.getTimeline().layers[0].frames[0].elements[2]; fl.trace(t.x + ' ' + t.y);\n"
                "fl.trace(d.livePreview); d.livePreview = false; fl.trace(d.livePreview);\n"
                "d.testMovie();\n"
                "try { d.convertLinesToFills(); } catch (e) { fl.trace('lines to fills: ' + e.message); }\n"
                "fl.compilerErrors.clear();\n"
                "var tl = d.getTimeline(); tl.insertFrames(9); tl.convertToKeyframes(9); tl.createMotionTween(0, 0);\n"
                "var frame = tl.layers[0].frames[0];\n"
                "frame.setCustomEase('all', [{x: 0, y: 0}, {x: 0.3, y: 0}, {x: 0.7, y: 1}, {x: 1, y: 1}]);\n"
                "fl.trace(frame.hasCustomEase + ' ' + frame.getCustomEase('position').length + ' ' +\n"
                "    frame.getCustomEase().pop().x + ' ' + frame.useSingleEaseCurve);\n"
                "var lib = d.library; d.selectAll(); d.convertToSymbol('movie clip', 'Box', 'top left');\n"
                "var item = lib.items[lib.findItemIndex('Box')];\n"
                "item.scalingGridRect = {left: 5, top: 4, right: 30, bottom: 16};\n"
                "var r = item.scalingGridRect; fl.trace(r.left + ' ' + r.top + ' ' + r.right + ' ' + r.bottom);\n")
              .ok);
    expectOutput(f, "1 shape true\n2\ntrue\n10 60\ntrue\nfalse\nTest Movie needs the Phoenix window.\n"
                    "lines to fills: convertLinesToFills needs the Phoenix window.\ntrue 4 1 true\n5 4 30 16\n");
    // The text block is as wide and tall as the rectangle, and stays so.
    const fla::StaticText* text = nullptr;
    for (const fla::Timeline* timeline : {&f.scene()})
        for (const fla::Element* element : timeline->layers[0]->frames[0]->elements)
            if (const auto* found = dynamic_cast<const fla::StaticText*>(element))
                text = found;
    if (!text && f.data().symbolList)
        for (const fla::Symbol* symbol : f.data().symbolList->symbols)
            for (const fla::Element* element : symbol->timelines.front()->layers[0]->frames[0]->elements)
                if (const auto* found = dynamic_cast<const fla::StaticText*>(element))
                    text = found;
    CHECK(text && text->width == 200 && text->height == 40 && !text->autoExpand);
}
