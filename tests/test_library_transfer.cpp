#include "test_util.h"

#include "../src/data/bitmap.h"
#include "../src/data/bitmap_instance.h"
#include "../src/data/document.h"
#include "../src/data/folder.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/library_transfer.h"

#include <algorithm>
#include <cstdint>

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

fla::SymbolInstance* addInstance(fla::Symbol& into, fla::Symbol& of)
{
    fla::Frame* frame = into.timelines[0]->layers[0]->frames[0];
    fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = of.name;
    instance->symbol = &of;
    frame->elements.push_back(instance);
    return instance;
}

void addBitmap(fla::Document& document, fla::Symbol& into, const std::string& name)
{
    fla::Bitmap* bitmap = new fla::Bitmap(&document);
    bitmap->name = name;
    bitmap->bitmapDataHRef = "M 1 1.dat";
    document.resources.push_back(bitmap);
    fla::Frame* frame = into.timelines[0]->layers[0]->frames[0];
    fla::BitmapInstance* instance = new fla::BitmapInstance(frame);
    instance->libraryItemName = name;
    frame->elements.push_back(instance);
}

/// A character: "Hero" holds "Head", which shows the bitmap "face.png" and
/// plays "hello.wav" on its frame.
struct Source
{
    fla::Document document{nullptr};
    Source()
    {
        fla::Symbol* hero = addSymbol(document, "Hero", 1);
        fla::Symbol* head = addSymbol(document, "Head", 2);
        addInstance(*hero, *head);
        addBitmap(document, *head, "Faces/face.png");
        head->timelines[0]->layers[0]->frames[0]->extraAttributes.emplace_back("soundName", "hello.wav");
        document.sounds.push_back({"hello.wav", "44kHz 16bit Mono", "bin/M 2 1.dat"});
        document.extraMedia.push_back("<DOMSoundItem name=\"hello.wav\" itemID=\"1\" href=\"hello.wav\" "
                                      "soundDataHRef=\"M 2 1.dat\" format=\"44kHz 16bit Mono\"/>");
        document.extraFiles.emplace_back("bin/M 2 1.dat", std::vector<uint8_t>{1, 2, 3, 4});
    }
};

} // namespace

TEST(library_items_copy_between_documents_with_what_they_use)
{
    Source source;
    const std::vector<std::string> all = fla::transfer::withDependencies(source.document, {"Hero"});
    CHECK(all.size() == 4);

    fla::Document target(nullptr);
    CHECK(fla::transfer::conflicts(target, source.document, {"Hero"}).empty());
    fla::CommandStack stack;
    std::vector<std::string> added;
    stack.push(fla::transfer::copyItems(target, source.document, {"Hero"}, fla::transfer::Resolution::Skip, &added));
    CHECK(added.size() == 1 && added[0] == "Hero");
    CHECK(target.symbolList && target.symbolList->symbols.size() == 2 && target.resources.size() == 1);
    CHECK(target.sounds.size() == 1 && target.extraFiles.size() == 1 && target.extraFiles[0].second.size() == 4);
    CHECK(target.sounds[0].dataPath == target.extraFiles[0].first && target.sounds[0].dataPath != "bin/M 2 1.dat");
    // The folder the character's bitmap is in comes too.
    CHECK(target.folders.size() == 1 && target.folders[0]->name == "Faces");
    // The copied Hero's instance points at the target's Head.
    fla::Symbol* hero = target.symbolList->symbolMap["Hero"];
    fla::Symbol* head = target.symbolList->symbolMap["Head"];
    const auto* instance = static_cast<fla::SymbolInstance*>(hero->timelines[0]->layers[0]->frames[0]->elements[1]);
    CHECK(instance->symbol == head);
    stack.undo();
    CHECK(target.symbolList->symbols.empty() && target.resources.empty() && target.sounds.empty());
    stack.redo();
    CHECK(target.symbolList->symbols.size() == 2);
}

TEST(library_conflicts_replace_keep_both_or_skip)
{
    Source source;
    fla::Document target(nullptr);
    fla::Symbol* ownHead = addSymbol(target, "Head", 5);
    fla::Symbol* stage = addSymbol(target, "Stage", 1);
    fla::SymbolInstance* placed = addInstance(*stage, *ownHead);
    CHECK((fla::transfer::conflicts(target, source.document, {"Hero"}) == std::vector<std::string>{"Head"}));

    // Skip: the target's Head stays, and the copied Hero uses it.
    {
        fla::CommandStack stack;
        stack.push(fla::transfer::copyItems(target, source.document, {"Hero"}, fla::transfer::Resolution::Skip));
        CHECK(ownHead->timelines[0]->layers.size() == 5 && target.symbolList->symbols.size() == 3);
        stack.undo();
    }
    // Replace: the target's Head takes the source's contents; what used it
    // still does.
    {
        fla::CommandStack stack;
        stack.push(fla::transfer::copyItems(target, source.document, {"Hero"}, fla::transfer::Resolution::Replace));
        CHECK(ownHead->timelines[0]->layers.size() == 2 && placed->symbol == ownHead);
        stack.undo();
        CHECK(ownHead->timelines[0]->layers.size() == 5);
    }
    // Keep both: a renamed copy, and the copied Hero uses it.
    {
        fla::CommandStack stack;
        std::vector<std::string> added;
        stack.push(fla::transfer::copyItems(target, source.document, {"Hero"}, fla::transfer::Resolution::KeepBoth, &added));
        CHECK(target.symbolList->symbolMap.count("Head copy") == 1);
        fla::Symbol* hero = target.symbolList->symbolMap["Hero"];
        const auto* instance = static_cast<fla::SymbolInstance*>(hero->timelines[0]->layers[0]->frames[0]->elements[1]);
        CHECK(instance->libraryItemName == "Head copy" && instance->symbol == target.symbolList->symbolMap["Head copy"]);
        CHECK(ownHead->timelines[0]->layers.size() == 5);
    }
}
