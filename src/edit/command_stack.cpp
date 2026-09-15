#include "command_stack.h"

namespace fla {

CompoundCommand* CommandStack::currentMacro() const
{
    if (_macroStack.empty())
        return nullptr;
    return static_cast<CompoundCommand*>(_macroStack.back().get());
}

void CommandStack::push(CommandPtr command)
{
    if (!command)
        return;

    // The stack owns execution, so callers construct a command rather than
    // performing the edit and telling us about it afterwards. This runs whether
    // or not the command ends up merged, so it happens exactly once either way.
    command->redo();

    if (CompoundCommand* macro = currentMacro())
    {
        // Inside a macro nothing reaches the history until endMacro(), so undo
        // availability is unchanged and there is nothing to notify about.
        macro->addCommand(std::move(command));
        return;
    }

    // Only the most recent command is a merge candidate, and only when nothing
    // has been undone since it was pushed.
    if (_mergeAllowed && _index > 0 && _index == _commands.size())
    {
        Command* previous = _commands[_index - 1].get();
        const int mergeId = command->mergeId();
        if (mergeId >= 0 && previous->mergeId() == mergeId && previous->mergeWith(command.get()))
        {
            // The previous command now covers both edits and the new one is
            // dropped. The history index does not move, so a saved position at
            // the current index no longer describes the document.
            if (isClean())
                _cleanIndex = -1;
            notifyChanged();
            return;
        }
    }

    addToHistory(std::move(command));
    _mergeAllowed = true;
    notifyChanged();
}

void CommandStack::addToHistory(CommandPtr command)
{
    clearRedoBranch();
    _commands.push_back(std::move(command));
    ++_index;
}

void CommandStack::clearRedoBranch()
{
    if (_index == _commands.size())
        return;

    // The saved position may be inside the branch being discarded, in which case
    // it can never be reached again.
    if (_cleanIndex > static_cast<int>(_index))
        _cleanIndex = -1;

    _commands.erase(_commands.begin() + static_cast<long long>(_index), _commands.end());
}

void CommandStack::undo()
{
    if (!canUndo())
        return;

    --_index;
    _commands[_index]->undo();

    // A later edit must not fold into a command that is no longer adjacent to
    // the cursor.
    _mergeAllowed = false;
    notifyChanged();
}

void CommandStack::redo()
{
    if (!canRedo())
        return;

    _commands[_index]->redo();
    ++_index;

    _mergeAllowed = false;
    notifyChanged();
}

void CommandStack::clear()
{
    _macroStack.clear();
    _commands.clear();
    _index = 0;
    _cleanIndex = 0;
    _mergeAllowed = true;
    notifyChanged();
}

void CommandStack::beginMacro(const std::string& name)
{
    _macroStack.push_back(CommandPtr(new CompoundCommand(name)));

    // A gesture that opens a macro is a fresh undo step; it must not fold into
    // whatever came before it.
    _mergeAllowed = false;
}

void CommandStack::endMacro()
{
    if (_macroStack.empty())
        return;

    CommandPtr finished = std::move(_macroStack.back());
    _macroStack.pop_back();

    CompoundCommand* compound = static_cast<CompoundCommand*>(finished.get());

    // A macro wrapped speculatively around a gesture that turned out to change
    // nothing costs nothing.
    if (compound->isEmpty())
        return;

    // A single-command macro keeps its wrapper rather than being flattened to
    // that command: the name the caller chose ("Draw Rectangle") describes the
    // gesture better than the one command that happened to implement it.
    if (CompoundCommand* parent = currentMacro())
    {
        parent->addCommand(std::move(finished));
        return;
    }

    addToHistory(std::move(finished));

    // A macro is an explicit "this is one step" boundary, so the next edit must
    // start a new entry rather than folding into the gesture just closed.
    _mergeAllowed = false;
    notifyChanged();
}

void CommandStack::setClean()
{
    _cleanIndex = static_cast<int>(_index);
    notifyChanged();
}

} // namespace fla
