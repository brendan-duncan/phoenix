#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/bitmap.h"
#include "../src/data/bitmap_decoder.h"
#include "../src/data/bitmap_instance.h"
#include "../src/data/edge.h"
#include "../src/data/document.h"
#include "../src/data/filter.h"
#include "../src/data/folder.h"
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
#include <map>
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

TEST(other_apps_cut_by_inverted_mattes_and_keep_adjustment_art)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    writeSample(root / "inverted");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse((root / "inverted").string()));
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    fla::Timeline* scene = document.timelines.front();
    // The picture cut by its matte rather than shown through it, and art on
    // the adjustment layer.
    for (auto& attribute : scene->layers[2]->extraAttributes)
        if (attribute.first == "phoenixMatte")
            attribute.second = "alphaInverted";
    auto* art = new fla::Shape(scene->layers[0]->frames.front());
    scene->layers[0]->frames.front()->elements.push_back(art);
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes);

    // On top, the art as a mask over the adjusted clip, the clip as it is
    // under both.
    CHECK(scene->layers.size() == 3);
    CHECK(scene->layers[0]->layerType == fla::Layer::Type::Mask);
    CHECK(scene->layers[1]->layerType == fla::Layer::Type::Masked && scene->layers[1]->parentLayerIndex == 0);
    auto* adjusted = static_cast<fla::SymbolInstance*>(scene->layers[1]->frames.front()->elements.front());
    auto* plain = static_cast<fla::SymbolInstance*>(scene->layers[2]->frames.front()->elements.front());
    CHECK(adjusted->symbol == plain->symbol && !adjusted->filters.empty() && plain->filters.empty());
    // Inside, the matte and its layer in a clip with Layer blending, the
    // matte's art erasing.
    fla::Timeline* inside = plain->symbol->timelines.front();
    CHECK(inside->layers.size() == 1);
    auto* both = static_cast<fla::SymbolInstance*>(inside->layers[0]->frames.front()->elements.front());
    CHECK(both->blendMode == "layer" && both->symbol && both->symbol->name == "Picture cut by its matte");
    fla::Timeline* cut = both->symbol->timelines.front();
    CHECK(cut->layers.size() == 2 && cut->layers[1]->name == "Picture");
    auto* eraser = static_cast<fla::SymbolInstance*>(cut->layers[0]->frames.front()->elements.front());
    CHECK(eraser->blendMode == "erase" && eraser->symbol->timelines.front()->layers.front()->name == "Spot");
}

