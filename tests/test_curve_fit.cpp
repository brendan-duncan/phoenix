#include "test_util.h"

#include "../src/edit/curve_fit.h"

#include <cmath>
#include <vector>

using fla::CurveFit;
using fla::EditablePath;
using fla::Point;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Samples a straight line, the way a steady freehand drag would.
std::vector<Point> lineSamples(int count)
{
    std::vector<Point> points;
    for (int i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / (count - 1);
        points.push_back(Point(t * 100.0, t * 50.0));
    }
    return points;
}

std::vector<Point> arcSamples(int count, double radius)
{
    std::vector<Point> points;
    for (int i = 0; i < count; ++i)
    {
        const double angle = kPi * static_cast<double>(i) / (count - 1);
        points.push_back(Point(radius * std::cos(angle), radius * std::sin(angle)));
    }
    return points;
}

Point lerp(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

/// Worst distance from the fitted path to the points it was fitted to.
///
/// Sampled densely here rather than through EditablePath::closestPoint: a
/// measurement has to be finer than what it is measuring, and the tolerances
/// being checked are smaller than a coarse sweep can resolve.
double worstDeviation(const EditablePath& path, const std::vector<Point>& points)
{
    constexpr int kSamples = 400;

    std::vector<Point> curve;
    curve.reserve(path.segmentCount() * kSamples + 1);

    for (size_t segment = 0; segment < path.segmentCount(); ++segment)
    {
        size_t from = 0;
        size_t to = 0;
        if (!path.segmentAnchors(segment, from, to))
            continue;

        const fla::Anchor& a = path.anchors[from];
        const fla::Anchor& b = path.anchors[to];

        for (int i = 0; i <= kSamples; ++i)
        {
            const double t = static_cast<double>(i) / kSamples;
            if (!a.hasOutCurve() && !b.hasInCurve())
            {
                curve.push_back(lerp(a.position, b.position, t));
                continue;
            }

            const Point p0 = lerp(a.position, a.outHandle, t);
            const Point p1 = lerp(a.outHandle, b.inHandle, t);
            const Point p2 = lerp(b.inHandle, b.position, t);
            curve.push_back(lerp(lerp(p0, p1, t), lerp(p1, p2, t), t));
        }
    }

    double worst = 0.0;
    for (const Point& point : points)
    {
        double nearest = 1.0e30;
        for (const Point& sample : curve)
            nearest = std::min(nearest, std::hypot(sample.x - point.x, sample.y - point.y));
        worst = std::max(worst, nearest);
    }
    return worst;
}

} // namespace

TEST(curve_fit_needs_at_least_two_points)
{
    CurveFit::Options options;

    CHECK(CurveFit::fit({}, options).isEmpty());
    CHECK(CurveFit::fit({Point(1.0, 1.0)}, options).isEmpty());
}

TEST(curve_fit_drops_points_that_sit_on_top_of_each_other)
{
    // Freehand input clumps whenever the cursor pauses.
    const std::vector<Point> points = {
        Point(0.0, 0.0), Point(0.0, 0.0), Point(0.01, 0.0),
        Point(10.0, 0.0), Point(10.0, 0.0)
    };

    const std::vector<Point> cleaned = CurveFit::removeDuplicates(points, 0.5);

    CHECK(cleaned.size() == 2);
    if (cleaned.size() != 2)
        return;
    CHECK(cleaned[0].x == 0.0);
    CHECK(cleaned[1].x == 10.0);
}

TEST(curve_fit_collapses_a_straight_drag_to_two_anchors)
{
    CurveFit::Options options;
    options.tolerance = 1.0;

    const std::vector<Point> points = lineSamples(40);
    const EditablePath path = CurveFit::fit(points, options);

    // Forty input points become the two that describe the line.
    CHECK(path.anchors.size() == 2);
    CHECK(!path.closed);
    CHECK(worstDeviation(path, points) < options.tolerance);
}

