#include "element_commands.h"

#include "../data/element.h"

namespace fla {

namespace {

/// Shared by every transform command. Whether two of them actually merge is
/// decided by mergeWith, which also checks the element and the gesture.
constexpr int kTransformMergeId = 1;

} // namespace

SetElementTransformCommand::SetElementTransformCommand(Element* element,
    const Transform& before, const Transform& after, const std::string& name)
    : _element(element)
    , _before(before)
    , _after(after)
    , _name(name)
{}

void SetElementTransformCommand::redo()
{
    if (_element)
        _element->transform = _after;
}

void SetElementTransformCommand::undo()
{
    if (_element)
        _element->transform = _before;
}

int SetElementTransformCommand::mergeId() const
{
    return kTransformMergeId;
}

bool SetElementTransformCommand::mergeWith(const Command* other)
{
    const SetElementTransformCommand* next =
        static_cast<const SetElementTransformCommand*>(other);

    // Only fold together edits to the same object under the same gesture.
    // Merging a move into a scale would leave the Edit menu describing one while
    // undoing both.
    if (next->_element != _element || next->_name != _name)
        return false;

    // Keep this command's starting point so undo still returns to where the
    // gesture began, and take the later end state.
    _after = next->_after;
    return true;
}

} // namespace fla
