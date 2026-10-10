#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/group.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/static_text.h"
#include "../src/edit/element_clone.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/data/frame.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/selection.h"

using fla::CommandPtr;
using fla::CommandStack;
using fla::SetElementTransformCommand;
using fla::Transform;

namespace {

/// Group is the cheapest concrete Element to construct; only its transform
/// matters here.
class TestElement : public fla::Group
{
public:
    TestElement()
        : fla::Group(nullptr)
    {}
};

bool sameTransform(const Transform& a, const Transform& b)
{
    return a.m11 == b.m11 && a.m12 == b.m12 && a.m21 == b.m21 &&
           a.m22 == b.m22 && a.tx == b.tx && a.ty == b.ty;
}

CommandPtr move(fla::Element* element, const Transform& before, const Transform& after)
{
    return CommandPtr(new SetElementTransformCommand(element, before, after, "Move"));
}

} // namespace

TEST(grouping_moves_elements_into_a_group_where_the_topmost_was)
{
    fla::Frame frame(nullptr);
    TestElement* a = new TestElement();
    TestElement* b = new TestElement();
    TestElement* c = new TestElement();
    frame.elements = {a, b, c};

    CommandStack stack;
    stack.push(CommandPtr(new fla::GroupElementsCommand(&frame, {a, c}, "Group")));

    CHECK(frame.elements.size() == 2);
    CHECK(frame.elements[0] == b);
    fla::Group* group = frame.elements.size() == 2 ? dynamic_cast<fla::Group*>(frame.elements[1]) : nullptr;
    CHECK(group != nullptr);
    if (group)
    {
        CHECK(group->members.size() == 2 && group->members[0] == a && group->members[1] == c);
        CHECK(a->parent == group);
    }

    stack.undo();
    CHECK(frame.elements.size() == 3);
    CHECK(frame.elements[0] == a && frame.elements[1] == b && frame.elements[2] == c);
    CHECK(a->parent == &frame);

    stack.redo();
    CHECK(frame.elements.size() == 2);

    // Ungrouping puts the members back where the group was.
    stack.push(CommandPtr(new fla::UngroupCommand(&frame, group, "Ungroup")));
    CHECK(frame.elements.size() == 3);
    CHECK(frame.elements[0] == b && frame.elements[1] == a && frame.elements[2] == c);
    CHECK(c->parent == &frame);

    stack.undo();
    CHECK(frame.elements.size() == 2 && frame.elements[1] == group);
    CHECK(group->members.size() == 2);

    // Leaving everything applied, the frame owns the group and the group its
    // members; clear it so nothing is freed twice.
    stack.clear();
    for (fla::Element* element : frame.elements)
        delete element;
    frame.elements.clear();
}

TEST(converting_to_a_symbol_moves_the_elements_into_a_new_library_item)
{
    fla::Document document(nullptr);
    fla::Frame frame(nullptr);
    TestElement* a = new TestElement();
    TestElement* b = new TestElement();
    TestElement* c = new TestElement();
    frame.elements = {a, b, c};

    CommandStack stack;
    stack.push(CommandPtr(new fla::ConvertToSymbolCommand(&document, &frame, {a, c},
        "Symbol 1", 10.0, 20.0, "Convert to Symbol")));

    // One instance where the topmost element was, the others inside.
    CHECK(frame.elements.size() == 2);
    CHECK(frame.elements[0] == b);
    fla::SymbolInstance* instance = frame.elements.size() == 2
        ? dynamic_cast<fla::SymbolInstance*>(frame.elements[1]) : nullptr;
    CHECK(instance != nullptr);
    CHECK(document.symbolList != nullptr && document.symbolList->symbols.size() == 1);
    if (instance && document.symbolList && !document.symbolList->symbols.empty())
    {
        const fla::Symbol* symbol = document.symbolList->symbols.front();
        CHECK(instance->symbol == symbol);
        CHECK(instance->libraryItemName == "Symbol 1");
        CHECK(instance->transform.tx == 10.0 && instance->transform.ty == 20.0);
        CHECK(document.symbolList->symbolMap.count("Symbol 1") == 1);
        CHECK(symbol->timelines.size() == 1 && symbol->timelines[0]->layers.size() == 1);
        const fla::Frame* inside = symbol->timelines[0]->layers[0]->frames.front();
        CHECK(inside->elements.size() == 2 && inside->elements[0] == a && inside->elements[1] == c);
        CHECK(a->parent == inside);
    }

    stack.undo();
    CHECK(frame.elements.size() == 3);
    CHECK(frame.elements[0] == a && frame.elements[1] == b && frame.elements[2] == c);
    CHECK(a->parent == &frame);
    CHECK(document.symbolList->symbols.empty());

    // The type asked for is the instance's; it defaults to a movie clip.
    if (instance)
        CHECK(instance->symbolType == fla::SymbolType::MovieClip);

    stack.redo();
    CHECK(frame.elements.size() == 2);
    CHECK(document.symbolList->symbols.size() == 1);

    // Applied: the document owns the symbol and its contents, the frame the
    // instance. Take them apart so nothing is freed twice.
    stack.clear();
    delete frame.elements[0];
    delete frame.elements[1];
    frame.elements.clear();
}

