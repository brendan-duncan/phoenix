#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/shape_merge.h"
#include "../src/edit/combine_objects.h"
#include "../src/edit/shape_transform.h"
#include "../src/edit/editable_path.h"
#include "../src/geom/intersect.h"
#include "../src/geom/planar_map.h"
#include "../src/geom/shape_geometry.h"
#include "../src/parser/path_parser.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

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

/// The color a shape paints at a point, as a packed value, or -1 for nothing.
int colorAt(const Shape& shape, const Point& point)
{
    const int fill = fillAt(shape, point);
    if (fill == -1)
        return -1;

    const auto it = shape.fillsMap.find(fill);
    if (it == shape.fillsMap.end() || !it->second)
        return -1;
    if (it->second->type() != fla::FillStyle::Type::SolidColor)
        return -1;

    const fla::SolidColor* color = static_cast<const fla::SolidColor*>(it->second);
    return (color->color[0] << 16) | (color->color[1] << 8) | color->color[2];
}

constexpr int kRed = 0xFF0000;
constexpr int kBlue = 0x0000FF;

} // namespace

TEST(cloning_keeps_stroke_attributes_and_the_drawing_object_flag)
{
    std::unique_ptr<Shape> shape = filledRectangle(0, 0, 100, 100, 1, 2, 3);
    shape->isDrawingObject = true;

    fla::DashedStroke* stroke = new fla::DashedStroke(shape.get());
    stroke->weight = 2.5;
    stroke->solidStyle = "hairline";
    stroke->caps = "none";
    stroke->joints = "miter";
    stroke->miterLimit = 7.0;
    stroke->pixelHinting = true;
    stroke->dash1 = 11.0;
    stroke->dash2 = 4.0;
    stroke->dotSpace = 9.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;

    std::unique_ptr<Shape> copy(fla::cloneShape(*shape, nullptr));
    CHECK(copy->isDrawingObject);
    const fla::StrokeStyle* copied = copy->getStrokeStyleByIndex(1);
    CHECK(copied != nullptr);
    if (!copied)
        return;
    CHECK(copied->style() == fla::StrokeStyle::Style::Dashed);
    CHECK(copied->solidStyle == "hairline");
    CHECK(copied->caps == "none");
    CHECK(copied->joints == "miter");
    CHECK_NEAR(copied->miterLimit, 7.0);
    CHECK(copied->pixelHinting);
    CHECK_NEAR(copied->dash1, 11.0);
    CHECK_NEAR(copied->dash2, 4.0);
    CHECK_NEAR(copied->dotSpace, 9.0);
}

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
    // and the rest of each keeps its own color: one shape, three regions.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colorAt(*target, Point(125.0, 125.0)) == kBlue);
    // Outside both, still nothing.
    CHECK(colorAt(*target, Point(-25.0, -25.0)) == -1);
}

TEST(a_drawing_cut_out_and_merged_back_keeps_its_own_color)
{
    // What placing a drawing does: cut its area out of what is under it, then
    // later merge it in where it sits. The drawing's color must survive both.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> band = filledRectangle(1000, 800, 3000, 1200, 0, 0, 255);

    CHECK(ShapeMerger::subtract(*target, *band));
    CHECK(colorAt(*target, Point(75.0, 50.0)) == -1);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);

    CHECK(ShapeMerger::merge(*target, *band));
    CHECK(colorAt(*target, Point(75.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(125.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
}

/// A closed polygon, in twips, with the fill on the side its direction implies.
std::unique_ptr<Shape> filledPolygon(const std::vector<std::pair<int, int>>& points,
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

    std::string data = "!" + std::to_string(points[0].first) + " " + std::to_string(points[0].second);
    for (size_t i = 1; i <= points.size(); ++i)
    {
        const auto& p = points[i % points.size()];
        data += "|" + std::to_string(p.first) + " " + std::to_string(p.second);
    }

    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        area += double(a.first) * b.second - double(b.first) * a.second;
    }

    PathParser parser;
    fla::Edge* edge = parser.parse(data, shape.get());
    if (edge)
    {
        if (area >= 0.0)
            edge->fillStyle1 = 1;
        else
            edge->fillStyle0 = 1;
        shape->edges.push_back(edge);
    }
    return shape;
}

/// A brush stroke the way the stroker leaves it: a thin band whose long sides
/// are polylines of short, nearly collinear pieces, with polygonal round caps.
std::unique_ptr<Shape> brushBand(double x0, double y0, double x1, double y1, double width, int steps)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double length = std::sqrt(dx * dx + dy * dy);
    const double nx = -dy / length * width / 2.0;
    const double ny = dx / length * width / 2.0;

    std::vector<std::pair<int, int>> points;
    const auto add = [&](double x, double y) {
        points.push_back({static_cast<int>(std::lround(x * 20.0)), static_cast<int>(std::lround(y * 20.0))});
    };

    // One side, the far cap, the other side back, the near cap.
    for (int i = 0; i <= steps; ++i)
        add(x0 + dx * i / steps + nx, y0 + dy * i / steps + ny);
    const double angle0 = std::atan2(ny, nx);
    for (int i = 1; i < 10; ++i)
    {
        const double a = angle0 - 3.14159265358979323846 * i / 10.0;
        add(x1 + std::cos(a) * width / 2.0, y1 + std::sin(a) * width / 2.0);
    }
    for (int i = steps; i >= 0; --i)
        add(x0 + dx * i / steps - nx, y0 + dy * i / steps - ny);
    for (int i = 1; i < 10; ++i)
    {
        const double a = angle0 + 3.14159265358979323846 + 3.14159265358979323846 * (10 - i) / 10.0;
        add(x0 + std::cos(a) * width / 2.0, y0 + std::sin(a) * width / 2.0);
    }

    return filledPolygon(points, 0, 0, 255);
}

TEST(a_brush_band_cut_out_and_merged_back_keeps_its_own_color)
{
    // The square is 0..100 px; the band runs from well left of it to well
    // right, across its top-left corner, as a brush stroke did on screen.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> band = brushBand(-60.0, 30.0, 160.0, 5.0, 12.0, 16);

    CHECK(ShapeMerger::subtract(*target, *band));
    CHECK(colorAt(*target, Point(50.0, 17.5)) == -1);
    CHECK(colorAt(*target, Point(50.0, 60.0)) == kRed);

    CHECK(ShapeMerger::merge(*target, *band));
    CHECK(colorAt(*target, Point(50.0, 17.5)) == kBlue);
    CHECK(colorAt(*target, Point(-40.0, 27.7)) == kBlue);
    CHECK(colorAt(*target, Point(140.0, 7.3)) == kBlue);
    CHECK(colorAt(*target, Point(50.0, 60.0)) == kRed);
    CHECK(colorAt(*target, Point(150.0, 60.0)) == -1);
}

TEST(merging_renumbers_the_addition_styles)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    // Both shapes called their fill 1, so the addition's has to be given a
    // number of its own or the two colors would collide.
    CHECK(target->fillsMap.size() == 2);
    CHECK(target->fills.size() == 2);
}

