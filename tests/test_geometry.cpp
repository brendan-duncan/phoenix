#include "test_util.h"

#include "../src/data/point.h"
#include "../src/data/rect.h"
#include "../src/data/transform.h"
#include "../src/geom/curve.h"

using fla::Point;
using fla::Rect;
using fla::Transform;

// A 90-degree rotation: (x, y) -> (-y, x). Column-major as stored by Transform,
// so m11=0, m12=1, m21=-1, m22=0.
static Transform rotate90()
{
    return Transform(0.0, 1.0, -1.0, 0.0, 0.0, 0.0);
}

TEST(point_transform_matches_transformed)
{
    // Regression: Point::transform used m12 where it needed m21, so it
    // disagreed with Point::transformed for any rotation or skew.
    const Transform t(2.0, 3.0, 5.0, 7.0, 11.0, 13.0);
    const Point p(1.0, 2.0);

    Point mutated = p;
    mutated.transform(t);
    const Point copied = p.transformed(t);

    CHECK_NEAR(mutated.x, copied.x);
    CHECK_NEAR(mutated.y, copied.y);
    CHECK_NEAR(mutated.x, 2.0 * 1.0 + 5.0 * 2.0 + 11.0);
    CHECK_NEAR(mutated.y, 3.0 * 1.0 + 7.0 * 2.0 + 13.0);
}

TEST(point_transform_rotation)
{
    Point p(1.0, 0.0);
    p.transform(rotate90());
    CHECK_NEAR(p.x, 0.0);
    CHECK_NEAR(p.y, 1.0);
}

TEST(rect_transform_translate)
{
    Rect r(Point(1.0, 2.0), Point(3.0, 4.0));
    r.transform(Transform::fromTranslate(10.0, 20.0));
    CHECK_NEAR(r.topLeft.x, 11.0);
    CHECK_NEAR(r.topLeft.y, 22.0);
    CHECK_NEAR(r.bottomRight.x, 13.0);
    CHECK_NEAR(r.bottomRight.y, 24.0);
}

TEST(rect_transform_no_aliasing)
{
    // Regression: the old implementation assigned topLeft.x and then used the
    // freshly written value when computing topLeft.y. Exposing that needs a
    // transform where x actually changes (m21 != 0) *and* y depends on x
    // (m12 != 0) -- with either at zero the stale read is harmless.
    const Transform t(1.0, 1.0, 1.0, 2.0, 0.0, 0.0); // x' = x + y, y' = x + 2y
    Rect r(Point(2.0, 3.0), Point(4.0, 5.0));
    r.transform(t);

    // Corners map to (5,8) (7,10) (9,14) (7,12) -> AABB (5,8)-(9,14).
    // The aliased version produced topLeft.y = 11.
    CHECK_NEAR(r.topLeft.x, 5.0);
    CHECK_NEAR(r.topLeft.y, 8.0);
    CHECK_NEAR(r.bottomRight.x, 9.0);
    CHECK_NEAR(r.bottomRight.y, 14.0);
}

TEST(rect_transform_rotation_stays_axis_aligned)
{
    // Under 90 degrees the old two-corner version produced an inverted rect.
    Rect r(Point(0.0, 0.0), Point(2.0, 1.0));
    r.transform(rotate90());

    CHECK_NEAR(r.topLeft.x, -1.0);
    CHECK_NEAR(r.topLeft.y, 0.0);
    CHECK_NEAR(r.bottomRight.x, 0.0);
    CHECK_NEAR(r.bottomRight.y, 2.0);
    CHECK(r.width() >= 0.0);
    CHECK(r.height() >= 0.0);
}

