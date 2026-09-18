#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/shape_merge.h"
#include "../src/edit/shape_transform.h"
#include "../src/geom/planar_map.h"
#include "../src/geom/shape_geometry.h"
#include "../src/parser/path_parser.h"

#include <memory>
#include <string>

using fla::Point;
using fla::Shape;
using fla::ShapeCurve;
using fla::ShapeMerger;

namespace {

/// A filled rectangle, in twips, with the fill recorded on one side.
std::unique_ptr<Shape> filledRectangle(int left, int top, int right, int bottom,
    uint8_t red, uint8_t green, uint8_t blue)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));

    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    fill->color[0] = red;
    fill->color[1] = green;
    fill->color[2] = blue;
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
        edge->fillStyle0 = -1;
        shape->edges.push_back(edge);
    }

    return shape;
}

/// A stroke-only line, in twips.
std::unique_ptr<Shape> strokedLine(int x1, int y1, int x2, int y2)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));

    fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
    stroke->weight = 1.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;

    char data[80];
    std::snprintf(data, sizeof(data), "!%d %d|%d %d", x1, y1, x2, y2);

    PathParser parser;
    fla::Edge* edge = parser.parse(data, shape.get());
    if (edge)
    {
        edge->strokeStyle = 1;
        shape->edges.push_back(edge);
    }

    return shape;
}

/// What a shape paints at a point, asked through the arrangement.
int fillAt(const Shape& shape, const Point& point)
{
    const std::vector<ShapeCurve> curves = fla::shapeCurves(shape);

    fla::PlanarMap map;
    for (size_t i = 0; i < curves.size(); ++i)
        map.addCurve(curves[i].curve, static_cast<int>(i));
    map.build();
    fla::attributeFillsFromSource(map, curves);

    return map.fillAt(point);
}

/// The colour a shape paints at a point, as a packed value, or -1 for nothing.
int colourAt(const Shape& shape, const Point& point)
{
    const int fill = fillAt(shape, point);
    if (fill == -1)
        return -1;

    const auto it = shape.fillsMap.find(fill);
    if (it == shape.fillsMap.end() || !it->second)
        return -1;
    if (it->second->type() != fla::FillStyle::Type::SolidColor)
        return -1;

    const fla::SolidColor* colour = static_cast<const fla::SolidColor*>(it->second);
    return (colour->color[0] << 16) | (colour->color[1] << 8) | colour->color[2];
}

constexpr int kRed = 0xFF0000;
constexpr int kBlue = 0x0000FF;

} // namespace

TEST(merging_nothing_leaves_the_target_alone)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    const size_t before = target->edges.size();

    Shape empty(nullptr);
    CHECK(!ShapeMerger::merge(*target, empty));
    CHECK(target->edges.size() == before);
}

TEST(a_shape_drawn_over_another_replaces_it_where_they_overlap)
{
    // Red square, then a blue one across its corner. The overlap becomes blue,
    // and the rest of each keeps its own colour: one shape, three regions.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    CHECK(colourAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colourAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colourAt(*target, Point(125.0, 125.0)) == kBlue);
    // Outside both, still nothing.
    CHECK(colourAt(*target, Point(-25.0, -25.0)) == -1);
}

TEST(merging_renumbers_the_addition_styles)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    // Both shapes called their fill 1, so the addition's has to be given a
    // number of its own or the two colours would collide.
    CHECK(target->fillsMap.size() == 2);
    CHECK(target->fills.size() == 2);
}

TEST(the_seam_between_two_regions_of_one_colour_disappears)
{
    // Two red squares overlapping. Everything ends up red, so there is no
    // boundary left inside: the result is a single region.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 0, 3000, 2000, 255, 0, 0);

    CHECK(ShapeMerger::merge(*target, *addition));

    // Every edge still separates something.
    for (const fla::Edge* edge : target->edges)
        CHECK(edge->fillStyle0 != edge->fillStyle1 || edge->strokeStyle != -1);

    // The whole span is painted, including across where the seam would be.
    CHECK(fillAt(*target, Point(25.0, 50.0)) != -1);
    CHECK(fillAt(*target, Point(75.0, 50.0)) != -1);
    CHECK(fillAt(*target, Point(125.0, 50.0)) != -1);
}

