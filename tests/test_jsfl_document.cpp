// The JSFL Document's scenes and imports, and library sounds, videos and
// bitmap exports, run through a host that keeps everything in memory.
#include "test_util.h"

#include "jsfl_test_host.h"

#include "../src/data/bitmap.h"
#include "../src/data/bitmap_decoder.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/raw_element.h"
#include "../src/data/timeline.h"
#include "../src/edit/library_media.h"
#include "../src/jsfl/flfile.h"

#include "zlib.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using jsfl_test::Fixture;

namespace {

void writeFile(const std::string& path, const std::string& text)
{
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out << text;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// Checks what a run traced, showing it when it differs.
void expectOutput(Fixture& f, const std::string& wanted)
{
    const std::string got = f.take();
    CHECK(got == wanted);
    if (got != wanted)
        std::printf("    got:\n%s    wanted:\n%s", got.c_str(), wanted.c_str());
}

std::string sceneNames(Fixture& f)
{
    std::string names;
    for (const fla::Timeline* scene : f.data().timelines)
        names += (names.empty() ? "" : ",") + scene->name;
    return names;
}

} // namespace

TEST(jsfl_document_scenes)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM();\n"
                "function names() { var n = []; for (var i = 0; i < d.timelines.length; i++) n.push(d.timelines[i].name); return n.join(','); }\n"
                "fl.trace(d.addNewScene() + ' ' + names() + ' ' + d.currentTimeline + ' ' + d.getTimeline().name);\n"
                "fl.trace(d.addNewScene('Titles') + ' ' + d.addNewScene('Titles') + ' ' + names() + ' ' + d.currentTimeline);\n"
                "fl.trace(d.renameScene('Credits') + ' ' + d.renameScene('Scene 1') + ' ' + names());\n"
                "d.editScene(0); fl.trace(d.duplicateScene() + ' ' + names() + ' ' + d.getTimeline().name);\n"
                "d.reorderScene(0, -1); fl.trace(names() + ' ' + d.currentTimeline);\n"
                "d.reorderScene(3, 0); fl.trace(names() + ' ' + d.currentTimeline);\n"
                "d.currentTimeline = 3; fl.trace(d.deleteScene() + ' ' + names() + ' ' + d.getTimeline().name);\n"
                "d.currentTimeline = 1; fl.trace(d.deleteScene() + ' ' + names() + ' ' + d.getTimeline().name);\n"
                "d.currentTimeline = 0; d.deleteScene(); fl.trace(d.deleteScene() + ' ' + names());\n")
              .ok);
    expectOutput(f, "true Scene 1,Scene 2 1 Scene 2\n"
                    "true false Scene 1,Scene 2,Titles 2\n"
                    "true false Scene 1,Scene 2,Credits\n"
                    "true Scene 1,Scene 1 copy,Scene 2,Credits Scene 1 copy\n"
                    "Scene 1 copy,Scene 2,Credits,Scene 1 0\n"
                    "Scene 1,Scene 1 copy,Scene 2,Credits 1\n"
                    // The last deleted: the first becomes current.
                    "true Scene 1,Scene 1 copy,Scene 2 Scene 1\n"
                    // Otherwise the next one does.
                    "true Scene 1,Scene 2 Scene 2\n"
                    "false Scene 2\n");
    CHECK(sceneNames(f) == "Scene 2");
    CHECK(f.host.editedTimeline(f.document) == f.data().currentScene());
    // The whole run is one step: undone, the one scene is back.
    f.host.history(f.document).undo();
    CHECK(sceneNames(f) == "Scene 1");
    f.host.history(f.document).redo();
    CHECK(sceneNames(f) == "Scene 2");
}

