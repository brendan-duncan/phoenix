#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/edit/lip_sync.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/zip_reader.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

// The start screen's starter templates (resources/templates), read as
// Phoenix opens them: every one parses, its instances find their symbols,
// and each holds what its summary promises.
#ifdef PHOENIX_TEST_DATA

namespace {

std::filesystem::path templatesFolder()
{
    return std::filesystem::u8path(std::string(PHOENIX_TEST_DATA)) / ".." / ".." / "resources" / "templates";
}

/// The template \a id read from its XFL folder, or null.
std::unique_ptr<fla::Document> readTemplate(const std::string& id, ZipReader& reader)
{
    const std::filesystem::path folder = templatesFolder() / id;
    std::map<std::string, std::vector<uint8_t>> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
    {
        if (!entry.is_regular_file())
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        files[std::filesystem::relative(entry.path(), folder).generic_u8string()] = std::move(bytes);
    }
    const auto dom = files.find("DOMDocument.xml");
    if (dom == files.end())
        return nullptr;
    const std::string xml(dom->second.begin(), dom->second.end());
    reader.openMemory(files);
    DocumentParser parser(&reader);
    return std::unique_ptr<fla::Document>(parser.parse(xml, nullptr));
}

std::string attribute(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& name)
{
    for (const auto& [key, value] : attributes)
        if (key == name)
            return value;
    return std::string();
}

const fla::Layer* layerNamed(const fla::Timeline& timeline, const std::string& name)
{
    for (const fla::Layer* layer : timeline.layers)
        if (layer && layer->name == name)
            return layer;
    return nullptr;
}

/// Every instance on \a timeline's frames.
std::vector<const fla::SymbolInstance*> instancesOn(const fla::Timeline& timeline)
{
    std::vector<const fla::SymbolInstance*> found;
    for (const fla::Layer* layer : timeline.layers)
        for (const fla::Frame* frame : layer->frames)
            for (const fla::Element* element : frame->elements)
                if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element))
                    found.push_back(instance);
    return found;
}

} // namespace

TEST(starter_templates_open_and_hold_their_art)
{
    const std::vector<std::string> ids = {"bouncing-ball", "walk-cycle", "lip-sync-head", "title-card"};
    for (const std::string& id : ids)
    {
        CHECK(std::filesystem::exists(templatesFolder() / id / "template.json"));
        CHECK(std::filesystem::exists(templatesFolder() / id / "thumbnail.png"));
        ZipReader reader;
        const std::unique_ptr<fla::Document> document = readTemplate(id, reader);
        CHECK(document != nullptr);
        if (!document || document->timelines.empty())
            continue;
        CHECK(document->width == 1280 && document->height == 720);
        // Every instance finds its symbol.
        bool resolved = true;
        for (const fla::SymbolInstance* instance : instancesOn(*document->timelines.front()))
            resolved = resolved && instance->symbol != nullptr;
        CHECK(resolved);
    }

    // The ball: keyed with eased classic tweens, squashed on the floor.
    {
        ZipReader reader;
        const std::unique_ptr<fla::Document> ball = readTemplate("bouncing-ball", reader);
        const fla::Layer* layer = ball ? layerNamed(*ball->timelines.front(), "Ball") : nullptr;
        CHECK(layer && layer->frames.size() == 5);
        if (layer && layer->frames.size() == 5)
        {
            CHECK(layer->frames[0]->tweenType == fla::TweenType::Motion && layer->frames[0]->easeMethod == "quadIn");
            const auto* squash = dynamic_cast<const fla::SymbolInstance*>(layer->frames[2]->elements.front());
            CHECK(squash && squash->transform.m11 > 1.2 && squash->transform.m22 < 0.8);
        }
    }

    // The walk: each limb's keyframes parented to a layer that rigs it, a
    // cycle whose last pose is its first.
    {
        ZipReader reader;
        const std::unique_ptr<fla::Document> walk = readTemplate("walk-cycle", reader);
        const fla::Timeline* timeline = walk ? walk->timelines.front() : nullptr;
        CHECK(timeline != nullptr);
        if (timeline)
        {
            std::set<std::string> rigs;
            for (const fla::Layer* layer : timeline->layers)
                if (!attribute(layer->extraAttributes, "layerRiggingIndex").empty())
                    rigs.insert(attribute(layer->extraAttributes, "layerRiggingIndex"));
            CHECK(rigs.size() == 5);
            int parented = 0;
            bool known = true;
            for (const fla::Layer* layer : timeline->layers)
                for (const fla::Frame* frame : layer->frames)
                {
                    const std::string parent = attribute(frame->extraAttributes, "parentLayerIndex");
                    if (parent.empty())
                        continue;
                    ++parented;
                    known = known && rigs.count(parent);
                }
            CHECK(parented == 8 * 5 && known);
            const fla::Layer* thigh = layerNamed(*timeline, "Front Thigh");
            CHECK(thigh && thigh->frames.size() == 5);
            if (thigh && thigh->frames.size() == 5)
            {
                const auto* first = dynamic_cast<const fla::SymbolInstance*>(thigh->frames.front()->elements.front());
                const auto* last = dynamic_cast<const fla::SymbolInstance*>(thigh->frames.back()->elements.front());
                CHECK(first && last && std::fabs(first->transform.m11 - last->transform.m11) < 1e-6 &&
                      std::fabs(first->transform.m12 - last->transform.m12) < 1e-6);
            }
        }
    }

    // The head: its mouth's frames labeled with the nine shapes, which a
    // lip sync's chart finds.
    {
        ZipReader reader;
        const std::unique_ptr<fla::Document> head = readTemplate("lip-sync-head", reader);
        const fla::SymbolInstance* mouth = nullptr;
        if (head)
            for (const fla::SymbolInstance* instance : instancesOn(*head->timelines.front()))
                if (instance->libraryItemName == "Mouth")
                    mouth = instance;
        CHECK(mouth && mouth->symbol);
        if (mouth && mouth->symbol)
        {
            const std::map<std::string, int> labels = fla::lipsync::labelsOf(*mouth);
            const std::vector<std::string> shapes = {"A", "B", "C", "D", "E", "F", "G", "H", "X"};
            CHECK(labels.size() == shapes.size());
            const std::map<std::string, int> chart = fla::lipsync::defaultChart(shapes, labels);
            for (size_t i = 0; i < shapes.size(); ++i)
                CHECK(chart.at(shapes[i]) == static_cast<int>(i) + 1);
            // It rests on X.
            CHECK(mouth->firstFrame == 8);
        }
    }
}

#endif