TEST(reordering_moves_an_element_in_the_z_order_and_back)
{
    fla::Frame frame(nullptr);
    TestElement* a = new TestElement();
    TestElement* b = new TestElement();
    TestElement* c = new TestElement();
    frame.elements = {a, b, c};

    CommandStack stack;
    stack.push(CommandPtr(new fla::ReorderElementCommand(&frame, a, 2, "Bring to Front")));
    CHECK(frame.elements[0] == b && frame.elements[1] == c && frame.elements[2] == a);

    stack.undo();
    CHECK(frame.elements[0] == a && frame.elements[1] == b && frame.elements[2] == c);

    stack.clear();
    for (fla::Element* element : frame.elements)
        delete element;
    frame.elements.clear();
}

TEST(transform_command_applies_and_reverses)
{
    TestElement element;
    const Transform before = element.transform;
    const Transform after = Transform::fromTranslate(10.0, 20.0);

    CommandStack stack;
    stack.push(move(&element, before, after));

    CHECK(sameTransform(element.transform, after));

    stack.undo();
    CHECK(sameTransform(element.transform, before));

    stack.redo();
    CHECK(sameTransform(element.transform, after));
}

TEST(transform_command_names_the_gesture)
{
    TestElement element;
    CommandStack stack;
    stack.push(CommandPtr(new SetElementTransformCommand(
        &element, element.transform, Transform::fromScale(2.0, 2.0), "Scale")));

    // The Edit menu reads "Undo Scale", not "Undo Set Transform".
    CHECK(stack.undoName() == "Scale");
}

TEST(transform_commands_merge_for_the_same_element_and_gesture)
{
    TestElement element;
    const Transform start = element.transform;

    CommandStack stack;
    stack.push(move(&element, start, Transform::fromTranslate(5.0, 0.0)));
    stack.push(move(&element, Transform::fromTranslate(5.0, 0.0), Transform::fromTranslate(9.0, 0.0)));

    // Repeated nudges are one undo step that returns to where they started.
    CHECK(stack.count() == 1);
    CHECK(sameTransform(element.transform, Transform::fromTranslate(9.0, 0.0)));

    stack.undo();
    CHECK(sameTransform(element.transform, start));
}

TEST(transform_commands_do_not_merge_across_elements)
{
    TestElement first, second;
    CommandStack stack;

    stack.push(move(&first, first.transform, Transform::fromTranslate(5.0, 0.0)));
    stack.push(move(&second, second.transform, Transform::fromTranslate(7.0, 0.0)));

    CHECK(stack.count() == 2);
    CHECK(sameTransform(first.transform, Transform::fromTranslate(5.0, 0.0)));
    CHECK(sameTransform(second.transform, Transform::fromTranslate(7.0, 0.0)));
}

TEST(transform_commands_do_not_merge_across_gestures)
{
    TestElement element;
    CommandStack stack;

    stack.push(move(&element, element.transform, Transform::fromTranslate(5.0, 0.0)));
    stack.push(CommandPtr(new SetElementTransformCommand(
        &element, Transform::fromTranslate(5.0, 0.0), Transform::fromScale(2.0, 2.0), "Scale")));

    // Folding a scale into a move would leave the menu describing one gesture
    // while undoing both.
    CHECK(stack.count() == 2);
    CHECK(stack.undoName() == "Scale");
}

