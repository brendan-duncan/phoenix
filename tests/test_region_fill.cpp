#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/data/timeline.h"
#include "../src/edit/drawing_style.h"
#include "../src/edit/region_fill.h"
#include "../src/edit/region_split.h"
#include "../src/edit/shape_merge.h"
#include "../src/parser/path_parser.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using fla::Point;
using fla::RegionSplitter;
using fla::Shape;
namespace regionfill = fla::regionfill;

namespace {

/// Line art: strokes only, along each polyline (in pixels), moved by
/// \a dx, \a dy.
Shape* lineArt(fla::DOMElement* parent, const std::vector<std::vector<Point>>& lines, double dx = 0.0, double dy = 0.0)
{
    Shape* shape = new Shape(parent);
    fla::SolidStroke* stroke = new fla::SolidStroke(shape);
    stroke->weight = 1.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;
    PathParser parser;
    for (const std::vector<Point>& line : lines)
    {
        std::string text;
        for (size_t i = 0; i < line.size(); ++i)
        {
            text += i == 0 ? "!" : "|";
            text += std::to_string(static_cast<int>(std::lround((line[i].x + dx) * 20.0))) + " " +
                    std::to_string(static_cast<int>(std::lround((line[i].y + dy) * 20.0)));
        }
        fla::Edge* edge = parser.parse(text.c_str(), shape);
        if (!edge)
            continue;
        edge->fillStyle0 = -1;
        edge->fillStyle1 = -1;
        edge->strokeStyle = 1;
        shape->edges.push_back(edge);
    }
    return shape;
}

/// Three cells in a row, each 100 pixels square: 0..300 by 0..100.
std::vector<std::vector<Point>> threeCells()
{
    return {
        {Point(0, 0), Point(300, 0), Point(300, 100), Point(0, 100), Point(0, 0)},
        {Point(100, 0), Point(100, 100)},
        {Point(200, 0), Point(200, 100)},
    };
}

/// A square 0..size each way and, well away, a small square 300..320.
std::vector<std::vector<Point>> squareAndSpeck(double size)
{
    return {
        {Point(0, 0), Point(size, 0), Point(size, size), Point(0, size), Point(0, 0)},
        {Point(300, 0), Point(320, 0), Point(320, 20), Point(300, 20), Point(300, 0)},
    };
}

fla::DrawingStyle red()
{
    fla::DrawingStyle style;
    style.hasFill = true;
    style.fillColor[0] = 255;
    style.fillColor[1] = 0;
    style.fillColor[2] = 0;
    style.fillColor[3] = 255;
    return style;
}

/// The fill painting the region at \a at, or -1.
int fillAt(const Shape& shape, const Point& at)
{
    RegionSplitter regions(shape);
    const int face = regions.faceAt(at);
    return face < 0 ? -2 : regions.faceFill(face);
}

/// A keyframe at \a index holding \a shape.
fla::Frame* keyframe(fla::Layer* layer, int index, int duration, Shape* shape)
{
    fla::Frame* frame = new fla::Frame(layer);
    frame->index = index;
    frame->duration = duration;
    shape->parent = frame;
    frame->elements.push_back(shape);
    layer->frames.push_back(frame);
    return frame;
}

} // namespace

TEST(region_fill_scribble_takes_every_region_it_passes_over)
{
    std::unique_ptr<Shape> shape(lineArt(nullptr, threeCells()));
    RegionSplitter regions(*shape);
    CHECK(regions.boundedFaces().size() == 3);

    // Across the first two cells, then all three.
    std::vector<int> two = regionfill::facesAlong(regions, {Point(50, 50), Point(150, 60)}, 2.0);
    CHECK(two.size() == 2);
    CHECK(two.size() == 2 && two[0] == regions.faceAt(Point(20, 20)) && two[1] == regions.faceAt(Point(180, 80)));
    // A wiggle back and forth still takes each once.
    std::vector<int> all = regionfill::facesAlong(regions,
        {Point(50, 50), Point(250, 50), Point(40, 40), Point(260, 70)}, 2.0);
    CHECK(all.size() == 3);
    // Outside the art there is nothing to take.
    CHECK(regionfill::facesAlong(regions, {Point(-50, -50), Point(-10, 300)}, 2.0).empty());

    // Painted in one go: the two cells take the fill, the third stays empty.
    fla::DrawingStyle style = red();
    std::unique_ptr<Shape> working(fla::cloneShape(*shape, nullptr));
    RegionSplitter divided(*working);
    const int fill = regionfill::ensureFill(*working, style);
    CHECK(fill >= 1);
    std::unique_ptr<Shape> after(divided.withFacesFill(regionfill::facesAlong(divided, {Point(50, 50), Point(150, 60)}, 2.0), fill));
    CHECK(after != nullptr);
    if (after)
    {
        CHECK(fillAt(*after, Point(50, 50)) == fill);
        CHECK(fillAt(*after, Point(150, 50)) == fill);
        CHECK(fillAt(*after, Point(250, 50)) == -1);
        // The same color again reuses the fill.
        CHECK(regionfill::ensureFill(*after, style) == fill);
    }
}

