#pragma once

#include "../data/point.h"

#include <vector>

namespace fla {

class Edge;
class Path;

/// One point on a path being edited, with its bezier handles.
///
/// Handles are absolute positions rather than offsets, because that is what both
/// the pen and the subselection tool drag directly.
struct Anchor
{
    Point position;

    /// Governs the curve arriving at this anchor. Sitting on the position means
    /// that side is straight.
    Point inHandle;

    /// Governs the curve leaving this anchor.
    Point outHandle;

    /// Whether the two handles are kept opposite each other, so the curve runs
    /// smoothly through. A corner point breaks them apart.
    bool smooth = false;

    Anchor() = default;

    explicit Anchor(const Point& p)
        : position(p)
        , inHandle(p)
        , outHandle(p)
    {}

    bool hasInCurve() const;

    bool hasOutCurve() const;

    /// Moves the whole anchor, handles included.
    void translate(double dx, double dy);

    /// Moves the out handle, dragging the in handle to stay opposite when this
    /// is a smooth point.
    void setOutHandle(const Point& handle);

    void setInHandle(const Point& handle);
};

/// A path expressed as anchors and handles rather than as drawing commands.
///
/// `PathSegment` records only the points a renderer needs, which is enough to
/// draw but not to edit: it cannot say whether an anchor's handles are linked,
/// and it splits one conceptual anchor across two segments. Tools work on this
/// and compile down to segments when the edit is done.
///
/// Free of Qt.
class EditablePath
{
public:
    std::vector<Anchor> anchors;

    /// Whether the last anchor joins back to the first.
    bool closed = false;

    bool isEmpty() const { return anchors.empty(); }

    /// Reads an existing path back into anchors. Quadratic segments are raised
    /// to cubics, which describe the same curve.
    static EditablePath fromPath(const Path& path);

    /// Replaces everything on \a edge with a single path built from these
    /// anchors.
    void applyTo(Edge& edge) const;

    /// Number of segments. A closed path has one more than an open one, since
    /// the last anchor joins back to the first.
    size_t segmentCount() const;

    /// The anchors a segment runs between. Returns false for an out-of-range
    /// index.
    bool segmentAnchors(size_t segment, size_t& from, size_t& to) const;

    /// A point found on the path, used for inserting an anchor where the user
    /// clicked.
    struct PathPoint
    {
        bool valid = false;
        size_t segment = 0;
        /// Where along the segment, from 0 at its start to 1 at its end.
        double t = 0.0;
        Point position;
        double distance = 0.0;
    };

    /// The closest point on the path to a given point.
    PathPoint closestPoint(const Point& point) const;

    /// Inserts an anchor partway along a segment, leaving the curve exactly as
    /// it was. Returns the new anchor's index, or -1 if the segment is not
    /// there.
    int splitSegment(size_t segment, double t);

    /// Removes an anchor, joining its neighbours directly. Refuses to leave
    /// fewer than two anchors, since that is no longer a path.
    bool removeAnchor(size_t index);

    /// Flips an anchor between smooth and corner: a smooth point loses its
    /// handles, a corner grows them along the line through its neighbours.
    bool toggleAnchorSmooth(size_t index);
};

} // namespace fla
