#include "test_util.h"

#include "../src/geom/planar_map.h"

#include <cmath>
#include <vector>

using fla::Curve;
using fla::HalfEdge;
using fla::MapFace;
using fla::PlanarMap;
using fla::Point;

namespace {

/// Adds a closed run of straight edges through the given corners.
void addPolygon(PlanarMap& map, const std::vector<Point>& corners, int source = -1)
{
    for (size_t i = 0; i < corners.size(); ++i)
    {
        const Point& from = corners[i];
        const Point& to = corners[(i + 1) % corners.size()];
        map.addCurve(Curve::line(from, to), source);
    }
}

std::vector<Point> rectangle(double left, double top, double right, double bottom)
{
    return {Point(left, top), Point(right, top), Point(right, bottom), Point(left, bottom)};
}

/// Faces other than the unbounded one.
std::vector<const MapFace*> boundedFaces(const PlanarMap& map)
{
    std::vector<const MapFace*> found;
    for (const MapFace& face : map.faces())
    {
        if (!face.unbounded)
            found.push_back(&face);
    }
    return found;
}

/// Walks a face's boundary and counts the half-edges on it.
int boundaryLength(const PlanarMap& map, const MapFace& face)
{
    if (face.halfEdge < 0)
        return 0;

    int count = 0;
    int current = face.halfEdge;
    for (size_t step = 0; step <= map.halfEdges().size(); ++step)
    {
        ++count;
        current = map.halfEdges()[current].next;
        if (current < 0 || current == face.halfEdge)
            break;
    }
    return count;
}

bool nearly(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

TEST(planar_map_splits_collinear_overlaps_and_t_junctions)
{
    // Two segments running along the same line and overlapping for a stretch:
    // each is cut where the other ends, and the shared stretch is one edge.
    PlanarMap overlap;
    overlap.addCurve(Curve::line(Point(0.0, 0.0), Point(10.0, 0.0)), 0);
    overlap.addCurve(Curve::line(Point(5.0, 0.0), Point(15.0, 0.0)), 1);
    overlap.build();
    CHECK(overlap.vertices().size() == 4);
    CHECK(overlap.halfEdges().size() == 6);

    // A segment ending on the middle of another splits it there.
    PlanarMap tee;
    tee.addCurve(Curve::line(Point(0.0, 0.0), Point(10.0, 0.0)), 0);
    tee.addCurve(Curve::line(Point(5.0, 0.0), Point(5.0, 8.0)), 1);
    tee.build();
    CHECK(tee.vertices().size() == 4);
    CHECK(tee.halfEdges().size() == 6);
}

TEST(planar_map_starts_empty)
{
    PlanarMap map;
    map.build();

    CHECK(map.vertices().empty());
    CHECK(map.halfEdges().empty());
    CHECK(map.faces().empty());
    CHECK(map.unboundedFace() == -1);
}

TEST(planar_map_snaps_to_the_twip_grid)
{
    // A twentieth of a pixel is what the file format stores, so anything finer
    // has to land on the grid or the topology would depend on rounding.
    const Point snapped = PlanarMap::snapToTwips(Point(1.0 / 300.0, -1.0 / 300.0));

    CHECK(nearly(snapped.x, 0.0));
    CHECK(nearly(snapped.y, 0.0));

    const Point other = PlanarMap::snapToTwips(Point(0.06, 0.06));
    CHECK(nearly(other.x, 0.05));
    CHECK(nearly(other.y, 0.05));
}

TEST(planar_map_welds_endpoints_that_share_a_vertex)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0));
    map.build();

    // Four corners, not eight loose ends.
    CHECK(map.vertices().size() == 4);
    CHECK(map.halfEdges().size() == 8);

    for (const fla::MapVertex& vertex : map.vertices())
    {
        // Two edges in, two out, expressed as two outgoing half-edges.
        CHECK(vertex.outgoing.size() == 2);
    }
}

TEST(planar_map_welds_points_within_a_twip)
{
    PlanarMap map;
    // The two ends miss each other by a hundredth of a pixel, which is finer
    // than the grid: they are the same point.
    map.addCurve(Curve::line(Point(0.0, 0.0), Point(10.0, 0.0)));
    map.addCurve(Curve::line(Point(10.001, 0.0), Point(10.0, 10.0)));
    map.build();

    CHECK(map.vertices().size() == 3);
}

