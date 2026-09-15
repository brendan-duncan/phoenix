#include "test_util.h"

#include "../src/edit/command_stack.h"

#include <string>

using fla::Command;
using fla::CommandPtr;
using fla::CommandStack;
using fla::CompoundCommand;

namespace {

/// Appends a character to a log on redo and removes it on undo, so a test can
/// assert on the exact sequence of applications.
class AppendCommand : public Command
{
public:
    AppendCommand(std::string* target, char value, int mergeId = -1)
        : _target(target)
        , _value(value)
        , _mergeId(mergeId)
    {}

    void redo() override { _target->push_back(_value); }

    void undo() override { _target->pop_back(); }

    std::string name() const override { return std::string("Append ") + _value; }

    int mergeId() const override { return _mergeId; }

    /// Merging drops the other command's character, since this command already
    /// owns that position in the log. Its own value becomes the merged one so a
    /// later redo reproduces the combined result.
    bool mergeWith(const Command* other) override
    {
        const AppendCommand* append = static_cast<const AppendCommand*>(other);
        _target->pop_back();
        _target->pop_back();
        _value = append->_value;
        _target->push_back(_value);
        return true;
    }

private:
    std::string* _target;
    char _value;
    int _mergeId;
};

/// Reports a merge id but always refuses, so a test can confirm the stack copes
/// with a candidate that declines.
class RefusingCommand : public AppendCommand
{
public:
    RefusingCommand(std::string* target, char value, int mergeId)
        : AppendCommand(target, value, mergeId)
    {}

    bool mergeWith(const Command*) override { return false; }
};

CommandPtr append(std::string* target, char value, int mergeId = -1)
{
    return CommandPtr(new AppendCommand(target, value, mergeId));
}

} // namespace

TEST(stack_push_applies_command)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a'));

    CHECK(log == "a");
    CHECK(stack.count() == 1);
    CHECK(stack.canUndo());
    CHECK(!stack.canRedo());
}

TEST(stack_undo_redo_round_trip)
{
    std::string log;
    CommandStack stack;
    stack.push(append(&log, 'a'));
    stack.push(append(&log, 'b'));
    CHECK(log == "ab");

    stack.undo();
    CHECK(log == "a");
    stack.undo();
    CHECK(log == "");
    CHECK(!stack.canUndo());
    CHECK(stack.canRedo());

    stack.redo();
    CHECK(log == "a");
    stack.redo();
    CHECK(log == "ab");
    CHECK(!stack.canRedo());
}

TEST(stack_undo_redo_past_the_ends_is_a_no_op)
{
    std::string log;
    CommandStack stack;

    stack.undo();
    stack.redo();
    CHECK(log == "");

    stack.push(append(&log, 'a'));
    stack.redo();
    CHECK(log == "a");
    stack.undo();
    stack.undo();
    CHECK(log == "");
}

TEST(stack_push_discards_redo_branch)
{
    std::string log;
    CommandStack stack;
    stack.push(append(&log, 'a'));
    stack.push(append(&log, 'b'));
    stack.undo();
    CHECK(log == "a");

    stack.push(append(&log, 'c'));

    CHECK(log == "ac");
    CHECK(stack.count() == 2);
    CHECK(!stack.canRedo());
}

TEST(stack_names_track_position)
{
    std::string log;
    CommandStack stack;
    CHECK(stack.undoName() == "");
    CHECK(stack.redoName() == "");

    stack.push(append(&log, 'a'));
    CHECK(stack.undoName() == "Append a");
    CHECK(stack.redoName() == "");

    stack.undo();
    CHECK(stack.undoName() == "");
    CHECK(stack.redoName() == "Append a");
}

TEST(stack_merges_commands_with_matching_id)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a', 7));
    stack.push(append(&log, 'b', 7));

    // Both were applied, but they collapsed into a single undo step.
    CHECK(log == "b");
    CHECK(stack.count() == 1);

    stack.undo();
    CHECK(log == "");
    CHECK(!stack.canUndo());
}

TEST(stack_does_not_merge_without_an_id)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a'));
    stack.push(append(&log, 'b'));

    CHECK(log == "ab");
    CHECK(stack.count() == 2);
}

