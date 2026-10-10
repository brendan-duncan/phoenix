#pragma once

#include "editable_path.h"

#include "../data/point.h"

#include <vector>

namespace fla {

/// Fits a smooth bezier path through the points a freehand drag left behind.
///
/// This is Schneider's algorithm (Graphics Gems, 1990): fit one cubic to the
/// whole run by least squares, improve the parameterization with a couple of
/// Newton-Raphson passes, and if the worst point is still too far off, split
/// there and fit each half. The result is far fewer anchors than raw input
/// points, placed where the stroke actually changes direction.
///
/// Free of Qt, so the fitting can be tested on its own.
class CurveFit
{
public:
    /// How closely the fit has to follow the input, and how much of the input's
    /// wobble to keep.
    struct Options
    {
        /// Largest distance, in document units, the curve may stray from a
        /// point before that run is split.
        double tolerance = 2.0;

        /// Input points closer together than this are dropped. Freehand input
        /// arrives in clumps when the cursor pauses, and the clumps otherwise
        /// drag the fit around.
        double minimumSpacing = 0.5;

        /// Straighten mode: fit straight segments rather than curves, the way
        /// Animate's pencil does.
        bool straighten = false;

        /// Whether a sharp turn in the input becomes a corner anchor, with the
        /// runs either side fitted on their own. Off, every anchor is smooth
        /// and a corner comes out rounded.
        bool detectCorners = true;

        /// How far the stroke has to turn, in degrees, over a short stretch
        /// either side of a point for that point to be a corner.
        double cornerAngle = 60.0;
    };

    /// Fits \a points, which are expected in the order they were drawn.
    ///
    /// Returns an open path. Fewer than two usable points gives an empty path.
    static EditablePath fit(const std::vector<Point>& points, const Options& options);

    /// Drops points that sit on top of each other, which is what freehand input
    /// produces whenever the cursor stops moving.
    static std::vector<Point> removeDuplicates(const std::vector<Point>& points,
        double minimumSpacing);

    /// Fits one cubic through \a points with its ends pinned to the first and
    /// last point and its end tangents fixed to the unit directions given
    /// (each pointing into the curve from its end). Only the handle lengths
    /// are fitted. Returns false with fewer than two points.
    static bool fitSingleCubic(const std::vector<Point>& points,
        const Point& startTangent, const Point& endTangent, Point bezier[4]);
};

} // namespace fla
