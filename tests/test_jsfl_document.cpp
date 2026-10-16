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
