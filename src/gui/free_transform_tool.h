#pragma once

#include "cursors.h"
#include "tool.h"

#include "../data/transform.h"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <vector>

namespace fla {
class CommandStack;
class Element;
class Selection;
class Shape;
}

/// Animate's free transform tool: move, scale, rotate and skew whatever is
/// selected.
///
/// The tool keeps its own transform box rather than recomputing an axis-aligned
/// bounding box each frame. Once something has been rotated, its box is rotated
/// too, which is what makes the handles stay on the corners of the object
/// instead of snapping back to an upright rectangle.
///
/// A drag is applied live and committed as a single undo step on release.
class FreeTransformTool : public Tool
{
public:
    FreeTransformTool(fla::Selection& selection, fla::CommandStack& commandStack)
        : _selection(selection)
        , _commandStack(commandStack)
    {}

    ~FreeTransformTool() override;

    QString name() const override { return "Free Transform"; }

    /// Follows the pointer: a move box inside, a double arrow on a handle
    /// turned to its axis, skew arrows along an edge, a curling arrow just
    /// outside a corner.
    QCursor cursor() const override { return Cursors::cursor(_hoverKind, _hoverTurns); }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    /// The transform box stands in for the plain selection box.
    bool showsSelectionBounds() const override { return false; }

    /// Ctrl on a corner is Distort, so ctrl is not the temporary arrow here.
    bool usesControlKey() const override { return true; }

    void deactivate(PhoenixView& view) override;

    /// Discards the current transform box, so the next paint rebuilds it from
    /// the selection. Call when the selection changes.
    void resetBox() { _boxValid = false; }

private:
    /// What the press grabbed.
    enum class Grip
    {
        None,
        /// Inside the box: move.
        Body,
        /// One of the eight box handles: scale.
        Handle,
        /// Just outside a corner: rotate.
        Rotate,
        /// An edge between two handles: skew.
        Skew,
        /// Ctrl on a corner handle, with only shapes selected: the corner
        /// moves on its own and the geometry is warped to follow, Animate's
        /// Distort. Shift moves the neighbouring corner the opposite way
        /// along their edge, which tapers.
        Distort
    };

    /// Corner and edge-midpoint handles, in the order they sit around the box.
    static constexpr int kHandleCount = 8;

    void rebuildBox(PhoenixView& view);

    /// What a press or a hover at \a documentPos would grab on the current
    /// box, with the handle or edge index when there is one.
    Grip gripAt(PhoenixView& view, const QPointF& documentPos, int& index) const;

    /// Sets the cursor for a grip, turned to suit its handle or edge.
    void showCursorFor(PhoenixView& view, Grip grip, int index);

    /// The nearest 45 degree step to a direction, for turning a cursor.
    static int eighthTurnsFor(const QPointF& direction);

    /// Index of the handle within \a tolerance of \a documentPos, or -1.
    int handleAt(const QPointF& documentPos, double tolerance) const;

    /// Index of the edge whose midpoint region contains \a documentPos, or -1.
    int edgeAt(const QPointF& documentPos, double tolerance) const;

    /// Handle positions are read off a specific box. A gesture always measures
    /// against the box as it was when the drag began, so the anchor cannot creep
    /// as the live box moves under it.
    static QPointF handlePoint(const QPolygonF& box, int index);

    /// The handle diagonally opposite \a index, which a scale holds still.
    static QPointF anchorFor(const QPolygonF& box, int index);

    static QPointF centerOf(const QPolygonF& box);

    /// Applies \a adjust, a document-space matrix, on top of each selected
    /// element's starting transform.
    void applyAdjust(PhoenixView& view, const QTransform& adjust);

    /// Pushes the gesture as one undo step and clears the drag state.
    void commit(PhoenixView& view);

    /// The box a Distort drag has pulled the starting box into.
    QPolygonF distortedBox(const QPointF& documentPos, Qt::KeyboardModifiers modifiers) const;

    /// Rewrites each shape's geometry so the starting box maps onto \a box.
    void applyDistort(PhoenixView& view, const QPolygonF& box);

    /// Records the distortion as one undo step per shape.
    void commitDistort(PhoenixView& view);

    /// Puts the shapes back as they were before the Distort drag.
    void restoreDistortTargets();

    void clearDistortTargets();

    QTransform adjustForDrag(const QPointF& documentPos, Qt::KeyboardModifiers modifiers) const;

    /// Nudges the cursor position onto the grid or a neighbouring edge before
    /// the gesture is computed from it.
    QPointF snapDragPoint(PhoenixView& view, const QPointF& documentPos) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;

    /// The four corners of the transform box, in document space.
    QPolygonF _box;
    bool _boxValid = false;

    /// What the box was built for: each selected element and the transform it
    /// had. A press checks this first, so a box left over from before an undo
    /// or an edit by another tool is rebuilt rather than used.
    struct BoxOwner
    {
        fla::Element* element = nullptr;
        fla::Transform transform;
    };
    std::vector<BoxOwner> _boxOwners;

    void rememberBoxOwners();

    bool boxMatchesSelection() const;

    Grip _grip = Grip::None;
    int _gripIndex = -1;

    Cursors::Kind _hoverKind = Cursors::Kind::Arrow;
    int _hoverTurns = 0;
    QPointF _dragStart;
    QPolygonF _boxAtDragStart;

    /// Elements being transformed, with the transform each had when the drag
    /// began, so the gesture is always computed from the starting state rather
    /// than accumulating rounding error.
    struct Target
    {
        fla::Element* element = nullptr;
        fla::Transform startTransform;
    };
    std::vector<Target> _targets;

    /// The shapes a Distort drag is warping, each with a copy of its geometry
    /// from before the drag, which every mouse move starts again from.
    struct DistortTarget
    {
        fla::Shape* shape = nullptr;
        fla::Shape* original = nullptr;
    };
    std::vector<DistortTarget> _distortTargets;
};
