#pragma once

#include "curve.h"

#include <vector>

namespace fla {

/// Where two curves meet.
struct CurveIntersection
{
    /// Parameter on the first curve, from 0 at its start to 1 at its end.
    double t1 = 0.0;

    /// Parameter on the second curve.
    double t2 = 0.0;

    Point position;
};

/// Finds where two curves cross.
///
/// Straight pairs are solved directly. Anything curved is found by subdivision:
/// if the control-polygon boxes miss each other there is nothing to find, and
/// otherwise the pieces are halved until they are flat enough to treat as
/// segments. That is slower than Bezier clipping but far easier to get right,
/// and it degrades into the exact line solver rather than into guesswork.
///
/// \a tolerance is in document units and decides both when a piece counts as
/// straight and how close two hits have to be to count as the same one.
///
/// Results come back in order along the first curve. Curves that overlap along a
/// stretch rather than crossing at a point report the ends of that stretch, not
/// every point on it.
std::vector<CurveIntersection> intersectCurves(const Curve& a, const Curve& b,
    double tolerance = 1.0 / 40.0);

/// Where a curve crosses itself. A loop in a freehand stroke is the usual
/// source, and the planar map has to split there just as it would for two
/// separate curves.
std::vector<CurveIntersection> selfIntersections(const Curve& curve,
    double tolerance = 1.0 / 40.0);

} // namespace fla