TEST(transform_command_survives_a_null_element)
{
    // Guarding rather than crashing matters because the selection can outlive
    // what it points at if something is deleted out from under it.
    CommandStack stack;
    stack.push(CommandPtr(new SetElementTransformCommand(
        nullptr, Transform(), Transform::fromTranslate(1.0, 1.0), "Move")));

    stack.undo();
    stack.redo();
    CHECK(stack.count() == 1);
}

TEST(transform_command_undoes_a_whole_multi_element_gesture)
{
    TestElement first, second;
    const Transform firstStart = first.transform;
    const Transform secondStart = second.transform;

    CommandStack stack;

    // Dragging a multi-selection is one gesture, so the tool wraps the per
    // element commands in a macro.
    stack.beginMacro("Move");
    stack.push(move(&first, firstStart, Transform::fromTranslate(5.0, 5.0)));
    stack.push(move(&second, secondStart, Transform::fromTranslate(5.0, 5.0)));
    stack.endMacro();

    CHECK(stack.count() == 1);
    CHECK(stack.undoName() == "Move");

    stack.undo();
    CHECK(sameTransform(first.transform, firstStart));
    CHECK(sameTransform(second.transform, secondStart));

    stack.redo();
    CHECK(sameTransform(first.transform, Transform::fromTranslate(5.0, 5.0)));
    CHECK(sameTransform(second.transform, Transform::fromTranslate(5.0, 5.0)));
}

namespace {

/// Counts live instances so a test can prove an undone creation is freed rather
/// than leaked, and that a live one is never freed twice.
class CountedElement : public fla::Group
{
public:
    CountedElement()
        : fla::Group(nullptr)
    {
        ++liveCount;
    }

    ~CountedElement() override { --liveCount; }

    static int liveCount;
};

int CountedElement::liveCount = 0;

} // namespace

TEST(add_element_command_inserts_and_removes)
{
    fla::Frame frame(nullptr);
    TestElement* element = new TestElement();

    CommandStack stack;
    stack.push(CommandPtr(new fla::AddElementCommand(&frame, element, "Rectangle")));

    CHECK(frame.elements.size() == 1);
    CHECK(frame.elements[0] == element);

    stack.undo();
    CHECK(frame.elements.empty());

    stack.redo();
    CHECK(frame.elements.size() == 1);
    CHECK(frame.elements[0] == element);

    // The frame owns it now and will delete it.
}

TEST(add_element_command_keeps_z_order)
{
    fla::Frame frame(nullptr);
    TestElement* first = new TestElement();
    TestElement* last = new TestElement();
    frame.elements.push_back(first);
    frame.elements.push_back(last);

    TestElement* middle = new TestElement();
    CommandStack stack;
    stack.push(CommandPtr(new fla::AddElementCommand(&frame, middle, "Oval", nullptr, 1)));

    CHECK(frame.elements.size() == 3);
    CHECK(frame.elements[1] == middle);

    stack.undo();
    CHECK(frame.elements.size() == 2);

    // Redo must put it back where it was, not on top.
    stack.redo();
    CHECK(frame.elements.size() == 3);
    CHECK(frame.elements[1] == middle);
}

TEST(add_element_command_frees_an_undone_element)
{
    fla::Frame frame(nullptr);
    CountedElement::liveCount = 0;

    {
        CommandStack stack;
        stack.push(CommandPtr(new fla::AddElementCommand(
            &frame, new CountedElement(), "Rectangle")));
        CHECK(CountedElement::liveCount == 1);

        stack.undo();
        // Out of the frame but still held by the command, ready for redo.
        CHECK(CountedElement::liveCount == 1);
        CHECK(frame.elements.empty());
    }

    // The history went away while the element was out, so the command had to
    // free it. Anything else would leak.
    CHECK(CountedElement::liveCount == 0);
}

