#pragma once

#include <QCursor>

/// The stage cursors, drawn rather than loaded.
///
/// Animate tells the user what a click will do through the cursor: the arrow
/// grows a small box over something it can move, a corner or an arc over an
/// edge it can bend; the pen carries an x before a path starts, an o when a
/// click would close it, a + or - over a segment or an anchor. These are small
/// enough to draw, and drawing them keeps them in step with the tools.
class Cursors
{
public:
    enum class Kind
    {
        /// The plain selection arrow.
        Arrow,
        /// Arrow with a small box: a click moves what is under it.
        ArrowMove,
        /// Arrow with a right angle: dragging bends a corner.
        ArrowCorner,
        /// Arrow with an arc: dragging bends a curve.
        ArrowCurve,
        /// Arrow with a leaning box: ctrl-dragging a corner distorts.
        ArrowDistort,
        /// Pen with an x: the next click starts a path.
        PenStart,
        /// Pen with nothing: the next click continues the path.
        Pen,
        /// Pen with an o: the next click closes the path.
        PenClose,
        /// Pen with a +: the next click adds an anchor to a segment.
        PenAdd,
        /// Pen with a -: the next click removes an anchor.
        PenRemove,
        /// Pen with a ^: the next click converts an anchor.
        PenConvert,
        /// Magnifier with a +.
        ZoomIn,
        /// Magnifier with a -.
        ZoomOut,
        /// The paint bucket, pouring from its hotspot.
        Bucket,
        /// The ink bottle, pouring from its hotspot.
        InkBottle,
        /// The eyedropper, with its tip at the hotspot.
        Eyedropper,
        /// A double-headed arrow through the hotspot: the free transform
        /// tool's scale cursor, turned to point along the handle's axis.
        Scale,
        /// Two half arrows pulling opposite ways: skew, turned along the edge.
        Skew,
        /// An arrow curling round the hotspot: rotate.
        Rotate,
        /// A tap with a drop at the hotspot: the eraser's faucet.
        Faucet
    };

    /// The cursor for \a kind, built once and shared.
    static QCursor cursor(Kind kind);

    /// The cursor for \a kind turned by \a eighthTurns steps of 45 degrees
    /// about its hotspot, for the transform cursors that follow the box.
    static QCursor cursor(Kind kind, int eighthTurns);
};