TEST(a_cut_leaves_none_of_the_cutters_styles_behind)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
    std::unique_ptr<Shape> line = strokedLine(0, 1500, 3000, 1500);

    CHECK(ShapeMerger::subtract(*target, *cutter));
    CHECK(ShapeMerger::subtract(*target, *line));
    // Only the red the target had, still filed under 1, and no strokes.
    CHECK(target->fills.size() == 1 && target->fillsMap.size() == 1 && target->fillsMap.count(1) == 1);
    CHECK(target->strokes.empty() && target->strokesMap.empty());
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
}

TEST(a_fill_covered_entirely_leaves_the_table_and_the_rest_are_renumbered)
{
    // Blue drawn right over a smaller red: the red is gone from the picture,
    // so it goes from the table too, and the blue becomes fill 1.
    std::unique_ptr<Shape> target = filledRectangle(500, 500, 1500, 1500, 255, 0, 0);
    std::unique_ptr<Shape> cover = filledRectangle(0, 0, 2000, 2000, 0, 0, 255);
    CHECK(ShapeMerger::merge(*target, *cover));
    CHECK(target->fills.size() == 1 && target->fillsMap.count(1) == 1);
    CHECK(colorAt(*target, Point(50.0, 50.0)) == kBlue);
    for (const fla::Edge* edge : target->edges)
    {
        CHECK(edge->fillStyle0 == -1 || edge->fillStyle0 == 1);
        CHECK(edge->fillStyle1 == -1 || edge->fillStyle1 == 1);
    }

    // Drawn many times over, the table stays the size of what shows.
    for (int i = 0; i < 10; ++i)
    {
        std::unique_ptr<Shape> again = filledRectangle(0, 0, 2000, 2000, 0, static_cast<uint8_t>(i * 20), 0);
        CHECK(ShapeMerger::merge(*target, *again));
    }
    CHECK(target->fills.size() == 1);
}

TEST(the_seam_between_two_regions_of_one_color_disappears)
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
    // The behavior that makes merge drawing what it is: a stroke across a fill
    // cuts it in two, and both halves keep their color.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(1000, -400, 1000, 2400);

    CHECK(ShapeMerger::merge(*target, *line));

    CHECK(colorAt(*target, Point(25.0, 50.0)) == kRed);
    CHECK(colorAt(*target, Point(75.0, 50.0)) == kRed);

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

    CHECK(colorAt(target, Point(50.0, 50.0)) == kBlue);
    CHECK(colorAt(target, Point(-10.0, -10.0)) == -1);
}

TEST(order_decides_the_overlap)
{
    std::unique_ptr<Shape> redFirst = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> blue = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
    CHECK(ShapeMerger::merge(*redFirst, *blue));

    std::unique_ptr<Shape> blueFirst = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
    std::unique_ptr<Shape> red = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    CHECK(ShapeMerger::merge(*blueFirst, *red));

    // Same two squares, opposite drawing order, opposite color in the overlap.
    CHECK(colorAt(*redFirst, Point(75.0, 75.0)) == kBlue);
    CHECK(colorAt(*blueFirst, Point(75.0, 75.0)) == kRed);
}

TEST(a_shape_drawn_clear_of_another_keeps_both)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 1000, 1000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(4000, 4000, 5000, 5000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    // Nothing crosses, so both survive untouched in one shape.
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(225.0, 225.0)) == kBlue);
    CHECK(colorAt(*target, Point(125.0, 125.0)) == -1);
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
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(target->fillsMap.size() == 2);

    stack.undo();

    // And it went away completely: edges, fills and all.
    CHECK(target->edges.size() == edgesBefore);
    CHECK(target->fillsMap.size() == fillsBefore);
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kRed);
    CHECK(colorAt(*target, Point(125.0, 125.0)) == -1);

    stack.redo();

    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colorAt(*target, Point(125.0, 125.0)) == kBlue);
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
        CHECK(colorAt(*target, Point(75.0, 75.0)) == kRed);
        CHECK(target->fillsMap.size() == 1);

        stack.redo();
        CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
        CHECK(target->fillsMap.size() == 2);
    }
}

TEST(cloning_a_shape_copies_rather_than_shares)
{
    std::unique_ptr<Shape> original = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> copy(fla::cloneShape(*original, nullptr));

    CHECK(copy->edges.size() == original->edges.size());
    CHECK(copy->fillsMap.size() == original->fillsMap.size());
    CHECK(colorAt(*copy, Point(50.0, 50.0)) == kRed);

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
    CHECK(colorAt(*copy, Point(50.0, 50.0)) == kRed);
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
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colorAt(*target, Point(125.0, 125.0)) == kBlue);

    // Where it was drawn, before the drag, there is nothing at all.
    CHECK(colorAt(*target, Point(250.0, 250.0)) == -1);
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
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kRed);
    CHECK(colorAt(*target, Point(250.0, 250.0)) == kBlue);
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
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(75.0, 75.0)) == -1);
    // Beyond the target entirely, still nothing.
    CHECK(colorAt(*target, Point(125.0, 125.0)) == -1);
}

/// The hole takes the cutter's color with it: a hole is an absence, not a
/// differently colored patch.
TEST(a_hole_is_not_painted_with_the_cutter)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::subtract(*target, *cutter));

    for (const ShapeCurve& curve : fla::shapeCurves(*target))
    {
        CHECK(colorAt(*target, curve.curve.start()) != kBlue);
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
    CHECK(colorAt(*target, Point(50.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(50.0, 75.0)) == kRed);
}

/// Cutting with something that misses entirely changes nothing.
TEST(subtracting_something_clear_of_the_target_changes_nothing)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(5000, 5000, 6000, 6000, 0, 0, 255);

    ShapeMerger::subtract(*target, *cutter);

    CHECK(colorAt(*target, Point(50.0, 50.0)) == kRed);
}

/// The brush's fills-only modes paint under the lines: a stroke the new fill
/// covers on both sides stays where a normal merge would bury it.
TEST(merging_with_strokes_kept_spares_what_it_covers)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    fla::MergeOptions options;
    options.keepStrokes = true;
    CHECK(ShapeMerger::merge(*target, *addition, options));

    // The target's right edge between 50 and 100 lies inside the addition.
    bool keptBuriedEdge = false;
    for (const Point& point : strokedMidpoints(*target))
    {
        if (point.x > 99.5 && point.y > 50.5 && point.y < 99.5)
            keptBuriedEdge = true;
    }
    CHECK(keptBuriedEdge);

    // The fill underneath still changed hands.
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
}

