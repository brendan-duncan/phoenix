#include "test_util.h"

#include "../src/data/group.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_commands.h"

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