TEST(planar_map_gives_a_closed_shape_two_faces)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0));
    map.build();

    // Inside and outside.
    CHECK(map.faces().size() == 2);
    CHECK(map.unboundedFace() >= 0);

    const std::vector<const MapFace*> bounded = boundedFaces(map);
    CHECK(bounded.size() == 1);
    if (bounded.empty())
        return;

    CHECK(nearly(std::fabs(bounded[0]->area), 10000.0, 1.0));
    CHECK(boundaryLength(map, *bounded[0]) == 4);
}

TEST(planar_map_bounded_faces_are_positive_and_the_outside_is_negative)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 10.0, 10.0));
    map.build();

    for (const MapFace& face : map.faces())
    {
        if (face.unbounded)
            CHECK(face.area < 0.0);
        else
            CHECK(face.area > 0.0);
    }
}

TEST(planar_map_splits_two_crossing_lines)
{
    PlanarMap map;
    map.addCurve(Curve::line(Point(-10.0, 0.0), Point(10.0, 0.0)));
    map.addCurve(Curve::line(Point(0.0, -10.0), Point(0.0, 10.0)));
    map.build();

    // Four tips plus the crossing.
    CHECK(map.vertices().size() == 5);
    // Each line became two pieces, and every piece has two directions.
    CHECK(map.halfEdges().size() == 8);

    // An X encloses nothing, so there is only the outside.
    CHECK(map.faces().size() == 1);
    CHECK(map.unboundedFace() == 0);
}

TEST(planar_map_finds_the_overlap_of_two_rectangles)
{
    // The case merge drawing exists for: two shapes laid over each other become
    // three regions, not two shapes.
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0), 1);
    addPolygon(map, rectangle(50.0, 50.0, 150.0, 150.0), 2);
    map.build();

    const std::vector<const MapFace*> bounded = boundedFaces(map);

    // Left-only, the overlap, and right-only.
    CHECK(bounded.size() == 3);

    double total = 0.0;
    bool foundOverlap = false;
    for (const MapFace* face : bounded)
    {
        total += face->area;
        if (nearly(face->area, 2500.0, 1.0))
            foundOverlap = true;
    }

    // The overlap is a fifty by fifty square.
    CHECK(foundOverlap);
    // Together the three cover both rectangles with the overlap counted once.
    CHECK(nearly(total, 17500.0, 2.0));
}

TEST(planar_map_keeps_touching_rectangles_separate)
{
    // Sharing an edge is not overlapping: two faces, not three.
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 50.0, 50.0));
    addPolygon(map, rectangle(50.0, 0.0, 100.0, 50.0));
    map.build();

    const std::vector<const MapFace*> bounded = boundedFaces(map);
    CHECK(bounded.size() == 2);

    for (const MapFace* face : bounded)
        CHECK(nearly(face->area, 2500.0, 1.0));

    // Seven edges, not eight: the shared one is a single piece of geometry.
    CHECK(map.vertices().size() == 6);
    CHECK(map.halfEdges().size() == 14);
}

TEST(planar_map_handles_a_shape_inside_another)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0));
    addPolygon(map, rectangle(25.0, 25.0, 75.0, 75.0));
    map.build();

    // The two outlines never meet, so nothing is split.
    CHECK(map.vertices().size() == 8);

    const std::vector<const MapFace*> bounded = boundedFaces(map);
    // The inner square, and the ring around it.
    CHECK(bounded.size() == 2);
}

