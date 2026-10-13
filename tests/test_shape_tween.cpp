#include "test_util.h"
#include <cstdio>

#include "../src/data/edge.h"
#include "../src/data/frame.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/morph_shape.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/shape_tween.h"

#include <algorithm>
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

TEST(morphed_shape_is_half_way_in_position_color_and_weight)
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

namespace {

/// A square outline of \a size at (x, y), one edge with the fill on its left
/// (fillStyle1) or, written the other way round, on its right (fillStyle0).
void addSquare(fla::Shape& shape, double x, double y, double size, bool fillOnLeft)
{
    fla::Edge* edge = new fla::Edge(&shape);
    if (fillOnLeft)
        edge->fillStyle1 = 1;
    else
        edge->fillStyle0 = 1;
    fla::Path* path = new fla::Path(edge);
    std::vector<Point> corners = {Point(x, y), Point(x + size, y), Point(x + size, y + size), Point(x, y + size), Point(x, y)};
    if (!fillOnLeft)
        std::reverse(corners.begin(), corners.end());
    path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Move, {corners[0]}, path));
    for (size_t i = 1; i < corners.size(); ++i)
        path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Line, {corners[i]}, path));
    edge->paths.push_back(path);
    shape.edges.push_back(edge);
}

bool onSquare(const Point& p, double x, double y, double size)
{
    const double tolerance = 1.0e-3;
    const bool inside = p.x > x - tolerance && p.x < x + size + tolerance &&
        p.y > y - tolerance && p.y < y + size + tolerance;
    const bool onSide = near(p.x, x, tolerance) || near(p.x, x + size, tolerance) ||
        near(p.y, y, tolerance) || near(p.y, y + size, tolerance);
    return inside && onSide;
}

} // namespace

TEST(built_morph_runs_each_outline_onto_the_other)
{
    std::unique_ptr<fla::Shape> a = filled(255, 0, 0, 1.0);
    std::unique_ptr<fla::Shape> b = filled(0, 0, 255, 1.0);
    addSquare(*a, 0.0, 0.0, 100.0, true);
    addSquare(*b, 300.0, 50.0, 200.0, false);

    std::unique_ptr<fla::MorphShape> morph(fla::buildMorph(*a, *b, nullptr));
    CHECK(morph->segments.size() == size_t(1));
    const fla::MorphSegment* segment = morph->segments.front();
    CHECK(segment->fillIndex1 == 0);
    CHECK(segment->fillIndex2 == 1);
    CHECK(segment->curves.size() >= 4);
    // Both start at their leftmost, topmost corner.
    CHECK(near(segment->startPointA.x, 0.0, 1.0e-6) && near(segment->startPointA.y, 0.0, 1.0e-6));
    CHECK(near(segment->startPointB.x, 300.0, 1.0e-6) && near(segment->startPointB.y, 50.0, 1.0e-6));
    for (const fla::MorphCurves* curve : segment->curves)
    {
        CHECK(onSquare(curve->anchorPointA, 0.0, 0.0, 100.0));
        CHECK(onSquare(curve->anchorPointB, 300.0, 50.0, 200.0));
    }
    // Closed: each ends where it began.
    CHECK(near(segment->curves.back()->anchorPointA.x, 0.0, 1.0e-6));
    CHECK(near(segment->curves.back()->anchorPointB.y, 50.0, 1.0e-6));

    // Half way, the square is half way in place and size.
    std::unique_ptr<fla::Shape> mid(fla::morphedShape(*morph, *a, *b, 0.5,
        fla::Transform(), fla::Transform(), fla::Transform()));
    CHECK(near(mid->localBounds.topLeft.x, 150.0, 1.0e-6));
    CHECK(near(mid->localBounds.topLeft.y, 25.0, 1.0e-6));
    CHECK(near(mid->localBounds.bottomRight.x, 300.0, 1.0e-6));
    CHECK(near(mid->localBounds.bottomRight.y, 175.0, 1.0e-6));
}

TEST(built_morph_shrinks_an_outline_with_no_partner_to_a_point)
{
    std::unique_ptr<fla::Shape> a = filled(255, 0, 0, 1.0);
    std::unique_ptr<fla::Shape> b = filled(0, 0, 255, 1.0);
    addSquare(*a, 0.0, 0.0, 100.0, true);
    addSquare(*a, 200.0, 0.0, 50.0, true);
    addSquare(*b, 0.0, 0.0, 120.0, true);

    std::unique_ptr<fla::MorphShape> morph(fla::buildMorph(*a, *b, nullptr));
    CHECK(morph->segments.size() == size_t(2));
    // The bigger square pairs with B's square; the small one shrinks to its middle.
    const fla::MorphSegment* small = morph->segments[1];
    CHECK(onSquare(small->startPointA, 200.0, 0.0, 50.0));
    for (const fla::MorphCurves* curve : small->curves)
    {
        CHECK(near(curve->anchorPointB.x, 225.0, 1.0e-3));
        CHECK(near(curve->anchorPointB.y, 25.0, 1.0e-3));
    }
}

TEST(ensure_morph_rebuilds_only_when_a_keyframe_shape_changes)
{
    fla::Frame start(nullptr);
    fla::Frame end(nullptr);
    start.tweenType = fla::TweenType::Shape;
    fla::Shape* a = filled(255, 0, 0, 1.0).release();
    fla::Shape* b = filled(0, 0, 255, 1.0).release();
    addSquare(*a, 0.0, 0.0, 100.0, true);
    addSquare(*b, 50.0, 0.0, 100.0, true);
    start.elements.push_back(a);
    end.elements.push_back(b);

    CHECK(fla::ensureMorph(start, end));
    CHECK(start.morphShape != nullptr);
    CHECK(!fla::ensureMorph(start, end));
    addSquare(*b, 400.0, 0.0, 10.0, true);
    CHECK(fla::ensureMorph(start, end));
    CHECK(start.morphShape->segments.size() == size_t(2));
}