/// Erase Fills takes the fill out and leaves the lines through the hole.
TEST(subtracting_with_strokes_kept_cuts_the_fill_only)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    fla::MergeOptions options;
    options.keepStrokes = true;
    CHECK(ShapeMerger::subtract(*target, *cutter, options));

    CHECK(colorAt(*target, Point(75.0, 75.0)) == -1);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);

    bool keptEdgeInHole = false;
    for (const Point& point : strokedMidpoints(*target))
    {
        if (point.x > 99.5 && point.y > 50.5 && point.y < 99.5)
            keptEdgeInHole = true;
    }
    CHECK(keptEdgeInHole);
}

/// Erase Lines takes the strokes inside the cutter and leaves every fill.
TEST(erasing_strokes_leaves_the_fills)
{
    std::unique_ptr<Shape> target = strokedRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> cutter = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    CHECK(ShapeMerger::eraseStrokes(*target, *cutter));

    CHECK(colorAt(*target, Point(75.0, 75.0)) == kRed);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);

    bool keptTopEdge = false;
    bool keptEdgeInCutter = false;
    for (const Point& point : strokedMidpoints(*target))
    {
        if (point.y < 0.5 && point.x > 0.0 && point.x < 100.0)
            keptTopEdge = true;
        if (point.x > 99.5 && point.y > 50.5 && point.y < 99.5)
            keptEdgeInCutter = true;
    }
    CHECK(keptTopEdge);
    CHECK(!keptEdgeInCutter);
}

/// A fill divided by a line, then cut by a band crossing the line: every
/// region the cut leaves behind keeps the fill it had.
TEST(cutting_across_a_dividing_line_keeps_the_fill_on_both_sides)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(-500, 1000, 2500, 1000);
    CHECK(ShapeMerger::merge(*target, *line));

    // A slanted band from the top left to the bottom right, crossing the line.
    std::unique_ptr<Shape> band(new Shape(nullptr));
    fla::SolidColor* fill = new fla::SolidColor(band.get());
    fill->color[2] = 255;
    fill->color[3] = 255;
    band->fills.push_back(fill);
    band->fillsMap[1] = fill;
    PathParser parser;
    fla::Edge* edge = parser.parse("!-200 200|200 -200|2200 1800|1800 2200|-200 200", band.get());
    CHECK(edge != nullptr);
    if (!edge)
        return;
    edge->fillStyle1 = 1;
    band->edges.push_back(edge);

    CHECK(ShapeMerger::subtract(*target, *band));

    // Above the line, clear of the band on either side.
    CHECK(colorAt(*target, Point(5.0, 40.0)) == kRed);
    CHECK(colorAt(*target, Point(90.0, 20.0)) == kRed);
    // Below the line, both sides.
    CHECK(colorAt(*target, Point(10.0, 90.0)) == kRed);
    CHECK(colorAt(*target, Point(95.0, 60.0)) == kRed);
    // In the band: gone.
    CHECK(colorAt(*target, Point(50.0, 50.0)) == -1);
}

/// A click with the line tool that never moved makes a line of no length.
/// Merging it must not disturb anything, and cutting afterwards must still
/// leave every other region its fill.
TEST(a_zero_length_line_merges_harmlessly)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(-500, 1000, 2500, 1000);
    CHECK(ShapeMerger::merge(*target, *line));

    std::unique_ptr<Shape> dot = strokedLine(3000, 3000, 3000, 3000);
    ShapeMerger::merge(*target, *dot);

    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kRed);

    std::unique_ptr<Shape> band(new Shape(nullptr));
    fla::SolidColor* fill = new fla::SolidColor(band.get());
    fill->color[2] = 255;
    fill->color[3] = 255;
    band->fills.push_back(fill);
    band->fillsMap[1] = fill;
    PathParser parser;
    fla::Edge* edge = parser.parse("!-200 200|200 -200|2200 1800|1800 2200|-200 200", band.get());
    CHECK(edge != nullptr);
    if (!edge)
        return;
    edge->fillStyle1 = 1;
    band->edges.push_back(edge);

    CHECK(ShapeMerger::subtract(*target, *band));
    CHECK(colorAt(*target, Point(5.0, 40.0)) == kRed);
    CHECK(colorAt(*target, Point(90.0, 20.0)) == kRed);
    CHECK(colorAt(*target, Point(10.0, 90.0)) == kRed);
    CHECK(colorAt(*target, Point(95.0, 60.0)) == kRed);
    CHECK(colorAt(*target, Point(50.0, 50.0)) == -1);
}

namespace {

/// A filled oval of four cubics, clockwise on screen so the fill sits on
/// fillStyle1, in pixels.
std::unique_ptr<Shape> filledOval(double cx, double cy, double rx, double ry,
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

    const double k = 0.5522847498;
    fla::Edge* edge = new fla::Edge(shape.get());
    edge->fillStyle1 = 1;
    fla::Path* path = new fla::Path(edge);
    using Cmd = fla::PathSegment::Command;
    path->segments.push_back(new fla::PathSegment(Cmd::Move, {Point(cx, cy - ry)}, path));
    path->segments.push_back(new fla::PathSegment(Cmd::Cubic,
        {Point(cx + k * rx, cy - ry), Point(cx + rx, cy - k * ry), Point(cx + rx, cy)}, path));
    path->segments.push_back(new fla::PathSegment(Cmd::Cubic,
        {Point(cx + rx, cy + k * ry), Point(cx + k * rx, cy + ry), Point(cx, cy + ry)}, path));
    path->segments.push_back(new fla::PathSegment(Cmd::Cubic,
        {Point(cx - k * rx, cy + ry), Point(cx - rx, cy + k * ry), Point(cx - rx, cy)}, path));
    path->segments.push_back(new fla::PathSegment(Cmd::Cubic,
        {Point(cx - rx, cy - k * ry), Point(cx - k * rx, cy - ry), Point(cx, cy - ry)}, path));
    edge->paths.push_back(path);
    shape->edges.push_back(edge);
    return shape;
}

} // namespace

/// An oval drawn over a rectangle: the oval keeps its own color inside, the
/// rectangle keeps its color outside, right up to the curve.
TEST(an_oval_drawn_over_a_rectangle_keeps_its_fill)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> oval = filledOval(50.0, 50.0, 40.0, 40.0, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *oval));

    CHECK(colorAt(*target, Point(50.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(50.0, 10.5)) == kBlue);
    CHECK(colorAt(*target, Point(5.0, 5.0)) == kRed);
    CHECK(colorAt(*target, Point(50.0, 8.0)) == kRed);
    CHECK(colorAt(*target, Point(95.0, 50.0)) == kRed);
}

