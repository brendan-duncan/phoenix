#pragma once

#include "editable_path.h"

#include <cstddef>
#include <vector>

namespace fla {

/// Bending a path by dragging a point on one of its segments.
///
/// The bend is not kept to the one segment grabbed: on a fitted outline, such
/// as a brush stroke's, segments are short and bending one alone folds the
/// outline into a crease. It spreads along the smooth run either side of the
/// grab, up to a corner, the end of the path, a quarter turn away or halfway
/// round a loop. Each control in the run takes a share of the drag that eases
/// off with distance along the outline from the grab, and the drag is scaled
/// so the point grabbed lands exactly where it is dragged. Straight segments
/// in the run first become curves with their controls on the chord.
///
/// Free of Qt, so it can be tested on its own.
class PathBend
{
public:
    /// Plans a bend of \a path grabbed at \a t along \a segment.
    PathBend(const EditablePath& path, size_t segment, double t);

    /// The path with the grabbed point moved by (\a dx, \a dy).
    EditablePath apply(double dx, double dy) const;

private:
    enum class Control
    {
        Position,
        In,
        Out
    };

    /// One control the bend moves, and its share of the drag.
    struct Influence
    {
        size_t anchor = 0;
        Control control = Control::Position;
        double weight = 0.0;
    };

    EditablePath _path;
    std::vector<Influence> _influence;
    std::vector<size_t> _lineSegments;
    /// The share of the drag the grabbed point itself takes.
    double _grabWeight = 1.0;
};

} // namespace fla