TEST(planar_map_every_half_edge_has_a_twin_and_a_face)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0));
    addPolygon(map, rectangle(50.0, 50.0, 150.0, 150.0));
    map.addCurve(Curve::line(Point(-50.0, 75.0), Point(200.0, 75.0)));
    map.build();

    const std::vector<HalfEdge>& edges = map.halfEdges();

    for (size_t i = 0; i < edges.size(); ++i)
    {
        const HalfEdge& edge = edges[i];

        CHECK(edge.twin >= 0 && edge.twin < static_cast<int>(edges.size()));
        if (edge.twin < 0)
            continue;

        // A twin runs the other way and points back.
        CHECK(edges[edge.twin].twin == static_cast<int>(i));
        CHECK(edges[edge.twin].from == edge.to);
        CHECK(edges[edge.twin].to == edge.from);

        // Every piece belongs to exactly one face, reached through next.
        CHECK(edge.face >= 0);
        CHECK(edge.next >= 0);
        if (edge.next >= 0)
            CHECK(edges[edge.next].from == edge.to);
    }
}

TEST(planar_map_face_walks_return_to_where_they_started)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0));
    addPolygon(map, rectangle(50.0, 50.0, 150.0, 150.0));
    map.build();

    for (const MapFace& face : map.faces())
    {
        CHECK(face.halfEdge >= 0);
        if (face.halfEdge < 0)
            continue;

        // Following next from any edge of a face has to come back around, and
        // every edge on the way has to agree it belongs to that face.
        int current = face.halfEdge;
        bool closed = false;
        for (size_t step = 0; step <= map.halfEdges().size(); ++step)
        {
            CHECK(map.halfEdges()[current].face == &face - map.faces().data());
            current = map.halfEdges()[current].next;
            if (current == face.halfEdge)
            {
                closed = true;
                break;
            }
            if (current < 0)
                break;
        }
        CHECK(closed);
    }
}

TEST(planar_map_splits_a_curve_crossing_a_line)
{
    PlanarMap map;
    // An arch over a line it cuts twice.
    map.addCurve(Curve::cubic(Point(0.0, 0.0), Point(0.0, 100.0),
        Point(100.0, 100.0), Point(100.0, 0.0)));
    map.addCurve(Curve::line(Point(0.0, 0.0), Point(100.0, 0.0)));
    map.addCurve(Curve::line(Point(-20.0, 40.0), Point(120.0, 40.0)));
    map.build();

    // The arch and the horizontal line cross twice, so both gain vertices there.
    CHECK(map.vertices().size() >= 6);

    const std::vector<const MapFace*> bounded = boundedFaces(map);
    // The line at y = 40 cuts the arch's interior into a lower band and an upper
    // cap.
    CHECK(bounded.size() == 2);
}

TEST(planar_map_carries_the_source_through_to_every_piece)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0), 7);
    addPolygon(map, rectangle(50.0, 50.0, 150.0, 150.0), 9);
    map.build();

    bool sawSeven = false;
    bool sawNine = false;
    for (const HalfEdge& edge : map.halfEdges())
    {
        // Splitting must not lose which input a piece came from: that is how a
        // fill and stroke follow their geometry through the merge.
        CHECK(edge.source == 7 || edge.source == 9);
        sawSeven = sawSeven || edge.source == 7;
        sawNine = sawNine || edge.source == 9;
    }

    CHECK(sawSeven);
    CHECK(sawNine);
}

TEST(planar_map_clear_forgets_everything)
{
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 10.0, 10.0));
    map.build();
    CHECK(!map.faces().empty());

    map.clear();
    map.build();

    CHECK(map.vertices().empty());
    CHECK(map.halfEdges().empty());
    CHECK(map.faces().empty());
}

/// Two curves leaving a vertex along the same tangent but bending apart are
/// ordered by where they have got to a little way out, and a curve whose
/// first control point sits on the vertex (a retracted pen handle) still
/// finds its place.
TEST(planar_map_orders_edges_by_where_they_leave)
{
    PlanarMap map;
    // A square, with two curves from its top-left corner into the inside:
    // both set off straight down, one bending right, one bending further
    // right, and one of them with a retracted first handle.
    addPolygon(map, rectangle(0.0, 0.0, 100.0, 100.0));
    map.addCurve(Curve::cubic(Point(0.0, 0.0), Point(0.0, 0.0), Point(20.0, 60.0), Point(60.0, 100.0)), 1);
    map.addCurve(Curve::cubic(Point(0.0, 0.0), Point(0.0, 40.0), Point(60.0, 40.0), Point(100.0, 60.0)), 2);
    map.build();

    // Every half-edge still finds a face and every face walk closes: the
    // ordering round the corner did not link the wrong edges.
    for (const fla::HalfEdge& half : map.halfEdges())
    {
        CHECK(half.face >= 0);
        CHECK(half.next >= 0);
    }
    // The two curves cross once inside, so the square is cut into four.
    CHECK(boundedFaces(map).size() == 4);
}