TEST(other_apps_bake_pins_and_responsive_time)
{
    // A card of 6 frames whose middle stretches, its plate pinned to the
    // width of its name: drawn "Name", then "Longer Name" from frame 4. It
    // is placed for 10 frames with a longer name.
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    const fs::path folder = root / "pins";
    fs::remove_all(folder);
    const std::string ns = kNs;
    writeText(folder / "pins.xfl", "PROXY-CS5");
    writeText(folder / "LIBRARY" / "Plate.xml",
        "<DOMSymbolItem " + ns + " name=\"Plate\" symbolType=\"graphic\"><timeline><DOMTimeline name=\"Plate\"><layers>"
        "<DOMLayer name=\"L\"><frames><DOMFrame index=\"0\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/>"
        "</FillStyle></fills><edges><Edge fillStyle1=\"1\" edges=\"!0 0|2000 0!2000 0|2000 400!2000 400|0 400!0 400|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    const auto field = [](const std::string& words) {
        return "<DOMDynamicText instanceName=\"name\" width=\"300\" height=\"30\"><matrix><Matrix tx=\"10\"/></matrix><textRuns><DOMTextRun>"
               "<characters>" + words + "</characters><textAttrs><DOMTextAttrs face=\"ArialMT\" size=\"20\"/></textAttrs></DOMTextRun>"
               "</textRuns></DOMDynamicText>";
    };
    writeText(folder / "LIBRARY" / "Card.xml",
        "<DOMSymbolItem " + ns + " name=\"Card\" symbolType=\"graphic\"><timeline><DOMTimeline name=\"Card\"><layers>"
        "<DOMLayer name=\"Labels\"><frames><DOMFrame index=\"0\" duration=\"2\"><elements/></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"2\" name=\"stretch\"><elements/></DOMFrame><DOMFrame index=\"4\" duration=\"2\" name=\"outro\">"
        "<elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"Name\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements>" + field("Name") + "</elements></DOMFrame>"
        "<DOMFrame index=\"3\" duration=\"3\"><elements>" + field("Longer Name") + "</elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"Plate\"><frames><DOMFrame index=\"0\" duration=\"6\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Plate\" symbolType=\"graphic\" loop=\"loop\" phoenixPin=\"text=name&amp;mode=width\"/>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    writeText(folder / "DOMDocument.xml",
        "<DOMDocument " + ns + " width=\"300\" height=\"200\" frameRate=\"24\">"
        "<symbols><Include href=\"Plate.xml\"/><Include href=\"Card.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"Card\"><frames><DOMFrame index=\"0\" duration=\"10\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Card\" symbolType=\"graphic\" loop=\"play once\" phoenixTemplate=\"name=Ada%20Lovelace\"/>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse(folder.string()));
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    CHECK(fla::otherapps::needsBaking(document));
    // Letters 10 pixels wide; the plate 100 by 20.
    fla::otherapps::Measure measure;
    measure.textReach = [](const fla::StaticText& text, fla::Rect& reach) {
        size_t letters = 0;
        for (const fla::TextRun& run : text.runs)
            letters += run.text.size();
        reach = fla::Rect(fla::Point(text.transform.tx, 0.0), fla::Point(text.transform.tx + 10.0 * letters, 20.0));
        return true;
    };
    measure.symbolBounds = [](const fla::Symbol& symbol, int, fla::Rect& bounds) {
        bounds = fla::Rect(fla::Point(0.0, 0.0), fla::Point(100.0, 20.0));
        return symbol.name == "Plate";
    };
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes, fla::otherapps::Renderer(), &measure);
    CHECK(!fla::otherapps::needsBaking(document));

    // The card plays a copy for its 10 frames: the start and the ending at
    // their own speed, the middle frames held longer.
    auto* card = static_cast<fla::SymbolInstance*>(document.timelines.front()->layers.front()->frames.front()->elements.front());
    CHECK(card->symbol && card->symbol->name == "Card (Ada Lovelace) (10 frames)" && card->loopType == fla::LoopType::PlayOnce &&
        card->firstFrame == 0);
    if (!card->symbol)
        return;
    const std::vector<fla::Frame*>& held = card->symbol->timelines.front()->layers.front()->frames;
    const int shown[][3] = {{0, 1, 0}, {1, 1, 1}, {2, 3, 2}, {5, 3, 3}, {8, 1, 4}, {9, 1, 5}};
    CHECK(held.size() == 6);
    for (size_t i = 0; i < held.size() && i < 6; ++i)
    {
        const auto* inner = static_cast<const fla::SymbolInstance*>(held[i]->elements.front());
        CHECK(held[i]->index == shown[i][0] && held[i]->duration == shown[i][1] && inner->firstFrame == shown[i][2] &&
            inner->loopType == fla::LoopType::SingleFrame && inner->symbol->name == "Card (Ada Lovelace)");
    }

    // Inside the filled-in copy, the plate stretched to the longer name:
    // 120 pixels of words against 40 drawn, then against 110.
    fla::Symbol* copy = symbolNamed(document, "Card (Ada Lovelace)");
    CHECK(copy != nullptr);
    if (!copy)
        return;
    const std::vector<fla::Frame*>& plate = copy->timelines.front()->layers[2]->frames;
    CHECK(plate.size() == 2);
    if (plate.size() != 2)
        return;
    const auto* first = static_cast<const fla::SymbolInstance*>(plate[0]->elements.front());
    const auto* second = static_cast<const fla::SymbolInstance*>(plate[1]->elements.front());
    CHECK(plate[0]->index == 0 && plate[0]->duration == 3 && plate[1]->index == 3 && plate[1]->duration == 3);
    CHECK(near(first->transform.m11, 1.8) && near(second->transform.m11, 1.1) && near(first->transform.tx, 0.0));
    CHECK(!fla::templates::pinOf(*first).any() && !fla::templates::pinOf(*second).any());
    // The second piece carries the looping plate on from where it had got.
    CHECK(second->firstFrame == 3 && second->loopType == fla::LoopType::Loop);
    // The original symbol's plate is as drawn.
    const std::vector<fla::Frame*>& drawn = symbolNamed(document, "Card")->timelines.front()->layers[2]->frames;
    CHECK(drawn.size() == 1 && near(drawn.front()->elements.front()->transform.m11, 1.0));
}