TEST(jsfl_document_import_file)
{
    Fixture f;
    const std::string folder = jsfl_test::scratchFolder("import_file");
    writeFile(folder + "/beep.wav", "RIFF");
    writeFile(folder + "/notes.txt", "text");
    const std::string base = jsfl::uri::fromPath(folder) + "/";
    CHECK(f.run("var d = fl.getDocumentDOM(); var base = '" + base + "';\n"
                "fl.trace(d.importFile(base + 'beep.wav', true) + ' ' + d.library.itemExists('beep.wav'));\n"
                "fl.trace(d.importFile(base + 'missing.wav'));\n"
                "fl.trace(d.importFile(base + 'notes.txt'));\n"
                "fl.trace(d.library.getItemType('beep.wav'));\n")
              .ok);
    expectOutput(f, "true true\nfalse\nnotes.txt is not a file Phoenix imports.\nfalse\nsound\n");
    // URIs reach the host as paths; a missing file never does.
    CHECK(f.host.imports.size() == 2);
    CHECK(f.host.imports[0] == jsfl::uri::toPath(base + "beep.wav") + " library");
    CHECK(f.host.imports[1].find("notes.txt stage") != std::string::npos);
    // The import is part of the script's one undo step.
    f.host.history(f.document).undo();
    CHECK(f.data().sounds.empty() && f.data().extraMedia.empty());
}

TEST(jsfl_library_sounds_rename_delete_export)
{
    Fixture f;
    const std::string folder = jsfl_test::scratchFolder("library_sounds");
    writeFile(folder + "/beep.wav", "RIFF");
    const std::string base = jsfl::uri::fromPath(folder) + "/";
    CHECK(f.run("fl.getDocumentDOM().importFile('" + base + "beep.wav', true);").ok);
    CHECK(f.data().sounds.size() == 1);
    const size_t files = f.data().extraFiles.size();
    CHECK(f.run("var lib = fl.getDocumentDOM().library; var base = '" + base + "';\n"
                "var item = lib.items[lib.findItemIndex('beep.wav')];\n"
                "item.name = 'Beep'; fl.trace(item.name + ' ' + lib.itemExists('Beep') + ' ' + lib.itemExists('beep.wav'));\n"
                "lib.newFolder('Sounds'); lib.moveToFolder('Sounds', 'Beep');\n"
                "lib.selectItem('Sounds/Beep'); fl.trace(lib.renameItem('Boop') + ' ' + lib.itemExists('Sounds/Boop'));\n"
                "fl.trace(lib.items[lib.findItemIndex('Sounds/Boop')].exportToFile(base + 'out.wav'));\n"
                "var frame = fl.getDocumentDOM().getTimeline().layers[0].frames[0];\n"
                "frame.soundName = 'Sounds/Boop';\n"
                "try { lib.deleteItem('Sounds/Boop'); } catch (e) { fl.trace('used'); }\n"
                "frame.soundName = '';\n"
                "fl.trace(lib.deleteItem('Sounds/Boop') + ' ' + lib.itemExists('Sounds/Boop'));\n")
              .ok);
    expectOutput(f, "Beep true false\ntrue true\ntrue\nused\ntrue false\n");
    CHECK(f.data().sounds.empty() && f.data().extraMedia.empty());
    CHECK(f.data().extraFiles.size() + 1 == files);
    // A WAV of the 8 silent samples: 44.1 kHz, 16-bit, mono.
    const std::string wav = readFile(folder + "/out.wav");
    CHECK(wav.size() == 44 + 16 && wav.compare(0, 4, "RIFF") == 0 && wav.compare(8, 4, "WAVE") == 0);
    // Undone, the sound is back under its first name, with its data.
    f.host.history(f.document).undo();
    CHECK(f.data().sounds.size() == 1 && f.data().sounds[0].name == "beep.wav");
    CHECK(f.data().extraFiles.size() == files && f.data().extraMedia.size() == 1);
    CHECK(f.data().extraMedia[0].find("name=\"beep.wav\"") != std::string::npos);
}

