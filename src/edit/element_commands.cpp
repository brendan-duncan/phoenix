#include "element_commands.h"

#include "../data/edge.h"
#include "../data/element.h"
#include "../data/frame.h"
#include "selection.h"
#include "shape_merge.h"

#include "../data/shape.h"

#include <algorithm>

namespace fla {

namespace {

/// Shared by every transform command. Whether two of them actually merge is
/// decided by mergeWith, which also checks the element and the gesture.
constexpr int kTransformMergeId = 1;

/// Shared by every edge-geometry command; mergeWith decides the rest.
constexpr int kGeometryMergeId = 2;

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

SetEdgeGeometryCommand::SetEdgeGeometryCommand(Edge* edge, const EditablePath& before,
    const EditablePath& after, const std::string& name)
    : _edge(edge)
    , _before(before)
    , _after(after)
    , _name(name)
{}

void SetEdgeGeometryCommand::redo()
{
    if (_edge)
        _after.applyTo(*_edge);
}

void SetEdgeGeometryCommand::undo()
{
    if (_edge)
        _before.applyTo(*_edge);
}

int SetEdgeGeometryCommand::mergeId() const
{
    return kGeometryMergeId;
}

bool SetEdgeGeometryCommand::mergeWith(const Command* other)
{
    const SetEdgeGeometryCommand* next =
        static_cast<const SetEdgeGeometryCommand*>(other);

    if (next->_edge != _edge || next->_name != _name)
        return false;

    // Keep this command's starting geometry so undo still returns to where the
    // gesture began.
    _after = next->_after;
    return true;
}

MergeShapeCommand::MergeShapeCommand(Shape* target, const Shape& addition,
    const std::string& name)
    : _target(target)
    , _name(name)
{
    if (!_target)
        return;

    _before = cloneShape(*_target, nullptr);

    // The merge runs once, here. redo() and undo() only ever swap a snapshot
    // back in, so repeating either is cheap and cannot drift.
    ShapeMerger::merge(*_target, addition);

    _after = cloneShape(*_target, nullptr);
}

MergeShapeCommand::~MergeShapeCommand()
{
    delete _before;
    delete _after;
}

void MergeShapeCommand::redo()
{
    if (_target && _after)
        setShapeContents(*_target, *_after);
}

void MergeShapeCommand::undo()
{
    if (_target && _before)
        setShapeContents(*_target, *_before);
}

AddElementCommand::AddElementCommand(Frame* frame, Element* element,
    const std::string& name, Selection* selection, int index)
    : _frame(frame)
    , _element(element)
    , _name(name)
    , _selection(selection)
    , _index(index)
{}

AddElementCommand::~AddElementCommand()
{
    // Only delete it if it is not in the frame: once it is in, the frame owns it.
    if (_owned)
        delete _element;
}

void AddElementCommand::redo()
{
    if (!_frame || !_element || !_owned)
        return;

    std::vector<Element*>& elements = _frame->elements;

    // Re-inserting at the recorded index is what preserves z-order across an
    // undo and redo, rather than the object jumping to the front.
    if (_index < 0 || _index > static_cast<int>(elements.size()))
        _index = static_cast<int>(elements.size());

    elements.insert(elements.begin() + _index, _element);
    _owned = false;
}

void AddElementCommand::undo()
{
    if (!_frame || !_element || _owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    const auto it = std::find(elements.begin(), elements.end(), _element);
    if (it == elements.end())
        return;

    _index = static_cast<int>(it - elements.begin());
    elements.erase(it);
    _owned = true;

    // The element is no longer in the document, so nothing may keep pointing at
    // it.
    if (_selection)
        _selection->remove(_element);
}

RemoveElementCommand::RemoveElementCommand(Frame* frame, Element* element,
    const std::string& name, Selection* selection)
    : _frame(frame)
    , _element(element)
    , _name(name)
    , _selection(selection)
{}

RemoveElementCommand::~RemoveElementCommand()
{
    if (_owned)
        delete _element;
}

void RemoveElementCommand::redo()
{
    if (!_frame || !_element || _owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    const auto it = std::find(elements.begin(), elements.end(), _element);
    if (it == elements.end())
        return;

    _index = static_cast<int>(it - elements.begin());
    elements.erase(it);
    _owned = true;

    if (_selection)
        _selection->remove(_element);
}

void RemoveElementCommand::undo()
{
    if (!_frame || !_element || !_owned)
        return;

    std::vector<Element*>& elements = _frame->elements;
    if (_index < 0 || _index > static_cast<int>(elements.size()))
        _index = static_cast<int>(elements.size());

    elements.insert(elements.begin() + _index, _element);
    _owned = false;
}

} // namespace fla
