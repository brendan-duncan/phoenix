#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/parser/fla_doctor.h"

#include <memory>

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
