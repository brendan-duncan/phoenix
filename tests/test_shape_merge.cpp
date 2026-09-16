#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/shape_merge.h"
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
