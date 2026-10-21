#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/controllers.h"

#include <cmath>
#include <memory>
#include <cstdlib>

TEST(a_controller_drives_several_layers_and_keys_them_on_the_frame)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    document->symbolList = new fla::SymbolList(document.get());
    fla::Symbol* head = new fla::Symbol(document->symbolList);
    head->name = "Head";
    document->symbolList->symbols.push_back(head);
    document->symbolList->symbolMap["Head"] = head;
    fla::Timeline* scene = new fla::Timeline(document.get());
    document->timelines.push_back(scene);
    for (const char* name : {"head", "eyes"})
    {
        fla::Layer* layer = new fla::Layer(scene);
        layer->name = name;
        fla::Frame* frame = new fla::Frame(layer);
        frame->duration = 20;
        fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
        instance->libraryItemName = "Head";
        instance->symbol = head;
        frame->elements.push_back(instance);
        layer->frames.push_back(frame);
        scene->layers.push_back(layer);
    }

    fla::controllers::Controller turn;
    turn.name = "Head turn";
    turn.bindings.push_back({"scene:0", "head", "rotation", 0.0, 30.0});
    turn.bindings.push_back({"scene:0", "head", "x", 100.0, 140.0});
    turn.bindings.push_back({"scene:0", "eyes", "frame", 1.0, 5.0});
    turn.bindings.push_back({"scene:0", "gone", "x", 0.0, 1.0});

    // Kept in the document and read back the same.
    document->extraAttributes = fla::controllers::saved(*document, {turn});
    const std::vector<fla::controllers::Controller> loaded = fla::controllers::load(*document);
    CHECK(loaded.size() == 1 && loaded[0].name == "Head turn" && loaded[0].bindings.size() == 4);
    CHECK(loaded[0].bindings[1].from == 100.0 && loaded[0].bindings[1].to == 140.0);

    fla::CommandStack stack;
    stack.push(fla::controllers::apply(*document, turn, 0.5, 10));
    fla::Layer* headLayer = scene->layers[0];
    fla::Layer* eyesLayer = scene->layers[1];
    CHECK(headLayer->frames.size() == 2 && headLayer->frames[1]->index == 10);
    CHECK(headLayer->frames[0]->duration == 10 && headLayer->frames[1]->duration == 10);
    const auto* keyed = dynamic_cast<const fla::SymbolInstance*>(headLayer->frames[1]->elements.front());
    CHECK(keyed && std::abs(keyed->transform.tx - 120.0) < 1e-6);
    double rotation = 0.0;
    CHECK(fla::controllers::currentValue(*headLayer, 12, "rotation", rotation) && std::abs(rotation - 15.0) < 1e-6);
    // The frame before the key is as it was.
    CHECK(fla::controllers::currentValue(*headLayer, 3, "rotation", rotation) && std::abs(rotation) < 1e-6);
    const auto* eyes = dynamic_cast<const fla::SymbolInstance*>(eyesLayer->frames[1]->elements.front());
    CHECK(eyes && eyes->firstFrame == 2);

    stack.undo();
    CHECK(headLayer->frames.size() == 1 && headLayer->frames[0]->duration == 20);
    stack.redo();
    CHECK(headLayer->frames.size() == 2);
    CHECK(fla::controllers::currentValue(*headLayer, 15, "x", rotation) && std::abs(rotation - 120.0) < 1e-6);
}

// Keying inside a span cuts it; undo gives back exactly the frames there
// were (the keyframe insertion is a frame edit).
TEST(undoing_a_controller_puts_the_frames_back_exactly)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    document->symbolList = new fla::SymbolList(document.get());
    fla::Symbol* head = new fla::Symbol(document->symbolList);
    head->name = "Head";
    document->symbolList->symbols.push_back(head);
    document->symbolList->symbolMap["Head"] = head;
    fla::Timeline* scene = new fla::Timeline(document.get());
    document->timelines.push_back(scene);
    fla::Layer* layer = new fla::Layer(scene);
    layer->name = "head";
    fla::Frame* frame = new fla::Frame(layer);
    frame->duration = 5;
    fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = "Head";
    instance->symbol = head;
    frame->elements.push_back(instance);
    layer->frames.push_back(frame);
    scene->layers.push_back(layer);

    fla::controllers::Controller slide;
    slide.name = "Slide";
    slide.bindings.push_back({"scene:0", "head", "x", 0.0, 100.0});
    fla::CommandStack stack;
    stack.push(fla::controllers::apply(*document, slide, 1.0, 3));
    CHECK(layer->frames.size() >= 2);
    for (int i = 0; i < 2; ++i)
    {
        stack.undo();
        CHECK(layer->frames.size() == 1 && layer->frames[0] == frame);
        CHECK(frame->index == 0 && frame->duration == 5);
        CHECK(std::abs(instance->transform.tx) < 1e-9);
        stack.redo();
        CHECK(layer->frames.size() >= 2);
    }
}

