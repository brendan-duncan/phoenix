#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/property_command.h"

#include <memory>

TEST(set_property_command_sets_and_restores_a_field)
{
    fla::Frame frame(nullptr);
    frame.acceleration = 10;
    fla::CommandStack stack;

    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(frame.acceleration, 10, 40, "Ease")));
    CHECK(frame.acceleration == 40);
    CHECK(stack.undoName() == "Ease");

    stack.undo();
    CHECK(frame.acceleration == 10);
    stack.redo();
    CHECK(frame.acceleration == 40);
}

TEST(set_property_commands_from_one_drag_fold_into_one_step)
{
    fla::Frame frame(nullptr);
    fla::CommandStack stack;

    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
        frame.acceleration, 0, 5, "Ease", fla::kPropertyDragMergeId)));
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
        frame.acceleration, 5, 12, "Ease", fla::kPropertyDragMergeId)));
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
        frame.acceleration, 12, 30, "Ease", fla::kPropertyDragMergeId)));
    CHECK(frame.acceleration == 30);

    // One undo takes the whole drag back to where it began.
    stack.undo();
    CHECK(frame.acceleration == 0);
    CHECK(!stack.canUndo());
}

TEST(set_property_commands_for_different_fields_do_not_merge)
{
    fla::Frame frame(nullptr);
    fla::CommandStack stack;

    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
        frame.acceleration, 0, 5, "Ease", fla::kPropertyDragMergeId)));
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
        frame.motionTweenRotateTimes, 0, 2, "Ease", fla::kPropertyDragMergeId)));
    stack.undo();
    CHECK(frame.motionTweenRotateTimes == 0);
    CHECK(frame.acceleration == 5);
    CHECK(stack.canUndo());
}

TEST(set_color_command_sets_and_restores_four_bytes)
{
    uint8_t color[4] = {1, 2, 3, 4};
    const uint8_t after[4] = {10, 20, 30, 40};
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::SetColorCommand(color, color, after, "Stage Color")));
    CHECK(color[0] == 10 && color[1] == 20 && color[2] == 30 && color[3] == 40);
    stack.undo();
    CHECK(color[0] == 1 && color[1] == 2 && color[2] == 3 && color[3] == 4);
}
