#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/region_split.h"
#include "../src/edit/shape_merge.h"
#include "../src/geom/shape_geometry.h"
#include "../src/parser/path_parser.h"

#include <cstdio>
#include <memory>

using fla::Point;
using fla::RegionPick;
using fla::RegionSplitter;
using fla::Shape;

namespace {

/// Two rectangles merged the way rect_2.fla stores them: fill 1 on the
/// left, fill 2 on the right, sharing an edge, with a stroke round the lot.
/// Coordinates in twips; the edge strings are the file's own format.
std::unique_ptr<Shape> twoFills(bool stroked)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));

    for (int index = 1; index <= 2; ++index)
    {
        fla::SolidColor* fill = new fla::SolidColor(shape.get());
        fill->color[0] = static_cast<uint8_t>(index * 100);
        shape->fills.push_back(fill);
        shape->fillsMap[index] = fill;
    }

    if (stroked)
    {
        fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
        stroke->weight = 1.0;
        stroke->fill = new fla::SolidColor(stroke);
        shape->strokes.push_back(stroke);
        shape->strokesMap[1] = stroke;
    }

    // Left square 0..100, right square 100..200, both 0..100 tall, clockwise
    // on screen so the fill sits on fillStyle1.
    const char* outlines[3] = {
        "!0 0|100 0",
        "!100 0|200 0|200 100|100 100",
        "!100 100|0 100|0 0",
    };
    const int fill1[3] = {1, 2, 1};

    PathParser parser;
    for (int i = 0; i < 3; ++i)
    {
        fla::Edge* edge = parser.parse(outlines[i], shape.get());
        if (!edge)
            continue;
        edge->fillStyle1 = fill1[i];
        edge->fillStyle0 = -1;
        edge->strokeStyle = stroked ? 1 : -1;
        shape->edges.push_back(edge);
    }

    // The shared edge, with fill 1 on one side and fill 2 on the other.
    fla::Edge* shared = parser.parse("!100 0|100 100", shape.get());
    if (shared)
    {
        shared->fillStyle1 = 2;
        shared->fillStyle0 = 1;
        shared->strokeStyle = -1;
        shape->edges.push_back(shared);
    }

    return shape;
}

/// Which fills the shape's edges still refer to.
bool usesFill(const Shape& shape, int fill)
{
    for (const fla::ShapeCurve& curve : fla::shapeCurves(shape))
    {
        if (curve.fillStyle0 == fill || curve.fillStyle1 == fill)
            return true;
    }
    return false;
}

bool usesStroke(const Shape& shape)
{
    for (const fla::ShapeCurve& curve : fla::shapeCurves(shape))
    {
        if (curve.strokeStyle != -1)
            return true;
    }
    return false;
}

} // namespace

TEST(clicking_a_fill_picks_that_face_alone)
{
    std::unique_ptr<Shape> shape = twoFills(false);
    RegionSplitter splitter(*shape);
    CHECK(splitter.valid());

    // Coordinates here are pixels: the parser converts twips.
    const RegionPick left = splitter.pickAt(Point(1.0, 2.0), 0.2, false);
    CHECK(left.kind == RegionPick::Kind::Fill);
    CHECK(left.strokeHalfEdges.empty());

    const RegionPick right = splitter.pickAt(Point(8.0, 2.0), 0.2, false);
    CHECK(right.kind == RegionPick::Kind::Fill);
    CHECK(right.face != left.face);

    const RegionPick outside = splitter.pickAt(Point(50.0, 50.0), 0.2, false);
    CHECK(outside.isEmpty());
}

TEST(splitting_a_fill_lifts_it_and_leaves_the_rest)
{
    std::unique_ptr<Shape> shape = twoFills(false);
    RegionSplitter splitter(*shape);

    const RegionPick left = splitter.pickAt(Point(1.0, 2.0), 0.2, false);
    Shape* piece = nullptr;
    Shape* remainder = nullptr;
    CHECK(splitter.split(left, piece, remainder));
    std::unique_ptr<Shape> pieceOwner(piece);
    std::unique_ptr<Shape> remainderOwner(remainder);
    if (!piece || !remainder)
        return;

    // The piece is the left fill and nothing else.
    CHECK(usesFill(*piece, 1));
    CHECK(!usesFill(*piece, 2));
    for (const fla::ShapeCurve& curve : fla::shapeCurves(*piece))
        CHECK(curve.fillStyle1 == 1 && curve.fillStyle0 == -1);

    // The remainder keeps the right fill and has lost the left one.
    CHECK(usesFill(*remainder, 2));
    CHECK(!usesFill(*remainder, 1));

    // The shared edge survives in the remainder as the right fill's boundary.
    RegionSplitter after(*remainder);
    CHECK(after.pickAt(Point(8.0, 2.0), 0.2, false).kind == RegionPick::Kind::Fill);
    CHECK(after.pickAt(Point(1.0, 2.0), 0.2, false).isEmpty());
}

