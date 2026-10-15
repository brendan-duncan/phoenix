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
class DrawingStyle;
class Element;
class Selection;
class Shape;
class Snapper;
}

/// The free transform tool: move, scale, rotate and skew whatever is
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
    FreeTransformTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
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

    /// A double click on the transformation point puts it back at the
    /// center of the box.
    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    /// The transform box stands in for the plain selection box.
    bool showsSelectionBounds() const override { return false; }

    /// Ctrl on a corner is Distort, so ctrl is not the temporary arrow here.
    bool usesControlKey() const override { return true; }

    void deactivate(PhoenixView& view) override;

    /// Discards the current transform box, so the next paint rebuilds it from
    /// the selection. Call when the selection changes.
    void resetBox()
    {
        _boxValid = false;
        clearEnvelope();
    }

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
        /// moves on its own and the geometry is warped to follow. Shift moves the neighboring corner the opposite way
        /// along their edge, which tapers.
        Distort,
        /// An anchor or a handle of the envelope: the box's sides bend and
        /// the geometry follows.
        Envelope,
        /// The transformation point: a drag moves it, snapping to the box's
        /// center and handles.
        Pivot
    };

    /// Envelope mode's mesh: eight anchors round the box, a corner and a
    /// midpoint on each side, each with a handle towards either neighbor.
    /// The eight cubics they make are the sides of a Coons patch, and the
    /// shapes are warped from the straight box the session began with onto
    /// that patch. The session lasts until the selection or the box changes.
    struct EnvelopeAnchor
    {
        QPointF point;
        QPointF in;
        QPointF out;
    };

    /// Builds the envelope on the current box and snapshots the shapes.
    void setUpEnvelope();

    void clearEnvelope();

    bool envelopeReady() const { return _envelope.size() == 8; }

    /// A point of the patch for (u, v) in the unit square.
    QPointF envelopePoint(double u, double v) const;

    /// The patch side segment from anchor \a index to the next, at \a t.
    QPointF envelopeSegment(int index, double t) const;

    /// What is under a point: an anchor (handle -1) or one of its handles
    /// (0 in, 1 out). Returns false for neither.
    bool envelopeHit(const QPointF& documentPos, double tolerance, int& anchor, int& handle) const;

    /// Rewrites the shapes from their session originals onto the patch.
    void applyEnvelope(PhoenixView& view);

    /// Records the envelope drag as one undo step per shape, keeping the
    /// session going.
    void commitEnvelope(PhoenixView& view);

    void drawEnvelope(QPainter& painter, double scale) const;

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

    /// Whether everything selected is a shape, which Distort needs.
    bool onlyShapesSelected() const;

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

    /// The box's own frame: a transform taking page vectors to coordinates
    /// along the box's sides. Scaling and skewing happen in this frame so a
    /// rotated box behaves like an upright one.
    static QTransform boxFrame(const QPolygonF& box);

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

    /// The change the drag makes. With \a snapper, a scale handle is snapped
    /// by solving its scale so the handle itself lands on what is in reach.
    QTransform adjustForDrag(const QPointF& documentPos, Qt::KeyboardModifiers modifiers,
        const fla::Snapper* snapper = nullptr) const;

    /// Nudges the cursor position onto the grid or a neighboring edge before
    /// the gesture is computed from it. A scale handle is left alone: it is
    /// snapped by handleSnapper's solve instead.
    QPointF snapDragPoint(PhoenixView& view, const QPointF& documentPos) const;

    /// The view's snapper, ready for a scale handle drag, or null when this is
    /// not one or nothing would snap.
    const fla::Snapper* handleSnapper(PhoenixView& view) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    /// The four corners of the transform box, in document space.
    QPolygonF _box;
    bool _boxValid = false;

    /// The transformation point, in document space: what rotation turns
    /// about and alt-scaling scales about. It comes from a single selected
    /// element's own transformation point, otherwise the box center, and it
    /// rides along with every gesture.
    QPointF _pivot;
    QPointF _pivotAtDragStart;

    /// Reads the pivot for the selection.
    void placePivot(PhoenixView& view);

    /// Writes a moved pivot back to a single selected element, in its own
    /// space, so the file keeps it.
    void storePivot(PhoenixView& view);

    /// The pivot drag snapped to the box's center or a handle within
    /// tolerance, else to the grid and neighbors.
    QPointF snapPivot(PhoenixView& view, const QPointF& documentPos) const;

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
        /// As shown, where a motion tween may have moved it.
        fla::Transform startTransform;
        /// As stored.
        fla::Transform storedStart;
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

    std::vector<EnvelopeAnchor> _envelope;
    QPolygonF _envelopeBase;
    std::vector<DistortTarget> _envelopeOriginals;
    /// What a drag of the envelope started from: the mesh, and each shape
    /// as it was, for the undo step.
    std::vector<EnvelopeAnchor> _envelopeAtDragStart;
    std::vector<DistortTarget> _envelopeDragBefore;
    int _envelopeAnchor = -1;
    int _envelopeHandle = -1;
};