TEST(edited_cubics_save_as_quadratics_and_a_cubics_edge)
{
    // A straight edge of a symbol's shape bent into a cubic, as the pen or
    // the selection tool leaves it, then saved as an XFL folder.
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    writeSample(root / "cubics");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse((root / "cubics").string()));
    CHECK(file && file->document);
    fla::Symbol* bar = file && file->document ? symbolNamed(*file->document, "Bar") : nullptr;
    CHECK(bar != nullptr);
    if (!bar)
        return;
    auto* shape = static_cast<fla::Shape*>(bar->timelines.front()->layers.front()->frames.front()->elements.front());
    fla::Edge* edge = shape->edges.front();
    fla::Path* path = edge->paths.front();
    CHECK(path->segments.size() >= 2 && path->segments[1]->command == fla::PathSegment::Command::Line);
    path->segments[1]->command = fla::PathSegment::Command::Cubic;
    path->segments[1]->points = {fla::Point(100, -50), fla::Point(300, -50), fla::Point(400, 0)};
    edge->data.clear();

    fs::remove_all(root / "cubics_out");
    fla::XFLFolderWriter writer;
    CHECK(writer.write(*file->document, (root / "cubics_out").string()));
    std::ifstream in(root / "cubics_out" / "LIBRARY" / "Bar.xml", std::ios::binary);
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // The edges hold quadratics only; the cubic goes in an edge of its own,
    // in the form Animate writes: start, then control, control and end in
    // twips, then the quadratics.
    const size_t edges = xml.find("edges=\"");
    CHECK(edges != std::string::npos && xml.find('(', edges) > xml.find('"', edges + 7));
    CHECK(xml.find("<Edge cubics=\"!0 0(;2000,-1000 6000,-1000 8000,0q0 0Q") != std::string::npos);

    // And it reads back as the same curve.
    FLAParser again;
    std::unique_ptr<fla::FLADocument> back(again.parse((root / "cubics_out").string()));
    CHECK(back && back->document && symbolNamed(*back->document, "Bar"));
}

namespace {

/// A movie clip "Bar" placed on a layer of 3 frames with \a effects on it,
/// under an adjustment layer with \a adjustments (none when empty).
std::unique_ptr<fla::FLADocument> effectsSample(const fs::path& folder, const std::string& effects, const std::string& adjustments)
{
    writeSample(folder);
    const std::string ns = kNs;
    std::string layers;
    if (!adjustments.empty())
        layers += "<DOMLayer name=\"Grade\" phoenixAdjustmentLayer=\"true\" phoenixAdjustments=\"" + adjustments +
            "\"><frames><DOMFrame index=\"0\"><elements/></DOMFrame></frames></DOMLayer>";
    layers += "<DOMLayer name=\"Bars\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements>"
              "<DOMSymbolInstance libraryItemName=\"Bar\" name=\"bar\" symbolType=\"movie clip\" phoenixEffects=\"" + effects +
        "\"><filters><DropShadowFilter/></filters></DOMSymbolInstance></elements></DOMFrame></frames></DOMLayer>";
    writeText(folder / "DOMDocument.xml",
        "<DOMDocument " + ns + " width=\"300\" height=\"200\" frameRate=\"24\">"
        "<symbols><Include href=\"Bar.xml\"/><Include href=\"Lower.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>" + layers + "</layers></DOMTimeline></timelines></DOMDocument>");
    FLAParser parser;
    return std::unique_ptr<fla::FLADocument>(parser.parse(folder.string()));
}

fla::behaviors::Applied effect(const std::string& type, std::map<std::string, std::string> settings)
{
    fla::behaviors::Applied applied = fla::compositing::defaults(type, 1);
    for (const auto& [name, value] : settings)
        applied.params[name] = value;
    return applied;
}

/// A picture of \a width by \a height the renderer hands back, its pixels
/// all \a color.
fla::otherapps::Picture solid(int width, int height, uint32_t color, double x, double y)
{
    fla::otherapps::Picture picture;
    picture.width = width;
    picture.height = height;
    picture.x = x;
    picture.y = y;
    picture.pixels.assign(static_cast<size_t>(width) * height, color);
    return picture;
}

} // namespace