/// The same with the oval cut out: a hole, exactly the oval's shape.
TEST(an_oval_cut_out_of_a_rectangle_leaves_a_round_hole)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> oval = filledOval(50.0, 50.0, 40.0, 40.0, 0, 0, 255);

    CHECK(ShapeMerger::subtract(*target, *oval));

    CHECK(colorAt(*target, Point(50.0, 50.0)) == -1);
    CHECK(colorAt(*target, Point(50.0, 10.5)) == -1);
    CHECK(colorAt(*target, Point(5.0, 5.0)) == kRed);
    CHECK(colorAt(*target, Point(50.0, 8.0)) == kRed);
}

/// A small oval, where the sampled outline's sag is larger than the step a
/// face's interior point takes off its boundary.
TEST(a_small_oval_drawn_over_a_rectangle_keeps_its_fill)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> oval = filledOval(50.0, 50.0, 6.0, 6.0, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *oval));

    CHECK(colorAt(*target, Point(50.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(50.0, 45.0)) == kBlue);
    CHECK(colorAt(*target, Point(50.0, 43.0)) == kRed);
}

/// An oval straddling the rectangle's edge, so the arrangement cuts the
/// oval's curves where they cross it. Every piece keeps the right fill, right
/// up to the curve on both sides of the crossing.
TEST(an_oval_across_a_rectangle_edge_keeps_its_fill_on_both_sides)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> oval = filledOval(100.0, 50.0, 40.0, 40.0, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *oval));

    CHECK(colorAt(*target, Point(80.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(120.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(61.0, 50.0)) == kBlue);
    CHECK(colorAt(*target, Point(59.0, 50.0)) == kRed);
    CHECK(colorAt(*target, Point(99.0, 11.0)) == kBlue);
    CHECK(colorAt(*target, Point(99.0, 9.0)) == kRed);
    CHECK(colorAt(*target, Point(101.0, 9.0)) == -1);
    CHECK(colorAt(*target, Point(5.0, 5.0)) == kRed);

    // Cut out instead: the hole follows the curve on both sides too.
    std::unique_ptr<Shape> again = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    CHECK(ShapeMerger::subtract(*again, *oval));
    CHECK(colorAt(*again, Point(80.0, 50.0)) == -1);
    CHECK(colorAt(*again, Point(61.0, 50.0)) == -1);
    CHECK(colorAt(*again, Point(59.0, 50.0)) == kRed);
    CHECK(colorAt(*again, Point(99.0, 9.0)) == kRed);
}

/// Many ovals of many sizes over a rectangle, each checked at its center and
/// just inside its top: the sampled outline's sag must never flip a face.
TEST(ovals_of_every_size_keep_their_fill_over_a_rectangle)
{
    for (double r = 3.0; r <= 60.0; r += 4.5)
    {
        std::unique_ptr<Shape> target = filledRectangle(0, 0, 4000, 4000, 255, 0, 0);
        std::unique_ptr<Shape> oval = filledOval(100.0, 100.0, r, r * 0.7, 0, 0, 255);
        CHECK(ShapeMerger::merge(*target, *oval));
        CHECK(colorAt(*target, Point(100.0, 100.0)) == kBlue);
        CHECK(colorAt(*target, Point(100.0, 100.0 - r * 0.7 + 0.3)) == kBlue);
        CHECK(colorAt(*target, Point(100.0, 100.0 - r * 0.7 - 0.3)) == kRed);

        // Straddling the edge at x = 200.
        std::unique_ptr<Shape> edge = filledRectangle(0, 0, 4000, 4000, 255, 0, 0);
        std::unique_ptr<Shape> across = filledOval(200.0, 100.0, r, r * 0.7, 0, 0, 255);
        CHECK(ShapeMerger::merge(*edge, *across));
        CHECK(colorAt(*edge, Point(200.0 - r * 0.5, 100.0)) == kBlue);
        CHECK(colorAt(*edge, Point(200.0 + r * 0.5, 100.0)) == kBlue);
        CHECK(colorAt(*edge, Point(200.0 - r - 0.3, 100.0)) == kRed);
    }
}

/// A stroked rectangle drawn with one edge exactly along an unstroked fill's
/// edge: the shared edge keeps the newer drawing's stroke.
TEST(a_stroke_drawn_along_a_fill_edge_keeps_its_stroke)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = strokedRectangle(2000, 0, 4000, 2000, 0, 0, 255);

    CHECK(ShapeMerger::merge(*target, *addition));

    bool sharedEdgeStroked = false;
    for (const Point& point : strokedMidpoints(*target))
    {
        if (std::fabs(point.x - 100.0) < 0.5 && point.y > 0.0 && point.y < 100.0)
            sharedEdgeStroked = true;
    }
    CHECK(sharedEdgeStroked);
    CHECK(colorAt(*target, Point(50.0, 50.0)) == kRed);
    CHECK(colorAt(*target, Point(150.0, 50.0)) == kBlue);
}

/// A line drawn across a fill and then taken away again: the outline it cut
/// in two is one edge again, not two pieces with a seam vertex.
TEST(pieces_of_one_edge_are_joined_back_when_the_cut_between_them_goes)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(1000, -400, 1000, 2400);
    CHECK(ShapeMerger::merge(*target, *line));
    CHECK(target->edges.size() > 4);

    // The same line cut out again, which removes only its stroke: the fill
    // stays and the outline no longer has anything meeting it at the top or
    // bottom.
    CHECK(ShapeMerger::eraseStrokes(*target, *line));
    size_t stroked = 0;
    for (const fla::Edge* edge : target->edges)
        stroked += edge->strokeStyle != -1 ? 1 : 0;
    CHECK(stroked == 0);
    // Four sides again.
    CHECK(target->edges.size() == 4);
    CHECK(colorAt(*target, Point(25.0, 50.0)) == kRed);
    CHECK(colorAt(*target, Point(75.0, 50.0)) == kRed);
}

