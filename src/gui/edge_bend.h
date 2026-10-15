#pragma once

#include "tool.h"

#include "../data/point.h"
#include "../edit/editable_path.h"

#include <QPointF>
#include <QTransform>

#include <cstddef>
#include <vector>

namespace fla {
class CommandStack;
class Edge;
class Shape;
}

/// Reshaping artwork by dragging its outline, the way the Selection tool
/// does on merge-drawn shapes.
///
/// Dragging a segment bends it: the curve is moved so that the point grabbed
/// stays exactly under the cursor, which for a cubic means moving both inner
/// control points by the drag divided by 3t(1-t). A straight segment first
/// becomes a curve with its controls on the chord. Dragging an end point moves
/// the corner, taking every piece of outline that meets there, and ctrl-drag
/// on a segment inserts a new corner there and drags it.
///
/// The fills follow on their own: they are rebuilt from the same edges.
class EdgeBender
{
public:
    enum class Grab
    {
        None,
        /// A segment, which a drag bends.
        Curve,
        /// An end point, which a drag moves.
        Corner
    };

    /// What a press or a hover found under the cursor.
    struct Probe
    {
        Grab grab = Grab::None;
        fla::Shape* shape = nullptr;
        QTransform shapeToDocument;
        fla::Edge* edge = nullptr;
        size_t pathIndex = 0;
        size_t segment = 0;
        double t = 0.0;
        /// The anchor, for a corner, in the shape's own space.
        fla::Point position;
    };

    /// Looks for a corner or a segment of the hit shape within the pick
    /// tolerance of \a documentPos. Corners win over segments, since a segment
    /// always passes through its ends.
    static Probe probe(PhoenixView& view, const HitResult& hit, const QPointF& documentPos);

    bool active() const { return _grab != Grab::None; }

    /// Whether the gesture has traveled past the click threshold.
    bool moved() const { return _moved; }

    /// Starts the gesture. With \a insertCorner the segment is split at the
    /// grabbed point and the new corner is what gets dragged.
    void begin(PhoenixView& view, const Probe& probe, const QPointF& documentPos, bool insertCorner);

    void update(PhoenixView& view, const QPointF& documentPos);

    /// Writes the gesture to the history as one undo step. Does nothing when
    /// the cursor never moved.
    void commit(PhoenixView& view, fla::CommandStack& commandStack);

    /// Puts the geometry back and forgets the gesture.
    void cancel(PhoenixView& view);

private:
    struct Edited
    {
        fla::Edge* edge = nullptr;
        size_t pathIndex = 0;
        fla::EditablePath before;
        fla::EditablePath current;
        /// For a corner drag: which anchor of this path moves.
        size_t anchorIndex = 0;
        /// What the drag works from, when not before: the path with the
        /// corner a Ctrl-drag put in.
        fla::EditablePath start;
        bool started = false;
    };

    /// Gathers every anchor of \a shape sitting at \a position.
    void grabCorner(fla::Shape& shape, const fla::Point& position);

    void applyCurrent(PhoenixView& view);

    Grab _grab = Grab::None;
    fla::Shape* _shape = nullptr;
    QTransform _shapeToDocument;
    size_t _segment = 0;
    double _t = 0.0;
    QPointF _startLocal;
    bool _moved = false;
    std::vector<Edited> _edited;
};