TEST(other_apps_carry_effects_as_filters)
{
    using fla::otherapps::carryEffects;
    std::vector<fla::Filter> filters;
    fla::ColorTransform color;
    std::vector<std::string> left;

    // Glow and Outline as Glow filters, the outline solid near the edge.
    CHECK(carryEffects({effect("glow", {{"radius", "10"}, {"intensity", "150"}, {"color", "#FFE060"}}),
                           effect("outline", {{"width", "4"}, {"color", "#000000"}, {"amount", "50"}})},
        0, filters, color));
    CHECK(filters.size() == 2 && filters[0].type == fla::Filter::Type::Glow && near(filters[0].blurX, 20.0) &&
        near(filters[0].strength, 1.5) && filters[0].color[0] == 0xFF && filters[0].color[1] == 0xE0);
    CHECK(near(filters[1].blurX, 8.0) && near(filters[1].strength, 3.0) && filters[1].color[3] == 128);

    // A smear across is a Blur on that axis alone; a slanted one is not.
    filters.clear();
    CHECK(carryEffects({effect("dirblur", {{"angle", "180"}, {"length", "30"}})}, 0, filters, color));
    CHECK(filters.size() == 1 && near(filters[0].blurX, 30.0) && near(filters[0].blurY, 0.0) && filters[0].quality == 1);
    filters.clear();
    CHECK(!carryEffects({effect("dirblur", {{"angle", "45"}})}, 0, filters, color, &left));
    CHECK(filters.empty() && left.size() == 1 && left[0] == "Directional Blur");

    // Hue/Saturation as Adjust Color, unless it colorizes.
    filters.clear();
    CHECK(carryEffects({effect("huesat", {{"hue", "40"}, {"saturation", "-50"}, {"lightness", "10"}})}, 0, filters, color));
    CHECK(filters.size() == 1 && filters[0].type == fla::Filter::Type::AdjustColor && near(filters[0].hue, 40.0) &&
        near(filters[0].saturation, -50.0) && near(filters[0].brightness, 10.0));
    CHECK(!carryEffects({effect("huesat", {{"colorize", "true"}})}, 0, filters, color));

    // Fill is a full tint; a second color effect has no room.
    filters.clear();
    CHECK(carryEffects({effect("fill", {{"color", "#00FF00"}})}, 0, filters, color));
    CHECK(near(color.tintMultiplier, 1.0) && color.tintColor[1] == 0xFF && filters.empty());
    CHECK(!carryEffects({effect("tint", {})}, 0, filters, color));

    // Levels squeezed into a narrower range is a multiplier and an offset;
    // stretched, or with a gamma, it is not.
    fla::ColorTransform levels;
    CHECK(carryEffects({effect("levels", {{"outBlack", "20"}, {"outWhite", "220"}})}, 0, filters, levels));
    CHECK(near(levels.redMultiplier, 200.0 / 255.0) && levels.redOffset == 20 && levels.blueOffset == 20);
    fla::ColorTransform stretched;
    CHECK(!carryEffects({effect("levels", {{"inWhite", "200"}})}, 0, filters, stretched));
    CHECK(!carryEffects({effect("levels", {{"gamma", "1.5"}})}, 0, filters, stretched));

    // A wipe not started is nothing, finished is nothing to see, part way
    // cannot be carried; and Ripple never can.
    fla::ColorTransform wiped;
    CHECK(carryEffects({effect("linearwipe", {{"completion", "0"}})}, 0, filters, wiped) && wiped.isIdentity());
    CHECK(carryEffects({effect("linearwipe", {{"completion", "@0=0;10=100"}})}, 10, filters, wiped) &&
        near(wiped.alphaMultiplier, 0.0));
    left.clear();
    CHECK(!carryEffects({effect("linearwipe", {{"completion", "50"}}), effect("ripple", {})}, 0, filters, wiped, &left));
    CHECK(left.size() == 2 && left[1] == "Ripple");
    // Off, or at an amount of 0, changes nothing.
    fla::behaviors::Applied off = effect("ripple", {});
    off.enabled = false;
    CHECK(carryEffects({off, effect("ripple", {{"amount", "0"}})}, 0, filters, wiped));
}

