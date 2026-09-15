#include "test_util.h"

#include "../src/data/point.h"
#include "../src/data/rect.h"
#include "../src/data/transform.h"

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
