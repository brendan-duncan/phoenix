#pragma once

#include "curve.h"
#include "planar_map.h"

#include <vector>

namespace fla {

class Shape;

/// One curve taken from a shape's edges, with the styles the file recorded
/// alongside it.
///
/// The format stores a fill for each side of an edge and a stroke for the edge
/// itself. Which side is which depends on the direction the edge was written in,
/// so the curve and its styles have to travel together.
struct ShapeCurve
{
    Curve curve;

    /// Index of the Edge this came from.
    int edgeIndex = -1;

    /// The two fills the file records, one per side, and the stroke.
    int fillStyle0 = -1;
    int fillStyle1 = -1;
    int strokeStyle = -1;
};

/// Flattens a shape's edges into curves, keeping each one's styles.
///
/// Drawing commands become curves: a line or a cubic between two points, with
/// quadratics raised to cubics. A close command returns to where its subpath
/// began, so a closed outline comes back closed.
///
/// Free of Qt.
std::vector<ShapeCurve> shapeCurves(const Shape& shape);

/// Reads the fills the file already recorded into the arrangement's faces.
///
/// Each half-edge's source says what lies on each of its sides, and the
/// convention is that `fillStyle1` is the fill on the left of the direction the
/// edge was written in. That is enough to say what every face is painted with,
/// without re-deciding it from paint order.
///
/// Useful for taking an existing shape apart and putting it back together, and
/// for checking that an arrangement agrees with the file it came from.
void attributeFillsFromSource(PlanarMap& map, const std::vector<ShapeCurve>& sources);

/// Replaces a shape's edges with the arrangement's, keeping its fill and stroke
/// styles.
///
/// Each pair of opposite half-edges becomes one edge, carrying the fill from
/// each of its sides. A piece with no fill on either side and no stroke draws
/// nothing and is left out, which is how the seams inside a merged shape
/// disappear.
void rebuildShapeEdges(Shape& shape, const PlanarMap& map,
    const std::vector<ShapeCurve>& sources);

} // namespace fla