TEST(combine_objects_union_intersect_punch_and_crop)
{
    // A red square from 0 to 100 pixels and a blue one over it from 50 to 150.
    const auto pair = []() {
        std::vector<std::unique_ptr<Shape>> shapes;
        shapes.push_back(filledRectangle(0, 0, 2000, 2000, 255, 0, 0));
        shapes.push_back(filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255));
        return shapes;
    };
    const auto raw = [](std::vector<std::unique_ptr<Shape>>& shapes) {
        std::vector<Shape*> list;
        for (auto& shape : shapes)
            list.push_back(shape.get());
        return list;
    };
    {
        auto shapes = pair();
        fla::combine::Result result;
        CHECK(fla::combine::apply(fla::combine::Operation::Union, raw(shapes), result));
        std::unique_ptr<Shape> merged(result.replacement);
        CHECK(merged && merged->isDrawingObject && result.removed.size() == 2);
        CHECK(colorAt(*merged, Point(25, 25)) == kRed && colorAt(*merged, Point(75, 75)) == kBlue &&
            colorAt(*merged, Point(125, 125)) == kBlue);
    }
    {
        auto shapes = pair();
        fla::combine::Result result;
        CHECK(fla::combine::apply(fla::combine::Operation::Intersect, raw(shapes), result));
        std::unique_ptr<Shape> shared(result.replacement);
        CHECK(colorAt(*shared, Point(75, 75)) == kBlue);
        CHECK(colorAt(*shared, Point(25, 25)) == -1 && colorAt(*shared, Point(125, 125)) == -1);
    }
    {
        auto shapes = pair();
        fla::combine::Result result;
        CHECK(fla::combine::apply(fla::combine::Operation::Punch, raw(shapes), result));
        CHECK(result.removed.size() == 1 && result.removed[0] == shapes[1].get() && result.rewritten.size() == 1);
        std::unique_ptr<Shape> punched(result.rewritten.front().second);
        CHECK(colorAt(*punched, Point(25, 25)) == kRed && colorAt(*punched, Point(75, 75)) == -1);
    }
    {
        auto shapes = pair();
        // The top one moved 50 pixels up and left by its transform: it now
        // covers the red square exactly, so a crop keeps all of it.
        shapes[1]->transform.tx = -50.0;
        shapes[1]->transform.ty = -50.0;
        fla::combine::Result result;
        CHECK(fla::combine::apply(fla::combine::Operation::Crop, raw(shapes), result));
        std::unique_ptr<Shape> cropped(result.rewritten.front().second);
        CHECK(colorAt(*cropped, Point(25, 25)) == kRed && colorAt(*cropped, Point(75, 75)) == kRed);
        CHECK(colorAt(*cropped, Point(125, 125)) == -1);
    }
}

// Undoing and redoing a merge rebuilds every edge of the shape, freeing the
// ones an edge edit was made with; the edit finds its edge again by place.
TEST(an_edge_edit_survives_the_merge_under_it_being_undone_and_redone)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::MergeShapeCommand(target.get(), *addition, "Merge")));
    stack.breakMergeChain();

    fla::Edge* edge = target->edges.front();
    CHECK(!edge->paths.empty());
    const fla::EditablePath before = fla::EditablePath::fromPath(*edge->paths.front());
    fla::EditablePath after = before;
    CHECK(!after.anchors.empty());
    after.anchors.front().translate(7.0, 0.0);
    const double movedX = after.anchors.front().position.x;
    stack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(edge, before, after, "Move Anchor")));

    for (int i = 0; i < 2; ++i)
    {
        stack.undo();
        stack.undo();
        stack.redo();
        stack.redo();
        const fla::EditablePath now = fla::EditablePath::fromPath(*target->edges.front()->paths.front());
        CHECK(!now.anchors.empty() && std::fabs(now.anchors.front().position.x - movedX) < 1e-6);
    }
}

namespace {

/// What a shape paints, region by region: for each fill, how many faces of
/// its arrangement carry it and their total area. Bounded faces painting
/// nothing are counted under -1.
std::map<int, std::pair<int, double>> regionsOf(const Shape& shape)
{
    const std::vector<ShapeCurve> curves = fla::shapeCurves(shape);
    fla::PlanarMap map;
    for (size_t i = 0; i < curves.size(); ++i)
        map.addCurve(curves[i].curve, static_cast<int>(i));
    map.build();
    fla::attributeFillsFromSource(map, curves);

    std::map<int, std::pair<int, double>> regions;
    for (size_t face = 0; face < map.faces().size(); ++face)
    {
        if (map.faces()[face].unbounded)
            continue;
        auto& entry = regions[map.faceFill(static_cast<int>(face))];
        ++entry.first;
        entry.second += map.faces()[face].area;
    }
    return regions;
}

/// The fill index a shape gives a color, or -1.
int fillOfColor(const Shape& shape, int color)
{
    for (const auto& [index, fill] : shape.fillsMap)
    {
        if (!fill || fill->type() != fla::FillStyle::Type::SolidColor)
            continue;
        const fla::SolidColor* solid = static_cast<const fla::SolidColor*>(fill);
        if (((solid->color[0] << 16) | (solid->color[1] << 8) | solid->color[2]) == color)
            return index;
    }
    return -1;
}

/// A shape of one fill bounded by the given curves, each its own edge, with
/// the fill on the left of each (fillStyle1).
std::unique_ptr<Shape> filledCurves(const std::vector<fla::Curve>& curves,
    uint8_t red, uint8_t green, uint8_t blue, bool fillOnLeft = true)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));
    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    fill->color[0] = red;
    fill->color[1] = green;
    fill->color[2] = blue;
    fill->color[3] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;

    using Cmd = fla::PathSegment::Command;
    for (const fla::Curve& curve : curves)
    {
        fla::Edge* edge = new fla::Edge(shape.get());
        (fillOnLeft ? edge->fillStyle1 : edge->fillStyle0) = 1;
        fla::Path* path = new fla::Path(edge);
        path->segments.push_back(new fla::PathSegment(Cmd::Move, {curve.start()}, path));
        if (curve.isLine())
            path->segments.push_back(new fla::PathSegment(Cmd::Line, {curve.end()}, path));
        else
            path->segments.push_back(new fla::PathSegment(Cmd::Cubic,
                {curve.controlPoint(1), curve.controlPoint(2), curve.end()}, path));
        edge->paths.push_back(path);
        shape->edges.push_back(edge);
    }
    return shape;
}

} // namespace

TEST(merges_paint_the_regions_and_areas_they_should)
{
    // Two overlapping squares: the newer one whole, the older one less the
    // overlap.
    {
        std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
        std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
        CHECK(ShapeMerger::merge(*target, *addition));
        const auto regions = regionsOf(*target);
        const int red = fillOfColor(*target, kRed);
        const int blue = fillOfColor(*target, kBlue);
        CHECK(regions.count(-1) == 0);
        CHECK(regions.at(red).first == 1);
        CHECK(std::fabs(regions.at(red).second - 7500.0) < 1.0e-6);
        CHECK(regions.at(blue).first == 1);
        CHECK(std::fabs(regions.at(blue).second - 10000.0) < 1.0e-6);
    }

    // An oval inside a square: the square becomes a frame with an oval hole
    // the oval fills exactly.
    {
        std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
        std::unique_ptr<Shape> oval = filledOval(50.0, 50.0, 30.0, 20.0, 0, 0, 255);
        CHECK(ShapeMerger::merge(*target, *oval));
        const auto regions = regionsOf(*target);
        const int red = fillOfColor(*target, kRed);
        const int blue = fillOfColor(*target, kBlue);
        CHECK(regions.at(blue).first == 1);
        CHECK(regions.at(red).first == 1);
        const double ovalArea = regions.at(blue).second;
        CHECK(std::fabs(ovalArea - 3.14159265358979 * 600.0) < 2.0);
        CHECK(std::fabs(regions.at(red).second + ovalArea - 10000.0) < 1.0e-6);
    }

    // A brush band across a square: the square split in two either side of it.
    {
        std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
        std::unique_ptr<Shape> band = brushBand(-20.0, 50.0, 120.0, 50.0, 10.0, 12);
        CHECK(ShapeMerger::merge(*target, *band));
        const auto regions = regionsOf(*target);
        const int red = fillOfColor(*target, kRed);
        CHECK(regions.at(red).first == 2);
        CHECK(std::fabs(regions.at(red).second - 9000.0) < 1.0);
        CHECK(regions.count(-1) == 0);
    }
}