/// A cubic that crosses itself is cut at the crossing, and the loop between
/// the two cuts, which starts and ends at the same vertex, is a face of its
/// own rather than a dropped piece.
TEST(a_curve_that_loops_back_on_itself_encloses_a_face)
{
    PlanarMap map;
    map.addCurve(Curve::cubic(Point(0.0, 0.0), Point(120.0, 80.0), Point(-80.0, 80.0), Point(40.0, 0.0)), 0);
    map.build();

    CHECK(boundedFaces(map).size() == 1);
    for (const fla::HalfEdge& half : map.halfEdges())
    {
        CHECK(half.face >= 0);
        CHECK(half.next >= 0);
    }
}

TEST(planar_map_arranges_a_dense_grid_cell_by_cell)
{
    // Lines crossing in a grid, each a little past the last line across it:
    // every cell is a face, a point in a cell finds that face, and painting a
    // face touches its own four sides and nothing else.
    constexpr int kLines = 24;
    constexpr double kSpacing = 10.0;
    const double far = (kLines - 1) * kSpacing + 1.0;
    PlanarMap map;
    for (int i = 0; i < kLines; ++i)
    {
        map.addCurve(Curve::line(Point(-1.0, i * kSpacing), Point(far, i * kSpacing)), i);
        map.addCurve(Curve::line(Point(i * kSpacing, -1.0), Point(i * kSpacing, far)), kLines + i);
    }
    map.build();

    CHECK(boundedFaces(map).size() == static_cast<size_t>((kLines - 1) * (kLines - 1)));

    std::vector<bool> seen(map.faces().size(), false);
    bool allFound = true;
    bool allFourSided = true;
    for (int row = 0; row + 1 < kLines; ++row)
    {
        for (int column = 0; column + 1 < kLines; ++column)
        {
            const Point middle((column + 0.5) * kSpacing, (row + 0.5) * kSpacing);
            const int face = map.faceAt(middle);
            if (face < 0 || seen[face])
            {
                allFound = false;
                continue;
            }
            seen[face] = true;
            const std::vector<int>& sides = map.faceHalfEdges(face);
            allFourSided = allFourSided && sides.size() == 4;
            for (const int side : sides)
                allFourSided = allFourSided && map.halfEdges()[side].face == face;
        }
    }
    CHECK(allFound);
    CHECK(allFourSided);
    CHECK(map.faceAt(Point(-5.0, -5.0)) == -1);

    const int painted = map.faceAt(Point(15.0, 25.0));
    map.setFaceFill(painted, 7);
    int sevens = 0;
    for (const HalfEdge& half : map.halfEdges())
    {
        if (half.leftFill == 7)
        {
            ++sevens;
            CHECK(half.face == painted);
        }
    }
    CHECK(sevens == 4);
    CHECK(map.fillAt(Point(15.0, 25.0)) == 7);
}

TEST(planar_map_leaves_out_curves_that_are_not_numbers)
{
    // A curve with a coordinate that is not a number has no place on the
    // grid; the rest arrange as if it were not there.
    PlanarMap map;
    addPolygon(map, rectangle(0.0, 0.0, 10.0, 10.0), 0);
    map.addCurve(Curve::line(Point(std::nan(""), 0.0), Point(5.0, 5.0)), 1);
    map.addCurve(Curve::line(Point(0.0, 5.0), Point(HUGE_VAL, 5.0)), 2);
    map.addCurve(Curve::cubic(Point(0.0, 0.0), Point(1.0e300, 3.0), Point(4.0, 4.0), Point(10.0, 10.0)), 3);
    map.build();

    CHECK(map.vertices().size() == 4);
    CHECK(boundedFaces(map).size() == 1);
    CHECK(map.faceAt(Point(5.0, 5.0)) >= 0);
}