TEST(add_element_command_leaves_a_live_element_to_the_frame)
{
    CountedElement::liveCount = 0;

    {
        fla::Frame frame(nullptr);
        {
            CommandStack stack;
            stack.push(CommandPtr(new fla::AddElementCommand(
                &frame, new CountedElement(), "Rectangle")));
            CHECK(CountedElement::liveCount == 1);
        }

        // The command is gone but the element is in the frame, so it must still
        // be alive: a double delete would show up here.
        CHECK(CountedElement::liveCount == 1);
        CHECK(frame.elements.size() == 1);
    }

    // The frame owns it and frees it.
    CHECK(CountedElement::liveCount == 0);
}

TEST(add_element_command_drops_the_element_from_the_selection)
{
    fla::Frame frame(nullptr);
    fla::Selection selection;
    TestElement* element = new TestElement();

    CommandStack stack;
    stack.push(CommandPtr(new fla::AddElementCommand(
        &frame, element, "Rectangle", &selection)));
    selection.select(element);
    CHECK(selection.contains(element));

    // Undoing takes the element out of the document, so the selection must let
    // go rather than keep a pointer that redo might never restore.
    stack.undo();
    CHECK(!selection.contains(element));
    CHECK(selection.isEmpty());
}

TEST(remove_element_command_takes_out_and_restores)
{
    fla::Frame frame(nullptr);
    TestElement* first = new TestElement();
    TestElement* second = new TestElement();
    frame.elements.push_back(first);
    frame.elements.push_back(second);

    CommandStack stack;
    stack.push(CommandPtr(new fla::RemoveElementCommand(&frame, first, "Delete")));

    CHECK(frame.elements.size() == 1);
    CHECK(frame.elements[0] == second);

    stack.undo();
    CHECK(frame.elements.size() == 2);
    // Restored to its original position, not appended.
    CHECK(frame.elements[0] == first);
}

TEST(remove_element_command_frees_what_it_holds)
{
    CountedElement::liveCount = 0;
    fla::Frame frame(nullptr);
    frame.elements.push_back(new CountedElement());

    {
        CommandStack stack;
        stack.push(CommandPtr(new fla::RemoveElementCommand(
            &frame, frame.elements[0], "Delete")));
        CHECK(frame.elements.empty());
        CHECK(CountedElement::liveCount == 1);
    }

    // The removal was never undone, so the command owned the element when the
    // history was dropped.
    CHECK(CountedElement::liveCount == 0);
}

namespace
{

/// A command that records whether the stack said it was executing while the
/// command ran, the way a selection observer would ask.
class ExecutionProbe : public fla::Command
{
public:
    explicit ExecutionProbe(const CommandStack& stack) : _stack(stack) {}

    void redo() override { seenOnRedo = _stack.isExecuting(); }
    void undo() override { seenOnUndo = _stack.isExecuting(); }
    std::string name() const override { return "Probe"; }

    bool seenOnRedo = false;
    bool seenOnUndo = false;

private:
    const CommandStack& _stack;
};

}

TEST(command_stack_reports_when_a_command_is_executing)
{
    CommandStack stack;
    CHECK(!stack.isExecuting());

    ExecutionProbe* probe = new ExecutionProbe(stack);
    stack.push(CommandPtr(probe));
    CHECK(probe->seenOnRedo);
    CHECK(!stack.isExecuting());

    probe->seenOnRedo = false;
    stack.undo();
    CHECK(probe->seenOnUndo);
    CHECK(!stack.isExecuting());

    stack.redo();
    CHECK(probe->seenOnRedo);
    CHECK(!stack.isExecuting());
}