TEST(a_line_drawn_across_a_fill_divides_it_without_erasing)
{
    // The behaviour that makes merge drawing what it is: a stroke across a fill
    // cuts it in two, and both halves keep their colour.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(1000, -400, 1000, 2400);

    CHECK(ShapeMerger::merge(*target, *line));

    CHECK(colourAt(*target, Point(25.0, 50.0)) == kRed);
    CHECK(colourAt(*target, Point(75.0, 50.0)) == kRed);

    // The two halves are separate regions now, which is what lets them be
    // dragged apart.
    const std::vector<ShapeCurve> curves = fla::shapeCurves(*target);
    fla::PlanarMap map;
    for (size_t i = 0; i < curves.size(); ++i)
        map.addCurve(curves[i].curve, static_cast<int>(i));
    map.build();

    CHECK(map.faceAt(Point(25.0, 50.0)) != map.faceAt(Point(75.0, 50.0)));
}

TEST(a_stroke_survives_the_merge)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(1000, -400, 1000, 2400);

    CHECK(ShapeMerger::merge(*target, *line));

    // The stroke came with its own style, which has to arrive too.
    CHECK(target->strokesMap.size() == 1);

    int strokedEdges = 0;
    for (const fla::Edge* edge : target->edges)
    {
        if (edge->strokeStyle != -1)
            ++strokedEdges;
    }
    CHECK(strokedEdges > 0);
}

TEST(merging_into_an_empty_shape_just_adopts_it)
{
    Shape target(nullptr);
    std::unique_ptr<Shape> addition = filledRectangle(0, 0, 2000, 2000, 0, 0, 255);

    CHECK(ShapeMerger::merge(target, *addition));

    CHECK(colourAt(target, Point(50.0, 50.0)) == kBlue);
    CHECK(colourAt(target, Point(-10.0, -10.0)) == -1);
}

TEST(order_decides_the_overlap)
{
    std::unique_ptr<Shape> redFirst = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> blue = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
    CHECK(ShapeMerger::merge(*redFirst, *blue));

    std::unique_ptr<Shape> blueFirst = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
    std::unique_ptr<Shape> red = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    CHECK(ShapeMerger::merge(*blueFirst, *red));

    // Same two squares, opposite drawing order, opposite colour in the overlap.
    CHECK(colourAt(*redFirst, Point(75.0, 75.0)) == kBlue);
    CHECK(colourAt(*blueFirst, Point(75.0, 75.0)) == kRed);
}

TEST(a_shape_drawn_clear_of_another_keeps_both)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 1000, 1000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(4000, 4000, 5000, 5000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    // Nothing crosses, so both survive untouched in one shape.
    CHECK(colourAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colourAt(*target, Point(225.0, 225.0)) == kBlue);
    CHECK(colourAt(*target, Point(125.0, 125.0)) == -1);
}

TEST(the_merge_command_can_be_undone)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    const size_t edgesBefore = target->edges.size();
    const size_t fillsBefore = target->fillsMap.size();

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::MergeShapeCommand(
        target.get(), *addition, "Merge")));

    // The merge happened.
    CHECK(colourAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(target->fillsMap.size() == 2);

    stack.undo();

    // And it went away completely: edges, fills and all.
    CHECK(target->edges.size() == edgesBefore);
    CHECK(target->fillsMap.size() == fillsBefore);
    CHECK(colourAt(*target, Point(75.0, 75.0)) == kRed);
    CHECK(colourAt(*target, Point(125.0, 125.0)) == -1);

    stack.redo();

    CHECK(colourAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colourAt(*target, Point(125.0, 125.0)) == kBlue);
}

TEST(undo_and_redo_of_a_merge_are_repeatable)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::MergeShapeCommand(
        target.get(), *addition, "Merge")));

    // Snapshots are taken once, so going back and forth must not drift or leak.
    for (int i = 0; i < 3; ++i)
    {
        stack.undo();
        CHECK(colourAt(*target, Point(75.0, 75.0)) == kRed);
        CHECK(target->fillsMap.size() == 1);

        stack.redo();
        CHECK(colourAt(*target, Point(75.0, 75.0)) == kBlue);
        CHECK(target->fillsMap.size() == 2);
    }
}