TEST(a_stroke_within_reach_wins_over_the_fill_under_it)
{
    std::unique_ptr<Shape> shape = twoFills(true);
    RegionSplitter splitter(*shape);

    // On the top edge of the left square.
    const RegionPick pick = splitter.pickAt(Point(2.0, 0.1), 0.3, false);
    CHECK(pick.kind == RegionPick::Kind::Stroke);
    CHECK(pick.strokeHalfEdges.size() == 1);

    Shape* piece = nullptr;
    Shape* remainder = nullptr;
    CHECK(splitter.split(pick, piece, remainder));
    std::unique_ptr<Shape> pieceOwner(piece);
    std::unique_ptr<Shape> remainderOwner(remainder);
    if (!piece || !remainder)
        return;

    // The piece is a stroke only; the remainder keeps both fills.
    CHECK(usesStroke(*piece));
    CHECK(!usesFill(*piece, 1) && !usesFill(*piece, 2));
    CHECK(usesFill(*remainder, 1) && usesFill(*remainder, 2));
}

TEST(a_double_click_takes_the_strokes_with_the_fill)
{
    std::unique_ptr<Shape> shape = twoFills(true);
    RegionSplitter splitter(*shape);

    const RegionPick pick = splitter.pickAt(Point(1.0, 2.0), 0.2, true);
    CHECK(pick.kind == RegionPick::Kind::Fill);
    // Three stroked sides round the left square; the shared edge is unstroked.
    CHECK(pick.strokeHalfEdges.size() == 3);

    Shape* piece = nullptr;
    Shape* remainder = nullptr;
    CHECK(splitter.split(pick, piece, remainder));
    std::unique_ptr<Shape> pieceOwner(piece);
    std::unique_ptr<Shape> remainderOwner(remainder);
    if (!piece || !remainder)
        return;

    CHECK(usesStroke(*piece));

    // The remainder's stroke now runs only round the right square.
    int strokedPieces = 0;
    for (const fla::ShapeCurve& curve : fla::shapeCurves(*remainder))
        strokedPieces += curve.strokeStyle != -1 ? 1 : 0;
    CHECK(strokedPieces == 3);
}

TEST(a_double_click_on_a_stroke_follows_it_round_corners)
{
    std::unique_ptr<Shape> shape = twoFills(true);
    RegionSplitter splitter(*shape);

    // The outline round both squares is one run of six stroked pieces joined
    // through corners where only two pieces meet; the top-left corner of the
    // right square has three (the shared edge too), which ends the run.
    const RegionPick pick = splitter.pickAt(Point(2.0, 0.1), 0.3, true);
    CHECK(pick.kind == RegionPick::Kind::Stroke);
    CHECK(pick.strokeHalfEdges.size() == 3);
}

TEST(the_paint_bucket_fills_an_enclosed_empty_area)
{
    // A stroked square with nothing inside it.
    std::unique_ptr<Shape> shape(new Shape(nullptr));
    fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
    stroke->weight = 1.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;

    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|100 0|100 100|0 100|0 0", shape.get());
    CHECK(edge != nullptr);
    if (!edge)
        return;
    edge->strokeStyle = 1;
    shape->edges.push_back(edge);

    fla::SolidColor* paint = new fla::SolidColor(shape.get());
    shape->fills.push_back(paint);
    shape->fillsMap[1] = paint;

    RegionSplitter regions(*shape);
    CHECK(regions.pickAt(Point(2.5, 2.5), 0.2, false).isEmpty());
    const int face = regions.faceAt(Point(2.5, 2.5));
    CHECK(face >= 0);
    CHECK(regions.faceFill(face) == -1);

    std::unique_ptr<Shape> painted(regions.withFaceFill(face, 1));
    CHECK(painted != nullptr);
    if (!painted)
        return;
    CHECK(usesFill(*painted, 1));
    CHECK(usesStroke(*painted));

    // The outside stays empty.
    RegionSplitter after(*painted);
    CHECK(after.faceAt(Point(50.0, 50.0)) < 0);
}

TEST(the_ink_bottle_strokes_the_outline_of_one_fill)
{
    std::unique_ptr<Shape> shape = twoFills(false);
    fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
    stroke->weight = 2.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;

    RegionSplitter regions(*shape);
    const RegionPick left = regions.pickAt(Point(1.0, 2.0), 0.2, false);
    CHECK(left.kind == RegionPick::Kind::Fill);

    std::unique_ptr<Shape> inked(regions.withStroke(left, 1));
    CHECK(inked != nullptr);
    if (!inked)
        return;

    // Three outer sides plus the shared edge are stroked; the right square's
    // other three sides are not.
    int stroked = 0;
    int unstroked = 0;
    for (const fla::ShapeCurve& curve : fla::shapeCurves(*inked))
        (curve.strokeStyle != -1 ? stroked : unstroked)++;
    CHECK(stroked == 4);
    CHECK(unstroked == 3);
    CHECK(usesFill(*inked, 1) && usesFill(*inked, 2));
}

