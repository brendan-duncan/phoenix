#include "curve.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

Point lerp(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

/// Distance from \a point to the infinite line through \a a and \a b. Falls back
/// to the distance from \a a when the two are the same place.
double distanceToLine(const Point& point, const Point& a, const Point& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);

    if (length <= 1.0e-12)
        return std::hypot(point.x - a.x, point.y - a.y);

    return std::fabs(dx * (a.y - point.y) - (a.x - point.x) * dy) / length;
}

} // namespace

Curve Curve::line(const Point& from, const Point& to)
{
    Curve curve;
    curve._isLine = true;
    curve._points[0] = from;
    // Placing the inner controls a third of the way along means the cubic
    // formulae describe the straight line exactly, so nothing has to special
    // case a line.
    curve._points[1] = lerp(from, to, 1.0 / 3.0);
    curve._points[2] = lerp(from, to, 2.0 / 3.0);
    curve._points[3] = to;
    return curve;
}

Curve Curve::cubic(const Point& from, const Point& control1,
    const Point& control2, const Point& to)
{
    Curve curve;
    curve._isLine = false;
    curve._points[0] = from;
    curve._points[1] = control1;
    curve._points[2] = control2;
    curve._points[3] = to;
    return curve;
}

Curve Curve::quadratic(const Point& from, const Point& control, const Point& to)
{
    return cubic(from,
        Point(from.x + (2.0 / 3.0) * (control.x - from.x),
              from.y + (2.0 / 3.0) * (control.y - from.y)),
        Point(to.x + (2.0 / 3.0) * (control.x - to.x),
              to.y + (2.0 / 3.0) * (control.y - to.y)),
        to);
}

Point Curve::pointAt(double t) const
{
    const Point a = lerp(_points[0], _points[1], t);
    const Point b = lerp(_points[1], _points[2], t);
    const Point c = lerp(_points[2], _points[3], t);
    return lerp(lerp(a, b, t), lerp(b, c, t), t);
}

Point Curve::tangentAt(double t) const
{
    const double u = 1.0 - t;

    // Derivative of the cubic, which is three times the quadratic through the
    // differences of the control points.
    const double x = 3.0 * (u * u * (_points[1].x - _points[0].x) +
                            2.0 * u * t * (_points[2].x - _points[1].x) +
                            t * t * (_points[3].x - _points[2].x));
    const double y = 3.0 * (u * u * (_points[1].y - _points[0].y) +
                            2.0 * u * t * (_points[2].y - _points[1].y) +
                            t * t * (_points[3].y - _points[2].y));

    return Point(x, y);
}

void Curve::controlBounds(double& left, double& top, double& right, double& bottom) const
{
    left = _points[0].x;
    right = _points[0].x;
    top = _points[0].y;
    bottom = _points[0].y;

    for (int i = 1; i < 4; ++i)
    {
        left = std::min(left, _points[i].x);
        right = std::max(right, _points[i].x);
        top = std::min(top, _points[i].y);
        bottom = std::max(bottom, _points[i].y);
    }
}

void Curve::split(double t, Curve& before, Curve& after) const
{
    // De Casteljau: the intermediate points of the evaluation are exactly the
    // control points of the two halves.
    const Point a = lerp(_points[0], _points[1], t);
    const Point b = lerp(_points[1], _points[2], t);
    const Point c = lerp(_points[2], _points[3], t);
    const Point d = lerp(a, b, t);
    const Point e = lerp(b, c, t);
    const Point f = lerp(d, e, t);

    before = cubic(_points[0], a, d, f);
    after = cubic(f, e, c, _points[3]);

    // Splitting a line gives two lines.
    before._isLine = _isLine;
    after._isLine = _isLine;
}

Curve Curve::subcurve(double from, double to) const
{
    if (to < from)
        std::swap(from, to);

    Curve before;
    Curve rest;
    split(from, before, rest);

    if (to >= 1.0 - 1.0e-12)
        return rest;

    // Re-parameterise the cut into what is left after the first split.
    const double remaining = 1.0 - from;
    const double t = remaining <= 1.0e-12 ? 0.0 : (to - from) / remaining;

    Curve middle;
    Curve after;
    rest.split(t, middle, after);
    return middle;
}

double Curve::flatness() const
{
    if (_isLine)
        return 0.0;

    return std::max(distanceToLine(_points[1], _points[0], _points[3]),
                    distanceToLine(_points[2], _points[0], _points[3]));
}

double Curve::chordLength() const
{
    return std::hypot(_points[3].x - _points[0].x, _points[3].y - _points[0].y);
}

} // namespace fla