TEST(curve_fit_follows_an_arc_within_tolerance)
{
    CurveFit::Options options;
    options.tolerance = 1.0;

    const std::vector<Point> points = arcSamples(60, 100.0);
    const EditablePath path = CurveFit::fit(points, options);

    CHECK(path.anchors.size() >= 2);
    // Far fewer anchors than input points is the whole point of fitting.
    CHECK(path.anchors.size() < points.size() / 4);

    const double deviation = worstDeviation(path, points);
    if (deviation >= options.tolerance)
        std::printf("    arc deviation %g with %d anchors\n", deviation,
            static_cast<int>(path.anchors.size()));
    CHECK(deviation < options.tolerance);
}

TEST(curve_fit_keeps_the_ends_exactly)
{
    CurveFit::Options options;
    const std::vector<Point> points = arcSamples(30, 50.0);

    const EditablePath path = CurveFit::fit(points, options);
    CHECK(path.anchors.size() >= 2);
    if (path.anchors.size() < 2)
        return;

    // The stroke has to start and finish where the user's did.
    CHECK(std::fabs(path.anchors.front().position.x - points.front().x) < 1.0e-9);
    CHECK(std::fabs(path.anchors.front().position.y - points.front().y) < 1.0e-9);
    CHECK(std::fabs(path.anchors.back().position.x - points.back().x) < 1.0e-9);
    CHECK(std::fabs(path.anchors.back().position.y - points.back().y) < 1.0e-9);
}

TEST(curve_fit_uses_more_anchors_at_a_tighter_tolerance)
{
    const std::vector<Point> points = arcSamples(80, 200.0);

    CurveFit::Options loose;
    loose.tolerance = 8.0;
    CurveFit::Options tight;
    tight.tolerance = 0.2;

    const size_t looseCount = CurveFit::fit(points, loose).anchors.size();
    const size_t tightCount = CurveFit::fit(points, tight).anchors.size();

    CHECK(tightCount >= looseCount);
}

TEST(curve_fit_produces_smooth_anchors)
{
    CurveFit::Options options;
    const EditablePath path = CurveFit::fit(arcSamples(40, 80.0), options);

    CHECK(path.anchors.size() >= 2);
    if (path.anchors.size() < 2)
        return;

    // An interior anchor of a fitted curve carries handles on both sides.
    const fla::Anchor& last = path.anchors.back();
    CHECK(last.hasInCurve());
}

TEST(straighten_mode_produces_no_curves)
{
    CurveFit::Options options;
    options.straighten = true;
    options.tolerance = 2.0;

    const EditablePath path = CurveFit::fit(arcSamples(60, 100.0), options);

    CHECK(path.anchors.size() >= 2);
    for (const fla::Anchor& anchor : path.anchors)
    {
        CHECK(!anchor.hasInCurve());
        CHECK(!anchor.hasOutCurve());
    }
}

TEST(straighten_mode_keeps_a_corner)
{
    CurveFit::Options options;
    options.straighten = true;
    options.tolerance = 1.0;

    // An L: straight along x, then straight up.
    std::vector<Point> points;
    for (int i = 0; i <= 20; ++i)
        points.push_back(Point(i * 5.0, 0.0));
    for (int i = 1; i <= 20; ++i)
        points.push_back(Point(100.0, i * 5.0));

    const EditablePath path = CurveFit::fit(points, options);

    // Three anchors: both ends and the corner between them.
    CHECK(path.anchors.size() == 3);
    if (path.anchors.size() != 3)
        return;
    CHECK(std::fabs(path.anchors[1].position.x - 100.0) < 6.0);
    CHECK(std::fabs(path.anchors[1].position.y) < 6.0);
}

TEST(curve_fit_survives_a_degenerate_drag)
{
    CurveFit::Options options;

    // Every point in the same place: nothing to fit, and nothing may divide by
    // zero on the way to finding that out.
    std::vector<Point> points(20, Point(5.0, 5.0));
    const EditablePath path = CurveFit::fit(points, options);
    CHECK(path.isEmpty());
}
