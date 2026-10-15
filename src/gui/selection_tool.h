#pragma once

#include "cursors.h"
#include "edge_bend.h"
#include "ik_poser.h"
#include "tool.h"

#include "../data/transform.h"

#include <QPointF>
#include <QRectF>

#include <vector>

namespace fla {
class CommandStack;
class Element;
class Selection;
}

/// The arrow tool.
///
/// Click selects the topmost object, shift-click adds or removes, dragging an
/// object moves it, dragging empty stage sweeps out a marquee, and clicking
/// empty stage clears. Dragging the outline of a shape that is not selected
/// bends it, dragging one of its corners moves the corner, and ctrl-drag on an
/// outline makes a new corner. Shift constrains a move to 45 degree steps and
/// alt-drag moves a copy.
///
/// Scaling, rotating and skewing live in FreeTransformTool, which keeps the
/// arrow and free transform tools apart.
class SelectionTool : public Tool
{
public:
    SelectionTool(fla::Selection& selection, fla::CommandStack& commandStack)
        : _selection(selection)
        , _commandStack(commandStack)
    {}

    QString name() const override { return "Selection"; }

    QCursor cursor() const override { return Cursors::cursor(_hoverKind); }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    /// Double-click widens a region selection: a fill with its strokes, a
    /// stroke with the strokes joined to it.
    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    /// The marquee so far, normalized so it is valid whichever way it was drawn.
    QRectF marqueeRect() const;

    /// Begins dragging the current selection from documentPos.
    void beginMove(PhoenixView& view, const QPointF& documentPos);

    /// Snaps a proposed move, returning the delta to actually apply, and
    /// notes whether anything snapped for the ring.
    QPointF snapMove(PhoenixView& view, const QPointF& delta);

    /// Moves the selection by a whole number of pixels, as the arrow keys do.
    /// Repeated nudges fold into one undo step.
    void nudge(PhoenixView& view, double dx, double dy);

    /// Applies the move so far and repaints.
    void updateMove(PhoenixView& view, const QPointF& documentPos, Qt::KeyboardModifiers modifiers);

    /// Turns the completed move into one undo step.
    void commitMove(PhoenixView& view);

    /// Alt-drag: replaces the moving shapes with copies, so the originals stay
    /// where they were. Only shapes can be copied yet.
    void duplicateTargets(PhoenixView& view);

    /// Puts the dragged objects back and forgets the move.
    void cancelMove(PhoenixView& view);

    /// What the arrow should show over \a documentPos when nothing is pressed.
    Cursors::Kind hoverKindAt(PhoenixView& view, const QPointF& documentPos) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;

    /// What is being dragged, with the transform each element had when the drag
    /// began, so the move is always measured from the start.
    struct Target
    {
        fla::Element* element = nullptr;
        fla::Transform startTransform;
    };
    std::vector<Target> _targets;

    bool _moveActive = false;
    QPointF _moveStart;

    /// Whether the press has traveled far enough to count as a drag.
    ///
    /// A press alone is a selection click, and must leave the objects where they
    /// are. Snapping asks where the box would land rather than how far the
    /// cursor moved, so running it at zero distance would answer "on the nearest
    /// grid line" and shift anything not already sitting on one.
    bool _moveDragging = false;

    /// Document-space bounds of the selection when the drag began. Snapping
    /// works on where the box would land, not on the cursor.
    QRectF _moveBounds;

    /// Whether this drag has already swapped the originals for copies.
    bool _duplicated = false;

    bool _marqueeActive = false;
    QPointF _marqueeStart;
    QPointF _marqueeEnd;

    /// Whether the press that started this gesture had shift held, which decides
    /// between replacing the selection and adding to it.
    bool _additive = false;

    /// Shift-click on something already selected removes it, but only once the
    /// button comes up without a drag: a shift-drag moves the lot.
    fla::Element* _pressedElement = nullptr;
    bool _removeOnRelease = false;

    /// Reshaping an unselected shape's outline.
    EdgeBender _bender;
    /// Dragging a bone of an armature into a pose.
    IKPoser _poser;

    /// Where and on what the bend gesture began, so a press on an outline that
    /// never moves can turn into a click that selects the stroke or fill.
    HitResult _bendHit;
    QPointF _bendPress;

    /// What the arrow is over, so the cursor can say what a press would do.
    Cursors::Kind _hoverKind = Cursors::Kind::Arrow;

    /// The snap ring: a small ring is drawn at the grab point while
    /// snapping is on, which grows when the drag locks onto something.
    bool _snapped = false;
    QPointF _snapRingPos;
};
