#include "test_util.h"

#include "../src/edit/editable_path.h"
#include "../src/edit/path_tidy.h"

#include <cmath>
#include <vector>

using fla::Anchor;
using fla::EditablePath;
using fla::Point;

namespace {

EditablePath polyline(const std::vector<Point>& points, bool closed = false)
{
    EditablePath path;
    for (const Point& p : points)
        path.anchors.push_back(Anchor(p));
    path.closed = closed;
    return path;
}

constexpr double kPi = 3.14159265358979323846;

/// A circle of radius 100 as four cubic quarter arcs.
EditablePath circle()
{
    constexpr double k = 0.5522847498 * 100.0;
    EditablePath path;
    const Point centre(0.0, 0.0);
    for (int i = 0; i < 4; ++i)
    {
        const double a = i * kPi / 2.0;
        Anchor anchor(Point(100.0 * std::cos(a), 100.0 * std::sin(a)));
        anchor.inHandle = Point(anchor.position.x + k * std::sin(a), anchor.position.y - k * std::cos(a));
        anchor.outHandle = Point(anchor.position.x - k * std::sin(a), anchor.position.y + k * std::cos(a));
        anchor.smooth = true;
        path.anchors.push_back(anchor);
    }
    path.closed = true;
    return path;
}

} // namespace

TEST(sampling_a_path_walks_its_whole_length)
{
    const EditablePath path = polyline({Point(0, 0), Point(100, 0), Point(100, 50)});
    const std::vector<Point> samples = fla::samplePath(path, 10.0);
    CHECK(samples.size() >= 15);
    CHECK(std::fabs(samples.front().x) < 1.0e-9);
    CHECK(std::fabs(samples.back().x - 100.0) < 1.0e-9 && std::fabs(samples.back().y - 50.0) < 1.0e-9);
}

TEST(smoothing_a_corner_grows_handles_through_its_neighbours)
{
    EditablePath path = polyline({Point(0, 0), Point(100, 0), Point(100, 100)});
    fla::smoothPath(path, 1.0);

    const Anchor& corner = path.anchors[1];
    CHECK(corner.smooth);
    CHECK(corner.hasInCurve());
    CHECK(corner.hasOutCurve());
    // The tangent runs from (0,0) towards (100,100), so the out handle sits
    // down and to the right of the corner and the in handle up and left.
    CHECK(corner.outHandle.x > 100.0 && corner.outHandle.y > 0.0);
    CHECK(corner.inHandle.x < 100.0 && corner.inHandle.y < 0.0);
    // Half the amount reaches half as far.
    EditablePath half = polyline({Point(0, 0), Point(100, 0), Point(100, 100)});
    fla::smoothPath(half, 0.5);
    CHECK(std::fabs(half.anchors[1].outHandle.y - corner.outHandle.y / 2.0) < 1.0e-6);
}

TEST(straightening_a_circle_leaves_lines_within_tolerance)
{
    EditablePath path = circle();
    fla::straightenPath(path, 3.0);

    CHECK(path.closed);
    CHECK(path.anchors.size() >= 6);
    for (const Anchor& anchor : path.anchors)
    {
        CHECK(!anchor.hasInCurve() && !anchor.hasOutCurve());
        CHECK(std::fabs(std::hypot(anchor.position.x, anchor.position.y) - 100.0) < 1.0);
    }
}

TEST(optimizing_a_dense_path_drops_anchors_it_does_not_need)
{
    // A circle drawn as a 72-gon of straight anchors.
    std::vector<Point> points;
    for (int i = 0; i < 72; ++i)
        points.push_back(Point(100.0 * std::cos(i * kPi / 36.0), 100.0 * std::sin(i * kPi / 36.0)));
    EditablePath path = polyline(points, true);

    fla::optimizePath(path, 1.0);
    CHECK(path.closed);
    CHECK(path.anchors.size() < 20);
    for (const Anchor& anchor : path.anchors)
        CHECK(std::fabs(std::hypot(anchor.position.x, anchor.position.y) - 100.0) < 1.5);
}

TEST(optimizing_keeps_a_path_that_is_already_lean)
{
    EditablePath path = circle();
    fla::optimizePath(path, 1.0);
    CHECK(path.anchors.size() == 4);
}