TEST(shape_hints_pin_points_of_the_morph)
{
    // Two like squares; a hint takes the first one's top left corner to the
    // second one's bottom right, so the morph turns half round.
    std::unique_ptr<fla::Shape> a = filled(255, 0, 0, 1.0);
    std::unique_ptr<fla::Shape> b = filled(0, 0, 255, 1.0);
    addSquare(*a, 0.0, 0.0, 100.0, true);
    addSquare(*b, 0.0, 0.0, 100.0, true);
    std::unique_ptr<fla::MorphShape> plain(fla::buildMorph(*a, *b, nullptr));
    CHECK(near(plain->segments.front()->startPointB.x, 0.0, 1.0e-6));

    fla::ShapeHint hint;
    hint.start = Point(0.0, 0.0);
    hint.end = Point(100.0, 100.0);
    std::unique_ptr<fla::MorphShape> hinted(fla::buildMorph(*a, *b, nullptr, {hint}));
    CHECK(hinted->segments.size() == size_t(1));
    const fla::MorphSegment* segment = hinted->segments.front();
    CHECK(near(segment->startPointA.x, 0.0, 1.0e-3) && near(segment->startPointA.y, 0.0, 1.0e-3));
    CHECK(near(segment->startPointB.x, 100.0, 1.0e-3) && near(segment->startPointB.y, 100.0, 1.0e-3));
    // Pinned a quarter turn round instead, half way the square is turning:
    // a filled shape, not collapsed to its middle.
    fla::ShapeHint quarter;
    quarter.start = Point(0.0, 0.0);
    quarter.end = Point(100.0, 0.0);
    std::unique_ptr<fla::MorphShape> turning(fla::buildMorph(*a, *b, nullptr, {quarter}));
    std::unique_ptr<fla::Shape> mid(fla::morphedShape(*turning, *a, *b, 0.5,
        fla::Transform(), fla::Transform(), fla::Transform()));
    CHECK(mid->localBounds.width() > 40.0 && mid->localBounds.height() > 40.0);

    // A second hint, a quarter of the way round each: the part between them
    // runs onto the part between theirs.
    fla::ShapeHint second;
    second.start = Point(100.0, 0.0);
    second.end = Point(0.0, 100.0);
    std::unique_ptr<fla::MorphShape> twice(fla::buildMorph(*a, *b, nullptr, {hint, second}));
    bool pinned = false;
    for (const fla::MorphCurves* curve : twice->segments.front()->curves)
        pinned = pinned || (near(curve->anchorPointA.x, 100.0, 1.0e-3) && near(curve->anchorPointA.y, 0.0, 1.0e-3) &&
                               near(curve->anchorPointB.x, 0.0, 1.0e-3) && near(curve->anchorPointB.y, 100.0, 1.0e-3));
    CHECK(pinned);

    // Kept on the frame.
    fla::Frame frame(nullptr);
    fla::setHints(frame.extraAttributes, {hint, second});
    const std::vector<fla::ShapeHint> read = fla::hintsOf(frame);
    CHECK(read.size() == 2 && near(read[1].end.y, 100.0, 1.0e-6));
}

TEST(built_morph_pairs_outlines_of_thousands_of_pieces)
{
    // Two round fills of thousands of straight pieces each, the pieces listed
    // as separate edges in a scrambled order: each chains back into one
    // outline, and the two pair into one segment running round both.
    const double pi = 3.14159265358979323846;
    const auto round = [&](int pieces, double rx, double ry) {
        std::unique_ptr<fla::Shape> shape(new fla::Shape(nullptr));
        fla::SolidColor* fill = new fla::SolidColor(shape.get());
        shape->fills.push_back(fill);
        shape->fillsMap[1] = fill;
        for (int k = 0; k < pieces; ++k)
        {
            const int i = static_cast<int>((static_cast<long long>(k) * 11) % pieces);
            const double a0 = 2.0 * pi * i / pieces;
            const double a1 = 2.0 * pi * (i + 1) / pieces;
            fla::Edge* edge = new fla::Edge(shape.get());
            edge->fillStyle1 = 1;
            fla::Path* path = new fla::Path(edge);
            path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Move,
                {Point(rx * std::cos(a0), ry * std::sin(a0))}, path));
            path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Line,
                {Point(rx * std::cos(a1), ry * std::sin(a1))}, path));
            edge->paths.push_back(path);
            shape->edges.push_back(edge);
        }
        return shape;
    };

    const std::unique_ptr<fla::Shape> from = round(1500, 200.0, 200.0);
    const std::unique_ptr<fla::Shape> to = round(1600, 300.0, 150.0);
    const std::unique_ptr<fla::MorphShape> morph(fla::buildMorph(*from, *to, nullptr));
    CHECK(morph->segments.size() == 1);
    if (morph->segments.size() != 1)
        return;

    // Cut at every corner of either outline.
    const fla::MorphSegment* segment = morph->segments.front();
    CHECK(segment->curves.size() >= 1600);

    bool onA = true;
    bool onB = true;
    for (const fla::MorphCurves* curves : segment->curves)
    {
        const Point& a = curves->anchorPointA;
        const Point& b = curves->anchorPointB;
        onA = onA && near(std::hypot(a.x, a.y), 200.0, 0.01);
        const double ellipse = (b.x / 300.0) * (b.x / 300.0) + (b.y / 150.0) * (b.y / 150.0);
        onB = onB && near(ellipse, 1.0, 0.001);
    }
    CHECK(onA);
    CHECK(onB);
}