TEST(horizontal_ray_crossings_are_exact)
{
    using fla::Curve;
    using fla::Point;

    // A straight segment: crossed once from the left, not from the right.
    const Curve line = Curve::line(Point(10.0, 0.0), Point(10.0, 10.0));
    CHECK(line.horizontalRayCrossings(Point(0.0, 5.0)) == 1);
    CHECK(line.horizontalRayCrossings(Point(20.0, 5.0)) == 0);
    // Through the segment's ends: counted once over the two pieces that meet
    // there, so a ray through a vertex of a closed outline counts once.
    const Curve next = Curve::line(Point(10.0, 10.0), Point(10.0, 20.0));
    CHECK(line.horizontalRayCrossings(Point(0.0, 10.0)) +
        next.horizontalRayCrossings(Point(0.0, 10.0)) == 1);

    // A quarter circle of radius 50 round the origin, from the top to the
    // right. A point just inside the arc sees one crossing; just outside, none.
    const double r = 50.0;
    const double k = r * 0.5522847498;
    const Curve arc = Curve::cubic(Point(0.0, -r), Point(k, -r), Point(r, -k), Point(r, 0.0));
    const double y = -r * 0.7071;
    const double xOnArc = r * 0.7071;
    CHECK(arc.horizontalRayCrossings(Point(xOnArc - 0.1, y)) == 1);
    CHECK(arc.horizontalRayCrossings(Point(xOnArc + 0.1, y)) == 0);

    // A curve that bends back on itself in y is crossed twice from a point
    // beside its bulge.
    const Curve bulge = Curve::cubic(Point(0.0, 0.0), Point(50.0, -40.0), Point(50.0, 40.0), Point(0.0, 0.0));
    CHECK(bulge.horizontalRayCrossings(Point(-10.0, 5.0)) == 2);
    CHECK(bulge.horizontalRayCrossings(Point(-10.0, 40.0)) == 0);
}

TEST(flatness_measures_distance_to_the_chord_segment)
{
    using fla::Curve;
    using fla::Point;

    // Controls on the chord's line but beyond its ends: the curve overshoots,
    // which a distance to the infinite line would call flat.
    const Curve overshoot = Curve::cubic(Point(0.0, 0.0), Point(-30.0, 0.0), Point(130.0, 0.0), Point(100.0, 0.0));
    CHECK(overshoot.flatness() > 29.0);

    const Curve straight = Curve::cubic(Point(0.0, 0.0), Point(30.0, 0.0), Point(70.0, 0.0), Point(100.0, 0.0));
    CHECK_NEAR(straight.flatness(), 0.0);
}

#include "../src/geom/intersect.h"

#include <cmath>

namespace {

/// Crossings of a curve with itself found the slow way: a fine polyline
/// checked segment against segment.
int bruteForceSelfCrossings(const fla::Curve& curve)
{
    const int n = 600;
    std::vector<fla::Point> pts;
    for (int i = 0; i <= n; ++i)
        pts.push_back(curve.pointAt(static_cast<double>(i) / n));
    int crossings = 0;
    for (int i = 0; i + 1 <= n; ++i)
    {
        for (int j = i + 2; j + 1 <= n; ++j)
        {
            const fla::Point& p0 = pts[i];
            const fla::Point& p1 = pts[i + 1];
            const fla::Point& q0 = pts[j];
            const fla::Point& q1 = pts[j + 1];
            const double rx = p1.x - p0.x, ry = p1.y - p0.y;
            const double sx = q1.x - q0.x, sy = q1.y - q0.y;
            const double den = rx * sy - ry * sx;
            if (std::fabs(den) < 1.0e-12)
                continue;
            const double dx = q0.x - p0.x, dy = q0.y - p0.y;
            const double t = (dx * sy - dy * sx) / den;
            const double u = (dx * ry - dy * rx) / den;
            if (t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0)
                ++crossings;
        }
    }
    return crossings;
}

} // namespace

TEST(self_intersections_are_found_wherever_the_loop_lies)
{
    using fla::Curve;
    using fla::Point;

    const Curve loops[] = {
        // A loop across the middle.
        Curve::cubic(Point(0.0, 0.0), Point(120.0, 80.0), Point(-80.0, 80.0), Point(40.0, 0.0)),
        // A loop lying in the first half of the parameter range.
        Curve::cubic(Point(0.0, 0.0), Point(200.0, 100.0), Point(-150.0, 60.0), Point(400.0, 0.0)),
        // And one in the second half.
        Curve::cubic(Point(0.0, 0.0), Point(400.0, 0.0), Point(-150.0, 60.0), Point(200.0, 100.0)),
        // No loop at all.
        Curve::cubic(Point(0.0, 0.0), Point(30.0, 40.0), Point(70.0, 40.0), Point(100.0, 0.0)),
    };
    for (const Curve& curve : loops)
    {
        const int expected = bruteForceSelfCrossings(curve);
        const std::vector<fla::CurveIntersection> found = fla::selfIntersections(curve);
        CHECK(static_cast<int>(found.size()) == expected);
    }
}