TEST(two_curves_cut_from_one_are_joined_back_into_it)
{
    const fla::Curve whole = fla::Curve::cubic(Point(0.0, 0.0), Point(30.0, -60.0),
        Point(80.0, 40.0), Point(120.0, 10.0));
    fla::Curve before;
    fla::Curve after;
    whole.split(0.37, before, after);

    fla::Curve joined;
    CHECK(fla::joinCurves({before, after}, fla::PlanarMap::kTwip * 0.5, joined));
    for (int i = 0; i < 4; ++i)
    {
        CHECK(std::fabs(joined.controlPoint(i).x - whole.controlPoint(i).x) < 1.0e-3);
        CHECK(std::fabs(joined.controlPoint(i).y - whole.controlPoint(i).y) < 1.0e-3);
    }

    // Three pieces of it, one barely longer than a twip.
    fla::Curve middle;
    fla::Curve rest;
    after.split(0.002, middle, rest);
    CHECK(fla::joinCurves({before, middle, rest}, fla::PlanarMap::kTwip * 0.5, joined));

    // An oval's quarter cut near one end, the cut welded onto the twip grid
    // a hundredth of a pixel off the curve: still one curve to the eye.
    CHECK(fla::joinCurves({
        fla::Curve::cubic(Point(50.0, 20.0), Point(51.6934, 20.0), Point(53.3622, 20.0789), Point(55.0, 20.25)),
        fla::Curve::cubic(Point(55.0, 20.25), Point(74.7317, 22.0776), Point(90.0, 34.7015), Point(90.0, 50.0))},
        fla::PlanarMap::kTwip * 0.5, joined));

    // Two quarters of a large circle meet smoothly, but no one cubic follows
    // half a circle closely: they stay apart.
    const double k = 0.5522847498;
    const double r = 50.0;
    const fla::Curve first = fla::Curve::cubic(Point(0.0, -r), Point(k * r, -r), Point(r, -k * r), Point(r, 0.0));
    const fla::Curve second = fla::Curve::cubic(Point(r, 0.0), Point(r, k * r), Point(k * r, r), Point(0.0, r));
    CHECK(!fla::joinCurves({first, second}, fla::PlanarMap::kTwip * 0.5, joined));

    // A corner is not smoothed over.
    CHECK(!fla::joinCurves({fla::Curve::line(Point(0.0, 0.0), Point(10.0, 0.0)),
        fla::Curve::cubic(Point(10.0, 0.0), Point(10.0, 5.0), Point(10.0, 10.0), Point(10.0, 20.0))},
        fla::PlanarMap::kTwip * 0.5, joined));
}

TEST(a_curve_cut_by_an_earlier_merge_is_joined_when_the_cut_goes)
{
    // A line across an oval cuts its top and bottom curves; once saved the
    // pieces are separate edges. Taking the line away again leaves each pair
    // meeting smoothly with nothing else there, and they become one curve.
    std::unique_ptr<Shape> target = filledOval(50.0, 50.0, 40.0, 30.0, 255, 0, 0);
    std::unique_ptr<Shape> line = strokedLine(1100, -400, 1100, 2400);
    CHECK(ShapeMerger::merge(*target, *line));
    CHECK(target->edges.size() > 4);

    CHECK(ShapeMerger::eraseStrokes(*target, *line));
    CHECK(target->edges.size() == 4);
    for (const fla::Edge* edge : target->edges)
        CHECK(edge->strokeStyle == -1);
    CHECK(colorAt(*target, Point(50.0, 50.0)) == kRed);
    CHECK(colorAt(*target, Point(50.0, 21.0)) == kRed);
    CHECK(colorAt(*target, Point(50.0, 19.0)) == -1);
    const auto regions = regionsOf(*target);
    CHECK(regions.size() == 1);
    CHECK(std::fabs(regions.begin()->second.second - 3.14159265358979 * 1200.0) < 2.0);
}

TEST(merging_along_a_nearly_shared_edge_leaves_no_gap)
{
    // A red square, and a blue shape drawn above it whose bottom edge was
    // meant to run along the square's top but bows up by three hundredths
    // of a pixel. The hairline between them is too thin to be anything but
    // rounding, and is given to the side it borders most rather than left
    // as an unpainted gap.
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledCurves({
        fla::Curve::cubic(Point(0.0, 0.0), Point(33.0, -0.04), Point(66.0, -0.04), Point(100.0, 0.0)),
        fla::Curve::line(Point(100.0, 0.0), Point(100.0, -50.0)),
        fla::Curve::line(Point(100.0, -50.0), Point(0.0, -50.0)),
        fla::Curve::line(Point(0.0, -50.0), Point(0.0, 0.0))}, 0, 0, 255, false);
    CHECK(colorAt(*addition, Point(50.0, -25.0)) == kBlue);

    CHECK(ShapeMerger::merge(*target, *addition));
    const auto regions = regionsOf(*target);
    CHECK(regions.count(-1) == 0);
    CHECK(regions.at(fillOfColor(*target, kRed)).first == 1);
    CHECK(regions.at(fillOfColor(*target, kBlue)).first == 1);
    CHECK(colorAt(*target, Point(50.0, -25.0)) == kBlue);
    CHECK(colorAt(*target, Point(50.0, 25.0)) == kRed);
}

