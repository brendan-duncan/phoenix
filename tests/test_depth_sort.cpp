#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/edit/depth_sort.h"
#include "../src/edit/other_apps.h"
#include "../src/edit/phoenix_features.h"
#include "../src/parser/fla_parser.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// Two cards on a layer sorted by depth: "near" placed first at Z 0, "far"
/// on top of it at Z 100; on frame 11 "near" has gone back to Z 200, a
/// classic tween between.
std::unique_ptr<fla::FLADocument> cards(const fs::path& folder, bool tween)
{
    fs::remove_all(folder);
    fs::create_directories(folder / "LIBRARY");
    const std::string ns = "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"http://ns.adobe.com/xfl/2008/\"";
    std::ofstream(folder / "sample.xfl") << "PROXY-CS5";
    std::ofstream(folder / "LIBRARY" / "Card.xml")
        << "<DOMSymbolItem " + ns + " name=\"Card\" symbolType=\"movie clip\"><timeline><DOMTimeline name=\"Card\"><layers>"
           "<DOMLayer name=\"L\"><frames><DOMFrame index=\"0\"><elements/></DOMFrame></frames></DOMLayer>"
           "</layers></DOMTimeline></timeline></DOMSymbolItem>";
    const auto card = [](const char* name, int z) {
        return std::string("<DOMSymbolInstance libraryItemName=\"Card\" name=\"") + name +
            "\" symbolType=\"movie clip\" phoenixZ=\"" + std::to_string(z) + "\"/>";
    };
    std::string frames = std::string("<DOMFrame index=\"0\" duration=\"10\"") + (tween ? " tweenType=\"motion\"" : "") +
        "><elements>" + card("near", 0) + card("far", 100) + "</elements></DOMFrame>";
    if (tween)
        frames += "<DOMFrame index=\"10\"><elements>" + card("near", 200) + card("far", 100) + "</elements></DOMFrame>";
    std::ofstream(folder / "DOMDocument.xml")
        << "<DOMDocument " + ns + " width=\"300\" height=\"200\" frameRate=\"24\"><symbols><Include href=\"Card.xml\"/></symbols>"
           "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"Cards\" phoenixSortByDepth=\"true\"><frames>" +
            frames + "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>";
    FLAParser parser;
    return std::unique_ptr<fla::FLADocument>(parser.parse(folder.string()));
}

std::string nameAt(const fla::Frame& frame, size_t index)
{
    return index < frame.elements.size() ? static_cast<const fla::SymbolInstance*>(frame.elements[index])->name : "";
}

} // namespace

TEST(depth_sort_draws_the_deepest_first)
{
    using fla::depthsort::order;
    CHECK((order({0.0, 100.0, -50.0}) == std::vector<size_t>{1, 0, 2}));
    // Equal depths keep their stacking order.
    CHECK((order({5.0, 5.0, 5.0}) == std::vector<size_t>{0, 1, 2}));
    CHECK((order({0.0, 30.0, 0.0, 30.0}) == std::vector<size_t>{1, 3, 0, 2}));
    CHECK(order({}).empty());
}

TEST(depth_sort_follows_z_along_a_tween)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_depth_sort_tests";
    std::unique_ptr<fla::FLADocument> file = cards(root / "tween", true);
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    fla::Layer& layer = *file->document->timelines.front()->layers.front();
    CHECK(fla::depthsort::sorts(layer));
    const fla::Frame& key = *layer.frames.front();
    CHECK(fla::depthsort::tweenTarget(layer, key) == layer.frames[1]);

    // At the start "far" is deeper and drawn first; half way both are at
    // 100; near the end "near" has gone behind it.
    const std::vector<double> start = fla::depthsort::depthsAt(key, layer.frames[1], 0);
    CHECK(start.size() == 2 && start[0] == 0.0 && start[1] == 100.0);
    const std::vector<double> half = fla::depthsort::depthsAt(key, layer.frames[1], 5);
    CHECK(std::abs(half[0] - 100.0) < 1e-9);
    CHECK((fla::depthsort::drawOrder(layer, key, 0) == std::vector<size_t>{1, 0}));
    CHECK((fla::depthsort::drawOrder(layer, key, 5) == std::vector<size_t>{0, 1}));
    CHECK((fla::depthsort::drawOrder(layer, key, 9) == std::vector<size_t>{0, 1}));

    // Not sorted: the stacking order as placed.
    layer.extraAttributes = fla::depthsort::withSorting(layer, false);
    CHECK(!fla::depthsort::sorts(layer));
    CHECK((fla::depthsort::drawOrder(layer, key, 0) == std::vector<size_t>{0, 1}));
    layer.extraAttributes = fla::depthsort::withSorting(layer, true);
    CHECK(fla::depthsort::sorts(layer));
    const std::vector<std::string> ids = fla::features::ids(fla::features::used(*file->document));
    CHECK(std::find(ids.begin(), ids.end(), "depth-sorting") != ids.end());
}

TEST(depth_sort_bakes_the_order_into_the_layer_for_other_apps)
{
    const fs::path root = fs::temp_directory_path() / "phoenix_depth_sort_tests";

    // Held still: the keyframe lists the deep card first.
    std::unique_ptr<fla::FLADocument> still = cards(root / "still", false);
    CHECK(still && still->document);
    if (!still || !still->document)
        return;
    CHECK(fla::otherapps::needsBaking(*still->document));
    std::vector<std::string> notes;
    fla::otherapps::bakeCompositing(*still->document, notes);
    fla::Layer& held = *still->document->timelines.front()->layers.front();
    CHECK(!fla::depthsort::sorts(held) && held.frames.size() == 1);
    CHECK(nameAt(*held.frames[0], 0) == "far" && nameAt(*held.frames[0], 1) == "near");
    CHECK(notes.size() == 1 && notes[0].find("depth order") != std::string::npos);
    CHECK(!fla::otherapps::needsBaking(*still->document));

    // Tweened past each other: a keyframe a frame, each in its own order.
    std::unique_ptr<fla::FLADocument> moving = cards(root / "moving", true);
    CHECK(moving && moving->document);
    if (!moving || !moving->document)
        return;
    notes.clear();
    CHECK(fla::depthsort::bake(*moving->document, 24.0, notes) == 1);
    fla::Layer& baked = *moving->document->timelines.front()->layers.front();
    CHECK(!fla::depthsort::sorts(baked) && baked.frames.size() == 11);
    if (baked.frames.size() != 11)
        return;
    CHECK(nameAt(*baked.frames[0], 0) == "far" && nameAt(*baked.frames[4], 0) == "far");
    CHECK(nameAt(*baked.frames[6], 0) == "near" && nameAt(*baked.frames[9], 0) == "near");
    CHECK(nameAt(*baked.frames[10], 0) == "near" && nameAt(*baked.frames[10], 1) == "far");
    CHECK(notes.size() == 1 && notes[0].find("a keyframe a frame") != std::string::npos);
    fs::remove_all(root);
}