TEST(cloning_a_shape_copies_rather_than_shares)
{
    std::unique_ptr<Shape> original = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> copy(fla::cloneShape(*original, nullptr));

    CHECK(copy->edges.size() == original->edges.size());
    CHECK(copy->fillsMap.size() == original->fillsMap.size());
    CHECK(colourAt(*copy, Point(50.0, 50.0)) == kRed);

    // Nothing is shared: the copy owns its own edges and styles, so freeing one
    // must not touch the other.
    for (size_t i = 0; i < copy->edges.size(); ++i)
        CHECK(copy->edges[i] != original->edges[i]);

    for (const auto& entry : copy->fillsMap)
    {
        const auto it = original->fillsMap.find(entry.first);
        CHECK(it != original->fillsMap.end());
        if (it != original->fillsMap.end())
            CHECK(entry.second != it->second);
    }

    original.reset();
    // Still readable after the original has gone.
    CHECK(colourAt(*copy, Point(50.0, 50.0)) == kRed);
}

/// A drawing can be dragged before it is let go, and letting go is what merges
/// it. The move lives in the shape's transform, while merging reads raw edge
/// coordinates -- so the transform has to come down into the geometry first or
/// the drawing merges where it was drawn rather than where it was left.
TEST(a_dragged_drawing_merges_where_it_was_left)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);

    // Drawn clear of the target, at 200..300 pixels, then dragged back onto its
    // far corner. Edge coordinates are stored in twips and a transform is in
    // pixels, but the parser converts, so a Point is already in pixels.
    std::unique_ptr<Shape> addition = filledRectangle(4000, 4000, 6000, 6000, 0, 0, 255);
    addition->transform = fla::Transform::fromTranslate(-150.0, -150.0);

    fla::bakeTransform(*addition);
    CHECK(ShapeMerger::merge(*target, *addition));

    // The blue now covers 50..150 pixels.
    CHECK(colourAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colourAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colourAt(*target, Point(125.0, 125.0)) == kBlue);

    // Where it was drawn, before the drag, there is nothing at all.
    CHECK(colourAt(*target, Point(250.0, 250.0)) == -1);
}

/// Without the bake the merge would use the drawn coordinates and land in the
/// wrong place, which is what this guards against.
TEST(merging_ignores_a_transform_that_was_not_baked)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(4000, 4000, 6000, 6000, 0, 0, 255);
    addition->transform = fla::Transform::fromTranslate(-150.0, -150.0);

    CHECK(ShapeMerger::merge(*target, *addition));

    // The transform was never consulted, so the blue stayed where it was drawn.
    CHECK(colourAt(*target, Point(75.0, 75.0)) == kRed);
    CHECK(colourAt(*target, Point(250.0, 250.0)) == kBlue);
}

namespace {

/// A rectangle with both a fill and an outline, in twips.
std::unique_ptr<Shape> strokedRectangle(int left, int top, int right, int bottom,
    uint8_t red, uint8_t green, uint8_t blue)
{
    std::unique_ptr<Shape> shape = filledRectangle(left, top, right, bottom,
        red, green, blue);

    fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
    stroke->weight = 1.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;

    for (fla::Edge* edge : shape->edges)
        edge->strokeStyle = 1;

    return shape;
}

/// The midpoint of every edge that still carries a stroke, in pixels.
std::vector<Point> strokedMidpoints(const Shape& shape)
{
    std::vector<Point> found;
    for (const ShapeCurve& curve : fla::shapeCurves(shape))
    {
        if (curve.strokeStyle == -1)
            continue;

        const Point start = curve.curve.start();
        const Point end = curve.curve.end();
        found.push_back(Point((start.x + end.x) / 2.0, (start.y + end.y) / 2.0));
    }
    return found;
}

} // namespace

