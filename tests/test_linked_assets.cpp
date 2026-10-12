#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/linked_assets.h"

#include <memory>

namespace {

fla::Symbol* addSymbol(fla::Document& document, const std::string& name, int layers)
{
    if (!document.symbolList)
        document.symbolList = new fla::SymbolList(&document);
    fla::Symbol* symbol = new fla::Symbol(document.symbolList);
    symbol->name = name;
    fla::Timeline* inner = new fla::Timeline(symbol);
    for (int i = 0; i < layers; ++i)
    {
        fla::Layer* layer = new fla::Layer(inner);
        layer->name = "Layer " + std::to_string(i + 1);
        fla::Frame* frame = new fla::Frame(layer);
        frame->elements.push_back(new fla::Shape(frame));
        layer->frames.push_back(frame);
        inner->layers.push_back(layer);
    }
    symbol->timelines.push_back(inner);
    document.symbolList->symbols.push_back(symbol);
    document.symbolList->symbolMap[name] = symbol;
    return symbol;
}

void addInstance(fla::Symbol& into, fla::Symbol& of)
{
    fla::Frame* frame = into.timelines[0]->layers[0]->frames[0];
    fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = of.name;
    instance->symbol = &of;
    frame->elements.push_back(instance);
}

} // namespace

TEST(a_linked_symbol_updates_from_its_master_with_what_it_uses)
{
    // The master: the rig "Hero" (three layers) holding a "Head".
    fla::Document master(nullptr);
    fla::Symbol* hero = addSymbol(master, "Hero", 3);
    fla::Symbol* head = addSymbol(master, "Head", 2);
    addInstance(*hero, *head);

    // The shot: an old one-layer "Hero", linked, used on the stage.
    std::unique_ptr<fla::Document> shot(new fla::Document(nullptr));
    fla::Symbol* oldHero = addSymbol(*shot, "Hero", 1);
    fla::links::setLink(*oldHero, {"../rigs/hero.fla", "Hero", false});
    fla::Timeline* scene = new fla::Timeline(shot.get());
    fla::Layer* layer = new fla::Layer(scene);
    fla::Frame* frame = new fla::Frame(layer);
    fla::SymbolInstance* onStage = new fla::SymbolInstance(frame);
    onStage->libraryItemName = "Hero";
    onStage->symbol = oldHero;
    frame->elements.push_back(onStage);
    layer->frames.push_back(frame);
    scene->layers.push_back(layer);
    shot->timelines.push_back(scene);

    CHECK(fla::links::linkedSymbols(*shot) == std::vector<std::string>{"Hero"});
    fla::links::Link link;
    CHECK(fla::links::linkOf(*oldHero, link) && link.master == "../rigs/hero.fla" && link.item == "Hero");

    std::string error;
    const std::vector<std::string> changed = fla::links::update(*shot, "Hero", master, link.item, error);
    CHECK(changed.size() == 2);
    // The same symbol object, refilled: the stage instance still points at it.
    CHECK(onStage->symbol == oldHero);
    CHECK(oldHero->timelines.size() == 1 && oldHero->timelines[0]->layers.size() == 3);
    CHECK(fla::links::linkOf(*oldHero, link));
    // The head came along, and the copy's instance points at this document's head.
    fla::Symbol* newHead = shot->symbolList->symbolMap["Head"];
    CHECK(newHead && newHead != head);
    const auto* inner = dynamic_cast<const fla::SymbolInstance*>(oldHero->timelines[0]->layers[0]->frames[0]->elements.back());
    CHECK(inner && inner->symbol == newHead);

    CHECK(fla::links::update(*shot, "Hero", master, "Nobody", error).empty() && !error.empty());
    CHECK(fla::links::resolve("../rigs/hero.fla", "C:/show/shots/sh010.fla") == std::string("C:/show/rigs/hero.fla") ||
          fla::links::resolve("../rigs/hero.fla", "C:/show/shots/sh010.fla") == std::string("C:\\show\\rigs\\hero.fla"));
}
