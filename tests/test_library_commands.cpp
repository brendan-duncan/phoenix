#include "test_util.h"

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
#include "../src/edit/library_commands.h"

#include <memory>

namespace {

/// A document with one symbol "Ball" (one shape), and an instance of it on
/// the main timeline.
std::unique_ptr<fla::Document> documentWithBall(fla::Symbol*& ball, fla::SymbolInstance*& instance)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    document->symbolList = new fla::SymbolList(document.get());
    ball = new fla::Symbol(document->symbolList);
    ball->name = "Ball";
    fla::Timeline* inner = new fla::Timeline(ball);
    fla::Layer* innerLayer = new fla::Layer(inner);
    fla::Frame* innerFrame = new fla::Frame(innerLayer);
    innerFrame->elements.push_back(new fla::Shape(innerFrame));
    innerLayer->frames.push_back(innerFrame);
    inner->layers.push_back(innerLayer);
    ball->timelines.push_back(inner);
    document->symbolList->symbols.push_back(ball);
    document->symbolList->symbolMap["Ball"] = ball;

    fla::Timeline* main = new fla::Timeline(document.get());
    fla::Layer* layer = new fla::Layer(main);
    fla::Frame* frame = new fla::Frame(layer);
    instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = "Ball";
    instance->symbol = ball;
    frame->elements.push_back(instance);
    layer->frames.push_back(frame);
    main->layers.push_back(layer);
    document->timelines.push_back(main);
    return document;
}

} // namespace

TEST(renaming_a_symbol_renames_its_instances)
{
    fla::Symbol* ball = nullptr;
    fla::SymbolInstance* instance = nullptr;
    std::unique_ptr<fla::Document> document = documentWithBall(ball, instance);
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::RenameSymbolCommand(document.get(), ball, "Sphere")));
    CHECK(ball->name == "Sphere");
    CHECK(instance->libraryItemName == "Sphere");
    CHECK(document->symbolList->symbolMap.count("Sphere") == 1);
    CHECK(document->symbolList->symbolMap.count("Ball") == 0);
    stack.undo();
    CHECK(ball->name == "Ball" && instance->libraryItemName == "Ball");
    CHECK(document->symbolList->symbolMap.count("Ball") == 1);
}

TEST(duplicating_a_symbol_copies_its_contents_under_a_free_name)
{
    fla::Symbol* ball = nullptr;
    fla::SymbolInstance* instance = nullptr;
    std::unique_ptr<fla::Document> document = documentWithBall(ball, instance);
    const std::string name = fla::library::freeCopyName(*document, "Ball");
    CHECK(name == "Ball copy");
    fla::Symbol* copy = fla::library::cloneSymbol(*ball, name);
    CHECK(copy->timelines.size() == 1);
    CHECK(copy->timelines[0]->layers[0]->frames[0]->elements.size() == 1);
    CHECK(copy->timelines[0]->layers[0]->frames[0]->elements[0] != ball->timelines[0]->layers[0]->frames[0]->elements[0]);

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::AddSymbolCommand(document.get(), copy, "Duplicate Symbol")));
    CHECK(document->symbolList->symbols.size() == 2);
    CHECK(fla::library::freeCopyName(*document, "Ball") == "Ball copy 2");
    stack.undo();
    CHECK(document->symbolList->symbols.size() == 1);
}

TEST(only_an_unused_symbol_is_deleted_and_undo_brings_it_back)
{
    fla::Symbol* ball = nullptr;
    fla::SymbolInstance* instance = nullptr;
    std::unique_ptr<fla::Document> document = documentWithBall(ball, instance);
    CHECK(fla::library::instancesOf(*document, ball).size() == 1);

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::RemoveSymbolCommand(document.get(), ball)));
    CHECK(document->symbolList->symbols.empty());
    stack.undo();
    CHECK(document->symbolList->symbols.size() == 1 && document->symbolList->symbolMap.count("Ball"));

    stack.push(fla::CommandPtr(new fla::AddFolderCommand(document.get(), "Props")));
    CHECK(document->folders.size() == 1 && !fla::library::nameIsFree(*document, "Props"));
    stack.undo();
    CHECK(document->folders.empty());
}
