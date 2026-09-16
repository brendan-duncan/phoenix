#include "test_util.h"

#include "../src/geom/intersect.h"

#include <cmath>
#include <vector>

using fla::Curve;
using fla::CurveIntersection;
using fla::Point;

namespace {

bool nearly(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) <= tolerance;
}

bool atPosition(const CurveIntersection& hit, double x, double y, double tolerance = 1.0e-4)
{
    return nearly(hit.position.x, x, tolerance) && nearly(hit.position.y, y, tolerance);
}

/// A curve arching from (0,0) up over (100,0), peaking near y = 75.
Curve arch()
{
    return Curve::cubic(Point(0.0, 0.0), Point(0.0, 100.0),
        Point(100.0, 100.0), Point(100.0, 0.0));
}

} // namespace

TEST(curve_line_evaluates_along_the_chord)
{
    const Curve line = Curve::line(Point(0.0, 0.0), Point(10.0, 20.0));

    CHECK(line.isLine());
    CHECK(nearly(line.pointAt(0.5).x, 5.0));
    CHECK(nearly(line.pointAt(0.5).y, 10.0));
    // The inner controls sit on the chord, so a line is flat by construction.
    CHECK(nearly(line.flatness(), 0.0));
}

TEST(curve_quadratic_is_raised_to_the_same_cubic)
{
    const Curve quad = Curve::quadratic(Point(0.0, 0.0), Point(50.0, 100.0), Point(100.0, 0.0));

    // The quadratic's midpoint is halfway between the chord and the control.
    CHECK(nearly(quad.pointAt(0.5).x, 50.0));
    CHECK(nearly(quad.pointAt(0.5).y, 50.0));
}

TEST(curve_split_halves_describe_the_original)
{
    const Curve curve = arch();

    Curve before;
    Curve after;
    curve.split(0.5, before, after);

    CHECK(nearly(before.start().x, curve.start().x));
    CHECK(nearly(after.end().x, curve.end().x));
    // The halves meet where the curve was cut.
    CHECK(nearly(before.end().x, curve.pointAt(0.5).x));
    CHECK(nearly(before.end().y, curve.pointAt(0.5).y));

    // Each half redraws its share of the original.
    for (int i = 0; i <= 10; ++i)
    {
        const double t = i / 10.0;
        CHECK(nearly(before.pointAt(t).x, curve.pointAt(t * 0.5).x, 1.0e-9));
        CHECK(nearly(after.pointAt(t).y, curve.pointAt(0.5 + t * 0.5).y, 1.0e-9));
    }
}

TEST(curve_subcurve_matches_the_stretch_it_came_from)
{
    const Curve curve = arch();
    const Curve middle = curve.subcurve(0.25, 0.75);

    for (int i = 0; i <= 10; ++i)
    {
        const double t = i / 10.0;
        const Point expected = curve.pointAt(0.25 + t * 0.5);
        CHECK(nearly(middle.pointAt(t).x, expected.x, 1.0e-9));
        CHECK(nearly(middle.pointAt(t).y, expected.y, 1.0e-9));
    }
}

TEST(lines_that_cross_report_one_hit)
{
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 10.0));
    const Curve b = Curve::line(Point(0.0, 10.0), Point(10.0, 0.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(a, b);

    CHECK(hits.size() == 1);
    if (hits.empty())
        return;
    CHECK(atPosition(hits[0], 5.0, 5.0));
    CHECK(nearly(hits[0].t1, 0.5));
    CHECK(nearly(hits[0].t2, 0.5));
}

TEST(lines_that_miss_report_nothing)
{
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 0.0));
    const Curve b = Curve::line(Point(0.0, 5.0), Point(10.0, 5.0));

    CHECK(fla::intersectCurves(a, b).empty());
}

TEST(lines_crossing_beyond_their_ends_report_nothing)
{
    // They would meet at (20,0) if they ran on, but neither does.
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 0.0));
    const Curve b = Curve::line(Point(20.0, -10.0), Point(20.0, 10.0));

    CHECK(fla::intersectCurves(a, b).empty());
}

TEST(a_t_junction_is_found_at_the_endpoint)
{
    // One line ends on the other, which is exactly what the planar map has to
    // split at.
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 0.0));
    const Curve b = Curve::line(Point(5.0, 0.0), Point(5.0, 10.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(a, b);

    CHECK(hits.size() == 1);
    if (hits.empty())
        return;
    CHECK(atPosition(hits[0], 5.0, 0.0));
    CHECK(nearly(hits[0].t2, 0.0));
}

TEST(lines_meeting_at_a_shared_corner_are_found)
{
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 0.0));
    const Curve b = Curve::line(Point(10.0, 0.0), Point(10.0, 10.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(a, b);

    CHECK(hits.size() == 1);
    if (hits.empty())
        return;
    CHECK(atPosition(hits[0], 10.0, 0.0));
}

TEST(parallel_lines_report_nothing)
{
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 0.0));
    const Curve b = Curve::line(Point(0.0, 1.0), Point(10.0, 1.0));

    CHECK(fla::intersectCurves(a, b).empty());
}

TEST(collinear_overlapping_lines_report_no_crossing)
{
    // Running along together is an overlap, not a crossing. Reporting a point
    // here would put a meaningless vertex into the map.
    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0, 0.0));
    const Curve b = Curve::line(Point(5.0, 0.0), Point(15.0, 0.0));

    CHECK(fla::intersectCurves(a, b).empty());
}

