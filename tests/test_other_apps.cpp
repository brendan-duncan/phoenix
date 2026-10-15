#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/adjustments.h"
#include "../src/edit/other_apps.h"
#include "../src/edit/templates.h"
#include "../src/parser/fla_parser.h"
#include "../src/writer/xfl_folder_writer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

const char* const kNs = "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"http://ns.adobe.com/xfl/2008/\"";

/// A lower-third template placed twice (one with values), a matte pair and
/// an adjustment layer with a keyed blur over two layers.
void writeSample(const fs::path& folder)
{
    fs::remove_all(folder);
    const std::string ns = kNs;
    writeText(folder / "sample.xfl", "PROXY-CS5");
    writeText(folder / "LIBRARY" / "Bar.xml",
        "<DOMSymbolItem " + ns + " name=\"Bar\" symbolType=\"movie clip\"><timeline><DOMTimeline name=\"Bar\"><layers>"
        "<DOMLayer name=\"L\"><frames><DOMFrame index=\"0\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/>"
        "</FillStyle></fills><edges><Edge fillStyle1=\"1\" edges=\"!0 0|400 0!400 0|400 40!400 40|0 40!0 40|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    writeText(folder / "LIBRARY" / "Lower.xml",
        "<DOMSymbolItem " + ns + " name=\"Lower\" symbolType=\"graphic\"><timeline><DOMTimeline name=\"Lower\"><layers>"
        "<DOMLayer name=\"T\"><frames><DOMFrame index=\"0\"><elements><DOMDynamicText instanceName=\"title\" width=\"200\" height=\"30\">"
        "<textRuns><DOMTextRun><characters>Name</characters><textAttrs><DOMTextAttrs face=\"ArialMT\" size=\"20\"/></textAttrs>"
        "</DOMTextRun></textRuns></DOMDynamicText><DOMSymbolInstance libraryItemName=\"Bar\" name=\"bar\" symbolType=\"movie clip\"/>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    writeText(folder / "DOMDocument.xml",
        "<DOMDocument " + ns + " width=\"300\" height=\"200\" frameRate=\"24\">"
        "<symbols><Include href=\"Bar.xml\"/><Include href=\"Lower.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"Grade\" phoenixAdjustmentLayer=\"true\" phoenixAdjustments=\"blur?radius=@0=0;9=10&amp;amount=100|tint?color=%23FF0000&amp;amount=50\">"
        "<frames><DOMFrame index=\"0\"><elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"Spot\"><frames><DOMFrame index=\"0\" duration=\"10\"><elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"Picture\" phoenixMatte=\"alpha\"><frames><DOMFrame index=\"0\" duration=\"10\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Lower\" symbolType=\"graphic\" loop=\"loop\"/>"
        "<DOMSymbolInstance libraryItemName=\"Lower\" symbolType=\"graphic\" loop=\"loop\" phoenixTemplate=\"title=Ada&amp;bar=%2300C000\"/>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

bool near(double a, double b)
{
    return std::fabs(a - b) < 1e-6;
}

fla::Symbol* symbolNamed(fla::Document& document, const std::string& name)
{
    const auto found = document.symbolList->symbolMap.find(name);
    return found == document.symbolList->symbolMap.end() ? nullptr : found->second;
}

} // namespace

TEST(other_apps_bake_templates_mattes_and_adjustments)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    writeSample(root / "sample");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse((root / "sample").string()));
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    CHECK(fla::otherapps::needsBaking(document));
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes);
    CHECK(!fla::otherapps::needsBaking(document));
    CHECK(notes.size() == 3);

    // The adjustment layer now shows a movie clip of the layers below it,
    // with a keyframe a frame for the keyed blur.
    fla::Timeline* scene = document.timelines.front();
    CHECK(scene->layers.size() == 1);
    fla::Layer* grade = scene->layers.front();
    CHECK(grade->frames.size() == 10);
    auto* clip = dynamic_cast<fla::SymbolInstance*>(grade->frames[9]->elements.front());
    CHECK(clip && clip->symbolType == fla::SymbolType::MovieClip && clip->filters.size() == 1);
    if (!clip || !clip->symbol || clip->filters.empty())
        return;
    CHECK(near(clip->filters.front().blurX, 20.0) && near(clip->colorTransform.tintMultiplier, 0.5));
    CHECK(near(dynamic_cast<fla::SymbolInstance*>(grade->frames[0]->elements.front())->filters.front().blurX, 0.0));

    // Inside: the matte became a mask over the picture.
    fla::Timeline* inside = clip->symbol->timelines.front();
    CHECK(inside->layers.size() == 2);
    CHECK(inside->layers[0]->layerType == fla::Layer::Type::Mask && inside->layers[1]->layerType == fla::Layer::Type::Masked &&
        inside->layers[1]->parentLayer == inside->layers[0] && inside->layers[1]->parentLayerIndex == 0);

    // The templated instance shows a copy with its words and color.
    const std::vector<fla::Element*>& placed = inside->layers[1]->frames.front()->elements;
    auto* plain = static_cast<fla::SymbolInstance*>(placed[0]);
    auto* filled = static_cast<fla::SymbolInstance*>(placed[1]);
    CHECK(plain->libraryItemName == "Lower" && filled->libraryItemName == "Lower (Ada)" && !fla::templates::any(*filled));
    fla::Symbol* copy = symbolNamed(document, "Lower (Ada)");
    CHECK(copy != nullptr);
    if (!copy)
        return;
    const std::vector<fla::Element*>& parts = copy->timelines.front()->layers.front()->frames.front()->elements;
    CHECK(static_cast<fla::StaticText*>(parts[0])->runs.front().text == "Ada");
    const auto* bar = static_cast<fla::SymbolInstance*>(parts[1]);
    CHECK(bar->colorTransform.tintMultiplier == 1.0 && bar->colorTransform.tintColor[1] == 0xC0);
    // The original symbol is untouched.
    CHECK(static_cast<fla::StaticText*>(symbolNamed(document, "Lower")->timelines.front()->layers.front()->frames.front()->elements[0])
              ->runs.front().text == "Name");

    // And it writes and reads back.
    fs::remove_all(root / "xfl");
    fla::XFLFolderWriter writer;
    CHECK(writer.write(document, (root / "xfl").string()));
    FLAParser again;
    std::unique_ptr<fla::FLADocument> back(again.parse((root / "xfl").string()));
    CHECK(back && back->document && symbolNamed(*back->document, "Lower (Ada)") &&
        symbolNamed(*back->document, "Adjusted by Grade"));
}
