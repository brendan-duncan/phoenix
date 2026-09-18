#pragma once

#include "command.h"

#include "../data/transform.h"
#include "editable_path.h"

#include <string>

namespace fla {

class Edge;
class Element;
class Frame;
class Selection;
class Shape;

/// Replaces an element's transform.
///
/// Moving, scaling, rotating and skewing a whole object are all the same edit
/// underneath -- only the matrix and the name differ -- so they share one
/// command rather than four near-identical ones.
///
/// A drag applies its transform live and pushes a single command on release, so
/// the whole gesture is one undo step. Merging exists for the other case:
/// repeated arrow-key nudges, which should also collapse into one step.
class SetElementTransformCommand : public Command
{
public:
    /// \a name is the gesture as the user would describe it ("Move", "Scale"),
    /// and becomes the Edit menu text.
    SetElementTransformCommand(Element* element, const Transform& before,
        const Transform& after, const std::string& name);

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    int mergeId() const override;

    /// Folds a later transform of the same element under the same gesture name
    /// into this one, keeping the original starting point.
    bool mergeWith(const Command* other) override;

    const Element* element() const { return _element; }

private:
    Element* _element;
    Transform _before;
    Transform _after;
    std::string _name;
};

/// Replaces the geometry of one edge.
///
/// Dragging an anchor or a handle rewrites the whole path rather than patching
/// one segment, because an anchor spans two segments and its handles live on
/// both sides of the join. Storing the before and after anchors keeps that
/// simple and makes undo exact.
class SetEdgeGeometryCommand : public Command
{
public:
    SetEdgeGeometryCommand(Edge* edge, const EditablePath& before,
        const EditablePath& after, const std::string& name);

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    int mergeId() const override;

    /// Folds a later edit of the same edge under the same name into this one.
    bool mergeWith(const Command* other) override;

private:
    Edge* _edge;
    EditablePath _before;
    EditablePath _after;
    std::string _name;
};

/// Merges a newly drawn shape into an existing one.
///
/// A merge rewrites the target completely -- its edges, its fills and its
/// strokes -- so undo keeps a copy of what was there rather than trying to
/// reverse the operation. Both snapshots are taken once, when the command is
/// built, and redo and undo just put one or the other back.
class MergeShapeCommand : public Command
{
public:
    /// Merges  addition into  target straight away, keeping snapshots of
    /// both sides of the edit. The addition is left untouched.
    MergeShapeCommand(Shape* target, const Shape& addition, const std::string& name);

    ~MergeShapeCommand() override;

    MergeShapeCommand(const MergeShapeCommand&) = delete;
    MergeShapeCommand& operator=(const MergeShapeCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    Shape* _target;
    Shape* _before = nullptr;
    Shape* _after = nullptr;
    std::string _name;
};

/// Cuts a shape's area out of another, leaving a hole.
///
/// Drawing over existing artwork destroys what was under it there and then, so
/// this runs when the drawing is placed rather than when it is moved. Moving it
/// afterwards simply uncovers the hole.
///
/// Like a merge, this rewrites the target completely and keeps snapshots of
/// both sides rather than trying to reverse the cut.
class SubtractShapeCommand : public Command
{
public:
    SubtractShapeCommand(Shape* target, const Shape& cutter, const std::string& name);

    ~SubtractShapeCommand() override;

    SubtractShapeCommand(const SubtractShapeCommand&) = delete;
    SubtractShapeCommand& operator=(const SubtractShapeCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    Shape* _target;
    Shape* _before = nullptr;
    Shape* _after = nullptr;
    std::string _name;
};

/// Adds an element to a frame, and takes it out again on undo.
///
/// Ownership moves with the element: a Frame deletes the elements it holds, so
/// while the element is out of the frame this command owns it, and deleting the
/// command then deletes the element. That is what makes it safe for an undone
/// creation to fall off the end of the history.
class AddElementCommand : public Command
{
public:
    /// The index is where the element sits among the frame's elements, which is
    /// its z-order; a negative index appends. The selection, when given, is told
    /// to let go of the element whenever it leaves the document, so undo cannot
    /// leave a selection pointing at freed memory.
    AddElementCommand(Frame* frame, Element* element, const std::string& name,
        Selection* selection = nullptr, int index = -1);

    ~AddElementCommand() override;

    AddElementCommand(const AddElementCommand&) = delete;
    AddElementCommand& operator=(const AddElementCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    Element* element() const { return _element; }

private:
    Frame* _frame;
    Element* _element;
    std::string _name;
    Selection* _selection;
    int _index;

    /// True while the element is out of the frame and this command holds it.
    bool _owned = true;
};

/// Removes an element from a frame, and puts it back on undo. The mirror of
/// AddElementCommand, with the same ownership rules.
class RemoveElementCommand : public Command
{
public:
    RemoveElementCommand(Frame* frame, Element* element, const std::string& name,
        Selection* selection = nullptr);

    ~RemoveElementCommand() override;

    RemoveElementCommand(const RemoveElementCommand&) = delete;
    RemoveElementCommand& operator=(const RemoveElementCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    Frame* _frame;
    Element* _element;
    std::string _name;
    Selection* _selection;

    /// Where it sat before removal, so undo restores the z-order too.
    int _index = -1;

    bool _owned = false;
};

} // namespace fla