TEST(a_line_through_a_curve_is_found_twice)
{
    const Curve curve = arch();
    // A horizontal line at y = 40 cuts the arch on the way up and on the way
    // down.
    const Curve line = Curve::line(Point(-50.0, 40.0), Point(150.0, 40.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(curve, line);

    CHECK(hits.size() == 2);
    if (hits.size() != 2)
        return;

    // Reported in order along the first curve.
    CHECK(hits[0].t1 < hits[1].t1);
    CHECK(nearly(hits[0].position.y, 40.0, 0.05));
    CHECK(nearly(hits[1].position.y, 40.0, 0.05));
    CHECK(hits[0].position.x < hits[1].position.x);
}

TEST(a_line_clear_of_a_curve_reports_nothing)
{
    const Curve curve = arch();
    const Curve line = Curve::line(Point(-50.0, 200.0), Point(150.0, 200.0));

    CHECK(fla::intersectCurves(curve, line).empty());
}

TEST(a_line_under_a_curve_reports_nothing)
{
    const Curve curve = arch();
    const Curve line = Curve::line(Point(-50.0, -20.0), Point(150.0, -20.0));

    CHECK(fla::intersectCurves(curve, line).empty());
}

TEST(two_curves_that_cross_are_found)
{
    const Curve up = arch();
    // The same arch mirrored downward, offset so the two cross twice.
    const Curve down = Curve::cubic(Point(0.0, 60.0), Point(0.0, -40.0),
        Point(100.0, -40.0), Point(100.0, 60.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(up, down);

    CHECK(hits.size() == 2);
    if (hits.size() != 2)
        return;

    // Each reported point has to be on both curves, which is the property that
    // actually matters.
    for (const CurveIntersection& hit : hits)
    {
        const Point onFirst = up.pointAt(hit.t1);
        const Point onSecond = down.pointAt(hit.t2);
        CHECK(nearly(onFirst.x, hit.position.x, 0.05));
        CHECK(nearly(onFirst.y, hit.position.y, 0.05));
        CHECK(nearly(onSecond.x, hit.position.x, 0.05));
        CHECK(nearly(onSecond.y, hit.position.y, 0.05));
    }
}

TEST(curves_whose_boxes_overlap_but_do_not_meet_report_nothing)
{
    // One arch nested under another. The inner curve's control box sits wholly
    // inside the outer one, so a box test alone would claim a hit; the curves
    // never actually touch.
    const Curve outer = Curve::cubic(Point(0.0, 0.0), Point(0.0, 100.0),
        Point(100.0, 100.0), Point(100.0, 0.0));
    const Curve inner = Curve::cubic(Point(20.0, 0.0), Point(20.0, 60.0),
        Point(80.0, 60.0), Point(80.0, 0.0));

    // Confirm the premise: the boxes really do overlap.
    double ol, ot, orr, ob;
    double il, it, ir, ib;
    outer.controlBounds(ol, ot, orr, ob);
    inner.controlBounds(il, it, ir, ib);
    CHECK(il >= ol && ir <= orr && it >= ot && ib <= ob);

    CHECK(fla::intersectCurves(outer, inner).empty());
}

TEST(a_curve_crossing_a_line_many_times_reports_each)
{
    // An S laid across a horizontal line crosses it three times.
    const Curve wave = Curve::cubic(Point(0.0, -50.0), Point(40.0, 150.0),
        Point(60.0, -150.0), Point(100.0, 50.0));
    const Curve line = Curve::line(Point(-10.0, 0.0), Point(110.0, 0.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(wave, line);

    CHECK(hits.size() == 3);
    for (const CurveIntersection& hit : hits)
        CHECK(nearly(hit.position.y, 0.0, 0.05));
}

TEST(intersections_come_back_in_order)
{
    const Curve wave = Curve::cubic(Point(0.0, -50.0), Point(40.0, 150.0),
        Point(60.0, -150.0), Point(100.0, 50.0));
    const Curve line = Curve::line(Point(-10.0, 0.0), Point(110.0, 0.0));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(wave, line);

    for (size_t i = 1; i < hits.size(); ++i)
        CHECK(hits[i - 1].t1 <= hits[i].t1);
}

TEST(a_looping_curve_finds_its_own_crossing)
{
    // A cubic whose controls cross over, so the curve makes a loop.
    const Curve loop = Curve::cubic(Point(0.0, 0.0), Point(120.0, 80.0),
        Point(-20.0, 80.0), Point(100.0, 0.0));

    const std::vector<CurveIntersection> hits = fla::selfIntersections(loop);

    CHECK(hits.size() == 1);
    if (hits.empty())
        return;

    // Both parameters describe the same place on the curve.
    const Point first = loop.pointAt(hits[0].t1);
    const Point second = loop.pointAt(hits[0].t2);
    CHECK(nearly(first.x, second.x, 0.1));
    CHECK(nearly(first.y, second.y, 0.1));
    CHECK(hits[0].t1 < hits[0].t2);
}

TEST(a_simple_curve_has_no_self_crossing)
{
    CHECK(fla::selfIntersections(arch()).empty());
    CHECK(fla::selfIntersections(Curve::line(Point(0.0, 0.0), Point(10.0, 10.0))).empty());
}

TEST(a_crossing_at_twip_scale_is_still_found)
{
    // Flash geometry is quantised to a twentieth of a pixel, so the map has to
    // resolve crossings at that scale rather than losing them to tolerance.
    const double twip = 1.0 / 20.0;

    const Curve a = Curve::line(Point(0.0, 0.0), Point(10.0 * twip, 0.0));
    const Curve b = Curve::line(Point(5.0 * twip, -twip), Point(5.0 * twip, twip));

    const std::vector<CurveIntersection> hits = fla::intersectCurves(a, b, twip / 8.0);

    CHECK(hits.size() == 1);
    if (hits.empty())
        return;
    CHECK(nearly(hits[0].position.x, 5.0 * twip, 1.0e-9));
}
