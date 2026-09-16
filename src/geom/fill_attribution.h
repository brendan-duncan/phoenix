#pragma once

#include "planar_map.h"

#include <vector>

namespace fla {

/// A closed outline and what it paints inside itself.
///
/// This is one drawn shape before merging: the outline it was built from, and
/// the fill it carries. A shape with no fill still takes part, because its
/// outline still cuts whatever it is drawn across.
struct FillRegion
{
    /// The curves bounding the region, in order around it.
    std::vector<Curve> outline;

    /// What to paint inside, or -1 for an outline that paints nothing.
    int fillStyle = -1;
};

/// Works out what fill each face of \a map ends up with, and records on every
/// half-edge the fill lying on its left.
///
/// Regions are applied in order, and a later one paints over an earlier one.
/// That is the whole of Animate's merge behaviour: drawing a shape across
/// another does not stack them, it replaces what was underneath inside the new
/// outline and leaves the rest.
///
/// An outline carrying no fill still cuts: it splits the faces beneath it
/// without repainting them, which is how a line drawn across a fill divides it
/// into two pieces that can then be moved apart.
///
/// The map must already have been built, and the same outlines must have been
/// added to it, or the faces will not line up with the regions.
void attributeFills(PlanarMap& map, const std::vector<FillRegion>& regions);

/// Whether a point falls inside a closed outline, by the even-odd rule.
bool outlineContains(const std::vector<Curve>& outline, const Point& point);

} // namespace fla
