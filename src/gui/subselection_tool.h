#pragma once

#include "tool.h"

#include "../edit/editable_path.h"

#include <QPointF>
#include <QRectF>

#include <cstddef>
#include <vector>

namespace fla {
class CommandStack;
class Edge;
class Selection;
class Shape;
}

/// Animate's subselection tool: show a shape's anchors and drag them.
///
/// Selecting a shape with it reveals every anchor and handle on the shape's
/// edges. Clicking an anchor selects it, shift-click adds to or removes from
/// the selection, and a drag on empty stage draws a marquee that selects the
/// anchors inside it. Dragging a selected anchor moves every selected anchor
/// with its handles; the arrow keys nudge them; Delete removes them. Dragging
/// a handle bends the curve, keeping the tangent straight through a smooth
/// point unless alt is held to break it.
///
/// A drag edits the geometry live and pushes one command on release, the same
/// way the transform tools work.
class SubselectionTool : public Tool
{
public:
    SubselectionTool(fla::Selection& selection, fla::CommandStack& commandStack)
        : _selection(selection)
        , _commandStack(commandStack)
    {}

    QString name() const override { return "Subselection"; }

    QCursor cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    bool showsFillHighlight() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// Which part of an anchor a drag grabbed.
    enum class Grip
    {
        None,
        Anchor,
        InHandle,
        OutHandle,
        /// A drag on empty stage: the marquee.
        Marquee
    };

    /// One anchor of the edited shape: which edge, which of its paths, and
    /// which anchor on it.
    struct AnchorRef
    {
        fla::Edge* edge = nullptr;
        size_t pathIndex = 0;
        size_t anchorIndex = 0;

        bool operator==(const AnchorRef& other) const
        {
            return edge == other.edge && pathIndex == other.pathIndex &&
                anchorIndex == other.anchorIndex;
        }
    };

    /// The shape currently showing its anchors, or null.
    fla::Shape* editedShape() const;

    /// Maps the shape's own coordinates to document coordinates.
    QTransform shapeToDocument(PhoenixView& view) const;

    void cancelDrag(PhoenixView& view);

    /// Rewrites \a edge to \a path as one undo step.
    void commitGeometry(PhoenixView& view, fla::Edge* edge,
        const fla::EditablePath& before, const fla::EditablePath& after,
        const char* gesture, size_t pathIndex = 0);

    /// One path caught by a gesture: which edge and path, the anchors on it
    /// that move, and the path's geometry when the gesture began.
    struct Grabbed
    {
        fla::Edge* edge = nullptr;
        size_t pathIndex = 0;
        std::vector<size_t> anchors;
        fla::EditablePath before;
        fla::EditablePath current;
    };

    /// Builds _grabbed from the selected anchors, taking every anchor in the
    /// shape that sits on one of them: a merged shape is written as one edge
    /// per piece, so its corners are shared, and moving only one copy would
    /// tear the outline apart.
    void grabSelectedAnchors(fla::Shape& shape);

    /// Adds every anchor sitting at \a position to \a refs.
    void anchorsAt(fla::Shape& shape, const fla::Point& position, std::vector<AnchorRef>& refs) const;

    bool isSelected(const AnchorRef& ref) const;

    void select(const AnchorRef& ref, bool add);

    /// Moves every grabbed anchor by \a delta in the shape's space and
    /// writes the geometry live.
    void moveGrabbed(PhoenixView& view, const QPointF& delta);

    /// Pushes the grabbed paths' changes as one undo step.
    void commitGrabbed(PhoenixView& view, const char* gesture);

    /// Removes the selected anchors. A corner shared between the ends of
    /// two open pieces with the same styles is removed by joining the pieces
    /// into one and refitting through the gap.
    void deleteSelected(PhoenixView& view);

    /// The pick tolerance in the shape's own space, so the grab distance
    /// stays the same on screen however the shape is scaled.
    double localTolerance(PhoenixView& view) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;

    /// The anchors that are selected, which a drag, a nudge or Delete acts on.
    std::vector<AnchorRef> _selected;

    /// Every path the gesture is moving.
    std::vector<Grabbed> _grabbed;

    Grip _grip = Grip::None;
    QPointF _dragStart;

    /// The marquee's corners in document space while it is being dragged.
    QPointF _marqueeStart;
    QPointF _marqueeEnd;
    bool _marqueeAdds = false;

    /// Whether the press has traveled far enough to be a drag. A click on an
    /// anchor only selects it; it must not write a command or modify the
    /// document.
    bool _dragMoved = false;

    /// Alt-dragging a corner point pulls handles out of it, turning it into a
    /// curve point. That is Animate's convert gesture with this tool.
    bool _convertDrag = false;
};