TEST(a_marquee_divides_a_fill_and_lifts_the_part_inside)
{
    // One red square 100 px across, and a marquee covering its left half.
    std::unique_ptr<Shape> shape(new Shape(nullptr));
    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|2000 0|2000 2000|0 2000|0 0", shape.get());
    CHECK(edge != nullptr);
    if (!edge)
        return;
    edge->fillStyle1 = 1;
    shape->edges.push_back(edge);

    // The marquee: an outline that cuts the arrangement and paints nothing.
    const std::vector<fla::Curve> marquee = {
        fla::Curve::line(Point(-10.0, -10.0), Point(50.0, -10.0)),
        fla::Curve::line(Point(50.0, -10.0), Point(50.0, 110.0)),
        fla::Curve::line(Point(50.0, 110.0), Point(-10.0, 110.0)),
        fla::Curve::line(Point(-10.0, 110.0), Point(-10.0, -10.0)),
    };

    RegionSplitter regions(*shape, marquee);
    const std::vector<int> inside = regions.facesWhere(
        [](const Point& p) { return p.x >= -10.0 && p.x <= 50.0 && p.y >= -10.0 && p.y <= 110.0; });
    CHECK(inside.size() == 1);

    Shape* piece = nullptr;
    Shape* remainder = nullptr;
    CHECK(regions.splitSet(inside, {}, piece, remainder));
    std::unique_ptr<Shape> pieceOwner(piece);
    std::unique_ptr<Shape> remainderOwner(remainder);
    if (!piece || !remainder)
        return;

    // The piece is the left half, the remainder the right half.
    RegionSplitter pieceRegions(*piece);
    CHECK(pieceRegions.faceAt(Point(25.0, 50.0)) >= 0);
    CHECK(pieceRegions.faceAt(Point(75.0, 50.0)) < 0);
    RegionSplitter restRegions(*remainder);
    CHECK(restRegions.faceAt(Point(25.0, 50.0)) < 0);
    CHECK(restRegions.faceAt(Point(75.0, 50.0)) >= 0);
    CHECK(restRegions.faceFill(restRegions.faceAt(Point(75.0, 50.0))) == 1);
}

TEST(the_split_command_undoes_back_to_one_shape)
{
    std::unique_ptr<Shape> shape = twoFills(false);
    fla::Frame frame(nullptr);
    frame.elements.push_back(shape.get());

    RegionSplitter splitter(*shape);
    const RegionPick left = splitter.pickAt(Point(1.0, 2.0), 0.2, false);
    Shape* piece = nullptr;
    Shape* remainder = nullptr;
    CHECK(splitter.split(left, piece, remainder));
    if (!piece || !remainder)
        return;

    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::SplitRegionCommand(
        shape.get(), *remainder, &frame, piece, "Select Fill")));
    delete remainder;

    CHECK(frame.elements.size() == 2);
    CHECK(frame.elements[1] == piece);
    CHECK(piece->parent == &frame);
    CHECK(!usesFill(*shape, 1));

    // Discarding the split is how an untouched selection is let go: no trace
    // is left, and the shape is whole again.
    CHECK(stack.discardLast());
    CHECK(frame.elements.size() == 1);
    CHECK(usesFill(*shape, 1));
    CHECK(stack.count() == 0);
    CHECK(stack.isClean());

    frame.elements.clear();
}

/// A stroked square whose last side stops six pixels short of closing. The
/// bucket cannot fill it as it is; joined across the gap, it can.
TEST(gap_joins_close_an_outline_that_stops_short)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));

    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    fill->color[0] = 200;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;

    fla::SolidStroke* stroke = new fla::SolidStroke(shape.get());
    stroke->weight = 1.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;

    PathParser parser;
    fla::Edge* outline = parser.parse("!0 0|2000 0|2000 2000|0 2000|0 120", shape.get());
    CHECK(outline != nullptr);
    if (!outline)
        return;
    outline->strokeStyle = 1;
    shape->edges.push_back(outline);

    RegionSplitter open(*shape);
    CHECK(open.valid());
    CHECK(open.faceAt(Point(50.0, 50.0)) == -1);

    // Too small a gap setting finds nothing; a large enough one joins the
    // loose end to the stroke's start.
    CHECK(open.gapJoins(4.0).empty());
    const std::vector<fla::Curve> joins = open.gapJoins(8.0);
    CHECK(joins.size() == 1);
    if (joins.empty())
        return;

    RegionSplitter closed(*shape, joins);
    const int face = closed.faceAt(Point(50.0, 50.0));
    CHECK(face >= 0);
    if (face < 0)
        return;
    CHECK(closed.faceFill(face) == -1);

    std::unique_ptr<Shape> painted(closed.withFaceFill(face, 1));
    CHECK(painted != nullptr);
    if (!painted)
        return;

    RegionSplitter check(*painted);
    const int filled = check.faceAt(Point(50.0, 50.0));
    CHECK(filled >= 0 && check.faceFill(filled) == 1);

    // The join bounds the fill but draws no stroke.
    bool joinWithoutStroke = false;
    for (const fla::Edge* edge : painted->edges)
    {
        if (edge->strokeStyle == -1 && (edge->fillStyle0 == 1 || edge->fillStyle1 == 1))
            joinWithoutStroke = true;
    }
    CHECK(joinWithoutStroke);
}