// A joystick: across drives one property and down another, and a frame
// grid picks the drawing under the handle; it is kept as it was set.
TEST(a_joystick_drives_its_axes_and_picks_from_a_frame_grid)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    document->symbolList = new fla::SymbolList(document.get());
    fla::Symbol* head = new fla::Symbol(document->symbolList);
    head->name = "Head";
    document->symbolList->symbols.push_back(head);
    document->symbolList->symbolMap["Head"] = head;
    fla::Timeline* scene = new fla::Timeline(document.get());
    document->timelines.push_back(scene);
    for (const char* name : {"head", "face"})
    {
        fla::Layer* layer = new fla::Layer(scene);
        layer->name = name;
        fla::Frame* frame = new fla::Frame(layer);
        frame->duration = 10;
        fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
        instance->libraryItemName = "Head";
        instance->symbol = head;
        frame->elements.push_back(instance);
        layer->frames.push_back(frame);
        scene->layers.push_back(layer);
    }

    fla::controllers::Controller look;
    look.name = "Look";
    look.joystick = true;
    fla::controllers::Binding across{"scene:0", "head", "x", -20.0, 20.0};
    fla::controllers::Binding down{"scene:0", "head", "y", -10.0, 10.0};
    down.axis = 'y';
    // Nine drawings, frames 1 to 9, three a row.
    fla::controllers::Binding grid{"scene:0", "face", "frameGrid", 1.0, 9.0};
    grid.columns = 3;
    look.bindings = {across, down, grid};

    CHECK_NEAR(fla::controllers::valueAt(look, across, 0.75, 0.0), 10.0);
    CHECK_NEAR(fla::controllers::valueAt(look, down, 0.75, 0.0), -10.0);
    CHECK_NEAR(fla::controllers::valueAt(look, grid, 0.0, 0.0), 1.0);
    CHECK_NEAR(fla::controllers::valueAt(look, grid, 1.0, 0.0), 3.0);
    CHECK_NEAR(fla::controllers::valueAt(look, grid, 0.5, 0.5), 5.0);
    CHECK_NEAR(fla::controllers::valueAt(look, grid, 1.0, 1.0), 9.0);
    CHECK_NEAR(fla::controllers::valueAt(look, grid, 0.1, 0.9), 7.0);

    // Kept and read back: a joystick, where its handle is, the axes and the grid.
    look.value = 0.25;
    look.valueY = 0.8;
    document->extraAttributes = fla::controllers::saved(*document, {look});
    const std::vector<fla::controllers::Controller> loaded = fla::controllers::load(*document);
    CHECK(loaded.size() == 1 && loaded[0].joystick);
    CHECK_NEAR(loaded[0].value, 0.25);
    CHECK_NEAR(loaded[0].valueY, 0.8);
    CHECK(loaded[0].bindings.size() == 3);
    CHECK(loaded[0].bindings[0].axis == 'x' && loaded[0].bindings[1].axis == 'y');
    CHECK(loaded[0].bindings[2].property == "frameGrid" && loaded[0].bindings[2].columns == 3);

    // A slider is written as before.
    fla::controllers::Controller slide;
    slide.name = "Slide";
    slide.bindings.push_back({"scene:0", "head", "x", 0.0, 1.0});
    std::string text;
    for (const auto& [key, value] : fla::controllers::saved(*document, {slide}))
        if (key == "phoenixControllers")
            text = value;
    CHECK(text == "Slide|0;scene:0|head|x|0|1");

    fla::CommandStack stack;
    stack.push(fla::controllers::apply(*document, look, 1.0, 0.0, 4));
    const auto* moved = dynamic_cast<const fla::SymbolInstance*>(scene->layers[0]->frames[1]->elements.front());
    CHECK(moved && std::abs(moved->transform.tx - 20.0) < 1e-6 && std::abs(moved->transform.ty + 10.0) < 1e-6);
    const auto* face = dynamic_cast<const fla::SymbolInstance*>(scene->layers[1]->frames[1]->elements.front());
    CHECK(face && face->firstFrame == 2);
}