TEST(breaking_an_instance_apart_puts_copies_of_the_symbol_frame_in_its_place)
{
    // A symbol with two layers, a shape on each, the lower layer listed last.
    fla::Symbol symbol(nullptr);
    fla::Timeline* timeline = new fla::Timeline(&symbol);
    symbol.timelines.push_back(timeline);

    fla::Layer* top = new fla::Layer(timeline);
    fla::Layer* bottom = new fla::Layer(timeline);
    timeline->layers = {top, bottom};

    fla::Frame* topFrame = new fla::Frame(top);
    top->frames.push_back(topFrame);
    fla::Shape* topShape = new fla::Shape(topFrame);
    topShape->transform = fla::Transform::fromTranslate(5.0, 0.0);
    topFrame->elements.push_back(topShape);

    fla::Frame* bottomFrame = new fla::Frame(bottom);
    bottom->frames.push_back(bottomFrame);
    fla::Shape* bottomShape = new fla::Shape(bottomFrame);
    bottomShape->isDrawingObject = true;
    bottomFrame->elements.push_back(bottomShape);

    // The instance, scaled and moved, between two other elements.
    fla::Frame frame(nullptr);
    TestElement* below = new TestElement();
    TestElement* above = new TestElement();
    fla::SymbolInstance* instance = new fla::SymbolInstance(&frame);
    instance->symbol = &symbol;
    instance->symbolType = fla::SymbolType::MovieClip;
    instance->transform = fla::Transform(2.0, 0.0, 0.0, 2.0, 100.0, 50.0);
    frame.elements = {below, instance, above};

    fla::Selection selection;
    selection.select(instance);

    CommandStack stack;
    fla::BreakApartInstanceCommand* command =
        new fla::BreakApartInstanceCommand(&frame, instance, "Break Apart", &selection);
    CHECK(command->pieces().size() == 2);
    stack.push(CommandPtr(command));

    // Bottom layer first, then the top one, where the instance was.
    CHECK(frame.elements.size() == 4);
    CHECK(frame.elements[0] == below);
    CHECK(frame.elements[3] == above);
    fla::Shape* lowPiece = dynamic_cast<fla::Shape*>(frame.elements[1]);
    fla::Shape* highPiece = dynamic_cast<fla::Shape*>(frame.elements[2]);
    CHECK(lowPiece != nullptr && highPiece != nullptr);
    if (lowPiece && highPiece)
    {
        CHECK(lowPiece != bottomShape && highPiece != topShape);
        CHECK(lowPiece->isDrawingObject);
        CHECK(!highPiece->isDrawingObject);
        CHECK(lowPiece->parent == &frame);

        // The instance's scale and offset on top of the shape's own offset.
        CHECK_NEAR(highPiece->transform.m11, 2.0);
        CHECK_NEAR(highPiece->transform.tx, 110.0);
        CHECK_NEAR(highPiece->transform.ty, 50.0);
        CHECK_NEAR(lowPiece->transform.tx, 100.0);
    }
    CHECK(selection.elements().size() == 2);

    // The symbol keeps its own artwork.
    CHECK(topFrame->elements.size() == 1 && topFrame->elements[0] == topShape);

    stack.undo();
    CHECK(frame.elements.size() == 3);
    CHECK(frame.elements[1] == instance);
    CHECK(instance->parent == &frame);
    CHECK(selection.elements().size() == 1 && selection.elements()[0] == instance);

    stack.redo();
    CHECK(frame.elements.size() == 4);

    // Applied: the frame owns the pieces and the command the instance.
    stack.clear();
    for (fla::Element* element : frame.elements)
        delete element;
    frame.elements.clear();
}

TEST(breaking_a_graphic_instance_apart_takes_the_frame_it_shows)
{
    fla::Symbol symbol(nullptr);
    fla::Timeline* timeline = new fla::Timeline(&symbol);
    symbol.timelines.push_back(timeline);
    fla::Layer* layer = new fla::Layer(timeline);
    timeline->layers.push_back(layer);

    // Keyframes at 0 and 3; frame 4 shows the second one.
    fla::Frame* first = new fla::Frame(layer);
    first->index = 0;
    first->duration = 3;
    first->elements.push_back(new fla::Shape(first));
    fla::Frame* second = new fla::Frame(layer);
    second->index = 3;
    second->duration = 3;
    second->elements.push_back(new fla::Shape(second));
    second->elements.push_back(new fla::Shape(second));
    layer->frames = {first, second};

    fla::Frame frame(nullptr);
    fla::SymbolInstance* instance = new fla::SymbolInstance(&frame);
    instance->symbol = &symbol;
    instance->symbolType = fla::SymbolType::Graphic;
    instance->firstFrame = 4;
    frame.elements.push_back(instance);

    CHECK(fla::BreakApartInstanceCommand::shownFrameIndex(*instance) == 4);

    CommandStack stack;
    stack.push(CommandPtr(new fla::BreakApartInstanceCommand(&frame, instance, "Break Apart")));
    CHECK(frame.elements.size() == 2);

    stack.clear();
    for (fla::Element* element : frame.elements)
        delete element;
    frame.elements.clear();
}