namespace {

/// A brush stroke's outline: a band \a width wide round the bent centerline
/// from \a a through \a bend to \a b, with round caps. With \a curved its
/// sides are cubic curves through points along it, as the brush fits them;
/// otherwise the polyline itself.
std::unique_ptr<Shape> curvedBand(Point a, Point bend, Point b, double width, int steps, bool curved)
{
    const auto along = [&](double t) {
        const double u = 1.0 - t;
        return Point(u * u * a.x + 2.0 * u * t * bend.x + t * t * b.x,
            u * u * a.y + 2.0 * u * t * bend.y + t * t * b.y);
    };
    const auto normal = [&](double t) {
        const double u = 1.0 - t;
        const double dx = 2.0 * u * (bend.x - a.x) + 2.0 * t * (b.x - bend.x);
        const double dy = 2.0 * u * (bend.y - a.y) + 2.0 * t * (b.y - bend.y);
        const double length = std::hypot(dx, dy);
        return Point(-dy / length, dx / length);
    };

    const double half = width / 2.0;
    std::vector<Point> ring;
    for (int i = 0; i <= steps; ++i)
    {
        const double t = static_cast<double>(i) / steps;
        const Point p = along(t);
        const Point n = normal(t);
        ring.push_back(Point(p.x + n.x * half, p.y + n.y * half));
    }
    const double pi = 3.14159265358979323846;
    const Point endNormal = normal(1.0);
    const double endAngle = std::atan2(endNormal.y, endNormal.x);
    for (int i = 1; i < 8; ++i)
    {
        const double angle = endAngle - pi * i / 8.0;
        ring.push_back(Point(b.x + std::cos(angle) * half, b.y + std::sin(angle) * half));
    }
    for (int i = steps; i >= 0; --i)
    {
        const double t = static_cast<double>(i) / steps;
        const Point p = along(t);
        const Point n = normal(t);
        ring.push_back(Point(p.x - n.x * half, p.y - n.y * half));
    }
    const Point startNormal = normal(0.0);
    const double startAngle = std::atan2(startNormal.y, startNormal.x);
    for (int i = 1; i < 8; ++i)
    {
        const double angle = startAngle + pi - pi * i / 8.0;
        ring.push_back(Point(a.x + std::cos(angle) * half, a.y + std::sin(angle) * half));
    }
    // On the twip grid, without points closer together than a brush's
    // fitted outline puts them.
    std::vector<Point> spaced;
    for (const Point& point : ring)
    {
        const Point snapped(std::round(point.x * 20.0) / 20.0, std::round(point.y * 20.0) / 20.0);
        if (spaced.empty() || std::hypot(snapped.x - spaced.back().x, snapped.y - spaced.back().y) >= width / 8.0)
            spaced.push_back(snapped);
    }
    ring = spaced;

    double area = 0.0;
    for (size_t i = 0; i < ring.size(); ++i)
    {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % ring.size()];
        area += p.x * q.y - q.x * p.y;
    }

    std::vector<fla::Curve> curves;
    const size_t count = ring.size();
    for (size_t i = 0; i < count; ++i)
    {
        const Point& p0 = ring[(i + count - 1) % count];
        const Point& p1 = ring[i];
        const Point& p2 = ring[(i + 1) % count];
        const Point& p3 = ring[(i + 2) % count];
        if (!curved)
        {
            curves.push_back(fla::Curve::line(p1, p2));
            continue;
        }
        // Catmull-Rom through the points, as cubic pieces.
        curves.push_back(fla::Curve::cubic(p1,
            Point(p1.x + (p2.x - p0.x) / 6.0, p1.y + (p2.y - p0.y) / 6.0),
            Point(p2.x - (p3.x - p1.x) / 6.0, p2.y - (p3.y - p1.y) / 6.0), p2));
    }
    return filledCurves(curves, 0, 0, 255, area >= 0.0);
}

/// The parts of the strokes, sampled on a grid \a step apart, that \a result
/// leaves unpainted: how many points lie well inside some stroke, and how
/// many of those the result paints nothing at.
std::pair<int, int> lostCoverage(const Shape& result, const std::vector<const Shape*>& strokes, double step,
    const Shape* except = nullptr)
{
    fla::PlanarMap exceptMap;
    if (except)
    {
        const std::vector<ShapeCurve> curves = fla::shapeCurves(*except);
        for (size_t i = 0; i < curves.size(); ++i)
            exceptMap.addCurve(curves[i].curve, static_cast<int>(i));
        exceptMap.build();
        fla::attributeFillsFromSource(exceptMap, curves);
    }
    const auto arrangeShape = [](const Shape& shape, fla::PlanarMap& map) {
        const std::vector<ShapeCurve> curves = fla::shapeCurves(shape);
        for (size_t i = 0; i < curves.size(); ++i)
            map.addCurve(curves[i].curve, static_cast<int>(i));
        map.build();
        fla::attributeFillsFromSource(map, curves);
    };

    // What the result paints, read the way it is drawn: a fill covers a point
    // when the edges with it on one side only cross a ray from there an odd
    // number of times.
    const std::vector<ShapeCurve> resultCurves = fla::shapeCurves(result);
    const auto painted = [&](const Point& point) {
        std::map<int, int> crossings;
        for (const ShapeCurve& curve : resultCurves)
        {
            if (curve.fillStyle0 == curve.fillStyle1)
                continue;
            const int count = curve.curve.horizontalRayCrossings(point);
            if (curve.fillStyle0 != -1)
                crossings[curve.fillStyle0] += count;
            if (curve.fillStyle1 != -1)
                crossings[curve.fillStyle1] += count;
        }
        for (const auto& [fill, count] : crossings)
        {
            if (count % 2)
                return true;
        }
        return false;
    };

    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    std::vector<std::unique_ptr<fla::PlanarMap>> maps;
    for (const Shape* stroke : strokes)
    {
        maps.emplace_back(new fla::PlanarMap());
        arrangeShape(*stroke, *maps.back());
        for (const ShapeCurve& curve : fla::shapeCurves(*stroke))
        {
            double l, t, r, b;
            curve.curve.controlBounds(l, t, r, b);
            left = std::min(left, l);
            top = std::min(top, t);
            right = std::max(right, r);
            bottom = std::max(bottom, b);
        }
    }

    int inside = 0;
    int lost = 0;
    const double margin = 0.2;
    for (double y = top + step * 0.37; y < bottom; y += step)
    {
        for (double x = left + step * 0.61; x < right; x += step)
        {
            bool within = false;
            for (const auto& map : maps)
            {
                if (map->fillAt(Point(x, y)) != -1 && map->fillAt(Point(x - margin, y)) != -1 &&
                    map->fillAt(Point(x + margin, y)) != -1 && map->fillAt(Point(x, y - margin)) != -1 &&
                    map->fillAt(Point(x, y + margin)) != -1)
                {
                    within = true;
                    break;
                }
            }
            if (!within || (except && exceptMap.fillAt(Point(x, y)) != -1))
                continue;
            ++inside;
            if (!painted(Point(x, y)))
                ++lost;
        }
    }
    return {inside, lost};
}

