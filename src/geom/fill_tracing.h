#pragma once

#include <map>
#include <vector>

namespace fla {

class Shape;
struct Path;

/// One leg of a traced outline: a path of the shape, walked forward or
/// backward.
struct TracedLeg
{
    const Path* path = nullptr;
    bool reversed = false;
};

/// One outline of a fill, legs end to end, with the fill on its right (on
/// screen, y down). An outline that could not be closed is still given,
/// open, as the renderer has always drawn such leftovers.
struct TracedLoop
{
    std::vector<TracedLeg> legs;
    bool closed = false;
};

/// Traces the outlines of every fill of a shape from its edges, which is how
/// a shape is drawn: a path walked forward bounds its fillStyle1 and walked
/// backward its fillStyle0, and each fill's outlines are found by joining
/// those walks end to start.
///
/// At a junction, paths starting exactly where the outline has got to (to a
/// twip) are preferred; only when there are none does a start within half a
/// pixel do. Of those, the one turning furthest right is taken, which keeps
/// to one region's boundary. Paths already closed on themselves are outlines
/// of their own. Free of Qt.
std::map<int, std::vector<TracedLoop>> traceFills(const Shape& shape);

} // namespace fla
