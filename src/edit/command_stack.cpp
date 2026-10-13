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
    ++_executing;
    command->redo();
    --_executing;

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
    setLimit(_limit);
    trimToBudget();
}

void CommandStack::setByteBudget(size_t bytes)
{
    _byteBudget = bytes;
    trimToBudget();
}

void CommandStack::trimToBudget()
{
    if (_byteBudget == 0 || !_macroStack.empty())
        return;
    size_t total = 0;
    for (const CommandPtr& command : _commands)
        total += command->memoryUse();
    // Only applied steps go, from the oldest, and never the newest.
    size_t drop = 0;
    while (total > _byteBudget && drop + 1 < _index)
    {
        total -= _commands[drop]->memoryUse();
        ++drop;
    }
    if (drop == 0)
        return;
    _commands.erase(_commands.begin(), _commands.begin() + static_cast<long long>(drop));
    _index -= drop;
    if (_cleanIndex >= 0)
        _cleanIndex = _cleanIndex >= static_cast<int>(drop) ? _cleanIndex - static_cast<int>(drop) : -1;
}

void CommandStack::setLimit(size_t limit)
{
    _limit = limit;
    if (_limit == 0 || _commands.size() <= _limit || !_macroStack.empty())
        return;
    // Dropping undone steps from the front while keeping later redoable ones
    // would leave those acting on what the dropped steps owned: when the cut
    // reaches past the applied steps, the redo branch goes first.
    if (_commands.size() - _limit > _index)
    {
        clearRedoBranch();
        if (_commands.size() <= _limit)
            return;
    }
    // The oldest steps go; the saved point moves with what is left, and is
    // lost when it went with them.
    const size_t drop = _commands.size() - _limit;
    _commands.erase(_commands.begin(), _commands.begin() + static_cast<long long>(drop));
    _index = _index >= drop ? _index - drop : 0;
    if (_cleanIndex >= 0)
        _cleanIndex = _cleanIndex >= static_cast<int>(drop) ? _cleanIndex - static_cast<int>(drop) : -1;
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
    ++_executing;
    _commands[_index]->undo();
    --_executing;

    // A later edit must not fold into a command that is no longer adjacent to
    // the cursor.
    _mergeAllowed = false;
    notifyChanged();
}

bool CommandStack::discardLast()
{
    // With steps undone after it, those would be redone onto a document
    // without it (and onto what it owned, freed): refused.
    if (!canUndo() || canRedo() || !_macroStack.empty())
        return false;

    --_index;
    ++_executing;
    _commands[_index]->undo();
    --_executing;
    _commands.erase(_commands.begin() + static_cast<std::ptrdiff_t>(_index));

    // A saved position past the cut can no longer be reached.
    if (_cleanIndex > static_cast<int>(_index))
        _cleanIndex = -1;

    _mergeAllowed = false;
    notifyChanged();
    return true;
}

void CommandStack::redo()
{
    if (!canRedo())
        return;

    ++_executing;
    _commands[_index]->redo();
    --_executing;
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

void CommandStack::retargetSelection(Selection* from, Selection* to)
{
    for (CommandPtr& command : _commands)
        command->retargetSelection(from, to);
    for (CommandPtr& macro : _macroStack)
        macro->retargetSelection(from, to);
}

void CommandStack::swapHistory(CommandStack& other)
{
    std::swap(_commands, other._commands);
    std::swap(_index, other._index);
    std::swap(_cleanIndex, other._cleanIndex);
    _mergeAllowed = false;
    other._mergeAllowed = false;
    notifyChanged();
    other.notifyChanged();
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
