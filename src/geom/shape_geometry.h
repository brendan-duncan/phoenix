#pragma once

#include "curve.h"

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

} // namespace fla