TEST(jsfl_library_videos_rename_delete)
{
    Fixture f;
    fla::Document& data = f.data();
    for (const char* name : {"clip.flv", "spare.flv"})
    {
        data.extraMedia.push_back(std::string("<DOMVideoItem name=\"") + name + "\" itemID=\"1\" videoDataHRef=\"" + name +
            ".dat\" width=\"8\" height=\"8\"/>");
        fla::Document::VideoInfo video;
        video.name = name;
        video.embedded = true;
        data.videos.push_back(video);
        data.extraFiles.emplace_back(std::string("bin/") + name + ".dat", std::vector<uint8_t>{1, 2, 3});
    }
    fla::Frame* frame = f.scene().layers[0]->frames[0];
    auto* raw = new fla::RawElement(frame);
    raw->videoItem = "clip.flv";
    raw->xml = "<DOMVideoInstance libraryItemName=\"clip.flv\" frameRight=\"160\" frameBottom=\"160\"/>";
    frame->elements.push_back(raw);
    CHECK(f.run("var lib = fl.getDocumentDOM().library;\n"
                "lib.items[lib.findItemIndex('clip.flv')].name = 'Intro';\n"
                "fl.trace(lib.itemExists('Intro') + ' ' + lib.getItemType('Intro'));\n"
                "try { lib.deleteItem('Intro'); } catch (e) { fl.trace('used'); }\n"
                "fl.trace(lib.deleteItem('spare.flv') + ' ' + lib.itemExists('spare.flv'));\n")
              .ok);
    expectOutput(f, "true video\nused\ntrue false\n");
    CHECK(raw->videoItem == "Intro" && raw->xml.find("libraryItemName=\"Intro\"") != std::string::npos);
    CHECK(data.videos.size() == 1 && data.extraMedia.size() == 1 && data.extraFiles.size() == 1);
    CHECK(data.extraMedia[0].find("name=\"Intro\"") != std::string::npos);
    f.host.history(f.document).undo();
    CHECK(raw->videoItem == "clip.flv" && data.videos.size() == 2 && data.extraFiles.size() == 2);
}

TEST(jsfl_document_arrange_selection)
{
    Fixture f;
    // Three instances, 10 by 10 in this host, at x 0, 40 and 100.
    CHECK(f.run("var d = fl.getDocumentDOM(), lib = d.library;\n"
                "lib.addNewItem('graphic', 'Box');\n"
                "lib.addItemToDocument({x: 5, y: 5}, 'Box'); var a = d.selection[0];\n"
                "lib.addItemToDocument({x: 45, y: 5}, 'Box'); var b = d.selection[0];\n"
                "lib.addItemToDocument({x: 105, y: 5}, 'Box'); var c = d.selection[0];\n"
                "b.name = 'hero';\n"
                "d.selectAll(); d.space('horizontal'); fl.trace(a.x + ' ' + b.x + ' ' + c.x);\n"
                "d.selection = [b]; d.invertSelection(); fl.trace(d.selection.length + ' ' + (d.selection.indexOf(b) < 0));\n"
                "fl.trace(d.getAlignToDocument()); d.setAlignToDocument(true); d.align('top'); d.setAlignToDocument(false);\n"
                "fl.trace(a.y + ' ' + c.y + ' ' + b.y);\n"
                "d.selection = [a]; d.match(true, false, true); fl.trace(a.matrix.a);\n"
                "d.resetTransformation(); d.skewSelection(45, 0); fl.trace(Math.round(a.matrix.c * 100) / 100 + ' ' + a.matrix.a);\n"
                "d.resetTransformation(); fl.trace(a.matrix.a + ' ' + a.matrix.c);\n"
                "a.locked = true; d.unlockAllElements(); fl.trace(a.locked);\n"
                "var found = fl.findObjectInDocByName('hero');\n"
                "fl.trace(found.length + ' ' + found[0].obj.name + ' ' + found[0].layer.name + ' ' + found[0].keyframe.startFrame);\n"
                "fl.trace(fl.findObjectInDocByType('instance', d).length + ' ' + fl.findObjectInDocByType('text').length);\n"
                "d.selectNone(); fl.trace(fl.selectElement(found[0], false) + ' ' + d.selection.length + ' ' + d.selection[0].name);\n"
                "d.selectAll(); d.distributeToLayers(); fl.trace(d.getTimeline().layerCount);\n"
                "fl.objectDrawingMode = true; fl.trace(fl.objectDrawingMode);\n")
              .ok);
    expectOutput(f, "0 50 100\n"
                    "2 true\n"
                    "false\n"
                    "0 0 0\n"
                    "55\n"
                    "1 1\n"
                    "1 0\n"
                    "false\n"
                    "1 hero Layer_1 0\n"
                    "3 0\n"
                    "true 1 hero\n"
                    "4\n"
                    "true\n");
    CHECK(f.host.style.objectDrawing);
    // All of it one step.
    f.host.history(f.document).undo();
    CHECK(f.scene().layers.size() == 1 && f.scene().layers[0]->frames[0]->elements.empty());
}