TEST(converting_to_a_symbol_takes_the_type_asked_for)
{
    fla::Document document(nullptr);
    fla::Frame frame(nullptr);
    TestElement* a = new TestElement();
    frame.elements = {a};

    CommandStack stack;
    stack.push(CommandPtr(new fla::ConvertToSymbolCommand(&document, &frame, {a},
        "Face", 0.0, 0.0, "Convert to Symbol", nullptr, fla::SymbolType::Graphic)));

    fla::SymbolInstance* instance = frame.elements.size() == 1
        ? dynamic_cast<fla::SymbolInstance*>(frame.elements[0]) : nullptr;
    CHECK(instance != nullptr);
    if (instance)
    {
        CHECK(instance->symbolType == fla::SymbolType::Graphic);
        CHECK(instance->libraryItemName == "Face");
    }

    stack.clear();
    delete frame.elements[0];
    frame.elements.clear();
}

TEST(set_text_contents_command_swaps_the_runs_and_box)
{
    fla::Frame frame(nullptr);
    fla::StaticText* text = new fla::StaticText(&frame);
    text->left = 10.0;
    text->top = 20.0;
    text->autoExpand = true;
    fla::TextRun run;
    run.text = "Hello";
    run.size = 12.0;
    text->runs.push_back(run);
    frame.elements.push_back(text);

    std::unique_ptr<fla::StaticText> before(
        static_cast<fla::StaticText*>(fla::cloneElement(*text, nullptr)));
    CHECK(before->runs.size() == 1 && before->runs[0].text == "Hello");

    // The edit, as the tool makes it in place.
    text->runs[0].text = "Hello world";
    text->width = 80.0;
    text->height = 16.0;

    CommandStack stack;
    stack.push(CommandPtr(new fla::SetTextContentsCommand(text, *before, *text, "Edit Text")));
    CHECK(text->runs[0].text == "Hello world");
    CHECK(text->width == 80.0);

    stack.undo();
    CHECK(text->runs[0].text == "Hello");
    CHECK(text->width == 0.0);
    CHECK(text->left == 10.0 && text->top == 20.0);

    stack.redo();
    CHECK(text->runs[0].text == "Hello world");
    CHECK(text->height == 16.0);

    stack.clear();
    delete text;
    frame.elements.clear();
}

TEST(breaking_an_instance_apart_bakes_its_color_effect)
{
    fla::Symbol symbol(nullptr);
    fla::Timeline* timeline = new fla::Timeline(&symbol);
    symbol.timelines.push_back(timeline);
    fla::Layer* layer = new fla::Layer(timeline);
    timeline->layers.push_back(layer);
    fla::Frame* inside = new fla::Frame(layer);
    layer->frames.push_back(inside);

    fla::Shape* shape = new fla::Shape(inside);
    fla::SolidColor* fill = new fla::SolidColor(shape);
    fill->color[0] = 100;
    fill->color[1] = 100;
    fill->color[2] = 100;
    fill->color[3] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    inside->elements.push_back(shape);

    fla::Frame frame(nullptr);
    fla::SymbolInstance* instance = new fla::SymbolInstance(&frame);
    instance->symbol = &symbol;
    instance->symbolType = fla::SymbolType::MovieClip;
    // Half brightness towards white, and half alpha.
    instance->colorTransform.brightness = 0.5;
    instance->colorTransform.alphaMultiplier = 0.5;
    frame.elements.push_back(instance);

    CommandStack stack;
    stack.push(CommandPtr(new fla::BreakApartInstanceCommand(&frame, instance, "Break Apart")));
    CHECK(frame.elements.size() == 1);
    fla::Shape* copy = frame.elements.empty() ? nullptr : dynamic_cast<fla::Shape*>(frame.elements[0]);
    CHECK(copy != nullptr);
    if (copy && !copy->fills.empty())
    {
        const fla::SolidColor* color = static_cast<const fla::SolidColor*>(copy->fills[0]);
        CHECK(color->color[0] == 177);
        CHECK(color->color[3] == 127);
    }

    // The symbol's own colors are untouched.
    CHECK(fill->color[0] == 100 && fill->color[3] == 255);

    stack.clear();
    for (fla::Element* element : frame.elements)
        delete element;
    frame.elements.clear();
}