/// Draws the strokes one after another with Object Drawing off, as the
/// drawing tools do: each cuts its hole in what it lands on, then is let go
/// where it was drawn and merges in. \a uncut does that as the app does,
/// merging into the artwork as it was before the cut; without it, the
/// drawing merges into the hole it cut.
std::unique_ptr<Shape> drawnInTurn(const std::vector<std::unique_ptr<Shape>>& strokes, bool uncut)
{
    std::unique_ptr<Shape> art(fla::cloneShape(*strokes.front(), nullptr));
    fla::CommandStack stack;
    for (size_t i = 1; i < strokes.size(); ++i)
    {
        const std::unique_ptr<Shape> before(fla::cloneShape(*art, nullptr));
        stack.push(fla::CommandPtr(new fla::SubtractShapeCommand(art.get(), *strokes[i], "Brush")));
        stack.push(fla::CommandPtr(new fla::MergeShapeCommand(art.get(), *strokes[i], "Merge Shape",
            fla::MergeOptions(), uncut ? before.get() : nullptr)));
    }
    return art;
}

/// Sixteen strokes crossing in a small area, as a seed picks them, each
/// repeated \a copies times round (50, 50) as radial symmetry draws it.
std::vector<std::unique_ptr<Shape>> crossingStrokes(unsigned seed, bool curved, int copies = 1)
{
    uint32_t state = seed * 2654435761u;
    const auto random = [&]() {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) / 16777216.0;
    };
    const double pi = 3.14159265358979323846;
    std::vector<std::unique_ptr<Shape>> strokes;
    for (int i = 0; i < 16; ++i)
    {
        const double angle = random() * pi;
        const double reach = 25.0 + random() * 20.0;
        const Point center(50.0 + (random() - 0.5) * 24.0, 50.0 + (random() - 0.5) * 24.0);
        const double sway = (random() - 0.5) * 20.0;
        const double width = 4.0 + random() * 6.0;

        // The stroke and its copies are one drawing, as the brush makes them.
        std::unique_ptr<Shape> drawing;
        for (int copy = 0; copy < copies; ++copy)
        {
            const double turn = 2.0 * pi * copy / copies;
            const auto turned = [&](double x, double y) {
                return Point(50.0 + (x - 50.0) * std::cos(turn) - (y - 50.0) * std::sin(turn),
                    50.0 + (x - 50.0) * std::sin(turn) + (y - 50.0) * std::cos(turn));
            };
            const Point dir(std::cos(angle), std::sin(angle));
            std::unique_ptr<Shape> stroke = curvedBand(
                turned(center.x - dir.x * reach, center.y - dir.y * reach),
                turned(center.x - dir.y * sway, center.y + dir.x * sway),
                turned(center.x + dir.x * reach, center.y + dir.y * reach), width, 12, curved);
            if (!drawing)
                drawing = std::move(stroke);
            else
                ShapeMerger::merge(*drawing, *stroke);
        }
        strokes.push_back(std::move(drawing));
    }
    return strokes;
}

/// What drawing the strokes in turn leaves unpainted that they covered.
int lostDrawingInTurn(const std::vector<std::unique_ptr<Shape>>& strokes, bool uncut, double step)
{
    std::vector<const Shape*> raw;
    for (const auto& stroke : strokes)
        raw.push_back(stroke.get());
    const std::unique_ptr<Shape> art = drawnInTurn(strokes, uncut);
    const auto [inside, lost] = lostCoverage(*art, raw, step);
    CHECK(inside > 0);
    return lost;
}

} // namespace

TEST(sixteen_crossing_strokes_let_go_where_drawn_lose_nothing)
{
    // Same-color brush strokes crossing in a small area, Object Drawing off.
    // Each drawing's hole had an outline a welded, refitted copy of the
    // drawing's own; merging the drawing into it put the two in one
    // arrangement, too nearly one curve to cut cleanly, and whole stretches
    // of strokes came out empty. Let go untouched, it merges into the
    // artwork as it was before the cut.
    for (const bool curved : {false, true})
    {
        for (unsigned seed = 1; seed <= 4; ++seed)
            CHECK(lostDrawingInTurn(crossingStrokes(seed, curved), true, 1.0) == 0);
    }
}

TEST(radial_copies_crossing_let_go_where_drawn_lose_nothing)
{
    for (const bool curved : {false, true})
        CHECK(lostDrawingInTurn(crossingStrokes(16, curved, 8), true, 1.0) == 0);
}

TEST(merging_into_the_hole_a_straight_sided_drawing_cut_loses_nothing)
{
    // A drawing moved away and back merges into its own hole after all. The
    // arrangement copes with the near-coincident outlines where they are
    // straight: two cuts welding to one vertex leave no zero-width loop with
    // the face on both sides, and an outline passing within a twip of a
    // vertex is cut there rather than crossed by the pieces welded onto it.
    for (unsigned seed = 1; seed <= 6; ++seed)
        CHECK(lostDrawingInTurn(crossingStrokes(seed, false), false, 1.0) == 0);
}

TEST(crossing_strokes_merged_in_turn_keep_every_fill)
{
    // Randomized crossing strokes, merged one after another. Where rounding
    // leaves an edge whose sides cannot say which fill is on them, the
    // others bordering the face outvote it.
    for (const bool curved : {false, true})
    {
        // Curved, the seeds an edge read from the wrong side or a vertex
        // welded across an outline used to empty.
        for (const unsigned seed : curved ? std::vector<unsigned>{6, 8, 9, 11} : std::vector<unsigned>{5, 6, 7, 8, 9, 10, 11, 12})
        {
            std::vector<std::unique_ptr<Shape>> strokes = crossingStrokes(seed, curved);
            std::vector<const Shape*> raw;
            for (const auto& stroke : strokes)
                raw.push_back(stroke.get());
            std::unique_ptr<Shape> art(fla::cloneShape(*strokes.front(), nullptr));
            for (size_t i = 1; i < strokes.size(); ++i)
                ShapeMerger::merge(*art, *strokes[i]);
            CHECK(lostCoverage(*art, raw, 1.0).second == 0);
        }
    }
}

TEST(a_merge_from_the_uncut_artwork_undoes_to_the_cut)
{
    std::unique_ptr<Shape> target = filledRectangle(0, 0, 2000, 2000, 255, 0, 0);
    std::unique_ptr<Shape> addition = filledRectangle(1000, 1000, 3000, 3000, 0, 0, 255);
    const std::unique_ptr<Shape> uncut(fla::cloneShape(*target, nullptr));

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::SubtractShapeCommand(target.get(), *addition, "Brush")));
    CHECK(colorAt(*target, Point(75.0, 75.0)) == -1);

    stack.push(fla::CommandPtr(new fla::MergeShapeCommand(target.get(), *addition, "Merge Shape",
        fla::MergeOptions(), uncut.get())));
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);
    CHECK(colorAt(*target, Point(125.0, 125.0)) == kBlue);

    // Undo puts back the cut, not the artwork the merge started from.
    stack.undo();
    CHECK(colorAt(*target, Point(75.0, 75.0)) == -1);
    CHECK(colorAt(*target, Point(25.0, 25.0)) == kRed);

    stack.redo();
    CHECK(colorAt(*target, Point(75.0, 75.0)) == kBlue);
}