/// Drawing a filled shape over something covers what was there, outlines
/// included. Without this the older rectangle's edges keep drawing across the
/// newer one, which is not what the artwork looks like.
TEST(a_fill_drawn_over_a_stroke_buries_it)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = strokedRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    // The addition covers 50..150 pixels. Nothing stroked may survive strictly
    // inside that, though its own outline runs along the boundary.
    for (const Point& point : strokedMidpoints(*target))
    {
        const bool insideAddition = point.x > 50.5 && point.x < 149.5 &&
            point.y > 50.5 && point.y < 149.5;
        CHECK(!insideAddition);
    }
}

/// Only the buried part goes. The older outline outside the new drawing is
/// untouched, and the new drawing's own outline survives whatever it lies over.
TEST(burying_a_stroke_spares_the_rest_of_it)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = strokedRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    const std::vector<Point> stroked = strokedMidpoints(*target);

    // The target's top edge, clear of the addition.
    bool keptTargetEdge = false;
    // The addition's own left edge where it crosses the target.
    bool keptAdditionEdge = false;

    for (const Point& point : stroked)
    {
        if (point.y < 0.5 && point.x > 0.0 && point.x < 100.0)
            keptTargetEdge = true;
        if (point.x > 49.5 && point.x < 50.5 && point.y > 50.0 && point.y < 100.0)
            keptAdditionEdge = true;
    }

    CHECK(keptTargetEdge);
    CHECK(keptAdditionEdge);
}

/// An outline drawn with no fill behind it cuts without erasing, which is how a
/// line dropped across a shape divides it. Nothing is buried in that case.
TEST(an_unfilled_drawing_buries_nothing)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    const size_t before = strokedMidpoints(*target).size();

    std::unique_ptr<Shape> addition = strokedLine(-500, 1000, 2500, 1000);
    CHECK(ShapeMerger::merge(*target, *addition));

    // The target's own outline is all still there; crossing it only split the
    // pieces the line passes through.
    size_t onTargetOutline = 0;
    for (const Point& point : strokedMidpoints(*target))
    {
        const bool onEdge = point.x < 0.5 || point.x > 99.5 ||
            point.y < 0.5 || point.y > 99.5;
        if (onEdge)
            ++onTargetOutline;
    }
    CHECK(onTargetOutline >= before);
}

/// Drawing over something destroys what was under it. Taking the drawing away
/// afterwards therefore reveals a hole rather than putting anything back.
TEST(subtracting_cuts_a_hole)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::subtract(*target, *cutter));

    // Outside the cutter the red survives; inside it there is nothing at all.
    CHECK(colourAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colourAt(*target, Point(75.0, 75.0)) == -1);
    // Beyond the target entirely, still nothing.
    CHECK(colourAt(*target, Point(125.0, 125.0)) == -1);
}

/// The hole takes the cutter's colour with it: a hole is an absence, not a
/// differently coloured patch.
TEST(a_hole_is_not_painted_with_the_cutter)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::subtract(*target, *cutter));

    for (const ShapeCurve& curve : fla::shapeCurves(*target))
    {
        CHECK(colourAt(*target, curve.curve.start()) != kBlue);
    }
}

/// The cutter leaves no outline round the hole, and takes the buried part of
/// the target's own outline with it.
TEST(subtracting_leaves_no_outline_behind)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = strokedRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::subtract(*target, *cutter));

    for (const Point& point : strokedMidpoints(*target))
    {
        const bool insideCutter = point.x > 50.5 && point.x < 149.5 &&
            point.y > 50.5 && point.y < 149.5;
        CHECK(!insideCutter);
    }
}

/// Only a filled cutter takes anything away. An outline on its own has no area,
/// so it cuts the geometry up without removing any of it.
TEST(a_stroke_only_cutter_removes_nothing)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = strokedLine(-500, 1000, 2500, 1000);

    ShapeMerger::subtract(*target, *cutter);

    // Both halves the line divided are still red.
    CHECK(colourAt(*target, Point(50.0, 25.0)) == kRed);
    CHECK(colourAt(*target, Point(50.0, 75.0)) == kRed);
}

/// Cutting with something that misses entirely changes nothing.
TEST(subtracting_something_clear_of_the_target_changes_nothing)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(5000, 5000, 6000, 6000, 0, 0, 255);

    ShapeMerger::subtract(*target, *cutter);

    CHECK(colourAt(*target, Point(50.0, 50.0)) == kRed);
}