TEST(jsfl_document_text_blend_and_filters)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM(), lib = d.library;\n"
                "d.addNewText({left: 0, top: 0, right: 100, bottom: 20}, 'Hello');\n"
                "d.setTextString('Bye'); d.setElementTextAttr('bold', true);\n"
                "fl.trace(d.getTextString() + ' ' + d.selection[0].getTextAttr('bold'));\n"
                "lib.addNewItem('movie clip', 'Clip'); lib.addItemToDocument({x: 0, y: 0}, 'Clip');\n"
                "d.setBlendMode('multiply'); fl.trace(d.selection[0].blendMode);\n"
                "d.setFilters([{name: 'blurFilter', blurX: 5, blurY: 5}]); d.addFilter('glowFilter');\n"
                "function names() { var n = []; var list = d.getFilters(); for (var i = 0; i < list.length; i++) n.push(list[i].name + (list[i].enabled ? '' : '-off')); return n.join(','); }\n"
                "fl.trace(names());\n"
                "d.disableFilter(0); fl.trace(names()); d.enableAllFilters(); d.disableOtherFilters(1); fl.trace(names());\n"
                "d.enableAllFilters(); d.changeFilterOrder(0, 1); fl.trace(names());\n"
                "d.setFilterProperty('blurX', 1, 8); fl.trace(d.getFilters()[1].blurX);\n"
                "d.removeFilter(0); fl.trace(names()); d.removeAllFilters(); fl.trace(d.getFilters().length);\n"
                "try { d.removeFilter(3); } catch (e) { fl.trace('no filter'); }\n")
              .ok);
    expectOutput(f, "Bye true\n"
                    "multiply\n"
                    "blurFilter,glowFilter\n"
                    "blurFilter-off,glowFilter\n"
                    "blurFilter-off,glowFilter\n"
                    "glowFilter,blurFilter\n"
                    "8\n"
                    "blurFilter\n"
                    "0\n"
                    "no filter\n");
}

TEST(jsfl_document_combine_and_distribute_to_keyframes)
{
    Fixture f;
    f.host.style.objectDrawing = true;
    f.host.style.hasFill = true;
    CHECK(f.run("var d = fl.getDocumentDOM(), tl = d.getTimeline();\n"
                "d.addNewRectangle({left: 0, top: 0, right: 20, bottom: 20}, 0);\n"
                "d.addNewRectangle({left: 10, top: 10, right: 30, bottom: 30}, 0);\n"
                "d.addNewRectangle({left: 100, top: 100, right: 120, bottom: 120}, 0);\n"
                "var e = tl.layers[0].frames[0].elements;\n"
                "d.selection = [e[0], e[1]]; fl.trace(d.union() + ' ' + tl.layers[0].frames[0].elements.length);\n"
                "e = tl.layers[0].frames[0].elements; d.selection = [e[0], e[1]];\n"
                "fl.trace(d.intersect() + ' ' + tl.layers[0].frames[0].elements.length);\n"
                "d.selectNone(); fl.trace(d.punch());\n"
                "var lib = d.library; lib.addNewItem('graphic', 'Dot');\n"
                "tl.addNewLayer('Dots');\n"
                "for (var i = 0; i < 3; i++) lib.addItemToDocument({x: i * 30, y: 0}, 'Dot');\n"
                "d.selection = tl.layers[0].frames[0].elements; fl.trace(d.distributeToKeyframes() + ' ' + tl.layers[0].frameCount);\n"
                "var n = []; for (var i = 0; i < 3; i++) n.push(tl.layers[0].frames[i].startFrame + ':' + tl.layers[0].frames[i].elements.length);\n"
                "fl.trace(n.join(' '));\n")
              .ok);
    expectOutput(f, "true 2\nfalse 2\nfalse\ntrue 3\n0:1 1:1 2:1\n");
}