TEST(stack_does_not_merge_mismatched_ids)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a', 1));
    stack.push(append(&log, 'b', 2));

    CHECK(log == "ab");
    CHECK(stack.count() == 2);
}

TEST(stack_honours_a_refused_merge)
{
    std::string log;
    CommandStack stack;

    // The stack offers the new command to the *previous* one, so the refusal has
    // to come from the command already on the stack.
    stack.push(CommandPtr(new RefusingCommand(&log, 'a', 3)));
    stack.push(append(&log, 'b', 3));

    // The rejected command must still have been applied exactly once, and must
    // have become its own history entry.
    CHECK(log == "ab");
    CHECK(stack.count() == 2);
}

TEST(stack_break_merge_chain_starts_a_new_step)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a', 7));
    stack.breakMergeChain();
    stack.push(append(&log, 'b', 7));

    CHECK(log == "ab");
    CHECK(stack.count() == 2);

    // Merging resumes after the break, folding into the command that followed it.
    stack.push(append(&log, 'c', 7));
    CHECK(log == "ac");
    CHECK(stack.count() == 2);
}

TEST(stack_does_not_merge_across_an_undo)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a', 7));
    stack.push(append(&log, 'x'));
    stack.undo();
    CHECK(log == "a");

    stack.push(append(&log, 'b', 7));

    CHECK(log == "ab");
    CHECK(stack.count() == 2);
}

TEST(stack_macro_is_one_undo_step)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Draw Shape");
    stack.push(append(&log, 'a'));
    stack.push(append(&log, 'b'));
    stack.push(append(&log, 'c'));
    CHECK(log == "abc");
    CHECK(stack.count() == 0); // nothing enters the history until the macro closes
    stack.endMacro();

    CHECK(stack.count() == 1);
    CHECK(stack.undoName() == "Draw Shape");

    stack.undo();
    CHECK(log == "");
    stack.redo();
    CHECK(log == "abc");
}

TEST(stack_empty_macro_is_discarded)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Nothing Happened");
    stack.endMacro();

    CHECK(stack.count() == 0);
    CHECK(!stack.canUndo());
}

TEST(stack_single_command_macro_keeps_its_name)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Draw Rectangle");
    stack.push(append(&log, 'a'));
    stack.endMacro();

    CHECK(stack.count() == 1);
    // The gesture name describes the edit better than the one command that
    // happened to implement it, so the wrapper is kept.
    CHECK(stack.undoName() == "Draw Rectangle");

    stack.undo();
    CHECK(log == "");
    stack.redo();
    CHECK(log == "a");
}

TEST(stack_macros_nest)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Outer");
    stack.push(append(&log, 'a'));
    stack.beginMacro("Inner");
    stack.push(append(&log, 'b'));
    stack.push(append(&log, 'c'));
    stack.endMacro();
    CHECK(stack.isInMacro());
    stack.push(append(&log, 'd'));
    stack.endMacro();
    CHECK(!stack.isInMacro());

    CHECK(log == "abcd");
    CHECK(stack.count() == 1);
    CHECK(stack.undoName() == "Outer");

    stack.undo();
    CHECK(log == "");
    stack.redo();
    CHECK(log == "abcd");
}

TEST(stack_empty_nested_macro_is_discarded)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Outer");
    stack.push(append(&log, 'a'));
    stack.beginMacro("Inner");
    stack.endMacro();
    stack.endMacro();

    CHECK(log == "a");
    CHECK(stack.count() == 1);
    CHECK(stack.undoName() == "Outer"); // the empty inner macro left no trace

    stack.undo();
    CHECK(log == "");
}

TEST(stack_unbalanced_end_macro_is_a_no_op)
{
    std::string log;
    CommandStack stack;

    stack.endMacro();
    stack.push(append(&log, 'a'));
    stack.endMacro();

    CHECK(log == "a");
    CHECK(stack.count() == 1);
}

