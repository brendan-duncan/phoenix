#include "test_util.h"

#include "../src/geom/fill_attribution.h"

#include <cmath>
#include <vector>

using fla::Curve;
using fla::FillRegion;
using fla::HalfEdge;
using fla::PlanarMap;
using fla::Point;

namespace {

std::vector<Curve> rectangleOutline(double left, double top, double right, double bottom)
{
    const Point corners[4] = {
        Point(left, top), Point(right, top), Point(right, bottom), Point(left, bottom)
    };

    std::vector<Curve> outline;
    for (int i = 0; i < 4; ++i)
        outline.push_back(Curve::line(corners[i], corners[(i + 1) % 4]));
    return outline;
}

/// Builds a map from the regions and attributes their fills, which is the whole
/// merge in one call.
void mergeInto(PlanarMap& map, const std::vector<FillRegion>& regions)
{
    for (size_t i = 0; i < regions.size(); ++i)
    {
        for (const Curve& curve : regions[i].outline)
            map.addCurve(curve, static_cast<int>(i));
    }

    map.build();
    fla::attributeFills(map, regions);
}

/// The fill of the face covering a point, or -1.
int fillAt(const PlanarMap& map, const Point& point)
{
    for (size_t i = 0; i < map.faces().size(); ++i)
    {
        if (map.faces()[i].unbounded)
            continue;
        if (map.faceContains(static_cast<int>(i), point))
            return map.faceFill(static_cast<int>(i));
    }
    return -1;
}

} // namespace

TEST(outline_containment_follows_the_even_odd_rule)
{
    const std::vector<Curve> square = rectangleOutline(0.0, 0.0, 10.0, 10.0);

    CHECK(fla::outlineContains(square, Point(5.0, 5.0)));
    CHECK(!fla::outlineContains(square, Point(15.0, 5.0)));
    CHECK(!fla::outlineContains(square, Point(5.0, -5.0)));
}

TEST(a_single_filled_shape_paints_its_inside_only)
{
    PlanarMap map;
    mergeInto(map, {FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1}});

    CHECK(fillAt(map, Point(50.0, 50.0)) == 1);
    // The outside is painted by nothing.
    CHECK(map.faceFill(map.unboundedFace()) == -1);
}

TEST(a_shape_drawn_over_another_replaces_it_where_they_overlap)
{
    // The whole point of merge drawing: the result is three regions with three
    // answers, not two shapes stacked.
    PlanarMap map;
    mergeInto(map, {
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1},
        FillRegion{rectangleOutline(50.0, 50.0, 150.0, 150.0), 2},
    });

    // Only the first shape.
    CHECK(fillAt(map, Point(25.0, 25.0)) == 1);
    // Both, and the later one wins.
    CHECK(fillAt(map, Point(75.0, 75.0)) == 2);
    // Only the second.
    CHECK(fillAt(map, Point(125.0, 125.0)) == 2);
}

TEST(order_decides_which_fill_survives)
{
    PlanarMap first;
    mergeInto(first, {
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1},
        FillRegion{rectangleOutline(50.0, 50.0, 150.0, 150.0), 2},
    });

    PlanarMap second;
    mergeInto(second, {
        FillRegion{rectangleOutline(50.0, 50.0, 150.0, 150.0), 2},
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1},
    });

    // Same geometry, opposite paint order, opposite answer in the overlap.
    CHECK(fillAt(first, Point(75.0, 75.0)) == 2);
    CHECK(fillAt(second, Point(75.0, 75.0)) == 1);
}

TEST(an_unfilled_outline_cuts_without_painting)
{
    // A line drawn across a fill has to divide it, so the two halves can be
    // moved apart, but must not repaint anything.
    PlanarMap map;
    mergeInto(map, {
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1},
        FillRegion{rectangleOutline(40.0, -20.0, 60.0, 120.0), -1},
    });

    // Left of the cut and right of it keep the original fill.
    CHECK(fillAt(map, Point(20.0, 50.0)) == 1);
    CHECK(fillAt(map, Point(80.0, 50.0)) == 1);
    // Inside the unfilled outline, nothing is painted.
    CHECK(fillAt(map, Point(50.0, 50.0)) == -1);

    // Five regions, not three: the cutting outline runs past the square, so the
    // stubs above and below it enclose regions of their own.
    int bounded = 0;
    for (const fla::MapFace& face : map.faces())
        bounded += face.unbounded ? 0 : 1;
    CHECK(bounded == 5);
}

TEST(an_open_stroke_divides_a_fill_without_repainting_it)
{
    // The realistic version of cutting: a line drawn across a fill. It has no
    // inside, so it is not a region at all -- it goes into the map to split the
    // faces and takes no part in attribution.
    const std::vector<FillRegion> regions = {
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1}
    };

    PlanarMap map;
    for (const Curve& curve : regions[0].outline)
        map.addCurve(curve, 0);

    map.addCurve(Curve::line(Point(50.0, -20.0), Point(50.0, 120.0)), 1);
    map.build();
    fla::attributeFills(map, regions);

    // One fill became two, both still the original colour.
    CHECK(fillAt(map, Point(25.0, 50.0)) == 1);
    CHECK(fillAt(map, Point(75.0, 50.0)) == 1);

    int bounded = 0;
    for (const fla::MapFace& face : map.faces())
        bounded += face.unbounded ? 0 : 1;
    CHECK(bounded == 2);

    // The two halves are separate faces, which is what lets them be dragged
    // apart.
    bool sawTwoFaces = false;
    for (size_t i = 0; i < map.faces().size(); ++i)
    {
        for (size_t j = i + 1; j < map.faces().size(); ++j)
        {
            if (map.faces()[i].unbounded || map.faces()[j].unbounded)
                continue;
            if (map.faceContains(static_cast<int>(i), Point(25.0, 50.0)) &&
                map.faceContains(static_cast<int>(j), Point(75.0, 50.0)))
            {
                sawTwoFaces = true;
            }
        }
    }
    CHECK(sawTwoFaces);
}