TEST(other_apps_carry_instance_effects)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    std::unique_ptr<fla::FLADocument> file = effectsSample(root / "instance_effects",
        "blur?radius=@0=2;2=6&amp;amount=100|fill?color=%2300FF00&amp;amount=100", "");
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    CHECK(fla::otherapps::needsBaking(document));
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes);
    CHECK(!fla::otherapps::needsBaking(document));
    CHECK(notes.empty());

    // The keyed blur: a keyframe a frame, each with its blur before the
    // drop shadow the instance had, and the fill as a tint.
    fla::Layer* bars = document.timelines.front()->layers.front();
    CHECK(bars->frames.size() == 3);
    if (bars->frames.size() != 3)
        return;
    for (int f = 0; f < 3; ++f)
    {
        auto* bar = static_cast<fla::SymbolInstance*>(bars->frames[static_cast<size_t>(f)]->elements.front());
        CHECK(bar->filters.size() == 2 && bar->filters[0].type == fla::Filter::Type::Blur &&
            bar->filters[1].type == fla::Filter::Type::DropShadow);
        CHECK(near(bar->filters[0].blurX, (2.0 + 2.0 * f) * 2.0));
        CHECK(near(bar->colorTransform.tintMultiplier, 1.0) && bar->colorTransform.tintColor[1] == 0xFF);
        CHECK(fla::compositing::instanceEffectsOf(*bar).empty());
    }
}

TEST(other_apps_draw_effects_with_no_filter_into_bitmaps)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    std::unique_ptr<fla::FLADocument> file = effectsSample(root / "bitmap_effects", "ripple?height=6|glow", "");
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    fla::Layer* bars = document.timelines.front()->layers.front();
    const fla::Element* original = bars->frames.front()->elements.front();

    // The renderer draws the instance on its own: the same picture on the
    // first two frames, another on the third.
    std::vector<fla::otherapps::Drawing> asked;
    const fla::otherapps::Renderer render = [&](const fla::otherapps::Drawing& drawing) {
        asked.push_back(drawing);
        fla::otherapps::Picture picture = solid(6, 4, drawing.frame < 2 ? 0xFFFF0000u : 0xFF0000FFu, 10.0, 20.0);
        picture.scale = 2.0;
        // Clear round the edge, which the bitmap leaves out.
        for (int x = 0; x < 6; ++x)
            picture.pixels[static_cast<size_t>(x)] = 0;
        return picture;
    };
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes, render);
    CHECK(asked.size() == 3);
    for (size_t i = 0; i < asked.size(); ++i)
        CHECK(asked[i].element == original && asked[i].frame == static_cast<int>(i) && asked[i].from == 0 &&
            asked[i].to == 1 && asked[i].margin > 6.0);
    CHECK(notes.size() == 1 && notes[0].find("Ripple has no filter") != std::string::npos);

    // Two keyframes of bitmaps, the first held for two frames.
    CHECK(bars->frames.size() == 2);
    if (bars->frames.size() != 2)
        return;
    CHECK(bars->frames[0]->duration == 2 && bars->frames[1]->index == 2);
    auto* first = dynamic_cast<fla::BitmapInstance*>(bars->frames[0]->elements.front());
    auto* last = dynamic_cast<fla::BitmapInstance*>(bars->frames[1]->elements.front());
    CHECK(first && last && first->libraryItemName != last->libraryItemName);
    if (!first || !last)
        return;
    CHECK(first->libraryItemName.rfind("Baked effects/", 0) == 0);
    // Placed where the picture was, its top row trimmed, at half size.
    CHECK(near(first->transform.m11, 0.5) && near(first->transform.tx, 10.0) && near(first->transform.ty, 20.5));
    bool folder = false;
    for (const fla::Folder* each : document.folders)
        folder = folder || each->name == "Baked effects";
    int bitmaps = 0;
    for (const fla::Resource* resource : document.resources)
        if (resource->resourceType() == fla::Resource::Type::Bitmap)
        {
            const auto* bitmap = static_cast<const fla::Bitmap*>(resource);
            fla::DecodedBitmap pixels;
            CHECK(fla::decodeLosslessBitmap(bitmap->datData.get(), pixels) && pixels.width == 6 && pixels.height == 3);
            ++bitmaps;
        }
    CHECK(folder && bitmaps == 2);

    // Without a renderer the effects that cannot be carried are left out.
    std::unique_ptr<fla::FLADocument> plain = effectsSample(root / "bitmap_effects_plain", "ripple?height=6|glow", "");
    notes.clear();
    fla::otherapps::bakeCompositing(*plain->document, notes);
    CHECK(notes.size() == 1 && notes[0].find("left out") != std::string::npos);
    auto* kept = dynamic_cast<fla::SymbolInstance*>(plain->document->timelines.front()->layers.front()->frames.front()->elements.front());
    CHECK(kept && kept->filters.size() == 2 && kept->filters[0].type == fla::Filter::Type::Glow);
}

