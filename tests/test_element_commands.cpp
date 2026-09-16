#include "test_util.h"

#include "../src/data/group.h"
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
