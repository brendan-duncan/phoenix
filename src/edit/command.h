#pragma once

#include <memory>
#include <string>
#include <vector>

namespace fla {

/// A single reversible edit.
///
/// Commands are executed by the CommandStack, never directly: push() calls
/// redo() once to apply the edit for the first time. Implementations must make
/// redo() and undo() exact inverses, and must be safe to run repeatedly in
/// alternation.
///
/// This layer is deliberately free of Qt so the data model and editing core can
/// be reused with another rendering framework.
class Command
{
public:
    virtual ~Command() = default;

    /// Applies the edit. Called once by CommandStack::push(), then again on
    /// every redo.
    virtual void redo() = 0;

    /// Reverses the edit, restoring the state from before redo() ran.
    virtual void undo() = 0;

    /// Human-readable description, used for the Edit menu ("Undo Move Anchor").
    virtual std::string name() const = 0;

    /// Identifies commands of the same kind for merging. Commands with a
    /// negative id never merge, which is the default.
    virtual int mergeId() const { return -1; }

    /// Folds \a other into this command so a continuous gesture (dragging an
    /// anchor, say) collapses into a single undo step. Only attempted when both
    /// commands report the same non-negative mergeId().
    ///
    /// Returns true if \a other was absorbed, in which case the stack discards
    /// it. The default rejects the merge.
    virtual bool mergeWith(const Command* other)
    {
        (void)other;
        return false;
    }
};

typedef std::unique_ptr<Command> CommandPtr;

/// Groups several commands so they undo and redo as one unit.
///
/// Children are redone in the order they were added and undone in reverse, so a
/// compound behaves like the sequence of edits it contains.
class CompoundCommand : public Command
{
public:
    explicit CompoundCommand(const std::string& name)
        : _name(name)
    {}

    /// Adds an already-applied child command. The compound takes ownership.
    ///
    /// The child is *not* executed here; CompoundCommand is assembled by the
    /// stack from commands that have already run.
    void addCommand(CommandPtr command)
    {
        _commands.push_back(std::move(command));
    }

    bool isEmpty() const { return _commands.empty(); }

    size_t count() const { return _commands.size(); }

    void redo() override
    {
        for (CommandPtr& command : _commands)
            command->redo();
    }

    void undo() override
    {
        for (size_t i = _commands.size(); i > 0; --i)
            _commands[i - 1]->undo();
    }

    std::string name() const override { return _name; }

    void setName(const std::string& name) { _name = name; }

private:
    std::string _name;
    std::vector<CommandPtr> _commands;
};

} // namespace fla