TEST(stack_commands_do_not_merge_into_a_closed_macro)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Gesture");
    stack.push(append(&log, 'a', 7));
    stack.endMacro();
    stack.push(append(&log, 'b', 7));

    // A closed macro is a deliberate step boundary; the later edit must not be
    // folded into it.
    CHECK(log == "ab");
    CHECK(stack.count() == 2);
    CHECK(stack.undoName() == "Append b");
}

TEST(stack_clean_state_follows_the_cursor)
{
    std::string log;
    CommandStack stack;
    CHECK(stack.isClean()); // a fresh stack matches a just-loaded document

    stack.push(append(&log, 'a'));
    CHECK(!stack.isClean());

    stack.setClean();
    CHECK(stack.isClean());

    stack.push(append(&log, 'b'));
    CHECK(!stack.isClean());

    // Undoing back to the saved position makes the document unmodified again.
    stack.undo();
    CHECK(stack.isClean());

    stack.undo();
    CHECK(!stack.isClean());
    stack.redo();
    CHECK(stack.isClean());
}

TEST(stack_merge_invalidates_clean_state)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a', 7));
    stack.setClean();
    CHECK(stack.isClean());

    // Merging leaves the history index untouched, so a naive implementation
    // would still call the document saved even though it just changed.
    stack.push(append(&log, 'b', 7));
    CHECK(log == "b");
    CHECK(stack.count() == 1);
    CHECK(!stack.isClean());

    stack.undo();
    CHECK(!stack.isClean());
}

TEST(stack_discarded_redo_branch_invalidates_clean_state)
{
    std::string log;
    CommandStack stack;

    stack.push(append(&log, 'a'));
    stack.push(append(&log, 'b'));
    stack.setClean();
    CHECK(stack.isClean());

    stack.undo();
    CHECK(!stack.isClean());

    // The saved position was in the branch this push discards, so it can never
    // be reached again.
    stack.push(append(&log, 'c'));
    CHECK(!stack.isClean());
    stack.undo();
    CHECK(!stack.isClean());
}

TEST(stack_clear_resets_history_and_clean_state)
{
    std::string log;
    CommandStack stack;
    stack.push(append(&log, 'a'));
    CHECK(!stack.isClean());

    stack.clear();

    CHECK(stack.count() == 0);
    CHECK(!stack.canUndo());
    CHECK(!stack.canRedo());
    CHECK(stack.isClean());
    CHECK(log == "a"); // clear() drops history without touching the document
}

TEST(stack_clear_drops_open_macros)
{
    std::string log;
    CommandStack stack;

    stack.beginMacro("Interrupted");
    stack.push(append(&log, 'a'));
    stack.clear();

    CHECK(!stack.isInMacro());
    stack.push(append(&log, 'b'));
    CHECK(stack.count() == 1);
    CHECK(stack.undoName() == "Append b");
}

TEST(stack_notifies_on_history_changes)
{
    std::string log;
    CommandStack stack;
    int notifications = 0;
    stack.setChangedCallback([&notifications]() { ++notifications; });

    stack.push(append(&log, 'a'));
    CHECK(notifications == 1);

    stack.undo();
    CHECK(notifications == 2);

    stack.redo();
    CHECK(notifications == 3);

    stack.setClean();
    CHECK(notifications == 4);

    // A redo at the end of the history changes nothing, so it stays quiet.
    stack.redo();
    CHECK(notifications == 4);
}

TEST(stack_macro_notifies_once_on_close)
{
    std::string log;
    CommandStack stack;
    int notifications = 0;
    stack.setChangedCallback([&notifications]() { ++notifications; });

    stack.beginMacro("Gesture");
    stack.push(append(&log, 'a'));
    stack.push(append(&log, 'b'));
    CHECK(notifications == 0); // nothing reached the history yet
    stack.endMacro();

    CHECK(notifications == 1);
}

TEST(compound_undoes_children_in_reverse)
{
    std::string log;
    CompoundCommand compound("Group");
    compound.addCommand(append(&log, 'a'));
    compound.addCommand(append(&log, 'b'));

    // Children are handed over already applied, so drive the compound directly.
    compound.redo();
    CHECK(log == "ab");

    compound.undo();
    CHECK(log == "");

    compound.redo();
    CHECK(log == "ab");
}
