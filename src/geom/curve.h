#pragma once

#include "../data/point.h"

namespace fla {

/// One piece of path geometry: a straight line or a cubic bezier.
///
/// The planar map works in these rather than in PathSegment, because it has to
/// split pieces at arbitrary parameters and ask questions about them in
/// isolation, neither of which a drawing command supports.
///
/// Quadratics are raised to cubics on the way in, so there is one curved case
/// rather than two.
///
/// Free of Qt.
class Curve
{
public:
    Curve() = default;

    static Curve line(const Point& from, const Point& to);

    static Curve cubic(const Point& from, const Point& control1,
        const Point& control2, const Point& to);

    /// Raises a quadratic to the cubic that draws the same curve.
    static Curve quadratic(const Point& from, const Point& control, const Point& to);

    bool isLine() const { return _isLine; }

    const Point& start() const { return _points[0]; }

    const Point& end() const { return _points[3]; }

    /// The control points. For a line, the inner two sit on the chord, so the
    /// cubic formulae work unchanged.
    const Point& controlPoint(int index) const { return _points[index]; }

    Point pointAt(double t) const;

    /// The direction the curve is heading at \a t, not normalised. Zero where
    /// the curve has a cusp or a degenerate control polygon.
    Point tangentAt(double t) const;

    /// Bounds of the control polygon. Always contains the curve, and is cheap,
    /// which is what a subdivision search needs.
    void controlBounds(double& left, double& top, double& right, double& bottom) const;

    /// The piece between two parameters.
    Curve subcurve(double from, double to) const;

    void split(double t, Curve& before, Curve& after) const;

    /// How far the control points stray from the chord. Zero for a straight
    /// piece, which is the test for "close enough to treat as a segment".
    double flatness() const;

    /// Length of the chord from start to end.
    double chordLength() const;

private:
    /// Control points, always four. A line repeats its endpoints inward.
    Point _points[4];

    bool _isLine = true;
};

} // namespace fla