TEST(other_apps_draw_adjustment_layers_with_no_filter_into_bitmaps)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    std::unique_ptr<fla::FLADocument> file = effectsSample(root / "bitmap_adjustments", "blur", "vignette|blur");
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    fla::Timeline* scene = document.timelines.front();
    const fla::Layer* grade = scene->layers.front();
    std::vector<fla::otherapps::Drawing> asked;
    const fla::otherapps::Renderer render = [&](const fla::otherapps::Drawing& drawing) {
        asked.push_back(drawing);
        return solid(30, 20, 0xFF808080u, 0.0, 0.0);
    };
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes, render);

    // It and all below, drawn a frame at a time; the instance below, being
    // in those pictures, is not drawn on its own.
    CHECK(asked.size() == 3);
    for (const fla::otherapps::Drawing& drawing : asked)
        CHECK(drawing.timeline == scene && drawing.from == 0 && drawing.to == 2 && !drawing.element);
    // One keyframe of the bitmap held over the three frames; the layers
    // below kept in the movie clip in the library.
    CHECK(scene->layers.size() == 1 && scene->layers.front() == grade);
    CHECK(grade->frames.size() == 1 && grade->frames.front()->duration == 3);
    CHECK(dynamic_cast<fla::BitmapInstance*>(grade->frames.front()->elements.front()) != nullptr);
    fla::Symbol* clip = symbolNamed(document, "Adjusted by Grade");
    CHECK(clip && clip->timelines.front()->layers.size() == 1);
    CHECK(!fla::otherapps::needsBaking(document));
    bool told = false;
    for (const std::string& note : notes)
        told = told || note.find("Vignette has no filter") != std::string::npos;
    CHECK(told);
}