TEST(jsfl_timeline_reverse_frames_and_guides)
{
    Fixture f;
    CHECK(f.run("var d = fl.getDocumentDOM(), tl = d.getTimeline();\n"
                "tl.insertFrames(3); tl.layers[0].frames[0].name = 'a';\n"
                "tl.insertKeyframe(2); tl.layers[0].frames[2].name = 'b';\n"
                "tl.reverseFrames(0, 4);\n"
                "var n = []; for (var i = 0; i < 4; i++) n.push(tl.layers[0].frames[i].startFrame + tl.layers[0].frames[i].name);\n"
                "fl.trace(n.join(',') + ' ' + tl.frameCount);\n"
                "fl.trace(tl.getGuidelines());\n"
                "fl.trace(tl.setGuidelines('<guidelines><guideline direction=\"v\">12.5</guideline><guideline direction=\"h\">40</guideline></guidelines>'));\n"
                "fl.trace(tl.getGuidelines());\n"
                "fl.trace(tl.setGuidelines('<guidelines><guideline direction=\"z\">1</guideline></guidelines>'));\n")
              .ok);
    // Frame by frame back to front, as Modify > Timeline > Reverse Frames does.
    expectOutput(f, "0,1b,1b,3a 4\n"
                    "<guidelines></guidelines>\n"
                    "true\n"
                    "<guidelines><guideline direction=\"v\">12.5</guideline><guideline direction=\"h\">40</guideline></guidelines>\n"
                    "false\n");
    CHECK(f.scene().guidesX.size() == 1 && f.scene().guidesY.size() == 1);
    f.host.history(f.document).undo();
    CHECK(f.scene().guidesX.empty() && f.scene().guidesY.empty());
}

TEST(jsfl_bitmap_export_lossless_as_png)
{
    Fixture f;
    // Two pixels: opaque red, and blue at half alpha (premultiplied).
    const std::vector<uint32_t> pixels = {0xFFFF0000u, 0x80000080u};
    fla::Bitmap* bitmap = fla::media::newBitmap(f.data(), "pixels.png", "", 2, 1, fla::encodeLosslessBitmap(2, 1, pixels), false);
    bitmap->parent = &f.data();
    f.data().resources.push_back(bitmap);
    const std::string folder = jsfl_test::scratchFolder("bitmap_export");
    const std::string base = jsfl::uri::fromPath(folder) + "/";
    CHECK(f.run("var lib = fl.getDocumentDOM().library; var item = lib.items[lib.findItemIndex('pixels.png')];\n"
                "fl.trace(item.exportToFile('" + base + "pixels.png'));\n"
                "try { item.exportToFile('" + base + "pixels.jpg'); } catch (e) { fl.trace('no jpeg'); }\n")
              .ok);
    expectOutput(f, "true\nno jpeg\n");
    const std::string png = readFile(folder + "/pixels.png");
    CHECK(png.size() > 33 && png.compare(1, 3, "PNG") == 0 && png.compare(12, 4, "IHDR") == 0);
    // The rows inflated back: filter byte, then straight RGBA.
    const size_t idat = png.find("IDAT");
    CHECK(idat != std::string::npos);
    if (idat == std::string::npos)
        return;
    const auto byte = [&png](size_t at) { return static_cast<uint32_t>(static_cast<uint8_t>(png[at])); };
    const uint32_t length = byte(idat - 4) << 24 | byte(idat - 3) << 16 | byte(idat - 2) << 8 | byte(idat - 1);
    std::vector<uint8_t> rows(9);
    uLongf size = static_cast<uLongf>(rows.size());
    CHECK(uncompress(rows.data(), &size, reinterpret_cast<const Bytef*>(png.data() + idat + 4), length) == Z_OK);
    CHECK(size == 9);
    const std::vector<uint8_t> wanted = {0, 255, 0, 0, 255, 0, 0, 255, 128};
    CHECK(rows == wanted);
}
