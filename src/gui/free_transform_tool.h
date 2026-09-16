#pragma once

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

    QString name() const override { return "Free Transform"; }

    QCursor cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

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
        Skew
    };

    /// Corner and edge-midpoint handles, in the order they sit around the box.
    static constexpr int kHandleCount = 8;

    void rebuildBox(PhoenixView& view);

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

    QTransform adjustForDrag(const QPointF& documentPos, Qt::KeyboardModifiers modifiers) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;

    /// The four corners of the transform box, in document space.
    QPolygonF _box;
    bool _boxValid = false;

    Grip _grip = Grip::None;
    int _gripIndex = -1;
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
};
