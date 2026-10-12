#pragma once

#include "command.h"

#include <functional>
#include <string>
#include <vector>

namespace fla {

/// Linear undo/redo history.
///
/// Commands are pushed after being applied by the stack. Pushing while undone
/// commands are pending discards that redo branch, which is the behavior Animate
/// and essentially every other editor has.
///
/// Kept free of Qt so the editing core does not depend on the GUI layer; the Qt
/// side observes it through setChangedCallback().
class CommandStack
{
public:
    CommandStack() = default;

    ~CommandStack() = default;

    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    /// Applies \a command and adds it to the history.
    ///
    /// The command runs via redo() before being stored, so callers construct a
    /// command describing the edit rather than performing the edit themselves.
    ///
    /// If a macro is open the command joins that macro instead. Otherwise the
    /// stack first offers the command to the previous one for merging.
    void push(CommandPtr command);

    bool canUndo() const { return _index > 0; }

    bool canRedo() const { return _index < _commands.size(); }

    /// Description of the command undo() would reverse, or "" if there is none.
    std::string undoName() const
    {
        return canUndo() ? _commands[_index - 1]->name() : std::string();
    }

    /// Description of the command redo() would apply, or "" if there is none.
    std::string redoName() const
    {
        return canRedo() ? _commands[_index]->name() : std::string();
    }

    void undo();

    void redo();

    /// The command undo() would reverse, or null. Used to tell whether an
    /// edit has happened since a particular command was pushed.
    const Command* topCommand() const
    {
        return _index > 0 ? _commands[_index - 1].get() : nullptr;
    }

    /// Reverses and removes the most recent command as if it had never been
    /// pushed. For edits that exist only to serve a selection, such as lifting
    /// a region out of a shape, which should leave no trace when the selection
    /// is let go untouched. Refused inside a macro or when nothing is applied.
    bool discardLast();

    /// Drops the entire history and any open macros. Does not change the
    /// document, but does reset the clean state, so the current document counts
    /// as saved -- which is what loading a file wants.
    void clear();

    /// Trades histories with \a other: the commands, the position in them
    /// and the saved point. Each stack keeps its own changed callback. A
    /// document put aside in a tab keeps its undo history this way until it
    /// comes back. Neither may be in a macro.
    void swapHistory(CommandStack& other);

    /// Number of commands retained, including those currently undone.
    size_t count() const { return _commands.size(); }

    /// The name of the \a i-th command of the history, oldest first.
    std::string nameAt(size_t i) const { return i < _commands.size() ? _commands[i]->name() : std::string(); }

    /// Position in the history: the number of commands currently applied.
    size_t index() const { return _index; }

    /// Opens a macro. Commands pushed until the matching endMacro() are
    /// collected and become a single undo step. Macros may nest; only the
    /// outermost one produces a history entry.
    void beginMacro(const std::string& name);

    /// Closes the macro opened by beginMacro().
    ///
    /// An empty macro is discarded, so wrapping a gesture that turns out to
    /// change nothing costs nothing. A macro that did collect commands always
    /// keeps its own name, even if it holds only one.
    ///
    /// Closing a macro also ends the merge chain: the next edit starts a fresh
    /// undo step rather than folding into the gesture just closed.
    void endMacro();

    bool isInMacro() const { return !_macroStack.empty(); }

    /// Whether a command is being applied or reversed right now. Observers
    /// that react to a command's side effects, such as a selection change,
    /// can tell a change made by the command from one made by the user.
    bool isExecuting() const { return _executing > 0; }

    /// Marks the current position as saved. isClean() reports true whenever the
    /// stack returns here, including by undoing back to it.
    void setClean();

    bool isClean() const { return _cleanIndex >= 0 && static_cast<size_t>(_cleanIndex) == _index; }

    /// Marks the document as unsaved wherever the history stands, as a
    /// document recovered after a crash is until it is saved.
    void setDirty() { _cleanIndex = -1; }

    /// Whether the next push() may merge into the previous command. Merging is
    /// enabled by default; call breakMergeChain() to force the next edit to
    /// start a new undo step (on mouse release, for instance).
    void breakMergeChain() { _mergeAllowed = false; }

    /// Invoked whenever the history or clean state changes, so the GUI can
    /// refresh menu items and the title bar.
    void setChangedCallback(std::function<void()> callback)
    {
        _changedCallback = std::move(callback);
    }

    /// The most steps of history kept (0 for no limit): past it, the
    /// oldest go as new ones come.
    void setLimit(size_t limit);
    size_t limit() const { return _limit; }

private:
    void notifyChanged() const
    {
        if (_changedCallback)
            _changedCallback();
    }

    /// Discards any commands after the current index, dropping the redo branch.
    void clearRedoBranch();

    /// Appends an applied command to the history, dropping the redo branch.
    void addToHistory(CommandPtr command);

    /// The innermost open macro, or null when no macro is open.
    CompoundCommand* currentMacro() const;

    std::vector<CommandPtr> _commands;
    size_t _limit = 0;

    /// Number of commands currently applied. Commands at or past this index
    /// have been undone and are available to redo.
    size_t _index = 0;

    /// Index the document was last saved at, or -1 when no history position
    /// corresponds to the saved document. Signed so that "never clean" is
    /// representable, and so it can be invalidated when a merge or a discarded
    /// redo branch makes the saved position unreachable.
    int _cleanIndex = 0;

    bool _mergeAllowed = true;

    int _executing = 0;

    /// Open macros, outermost first. Each owns its children until it closes, at
    /// which point it is handed to its parent macro or to the history. Holding
    /// them here rather than parenting on open means an abandoned empty macro
    /// can simply be dropped.
    std::vector<CommandPtr> _macroStack;

    std::function<void()> _changedCallback;
};

} // namespace fla