TEST(a_hole_is_not_painted_by_the_shape_around_it)
{
    PlanarMap map;
    mergeInto(map, {
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1},
        FillRegion{rectangleOutline(25.0, 25.0, 75.0, 75.0), -1},
    });

    // The ring keeps its fill; the middle is punched out.
    CHECK(fillAt(map, Point(10.0, 10.0)) == 1);
    CHECK(fillAt(map, Point(50.0, 50.0)) == -1);
}

TEST(each_half_edge_carries_the_fill_on_its_left)
{
    PlanarMap map;
    mergeInto(map, {FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1}});

    const std::vector<HalfEdge>& edges = map.halfEdges();

    int inward = 0;
    int outward = 0;
    for (const HalfEdge& edge : edges)
    {
        // Every half-edge takes the fill of the face it borders, and its twin
        // takes the fill on the other side. That pair is what the file format
        // records as fillStyle0 and fillStyle1.
        CHECK(edge.leftFill == map.faceFill(edge.face));

        if (edge.leftFill == 1)
            ++inward;
        else if (edge.leftFill == -1)
            ++outward;
    }

    // Four edges face the fill, four face the outside.
    CHECK(inward == 4);
    CHECK(outward == 4);

    // A half-edge and its twin never agree when they straddle the shape's edge.
    for (size_t i = 0; i < edges.size(); ++i)
        CHECK(edges[i].leftFill != edges[edges[i].twin].leftFill);
}

TEST(a_shared_edge_between_two_fills_carries_both)
{
    // Two colours meeting along one edge. That edge has to know about both, and
    // it is stored once rather than twice.
    PlanarMap map;
    mergeInto(map, {
        FillRegion{rectangleOutline(0.0, 0.0, 50.0, 50.0), 1},
        FillRegion{rectangleOutline(50.0, 0.0, 100.0, 50.0), 2},
    });

    CHECK(fillAt(map, Point(25.0, 25.0)) == 1);
    CHECK(fillAt(map, Point(75.0, 25.0)) == 2);

    // Find the half-edge pair along the join at x = 50.
    bool foundJoin = false;
    for (const HalfEdge& edge : map.halfEdges())
    {
        const Point middle = edge.curve.pointAt(0.5);
        if (std::fabs(middle.x - 50.0) > 1.0e-6)
            continue;

        const int otherSide = map.halfEdges()[edge.twin].leftFill;
        if ((edge.leftFill == 1 && otherSide == 2) ||
            (edge.leftFill == 2 && otherSide == 1))
        {
            foundJoin = true;
        }
    }

    CHECK(foundJoin);
}

TEST(attribution_survives_curved_outlines)
{
    // A circle-ish shape from four cubics, overlapped by a square.
    const double r = 50.0;
    const double k = r * 0.5523;

    std::vector<Curve> circle;
    circle.push_back(Curve::cubic(Point(-r, 0.0), Point(-r, -k), Point(-k, -r), Point(0.0, -r)));
    circle.push_back(Curve::cubic(Point(0.0, -r), Point(k, -r), Point(r, -k), Point(r, 0.0)));
    circle.push_back(Curve::cubic(Point(r, 0.0), Point(r, k), Point(k, r), Point(0.0, r)));
    circle.push_back(Curve::cubic(Point(0.0, r), Point(-k, r), Point(-r, k), Point(-r, 0.0)));

    PlanarMap map;
    mergeInto(map, {
        FillRegion{circle, 1},
        FillRegion{rectangleOutline(0.0, -100.0, 100.0, 100.0), 2},
    });

    // Left of the square, still the circle's fill.
    CHECK(fillAt(map, Point(-30.0, 0.0)) == 1);
    // Inside both, the square wins.
    CHECK(fillAt(map, Point(20.0, 0.0)) == 2);
    // Inside the square only.
    CHECK(fillAt(map, Point(80.0, 0.0)) == 2);
    // Outside everything.
    CHECK(fillAt(map, Point(-80.0, 0.0)) == -1);
}

TEST(a_face_interior_point_really_is_inside)
{
    PlanarMap map;
    mergeInto(map, {
        FillRegion{rectangleOutline(0.0, 0.0, 100.0, 100.0), 1},
        FillRegion{rectangleOutline(50.0, 50.0, 150.0, 150.0), 2},
    });

    for (size_t i = 0; i < map.faces().size(); ++i)
    {
        const int index = static_cast<int>(i);
        if (map.faces()[i].unbounded)
        {
            Point ignored;
            // The outside has no inside to find.
            CHECK(!map.interiorPoint(index, ignored));
            continue;
        }

        Point inside;
        CHECK(map.interiorPoint(index, inside));
        // The whole attribution rests on this point being in the right face.
        CHECK(map.faceContains(index, inside));
    }
}
