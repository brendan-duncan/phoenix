#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/edit/shape_transform.h"
#include "../src/parser/path_parser.h"

#include <cstdio>
#include <memory>
#include <vector>

using fla::Point;
using fla::Shape;
using fla::Transform;

namespace {

/// A filled rectangle, in twips, carrying the edge text it was built from --
/// which is what the writer prefers when it has not been invalidated.
std::unique_ptr<Shape> rectangle(int left, int top, int right, int bottom)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));

    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    fill->color[3] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;

    char data[160];
    std::snprintf(data, sizeof(data), "!%d %d|%d %d|%d %d|%d %d|%d %d",
        left, top, right, top, right, bottom, left, bottom, left, top);

    PathParser parser;
    fla::Edge* edge = parser.parse(data, shape.get());
    if (edge)
    {
        edge->fillStyle1 = 1;
        edge->data = data;
        shape->edges.push_back(edge);
    }

    shape->localBounds = fla::Rect(Point(left, top), Point(right, bottom));
    shape->bounds = shape->localBounds;
    return shape;
}

/// Every point in a shape's geometry, in order.
std::vector<Point> points(const Shape& shape)
{
    std::vector<Point> found;
    for (const fla::Edge* edge : shape.edges)
    {
        for (const fla::Path* path : edge->paths)
        {
            for (const fla::PathSegment* segment : path->segments)
            {
                for (const Point& point : segment->points)
                    found.push_back(point);
            }
        }
    }
    return found;
}

} // namespace

TEST(bake_transform_leaves_an_untransformed_shape_alone)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);
    const std::vector<Point> before = points(*shape);

    fla::bakeTransform(*shape);

    const std::vector<Point> after = points(*shape);
    CHECK(before.size() == after.size());
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
    {
        CHECK_NEAR(before[i].x, after[i].x);
        CHECK_NEAR(before[i].y, after[i].y);
    }
}

/// An untouched shape keeps the text it was read with, which round-trips
/// exactly. Only one that actually moved has to give that up.
TEST(bake_transform_keeps_edge_text_when_there_is_nothing_to_do)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);

    fla::bakeTransform(*shape);

    CHECK(!shape->edges.empty());
    CHECK(!shape->edges[0]->data.empty());
}

TEST(bake_transform_moves_the_points_by_a_translation)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);
    const std::vector<Point> before = points(*shape);

    shape->transform = Transform::fromTranslate(40.0, -25.0);
    fla::bakeTransform(*shape);

    const std::vector<Point> after = points(*shape);
    CHECK(before.size() == after.size());
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
    {
        CHECK_NEAR(after[i].x, before[i].x + 40.0);
        CHECK_NEAR(after[i].y, before[i].y - 25.0);
    }
}

TEST(bake_transform_leaves_the_transform_as_the_identity)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);
    shape->transform = Transform::fromTranslate(40.0, -25.0);

    fla::bakeTransform(*shape);

    CHECK_NEAR(shape->transform.m11, 1.0);
    CHECK_NEAR(shape->transform.m12, 0.0);
    CHECK_NEAR(shape->transform.m21, 0.0);
    CHECK_NEAR(shape->transform.m22, 1.0);
    CHECK_NEAR(shape->transform.tx, 0.0);
    CHECK_NEAR(shape->transform.ty, 0.0);
}

/// The writer prefers the text an edge was read with, so an edge whose points
/// have moved has to stop offering it -- otherwise saving would write the
/// geometry back exactly where it used to be.
TEST(bake_transform_drops_the_stale_edge_text)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);
    CHECK(!shape->edges[0]->data.empty());

    shape->transform = Transform::fromTranslate(40.0, -25.0);
    fla::bakeTransform(*shape);

    CHECK(shape->edges[0]->data.empty());
}

TEST(bake_transform_carries_the_bounds_across)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);

    shape->transform = Transform::fromTranslate(40.0, -25.0);
    fla::bakeTransform(*shape);

    CHECK_NEAR(shape->localBounds.topLeft.x, 40.0);
    CHECK_NEAR(shape->localBounds.topLeft.y, -25.0);
    CHECK_NEAR(shape->localBounds.bottomRight.x, 140.0);
    CHECK_NEAR(shape->localBounds.bottomRight.y, 175.0);
    CHECK_NEAR(shape->bounds.topLeft.x, shape->localBounds.topLeft.x);
    CHECK_NEAR(shape->bounds.bottomRight.y, shape->localBounds.bottomRight.y);
}

TEST(bake_transform_handles_scale)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);
    const std::vector<Point> before = points(*shape);

    shape->transform = Transform::fromScale(2.0, 0.5);
    fla::bakeTransform(*shape);

    const std::vector<Point> after = points(*shape);
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
    {
        CHECK_NEAR(after[i].x, before[i].x * 2.0);
        CHECK_NEAR(after[i].y, before[i].y * 0.5);
    }
}

/// Baking must agree with what the renderer would have drawn, or a shape would
/// jump the moment it merged. Rotation is where a transposed matrix would show
/// up, which the shear terms of a translation or a scale cannot catch.
TEST(bake_transform_matches_transforming_each_point)
{
    std::unique_ptr<Shape> shape = rectangle(0, 0, 100, 200);
    const std::vector<Point> before = points(*shape);

    const Transform rotation = Transform::fromRotation(30.0);
    shape->transform = rotation;
    fla::bakeTransform(*shape);

    const std::vector<Point> after = points(*shape);
    CHECK(before.size() == after.size());
    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
    {
        const Point expected = before[i].transformed(rotation);
        CHECK_NEAR(after[i].x, expected.x);
        CHECK_NEAR(after[i].y, expected.y);
    }
}