TEST(other_apps_bake_several_masks)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_other_apps_tests";
    const fs::path folder = root / "masks";
    fs::remove_all(folder);
    const std::string ns = kNs;
    const auto square = [](int x) {
        return "<DOMFrame index=\"0\" duration=\"4\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#000000\"/>"
               "</FillStyle></fills><edges><Edge fillStyle1=\"1\" edges=\"!" + std::to_string(x) + " 0|" + std::to_string(x + 400) +
            " 0|" + std::to_string(x + 400) + " 400|" + std::to_string(x) + " 400|" + std::to_string(x) +
            " 0\"/></edges></DOMShape></elements></DOMFrame>";
    };
    writeText(folder / "masks.xfl", "PROXY-CS5");
    writeText(folder / "DOMDocument.xml",
        "<DOMDocument " + ns + " width=\"300\" height=\"200\" frameRate=\"24\"><timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"Own\" layerType=\"mask\"><frames>" + square(0) + "</frames></DOMLayer>"
        "<DOMLayer name=\"Art\" layerType=\"masked\" parentLayerIndex=\"0\" phoenixMasks=\"add:Wide;subtract:Hole;intersect:Window\">"
        "<frames>" + square(200) + "</frames></DOMLayer>"
        "<DOMLayer name=\"Wide\" layerType=\"mask\"><frames>" + square(600) + "</frames></DOMLayer>"
        "<DOMLayer name=\"Hole\" layerType=\"mask\"><frames>" + square(100) + "</frames></DOMLayer>"
        "<DOMLayer name=\"Window\" layerType=\"mask\"><frames>" + square(50) + "</frames></DOMLayer>"
        "<DOMLayer name=\"Under\" layerType=\"masked\" parentLayerIndex=\"4\"><frames>" + square(0) + "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse(folder.string()));
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Document& document = *file->document;
    CHECK(fla::otherapps::needsBaking(document));
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(document, notes);
    CHECK(!fla::otherapps::needsBaking(document) && notes.size() == 1);

    // In the layer's place, still under its own mask, a layer showing a
    // movie clip of its art; a layer for the Add mask under the own mask's
    // layers; the layer masked further down still finds its mask.
    fla::Timeline* scene = document.timelines.front();
    CHECK(scene->layers.size() == 7);
    if (scene->layers.size() != 7)
        return;
    fla::Layer* art = scene->layers[1];
    CHECK(art->name == "Art" && art->layerType == fla::Layer::Type::Masked && art->parentLayerIndex == 0);
    CHECK(scene->layers[2]->layerType == fla::Layer::Type::Normal && scene->layers[2]->name == "Art added by Wide");
    CHECK(scene->layers[6]->parentLayerIndex == 5 && scene->layers[5]->name == "Window");
    for (const fla::Layer* layer : scene->layers)
        CHECK(fla::compositing::moreMasksOf(*layer).empty());

    // Inside the clip: the art cut by the Subtract mask (an Erase clip over
    // it, in a clip with Layer blending) and masked by the Intersect mask.
    fla::Symbol* clip = symbolNamed(document, "Art masked");
    CHECK(clip != nullptr);
    if (!clip)
        return;
    fla::Timeline* inside = clip->timelines.front();
    CHECK(inside->layers.size() == 1);
    const auto* both = static_cast<const fla::SymbolInstance*>(inside->layers[0]->frames.front()->elements.front());
    CHECK(both->blendMode == "layer");
    fla::Timeline* cut = both->symbol->timelines.front();
    CHECK(cut->layers.size() == 3);
    if (cut->layers.size() != 3)
        return;
    const auto* cutter = static_cast<const fla::SymbolInstance*>(cut->layers[0]->frames.front()->elements.front());
    CHECK(cutter->blendMode == "erase" && cutter->symbol->timelines.front()->layers.front()->name == "Hole");
    CHECK(cut->layers[1]->layerType == fla::Layer::Type::Mask && cut->layers[1]->name == "Window" &&
        cut->layers[2]->layerType == fla::Layer::Type::Masked && cut->layers[2]->parentLayerIndex == 1 &&
        cut->layers[2]->name == "Art");

    // The Add mask's layer: the clip masked by it, less the own mask.
    fla::Symbol* added = symbolNamed(document, "Art added by Wide");
    CHECK(added != nullptr);

    // And it writes and reads back.
    fs::remove_all(root / "masks_out");
    fla::XFLFolderWriter writer;
    CHECK(writer.write(document, (root / "masks_out").string()));
    FLAParser again;
    std::unique_ptr<fla::FLADocument> back(again.parse((root / "masks_out").string()));
    CHECK(back && back->document && symbolNamed(*back->document, "Art masked") &&
        symbolNamed(*back->document, "Art added by Wide"));
}
