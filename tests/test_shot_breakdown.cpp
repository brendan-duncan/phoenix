#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/shot_breakdown.h"

#include <memory>

namespace {

/// An episode of 20 frames: a label layer with sh010 at 1 and sh020 at 7,
/// and an art layer whose ball tweens from x 0 at frame 1 to x 100 at
/// frame 11, then holds to 20. A second symbol nothing uses.
std::unique_ptr<fla::Document> episode()
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    document->frameRate = 24.0;
    document->width = 640;
    document->height = 360;
    document->symbolList = new fla::SymbolList(document.get());
    for (const char* name : {"Ball", "Unused"})
    {
        fla::Symbol* symbol = new fla::Symbol(document->symbolList);
        symbol->name = name;
        fla::Timeline* inner = new fla::Timeline(symbol);
        fla::Layer* layer = new fla::Layer(inner);
        fla::Frame* frame = new fla::Frame(layer);
        frame->elements.push_back(new fla::Shape(frame));
        layer->frames.push_back(frame);
        inner->layers.push_back(layer);
        symbol->timelines.push_back(inner);
        document->symbolList->symbols.push_back(symbol);
        document->symbolList->symbolMap[name] = symbol;
    }

    fla::Timeline* scene = new fla::Timeline(document.get());
    scene->name = "Episode";
    fla::Layer* labels = new fla::Layer(scene);
    labels->name = "labels";
    for (const auto& [at, length, name] : {std::tuple<int, int, const char*>{0, 6, "sh010"}, {6, 14, "sh020"}})
    {
        fla::Frame* frame = new fla::Frame(labels);
        frame->index = at;
        frame->duration = length;
        frame->name = name;
        frame->labelType = "name";
        labels->frames.push_back(frame);
    }
    fla::Layer* art = new fla::Layer(scene);
    art->name = "art";
    for (const auto& [at, length, x] : {std::tuple<int, int, double>{0, 10, 0.0}, {10, 10, 100.0}})
    {
        fla::Frame* frame = new fla::Frame(art);
        frame->index = at;
        frame->duration = length;
        frame->tweenType = at == 0 ? fla::TweenType::Motion : fla::TweenType::None;
        fla::SymbolInstance* ball = new fla::SymbolInstance(frame);
        ball->libraryItemName = "Ball";
        ball->symbol = document->symbolList->symbolMap["Ball"];
        ball->transform.tx = x;
        frame->elements.push_back(ball);
        art->frames.push_back(frame);
    }
    scene->layers = {labels, art};
    document->timelines.push_back(scene);
    return document;
}

} // namespace

TEST(an_episode_splits_into_shots_by_label_with_tweens_baked_at_the_cut)
{
    std::unique_ptr<fla::Document> document = episode();
    const std::vector<fla::shots::Shot> shots = fla::shots::find(*document, fla::shots::CutBy::Label);
    CHECK(shots.size() == 2);
    CHECK(shots[0].name == "sh010" && shots[0].first == 0 && shots[0].last == 5);
    CHECK(shots[1].name == "sh020" && shots[1].first == 6 && shots[1].last == 19);
    CHECK(fla::shots::find(*document, fla::shots::CutBy::Scene).size() == 1);

    fla::shots::keepOnly(*document, shots[1]);
    CHECK(document->timelines.size() == 1 && document->timelines[0]->name == "sh020");
    fla::Layer* art = document->timelines[0]->layers[1];
    // Frames 7-10 of the tween are baked, each where the tween had it;
    // frame 11's keyframe is the shot's frame 5.
    CHECK(art->frames.front()->index == 0);
    CHECK(art->frames.front()->tweenType == fla::TweenType::None);
    const auto* first = dynamic_cast<const fla::SymbolInstance*>(art->frames.front()->elements.front());
    CHECK(first && first->transform.tx > 55.0 && first->transform.tx < 65.0);
    bool keyAtFive = false;
    for (const fla::Frame* frame : art->frames)
        keyAtFive = keyAtFive || frame->index == 4;
    CHECK(keyAtFive);
    int end = 0;
    for (const fla::Frame* frame : art->frames)
        end = std::max(end, frame->index + frame->duration);
    CHECK(end == 14);
    // The symbol nothing uses is left out.
    CHECK(document->symbolList->symbolMap.count("Unused") == 0);
    CHECK(document->symbolList->symbolMap.count("Ball") == 1);
    CHECK(fla::shots::fileName("sh:010/a?") == "sh_010_a_");
}
