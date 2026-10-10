#include "test_util.h"

#include "../src/data/edge.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/morph_shape.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/shape_tween.h"

#include <cmath>
#include <memory>

using fla::Point;

namespace {

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) < tolerance;
}

std::unique_ptr<fla::Shape> filled(uint8_t r, uint8_t g, uint8_t b, double strokeWeight)
{
    std::unique_ptr<fla::Shape> shape(new fla::Shape(nullptr));
    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    fill->color[0] = r;
    fill->color[1] = g;
    fill->color[2] = b;
    fill->color[3] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;

    fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
    stroke->weight = strokeWeight;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;
    return shape;
}

/// A square of side 100 at (x, y) in the A keyframe and at (x + dx, y + dy)
/// in the B keyframe, as one segment of four quadratic curves.
std::unique_ptr<fla::MorphShape> squareMorph(double x, double y, double dx, double dy,
    int fillIndex1, int fillIndex2, int strokeIndex)
{
    std::unique_ptr<fla::MorphShape> morph(new fla::MorphShape(nullptr));
    fla::MorphSegment* segment = new fla::MorphSegment(morph.get());
    segment->startPointA = Point(x, y);
    segment->startPointB = Point(x + dx, y + dy);
    segment->fillIndex1 = fillIndex1;
    segment->fillIndex2 = fillIndex2;
    segment->strokeIndex1 = strokeIndex;
    const Point corners[4] = {Point(x + 100, y), Point(x + 100, y + 100), Point(x, y + 100), Point(x, y)};
    Point previous(x, y);
    for (const Point& corner : corners)
    {
        fla::MorphCurves* curve = new fla::MorphCurves(segment);
        curve->controlPointA = Point((previous.x + corner.x) / 2.0, (previous.y + corner.y) / 2.0);
        curve->anchorPointA = corner;
        curve->controlPointB = Point(curve->controlPointA.x + dx, curve->controlPointA.y + dy);
        curve->anchorPointB = Point(corner.x + dx, corner.y + dy);
        segment->curves.push_back(curve);
        previous = corner;
    }
    morph->segments.push_back(segment);
    return morph;
}

} // namespace

TEST(morphed_shape_is_half_way_in_position_colour_and_weight)
{
    std::unique_ptr<fla::Shape> a = filled(255, 0, 0, 2.0);
    std::unique_ptr<fla::Shape> b = filled(0, 0, 255, 6.0);
    std::unique_ptr<fla::MorphShape> morph = squareMorph(0.0, 0.0, 100.0, 50.0, 0, 1, 1);

    std::unique_ptr<fla::Shape> mid(fla::morphedShape(*morph, *a, *b, 0.5,
        fla::Transform(), fla::Transform(), fla::Transform()));

    CHECK(mid->edges.size() == 1);
    CHECK(mid->fillsMap.size() == 1 && mid->strokesMap.size() == 1);
    if (mid->edges.empty() || mid->fillsMap.empty() || mid->strokesMap.empty())
        return;

    // Fill on the left (index 2), stroke 1.
    const fla::Edge* edge = mid->edges[0];
    CHECK(edge->fillStyle1 == 1 && edge->fillStyle0 == -1 && edge->strokeStyle == 1);

    // Half way from (0,0) to (100,50).
    const fla::Path* path = edge->paths[0];
    CHECK(path->segments.size() == 5);
    CHECK(near(path->segments[0]->points[0].x, 50.0) && near(path->segments[0]->points[0].y, 25.0));
    CHECK(path->segments[1]->command == fla::PathSegment::Command::Quad);
    CHECK(near(path->segments[1]->points[1].x, 150.0) && near(path->segments[1]->points[1].y, 25.0));

    // Purple, and a stroke of weight 4.
    const fla::SolidColor* fill = static_cast<const fla::SolidColor*>(mid->fillsMap[1]);
    CHECK(std::abs(static_cast<int>(fill->color[0]) - 128) <= 1);
    CHECK(fill->color[1] == 0);
    CHECK(std::abs(static_cast<int>(fill->color[2]) - 128) <= 1);
    CHECK(near(mid->strokesMap[1]->weight, 4.0));

    CHECK(near(mid->localBounds.topLeft.x, 50.0) && near(mid->localBounds.bottomRight.x, 150.0));
}

TEST(morphed_shape_maps_each_keyframe_through_its_own_transform)
{
    std::unique_ptr<fla::Shape> a = filled(255, 0, 0, 1.0);
    std::unique_ptr<fla::Shape> b = filled(255, 0, 0, 1.0);
    std::unique_ptr<fla::MorphShape> morph = squareMorph(0.0, 0.0, 0.0, 0.0, 0, 1, 0);

    // The B keyframe sits 200 to the right on the stage; the result is drawn
    // in a space shifted 10 down.
    fla::Transform bToDocument;
    bToDocument.tx = 200.0;
    fla::Transform documentToLocal;
    documentToLocal.ty = -10.0;

    std::unique_ptr<fla::Shape> mid(fla::morphedShape(*morph, *a, *b, 0.25,
        fla::Transform(), bToDocument, documentToLocal));
    const Point& start = mid->edges[0]->paths[0]->segments[0]->points[0];
    CHECK(near(start.x, 50.0) && near(start.y, -10.0));
}

TEST(unstyled_morph_segments_take_the_first_fill_and_stroke)
{
    std::unique_ptr<fla::Shape> a = filled(0, 255, 0, 3.0);
    std::unique_ptr<fla::Shape> b = filled(0, 255, 0, 3.0);
    std::unique_ptr<fla::MorphShape> morph = squareMorph(0.0, 0.0, 0.0, 0.0, 0, 0, 0);

    std::unique_ptr<fla::Shape> mid(fla::morphedShape(*morph, *a, *b, 0.5,
        fla::Transform(), fla::Transform(), fla::Transform()));
    CHECK(mid->edges[0]->fillStyle1 == 1);
    CHECK(mid->edges[0]->strokeStyle == 1);
}

TEST(blending_gradients_blends_their_stops)
{
    fla::LinearGradient a(nullptr);
    fla::LinearGradient b(nullptr);
    fla::GradientEntry stop;
    stop.ratio = 0.0;
    stop.color[0] = 0; stop.color[1] = 0; stop.color[2] = 0; stop.color[3] = 255;
    a.entries.push_back(stop);
    stop.ratio = 1.0;
    a.entries.push_back(stop);
    stop.ratio = 0.5;
    stop.color[0] = 200;
    b.entries.push_back(stop);
    stop.ratio = 1.0;
    b.entries.push_back(stop);

    std::unique_ptr<fla::FillStyle> mid(fla::blendFills(&a, &b, 0.5, nullptr));
    CHECK(mid->type() == fla::FillStyle::Type::LinearGradient);
    const fla::LinearGradient* gradient = static_cast<const fla::LinearGradient*>(mid.get());
    CHECK(gradient->entries.size() == 2);
    CHECK(near(gradient->entries[0].ratio, 0.25));
    CHECK(gradient->entries[0].color[0] == 100);

    // Unlike fills: a solid against a gradient stays the first.
    fla::SolidColor solid(nullptr);
    std::unique_ptr<fla::FillStyle> kept(fla::blendFills(&solid, &a, 0.5, nullptr));
    CHECK(kept->type() == fla::FillStyle::Type::SolidColor);
}
