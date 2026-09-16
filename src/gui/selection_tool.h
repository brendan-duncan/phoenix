#pragma once

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

/// Animate's arrow tool, in the part of its job that exists so far: picking
/// things on the stage.
///
/// Click selects the topmost object, shift-click adds or removes, dragging an
/// object moves it, dragging empty stage sweeps out a marquee, and clicking
/// empty stage clears.
///
/// Scaling, rotating and skewing live in FreeTransformTool, the same split
/// Animate makes between its arrow and free transform tools.
class SelectionTool : public Tool
{
public:
    SelectionTool(fla::Selection& selection, fla::CommandStack& commandStack)
        : _selection(selection)
        , _commandStack(commandStack)
    {}

    QString name() const override { return "Selection"; }

    QCursor cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    /// The marquee so far, normalised so it is valid whichever way it was drawn.
    QRectF marqueeRect() const;

    /// Begins dragging the current selection from documentPos.
    void beginMove(PhoenixView& view, const QPointF& documentPos);

    /// Snaps a proposed move, returning the delta to actually apply.
    QPointF snapMove(PhoenixView& view, const QPointF& delta) const;

    /// Applies the move so far and repaints.
    void updateMove(PhoenixView& view, const QPointF& documentPos);

    /// Turns the completed move into one undo step.
    void commitMove(PhoenixView& view);

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

    /// Whether the press has travelled far enough to count as a drag.
    ///
    /// A press alone is a selection click, and must leave the objects where they
    /// are. Snapping asks where the box would land rather than how far the
    /// cursor moved, so running it at zero distance would answer "on the nearest
    /// grid line" and shift anything not already sitting on one.
    bool _moveDragging = false;

    /// Document-space bounds of the selection when the drag began. Snapping
    /// works on where the box would land, not on the cursor.
    QRectF _moveBounds;

    bool _marqueeActive = false;
    QPointF _marqueeStart;
    QPointF _marqueeEnd;

    /// Whether the press that started this gesture had shift held, which decides
    /// between replacing the selection and adding to it.
    bool _additive = false;
};