TEST(region_fill_lasso_takes_the_regions_inside_the_loop)
{
    std::unique_ptr<Shape> shape(lineArt(nullptr, threeCells()));
    RegionSplitter regions(*shape);
    // Round the first two cells only.
    const std::vector<Point> loop = {Point(-10, -10), Point(205, -10), Point(205, 110), Point(-10, 110)};
    std::vector<int> inside = regionfill::facesInside(regions, loop);
    CHECK(inside.size() == 2);
    const int third = regions.faceAt(Point(250, 50));
    for (const int face : inside)
        CHECK(face != third);
    // A loop round nothing, or too few points to be a loop.
    CHECK(regionfill::facesInside(regions, {Point(400, 400), Point(500, 400), Point(450, 500)}).empty());
    CHECK(regionfill::facesInside(regions, {Point(0, 0), Point(300, 100)}).empty());
    CHECK(regionfill::insideLoop(loop, Point(100, 50)));
    CHECK(!regionfill::insideLoop(loop, Point(250, 50)));
}

TEST(region_fill_footprints_measure_a_region)
{
    std::unique_ptr<Shape> shape(lineArt(nullptr, threeCells()));
    RegionSplitter regions(*shape);
    const int middle = regions.faceAt(Point(150, 50));
    CHECK(middle >= 0);
    CHECK(std::fabs(regions.faceArea(middle) - 10000.0) < 1.0);
    const Point center = regions.faceCentroid(middle);
    CHECK(std::fabs(center.x - 150.0) < 0.5 && std::fabs(center.y - 50.0) < 0.5);

    // Placed on the stage moved and doubled.
    const regionfill::Footprint footprint = regionfill::footprintOf(regions, middle, fla::Transform(2, 0, 0, 2, 10, 0));
    CHECK(footprint.valid);
    CHECK(std::fabs(footprint.area - 40000.0) < 4.0);
    CHECK(std::fabs(footprint.centroid.x - 310.0) < 1.0);

    fla::Transform back;
    CHECK(regionfill::inverse(fla::Transform(2, 0, 0, 2, 10, 0), back));
    const Point origin = Point(310, 100).transformed(back);
    CHECK(std::fabs(origin.x - 150.0) < 1.0e-9 && std::fabs(origin.y - 50.0) < 1.0e-9);
    CHECK(!regionfill::inverse(fla::Transform(0, 0, 0, 0, 1, 1), back));

    // The same region a little moved scores well; one far off or much
    // smaller does not count.
    regionfill::Footprint near = footprint;
    near.centroid.x += 20.0;
    regionfill::Footprint far = footprint;
    far.centroid.x += 1000.0;
    regionfill::Footprint tiny = footprint;
    tiny.area /= 10.0;
    CHECK(regionfill::matchScore(footprint, near, false) >= 0.0);
    CHECK(regionfill::matchScore(footprint, near, true) < regionfill::matchScore(footprint, near, false));
    CHECK(regionfill::matchScore(footprint, far, false) < 0.0);
    CHECK(regionfill::matchScore(footprint, tiny, false) < 0.0);
}

TEST(region_fill_across_frames_finds_the_region_by_position)
{
    // Four drawings of a 100 pixel square, drifting right 30 pixels a
    // drawing, and a speck to one side.
    fla::Timeline scene(nullptr);
    fla::Layer* layer = new fla::Layer(&scene);
    scene.layers.push_back(layer);
    for (int i = 0; i < 4; ++i)
        keyframe(layer, i * 2, 2, lineArt(nullptr, squareAndSpeck(100), 30.0 * i));

    CHECK(regionfill::drawingsIn(scene, *layer, 0, 7).size() == 4);
    CHECK(regionfill::drawingsIn(scene, *layer, 3, 4).size() == 2);
    CHECK(regionfill::drawingsIn(scene, *layer, 5, 5).size() == 1 && regionfill::drawingsIn(scene, *layer, 5, 5)[0] == 4);

    fla::DrawingStyle style = red();
    regionfill::AcrossSettings settings;
    settings.style = &style;
    settings.stagePoint = Point(70, 50);
    // Clicked on the second drawing: the point is in the square on the
    // first three drawings (0..100, 30..130, 60..160) but not the fourth
    // (90..190).
    const regionfill::AcrossResult result =
        regionfill::fillAcross(scene, *layer, regionfill::drawingsIn(scene, *layer, 0, 7), 2, settings);
    CHECK(result.fills.size() == 3);
    CHECK(result.missed.size() == 1 && result.missed[0] == 6);
    for (const regionfill::DrawingFill& fill : result.fills)
    {
        CHECK(fill.frame != nullptr && fill.target == fill.frame->elements[0] && fill.after != nullptr);
        if (!fill.after)
            continue;
        const double dx = 30.0 * (fill.frameIndex / 2);
        // The square is painted with the new fill, the speck is not.
        const int painted = fillAt(*fill.after, Point(50 + dx, 50));
        CHECK(painted >= 1 && fill.after->fillsMap.count(painted) == 1);
        CHECK(fillAt(*fill.after, Point(310 + dx, 10)) == -1);
    }
    // The clicked drawing comes first.
    CHECK(!result.fills.empty() && result.fills[0].frameIndex == 2);
}

