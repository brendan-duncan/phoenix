#pragma once

#include "command.h"

#include "../data/dom_element.h"
#include "../data/transform.h"
#include "editable_path.h"
#include "shape_merge.h"

#include <string>
#include <vector>

namespace fla {

class Document;
class Edge;
class Element;
class StaticText;
class Frame;
class Group;
class Selection;
class Shape;
class Symbol;
class SymbolInstance;

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
    /// \a pathIndex is which of the edge's paths the anchors describe; the
    /// others are left alone.
    SetEdgeGeometryCommand(Edge* edge, const EditablePath& before,
        const EditablePath& after, const std::string& name, size_t pathIndex = 0);

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    int mergeId() const override;

    /// Folds a later edit of the same edge under the same name into this one.
    bool mergeWith(const Command* other) override;

private:
    /// The edge as it is now. An edge of a shape is found again by its
    /// place among the shape's edges each time: undoing or redoing a merge
    /// rebuilds every edge of the shape (setShapeContents), freeing the one
    /// this was made with, but in the same order, and the history always
    /// brings the shape back to the state this command saw.
    Edge* edge() const;

    Edge* _edge;
    Shape* _shape = nullptr;
    size_t _edgeIndex = 0;
    EditablePath _before;
    EditablePath _after;
    std::string _name;
    size_t _pathIndex = 0;
};

/// Roughly how many bytes a shape snapshot takes: its edges, their paths
/// and points, and its styles. For the history's memory budget.
size_t shapeMemory(const Shape* shape);

/// Merges a newly drawn shape into an existing one.
///
/// A merge rewrites the target completely -- its edges, its fills and its
/// strokes -- so undo keeps a copy of what was there rather than trying to
/// reverse the operation. Both snapshots are taken once, when the command is
/// built, and redo and undo just put one or the other back.
class MergeShapeCommand : public Command
{
public:
    /// Merges \a addition into \a target straight away, keeping snapshots of
    /// both sides of the edit. The addition is left untouched.
    MergeShapeCommand(Shape* target, const Shape& addition, const std::string& name,
        const MergeOptions& options = MergeOptions());

    ~MergeShapeCommand() override;

    MergeShapeCommand(const MergeShapeCommand&) = delete;
    MergeShapeCommand& operator=(const MergeShapeCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }
    size_t memoryUse() const override { return shapeMemory(_before) + shapeMemory(_after); }

private:
    Shape* _target;
    Shape* _before = nullptr;
    Shape* _after = nullptr;
    std::string _name;
};

/// Takes the strokes inside a cutter's area out of a shape, leaving its
/// fills: the eraser's Erase Lines mode. Snapshots both sides like a merge.
class EraseStrokesCommand : public Command
{
public:
    EraseStrokesCommand(Shape* target, const Shape& cutter, const std::string& name);

    ~EraseStrokesCommand() override;

    EraseStrokesCommand(const EraseStrokesCommand&) = delete;
    EraseStrokesCommand& operator=(const EraseStrokesCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }
    size_t memoryUse() const override { return shapeMemory(_before) + shapeMemory(_after); }

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
    SubtractShapeCommand(Shape* target, const Shape& cutter, const std::string& name,
        const MergeOptions& options = MergeOptions());

    ~SubtractShapeCommand() override;

    SubtractShapeCommand(const SubtractShapeCommand&) = delete;
    SubtractShapeCommand& operator=(const SubtractShapeCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }
    size_t memoryUse() const override { return shapeMemory(_before) + shapeMemory(_after); }

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

/// Replaces a shape's geometry and styles with a prepared result, keeping a
/// snapshot of what was there. The paint bucket and the ink bottle build
/// their result from the arrangement and hand it over here.
class SetShapeContentsCommand : public Command
{
public:
    SetShapeContentsCommand(Shape* target, const Shape& after, const std::string& name);

    ~SetShapeContentsCommand() override;

    SetShapeContentsCommand(const SetShapeContentsCommand&) = delete;
    SetShapeContentsCommand& operator=(const SetShapeContentsCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }
    size_t memoryUse() const override { return shapeMemory(_before) + shapeMemory(_after); }

private:
    Shape* _target;
    Shape* _before = nullptr;
    Shape* _after = nullptr;
    std::string _name;
};

/// Lifts a region out of a merge-drawn shape as a shape of its own.
///
/// Selecting a fill or a stroke of merge artwork is what this serves: the
/// region becomes the piece, floating above what is left of the source. Redo
/// rewrites the source with the remainder and puts the piece in the frame just
/// above it; undo takes the piece out and restores the source. The piece is
/// owned here while it is out of the frame, as AddElementCommand owns its
/// element.
class SplitRegionCommand : public Command
{
public:
    /// Takes ownership of \a piece. \a remainder is copied.
    SplitRegionCommand(Shape* source, const Shape& remainder, Frame* frame,
        Shape* piece, const std::string& name, Selection* selection = nullptr);

    ~SplitRegionCommand() override;

    SplitRegionCommand(const SplitRegionCommand&) = delete;
    SplitRegionCommand& operator=(const SplitRegionCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }
    size_t memoryUse() const override { return shapeMemory(_before) + shapeMemory(_remainder); }

    Shape* piece() const { return _piece; }

