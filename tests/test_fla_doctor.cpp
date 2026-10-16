#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/parser/fla_doctor.h"

#include <filesystem>
#include <fstream>
#include <memory>

TEST(the_doctor_salvages_nothing_outside_its_folder)
{
    // Stored entries: the document, one named to climb out of the folder,
    // and a deflated one whose header ends the file.
    std::vector<uint8_t> zip;
    const auto entry = [&zip](const std::string& name, const std::string& text, uint16_t method) {
        const auto u16 = [&zip](uint32_t v) {
            zip.push_back(static_cast<uint8_t>(v));
            zip.push_back(static_cast<uint8_t>(v >> 8));
        };
        zip.insert(zip.end(), {0x50, 0x4B, 0x03, 0x04, 20, 0, 0, 0});
        u16(method);
        zip.insert(zip.end(), 8, 0); // time, date, crc
        u16(static_cast<uint32_t>(text.size()));
        u16(0);
        u16(static_cast<uint32_t>(text.size()));
        u16(0);
        u16(static_cast<uint32_t>(name.size()));
        u16(0);
        zip.insert(zip.end(), name.begin(), name.end());
        zip.insert(zip.end(), text.begin(), text.end());
    };
    entry("DOMDocument.xml", "<DOMDocument/>", 0);
    entry("../phoenix_doctor_escaped.txt", "out", 0);
    entry("LIBRARY/end.xml", "", 8);
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "phoenix_doctor_salvage";
    fs::remove_all(base);
    fs::create_directories(base);
    const fs::path archive = base / "broken.fla";
    {
        std::ofstream out(archive, std::ios::binary);
        out.write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));
    }
    std::vector<std::string> recovered, notes;
    std::string error;
    CHECK(fla::doctor::salvage(archive.u8string(), (base / "out").u8string(), recovered, notes, error));
    CHECK(fs::exists(base / "out" / "DOMDocument.xml"));
    CHECK(!fs::exists(base / "phoenix_doctor_escaped.txt"));
    CHECK(recovered.size() == 1);
    fs::remove_all(base);
}

TEST(the_doctor_finds_and_repairs_broken_frames_and_missing_symbols)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    document->symbolList = new fla::SymbolList(document.get());
    fla::Symbol* box = new fla::Symbol(document->symbolList);
    box->name = "Box";
    fla::Timeline* inner = new fla::Timeline(box);
    fla::Layer* innerLayer = new fla::Layer(inner);
    fla::Frame* innerFrame = new fla::Frame(innerLayer);
    // The symbol holds itself.
    fla::SymbolInstance* self = new fla::SymbolInstance(innerFrame);
    self->libraryItemName = "Box";
    self->symbol = box;
    innerFrame->elements.push_back(self);
    innerLayer->frames.push_back(innerFrame);
    inner->layers.push_back(innerLayer);
    box->timelines.push_back(inner);
    document->symbolList->symbols.push_back(box);
    document->symbolList->symbolMap["Box"] = box;

    fla::Timeline* scene = new fla::Timeline(document.get());
    scene->name = "Scene 1";
    fla::Layer* layer = new fla::Layer(scene);
    layer->name = "Layer 1";
    layer->parentLayerIndex = 7;
    // Out of order, the second over the first, one of no length.
    fla::Frame* late = new fla::Frame(layer);
    late->index = 3;
    late->duration = 0;
    fla::Frame* early = new fla::Frame(layer);
    early->index = 0;
    early->duration = 5;
    fla::SymbolInstance* lost = new fla::SymbolInstance(early);
    lost->libraryItemName = "Gone";
    early->elements.push_back(lost);
    early->extraAttributes.emplace_back("soundName", "missing.wav");
    layer->frames = {late, early};
    scene->layers.push_back(layer);
    document->timelines.push_back(scene);

    fla::doctor::Report found;
    fla::doctor::checkDocument(*document, found, false);
    CHECK(found.count(fla::doctor::Issue::Severity::Error) >= 5);
    CHECK(layer->frames.front() == late);

    fla::doctor::Report fixed;
    fla::doctor::checkDocument(*document, fixed, true);
    CHECK(layer->frames.front() == early && layer->frames.back() == late);
    CHECK(early->duration == 3 && late->duration == 1);
    CHECK(layer->parentLayerIndex == -1);
    CHECK(lost->symbol && lost->symbol->name == "Gone");
    CHECK(document->symbolList->symbolMap.count("Gone") == 1);
    CHECK(innerFrame->elements.empty());
    CHECK(early->extraAttributes.empty());

    // Checked again, nothing is left.
    fla::doctor::Report again;
    fla::doctor::checkDocument(*document, again, false);
    CHECK(again.count(fla::doctor::Issue::Severity::Error) == 0);
}