TEST(region_fill_across_frames_follows_a_moving_region)
{
    // The square moves 60 pixels a drawing: no one point stays inside it,
    // but each drawing's square overlaps the last one's.
    fla::Timeline scene(nullptr);
    fla::Layer* layer = new fla::Layer(&scene);
    scene.layers.push_back(layer);
    for (int i = 0; i < 4; ++i)
        keyframe(layer, i, 1, lineArt(nullptr, squareAndSpeck(100), 60.0 * i, 10.0 * i));

    fla::DrawingStyle style = red();
    regionfill::AcrossSettings settings;
    settings.style = &style;
    settings.stagePoint = Point(20, 50);

    // By position only the first two drawings have the point inside.
    const regionfill::AcrossResult byPosition =
        regionfill::fillAcross(scene, *layer, regionfill::drawingsIn(scene, *layer, 0, 3), 0, settings);
    CHECK(byPosition.fills.size() == 1);
    CHECK(byPosition.missed.size() == 3);

    // Following, every drawing's square is found, and never the speck.
    settings.follow = true;
    const regionfill::AcrossResult followed =
        regionfill::fillAcross(scene, *layer, regionfill::drawingsIn(scene, *layer, 0, 3), 0, settings);
    CHECK(followed.fills.size() == 4);
    CHECK(followed.missed.empty());
    for (const regionfill::DrawingFill& fill : followed.fills)
    {
        if (!fill.after)
            continue;
        const double dx = 60.0 * fill.frameIndex;
        const double dy = 10.0 * fill.frameIndex;
        CHECK(fillAt(*fill.after, Point(50 + dx, 50 + dy)) >= 1);
        CHECK(fillAt(*fill.after, Point(310 + dx, 10 + dy)) == -1);
    }

    // Started from the last drawing it follows the square back the other way.
    settings.stagePoint = Point(230, 80);
    const regionfill::AcrossResult backwards =
        regionfill::fillAcross(scene, *layer, regionfill::drawingsIn(scene, *layer, 0, 3), 3, settings);
    CHECK(backwards.fills.size() == 4 && backwards.missed.empty());
}

TEST(region_fill_follows_only_into_regions_painted_alike)
{
    // The square shrinks and moves right; just below where it was there is
    // a black square, nearer in place and size but painted, as line art
    // drawn as fills is. Following must not jump onto it.
    fla::Timeline scene(nullptr);
    fla::Layer* layer = new fla::Layer(&scene);
    scene.layers.push_back(layer);
    keyframe(layer, 0, 1, lineArt(nullptr, {{Point(0, 0), Point(100, 0), Point(100, 100), Point(0, 100), Point(0, 0)}}));
    Shape* next = lineArt(nullptr, {{Point(100, 10), Point(180, 10), Point(180, 90), Point(100, 90), Point(100, 10)}});
    fla::SolidColor* black = new fla::SolidColor(next);
    next->fills.push_back(black);
    next->fillsMap[1] = black;
    PathParser parser;
    fla::Edge* painted = parser.parse("!0 2040|2000 2040|2000 4040|0 4040|0 2040", next);
    painted->fillStyle1 = 1;
    painted->fillStyle0 = -1;
    painted->strokeStyle = -1;
    next->edges.push_back(painted);
    keyframe(layer, 1, 1, next);

    fla::DrawingStyle style = red();
    regionfill::AcrossSettings settings;
    settings.style = &style;
    settings.stagePoint = Point(50, 50);
    settings.follow = true;
    const regionfill::AcrossResult result =
        regionfill::fillAcross(scene, *layer, regionfill::drawingsIn(scene, *layer, 0, 1), 0, settings);
    CHECK(result.fills.size() == 2 && result.missed.empty());
    if (result.fills.size() == 2 && result.fills[1].after)
    {
        const Shape& after = *result.fills[1].after;
        const int filled = fillAt(after, Point(140, 50));
        CHECK(filled >= 1 && regionfill::paintKey(after, filled) == "#ff0000ff");
        CHECK(regionfill::paintKey(after, fillAt(after, Point(50, 150))) == "#000000ff");
    }
}