    Shape* source() const { return _source; }

private:
    Shape* _source;
    Shape* _before = nullptr;
    Shape* _remainder = nullptr;
    Frame* _frame;
    Shape* _piece;
    std::string _name;
    Selection* _selection;
    bool _owned = true;
};

/// Gathers elements of a frame into a group, as Modify > Group does. The
/// group goes where the topmost of them was. The group is owned here while it
/// is out of the frame; the members always belong to one or the other.
class GroupElementsCommand : public Command
{
public:
    GroupElementsCommand(Frame* frame, const std::vector<Element*>& elements,
        const std::string& name, Selection* selection = nullptr);

    ~GroupElementsCommand() override;

    GroupElementsCommand(const GroupElementsCommand&) = delete;
    GroupElementsCommand& operator=(const GroupElementsCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    Group* group() const { return _group; }

private:
    Frame* _frame;
    std::vector<Element*> _elements;
    std::vector<int> _indices;
    Group* _group;
    std::string _name;
    Selection* _selection;
    bool _applied = false;
};

/// Takes a group apart, leaving its members in the frame where the group was.
/// The mirror of GroupElementsCommand.
class UngroupCommand : public Command
{
public:
    UngroupCommand(Frame* frame, Group* group, const std::string& name,
        Selection* selection = nullptr);

    ~UngroupCommand() override;

    UngroupCommand(const UngroupCommand&) = delete;
    UngroupCommand& operator=(const UngroupCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    Frame* _frame;
    Group* _group;
    std::vector<Element*> _members;
    int _index = -1;
    std::string _name;
    Selection* _selection;
    bool _held = false;
};

/// Moves an element to another place in its frame's z-order, for the Arrange
/// commands.
class ReorderElementCommand : public Command
{
public:
    ReorderElementCommand(Frame* frame, Element* element, int newIndex, const std::string& name);

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    void moveTo(int index);

    Frame* _frame;
    Element* _element;
    int _before = -1;
    int _after;
    std::string _name;
};

/// Turns a drawing object into merge artwork or back, which is what Break
/// Apart does to a drawing object.
class SetDrawingObjectCommand : public Command
{
public:
    SetDrawingObjectCommand(Shape* shape, bool drawingObject, const std::string& name);

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    Shape* _shape;
    bool _before;
    bool _after;
    std::string _name;
};

/// Swaps a text's contents for new ones, as the text tool does when editing
/// is finished. Holds a copy of the text before and after.
class SetTextContentsCommand : public Command
{
public:
    SetTextContentsCommand(StaticText* target, const StaticText& before,
        const StaticText& after, const std::string& name);

    ~SetTextContentsCommand() override;

    SetTextContentsCommand(const SetTextContentsCommand&) = delete;
    SetTextContentsCommand& operator=(const SetTextContentsCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

private:
    StaticText* _target;
    StaticText* _before;
    StaticText* _after;
    std::string _name;
};

/// Replaces a symbol instance with copies of what its symbol shows at the
/// instance's frame, as Modify > Break Apart does to an instance. The copies
/// land where the instance was, in drawing order, each carrying the
/// instance's transform on top of its own, so nothing moves on the stage. The
/// library item itself is left alone.
class BreakApartInstanceCommand : public Command
{
public:
    /// \a shown is the frame of the symbol to take apart, the one the stage
    /// shows; left out, the one shownFrameIndex says.
    BreakApartInstanceCommand(Frame* frame, SymbolInstance* instance,
        const std::string& name, Selection* selection = nullptr, int shown = -1);

    ~BreakApartInstanceCommand() override;

    BreakApartInstanceCommand(const BreakApartInstanceCommand&) = delete;
    BreakApartInstanceCommand& operator=(const BreakApartInstanceCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    /// The copies that stand in for the instance.
    const std::vector<Element*>& pieces() const { return _pieces; }

    /// The frame of the symbol an instance shows when it is not playing: the
    /// first frame of the symbol for a movie clip or a button, the instance's
    /// first frame for a graphic.
    static int shownFrameIndex(const SymbolInstance& instance);

private:
    Frame* _frame;
    SymbolInstance* _instance;
    std::vector<Element*> _pieces;
    int _index = -1;
    std::string _name;
    Selection* _selection;
    bool _applied = false;
};

/// Turns the selection into a library symbol and leaves an instance of it in
/// its place, as Modify > Convert to Symbol does.
///
/// The elements move into the first frame of the new symbol's timeline,
/// shifted so that the registration point becomes the symbol's origin, and
/// the instance carries the registration point back as its position. The
/// symbol joins the document's library, so saving writes it out.
class ConvertToSymbolCommand : public Command
{
public:
    ConvertToSymbolCommand(Document* document, Frame* frame,
        const std::vector<Element*>& elements, const std::string& symbolName,
        double registrationX, double registrationY, const std::string& name,
        Selection* selection = nullptr, SymbolType symbolType = SymbolType::MovieClip);

    ~ConvertToSymbolCommand() override;

    ConvertToSymbolCommand(const ConvertToSymbolCommand&) = delete;
    ConvertToSymbolCommand& operator=(const ConvertToSymbolCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    SymbolInstance* instance() const { return _instance; }

    Symbol* symbol() const { return _symbol; }

private:
    /// Every element whose transform carries position: groups move by their
    /// members.
    void collectMovable(Element* element, std::vector<Element*>& out) const;

    Document* _document;
    Frame* _frame;
    std::vector<Element*> _elements;
    std::vector<int> _indices;
    std::vector<Element*> _moved;
    std::vector<Transform> _movedTransforms;
    double _registrationX;
    double _registrationY;
    Symbol* _symbol;
    Frame* _symbolFrame = nullptr;
    SymbolInstance* _instance;
    std::string _name;
    Selection* _selection;
    bool _applied = false;
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
