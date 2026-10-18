#include "test_util.h"

#include "../src/edit/curve_fit.h"
#include "../src/edit/path_bend.h"

#include <cmath>
#include <vector>

using fla::Anchor;
using fla::EditablePath;
using fla::PathBend;
using fla::Point;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// How far along a quarter circle's tangent its cubic handles sit.
constexpr double kArc = 0.5522847498;

Anchor anchor(double x, double y, double inX, double inY, double outX, double outY)
{
    Anchor result(Point(x, y));
    result.inHandle = Point(inX, inY);
    result.outHandle = Point(outX, outY);
    result.smooth = true;
    return result;
}

/// The outline of a round-capped brush stroke along y = 0 to 40: the long
/// sides cut into short smooth segments, as a fitted outline is, and the
/// caps half circles, smooth all the way round.
EditablePath strokeOutline()
{
    EditablePath path;
    path.closed = true;
    const double step = 40.0;
    const double handle = step / 3.0;
    const double arc = 20.0 * kArc;

    // Along the top, left to right.
    path.anchors.push_back(anchor(0, 0, -arc, 0, handle, 0));
    for (double x = step; x < 200.0; x += step)
        path.anchors.push_back(anchor(x, 0, x - handle, 0, x + handle, 0));
    path.anchors.push_back(anchor(200, 0, 200 - handle, 0, 200 + arc, 0));
    // Round the right cap.
    path.anchors.push_back(anchor(220, 20, 220, 20 - arc, 220, 20 + arc));
    // Along the bottom, right to left.
    path.anchors.push_back(anchor(200, 40, 200 + arc, 40, 200 - handle, 40));
    for (double x = 200.0 - step; x > 0.0; x -= step)
        path.anchors.push_back(anchor(x, 40, x + handle, 40, x - handle, 40));
    path.anchors.push_back(anchor(0, 40, handle, 40, -arc, 40));
    // Round the left cap.
    path.anchors.push_back(anchor(-20, 20, -20, 20 + arc, -20, 20 - arc));
    return path;
}

/// The angle between the directions into and out of an anchor.
double kink(const Anchor& a)
{
    const double ix = a.position.x - a.inHandle.x;
    const double iy = a.position.y - a.inHandle.y;
    const double ox = a.outHandle.x - a.position.x;
    const double oy = a.outHandle.y - a.position.y;
    return std::fabs(std::atan2(ix * oy - iy * ox, ix * ox + iy * oy));
}

bool samePoint(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y) < 1.0e-9;
}

} // namespace

TEST(path_bend_keeps_the_grabbed_point_under_the_cursor_on_a_line)
{
    EditablePath line;
    line.anchors.push_back(Anchor(Point(0, 0)));
    line.anchors.push_back(Anchor(Point(100, 0)));

    const EditablePath bent = PathBend(line, 0, 0.5).apply(0, 30);

    CHECK(samePoint(bent.anchors[0].position, Point(0, 0)));
    CHECK(samePoint(bent.anchors[1].position, Point(100, 0)));
    const EditablePath::PathPoint found = bent.closestPoint(Point(50, 30));
    CHECK(found.valid);
    CHECK(found.distance < 1.0e-6);
}

TEST(path_bend_spreads_along_a_smooth_outline)
{
    const EditablePath outline = strokeOutline();

    // The middle of the top side, segment 2 running from x = 80 to 120.
    const EditablePath bent = PathBend(outline, 2, 0.5).apply(0, -30);

    const EditablePath::PathPoint found = bent.closestPoint(Point(100, -30));
    CHECK(found.valid);
    CHECK(found.distance < 1.0e-6);

    // The anchors either side of the grabbed segment move too.
    CHECK(bent.anchors[1].position.y < -1.0);
    CHECK(bent.anchors[4].position.y < -1.0);

    // Nothing along the bottom moves: the bend stops partway round each cap.
    for (size_t i = 0; i < outline.anchors.size(); ++i)
    {
        if (outline.anchors[i].position.y < 39.0)
            continue;
        CHECK(samePoint(bent.anchors[i].position, outline.anchors[i].position));
        CHECK(samePoint(bent.anchors[i].inHandle, outline.anchors[i].inHandle));
        CHECK(samePoint(bent.anchors[i].outHandle, outline.anchors[i].outHandle));
    }

    // And the outline stays smooth: no anchor folds into a crease.
    for (const Anchor& a : bent.anchors)
        CHECK(kink(a) < 3.0 * kPi / 180.0);
}

TEST(path_bend_stops_at_a_corner)
{
    // An open zigzag: the corner at (100, 0) bounds a bend of the first leg.
    EditablePath path;
    path.anchors.push_back(Anchor(Point(0, 0)));
    path.anchors.push_back(Anchor(Point(100, 0)));
    path.anchors.push_back(Anchor(Point(100, 100)));

    const EditablePath bent = PathBend(path, 0, 0.5).apply(0, -20);

    CHECK(samePoint(bent.anchors[1].position, Point(100, 0)));
    CHECK(samePoint(bent.anchors[2].position, Point(100, 100)));
    CHECK(samePoint(bent.anchors[1].outHandle, Point(100, 0)));
    CHECK(samePoint(bent.anchors[2].inHandle, Point(100, 100)));
}

TEST(path_bend_keeps_a_fitted_brush_outline_smooth)
{
    // A wavy stroke's outline, fitted from dense samples the way the brush
    // fits its strokes: a top side with a gentle wave, a round cap, a
    // straight bottom side and another cap.
    std::vector<Point> ring;
    for (int i = 0; i <= 200; ++i)
        ring.push_back(Point(i * 1.5, 4.0 * std::sin(i * 0.05)));
    for (int i = 1; i < 30; ++i)
    {
        const double angle = -kPi / 2.0 + kPi * i / 30.0;
        ring.push_back(Point(300.0 + 20.0 * std::cos(angle), 20.0 + 20.0 * std::sin(angle)));
    }
    for (int i = 200; i >= 0; --i)
        ring.push_back(Point(i * 1.5, 40.0));
    for (int i = 1; i < 30; ++i)
    {
        const double angle = kPi / 2.0 + kPi * i / 30.0;
        ring.push_back(Point(20.0 * std::cos(angle), 20.0 + 20.0 * std::sin(angle)));
    }
    ring.push_back(ring.front());

    fla::CurveFit::Options options;
    options.tolerance = 0.5;
    options.minimumSpacing = 0.2;
    EditablePath outline = fla::CurveFit::fit(ring, options);
    outline.anchors.pop_back();
    outline.closed = true;
    CHECK(outline.anchors.size() > 6);

    const EditablePath::PathPoint grab = outline.closestPoint(Point(150, 0));
    CHECK(grab.valid);
    const EditablePath bent = PathBend(outline, grab.segment, grab.t).apply(0, -25);

    const EditablePath::PathPoint found =
        bent.closestPoint(Point(grab.position.x, grab.position.y - 25));
    CHECK(found.distance < 1.0e-6);

    for (size_t i = 0; i < outline.anchors.size(); ++i)
    {
        // What was smooth stays smooth.
        if (kink(outline.anchors[i]) < 0.5 * kPi / 180.0)
            CHECK(kink(bent.anchors[i]) < 3.0 * kPi / 180.0);
        // The bottom side does not move.
        if (outline.anchors[i].position.y > 39.0)
            CHECK(samePoint(bent.anchors[i].position, outline.anchors[i].position));
    }
}
