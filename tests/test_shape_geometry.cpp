#include "test_util.h"

#include "../src/data/fla_document.h"
#include "../src/data/group.h"
#include "../src/data/shape.h"
#include "../src/geom/fill_tracing.h"
#include "../src/geom/planar_map.h"
#include "../src/geom/shape_geometry.h"
#include "../src/parser/fla_parser.h"
#include "../src/parser/path_parser.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

using fla::PlanarMap;
using fla::Point;
using fla::Shape;
using fla::ShapeCurve;

namespace {

/// Builds a shape holding a single edge parsed from edge data.
std::unique_ptr<Shape> shapeFromEdges(const std::string& data,
    int fillStyle0, int fillStyle1, int strokeStyle)
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));

    PathParser parser;
    fla::Edge* edge = parser.parse(data, shape.get());
    if (!edge)
        return shape;

    edge->fillStyle0 = fillStyle0;
    edge->fillStyle1 = fillStyle1;
    edge->strokeStyle = strokeStyle;
    shape->edges.push_back(edge);

    return shape;
}

/// Collects every shape in a document, wherever it sits.
void collectShapes(const fla::Timeline* timeline, std::vector<const Shape*>& found);

void collectFromElement(const fla::Element* element, std::vector<const Shape*>& found)
{
    if (!element)
        return;

    if (element->elementType() == fla::Element::Type::Shape)
    {
        found.push_back(static_cast<const Shape*>(element));
        return;
    }

    if (element->elementType() == fla::Element::Type::Group)
    {
        for (const fla::Element* member : static_cast<const fla::Group*>(element)->members)
            collectFromElement(member, found);
    }
}

void collectShapes(const fla::Timeline* timeline, std::vector<const Shape*>& found)
{
    if (!timeline)
        return;

    for (const fla::Layer* layer : timeline->layers)
    {
        if (!layer)
            continue;
        for (const fla::Frame* frame : layer->frames)
        {
            if (!frame)
                continue;
            for (const fla::Element* element : frame->elements)
                collectFromElement(element, found);
        }
    }
}

} // namespace

TEST(shape_curves_turns_lines_into_curves)
{
    std::unique_ptr<Shape> shape = shapeFromEdges("!0 0|100 0|100 100", -1, 1, -1);
    const std::vector<ShapeCurve> curves = fla::shapeCurves(*shape);

    CHECK(curves.size() == 2);
    if (curves.size() != 2)
        return;

    CHECK(curves[0].curve.isLine());
    CHECK(curves[0].fillStyle1 == 1);
    CHECK(curves[0].fillStyle0 == -1);
    // The curves run end to end, so the arrangement can weld them.
    CHECK(curves[0].curve.end().x == curves[1].curve.start().x);
}

TEST(shape_curves_raises_quadratics)
{
    std::unique_ptr<Shape> shape = shapeFromEdges("!0 0[60 60 120 0", -1, 1, -1);
    const std::vector<ShapeCurve> curves = fla::shapeCurves(*shape);

    CHECK(curves.size() == 1);
    if (curves.empty())
        return;

    CHECK(!curves[0].curve.isLine());
    // Same curve, expressed as a cubic.
    CHECK(std::fabs(curves[0].curve.pointAt(0.5).y - 1.5) < 1.0e-9);
}

TEST(shape_curves_carries_both_side_styles)
{
    std::unique_ptr<Shape> shape = shapeFromEdges("!0 0|100 0", 3, 7, 2);
    const std::vector<ShapeCurve> curves = fla::shapeCurves(*shape);

    CHECK(curves.size() == 1);
    if (curves.empty())
        return;

    // Which side is which depends on the direction, so both have to travel with
    // the geometry.
    CHECK(curves[0].fillStyle0 == 3);
    CHECK(curves[0].fillStyle1 == 7);
    CHECK(curves[0].strokeStyle == 2);
}

TEST(shape_curves_closes_a_closed_outline)
{
    // The reader turns a path that returns to its start into an explicit close.
    std::unique_ptr<Shape> shape =
        shapeFromEdges("!0 0|100 0|100 100|0 100|0 0!200 200|300 200", -1, 1, -1);
    const std::vector<ShapeCurve> curves = fla::shapeCurves(*shape);

    // The square's four sides plus the stray line.
    CHECK(curves.size() == 5);
}

TEST(shape_curves_ignores_an_empty_shape)
{
    Shape shape(nullptr);
    CHECK(fla::shapeCurves(shape).empty());
}

TEST(real_shapes_agree_on_which_side_a_fill_is)
{
    // The format records a fill for each side of an edge, but says nothing about
    // which side is which: that is a convention. Rather than guess it, this
    // reads real files and checks which reading makes them consistent.
    //
    // For a face of the arrangement, every half-edge around it borders the same
    // region, so every one of them must name the same fill. Under the right
    // convention they agree; under the wrong one they contradict each other.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }

    int leftIsStyle1 = 0;
    int leftIsStyle0 = 0;
    int facesTested = 0;
    int documents = 0;

    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        if (documents >= 8)
            break;

        std::string path;
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "DOMDocument.xml"))
            path = entry.path().string();
        else if (entry.is_regular_file() && entry.path().extension() == ".fla")
            path = entry.path().string();
        else
            continue;

        FLAParser parser;
        std::unique_ptr<fla::FLADocument> document(parser.parse(path));
        if (!document || !document->document)
            continue;

        ++documents;

        std::vector<const Shape*> shapes;
        for (const fla::Timeline* timeline : document->document->timelines)
            collectShapes(timeline, shapes);

        for (const Shape* shape : shapes)
        {
            const std::vector<ShapeCurve> curves = fla::shapeCurves(*shape);

            // Only shapes small enough to arrange quickly, and only ones that
            // actually record a fill on both sides somewhere.
            if (curves.empty() || curves.size() > 60)
                continue;

            bool hasTwoSided = false;
            for (const ShapeCurve& curve : curves)
                hasTwoSided = hasTwoSided || (curve.fillStyle0 != -1 && curve.fillStyle1 != -1);
            if (!hasTwoSided)
                continue;

            PlanarMap map;
            for (size_t i = 0; i < curves.size(); ++i)
                map.addCurve(curves[i].curve, static_cast<int>(i));
            map.build();

            // Group the half-edges by the face they border.
            std::vector<std::vector<int>> byFace(map.faces().size());
            for (size_t i = 0; i < map.halfEdges().size(); ++i)
            {
                const int face = map.halfEdges()[i].face;
                if (face >= 0 && face < static_cast<int>(byFace.size()))
                    byFace[face].push_back(static_cast<int>(i));
            }

            for (size_t face = 0; face < byFace.size(); ++face)
            {
                if (map.faces()[face].unbounded || byFace[face].size() < 2)
                    continue;

                bool style1Consistent = true;
                bool style0Consistent = true;
                int expected1 = -2;
                int expected0 = -2;

                for (int halfEdge : byFace[face])
                {
                    const fla::HalfEdge& edge = map.halfEdges()[halfEdge];
                    if (edge.source < 0 || edge.source >= static_cast<int>(curves.size()))
                        continue;

                    const ShapeCurve& source = curves[edge.source];

                    // Reading the edge forward puts one style on the left; going
                    // the other way swaps them.
                    const int ifStyle1 = edge.forward ? source.fillStyle1 : source.fillStyle0;
                    const int ifStyle0 = edge.forward ? source.fillStyle0 : source.fillStyle1;

                    if (expected1 == -2)
                    {
                        expected1 = ifStyle1;
                        expected0 = ifStyle0;
                        continue;
                    }

                    style1Consistent = style1Consistent && ifStyle1 == expected1;
                    style0Consistent = style0Consistent && ifStyle0 == expected0;
                }

                ++facesTested;
                if (style1Consistent)
                    ++leftIsStyle1;
                if (style0Consistent)
                    ++leftIsStyle0;
            }
        }
    }

    std::printf("    %d documents, %d faces: fillStyle1-on-left agreed %d, "
        "fillStyle0-on-left agreed %d\n",
        documents, facesTested, leftIsStyle1, leftIsStyle0);

    CHECK(facesTested > 0);
    if (facesTested == 0)
        return;

    // Real files answer this: fillStyle1 is the fill on the left of the edge's
    // direction, fillStyle0 the one on the right. Every face agreed under that
    // reading and almost none under the other.
    //
    // That it agrees at all is the stronger result: it means the arrangement
    // reproduces the topology these files encode, not just something plausible.
    const double share = static_cast<double>(leftIsStyle1) / facesTested;
    std::printf("    fillStyle1-on-left agreement %.0f%%\n", share * 100.0);

    CHECK(share >= 0.9);
    CHECK(leftIsStyle1 > leftIsStyle0);
}

namespace {

/// Runs a shape through the arrangement and back, keeping its own fills.
void roundTripThroughMap(Shape& shape)
{
    const std::vector<ShapeCurve> curves = fla::shapeCurves(shape);

    PlanarMap map;
    for (size_t i = 0; i < curves.size(); ++i)
        map.addCurve(curves[i].curve, static_cast<int>(i));
    map.build();

    fla::attributeFillsFromSource(map, curves);
    fla::rebuildShapeEdges(shape, map, curves);
}

/// The fills a shape's edges mention, so two shapes can be compared on what they
/// paint rather than on how they were written.
std::vector<int> mentionedFills(const Shape& shape)
{
    std::vector<int> fills;
    for (const fla::Edge* edge : shape.edges)
    {
        for (int fill : {edge->fillStyle0, edge->fillStyle1})
        {
            if (fill == -1)
                continue;
            bool seen = false;
            for (int existing : fills)
                seen = seen || existing == fill;
            if (!seen)
                fills.push_back(fill);
        }
    }
    std::sort(fills.begin(), fills.end());
    return fills;
}

} // namespace

TEST(rebuilding_a_square_keeps_its_fill_on_the_inside)
{
    std::unique_ptr<Shape> shape =
        shapeFromEdges("!0 0|2000 0|2000 2000|0 2000|0 0", -1, 1, -1);

    roundTripThroughMap(*shape);

    // Four sides, each knowing the fill is on one side and nothing on the other.
    CHECK(shape->edges.size() == 4);

    for (const fla::Edge* edge : shape->edges)
    {
        const bool oneSided = (edge->fillStyle1 == 1 && edge->fillStyle0 == -1) ||
                              (edge->fillStyle0 == 1 && edge->fillStyle1 == -1);
        CHECK(oneSided);
    }
}

TEST(rebuilding_drops_edges_that_draw_nothing)
{
    // A square with a line across it that has no fill on either side and no
    // stroke. The line splits the arrangement, but once both halves carry the
    // same fill there is nothing to draw along it.
    std::unique_ptr<Shape> shape =
        shapeFromEdges("!0 0|2000 0|2000 2000|0 2000|0 0", -1, 1, -1);

    PathParser parser;
    fla::Edge* cut = parser.parse("!1000 0|1000 2000", shape.get());
    CHECK(cut != nullptr);
    if (!cut)
        return;
    cut->fillStyle0 = 1;
    cut->fillStyle1 = 1;
    cut->strokeStyle = -1;
    shape->edges.push_back(cut);

    roundTripThroughMap(*shape);

    // The seam is gone: every surviving edge has something different on each
    // side, so it is a real boundary.
    for (const fla::Edge* edge : shape->edges)
        CHECK(edge->fillStyle0 != edge->fillStyle1);

    // Four: the cut met the top and bottom edges and split each in two, but
    // with the seam gone nothing else meets there, and the two halves of
    // each side are joined back into one edge.
    CHECK(shape->edges.size() == 4);
}

TEST(rebuilding_keeps_a_stroke_with_no_fill)
{
    // A bare line has nothing on either side but still has to survive, because
    // the stroke is the whole of it.
    std::unique_ptr<Shape> shape = shapeFromEdges("!0 0|2000 0", -1, -1, 1);

    roundTripThroughMap(*shape);

    CHECK(shape->edges.size() == 1);
    if (shape->edges.empty())
        return;
    CHECK(shape->edges[0]->strokeStyle == 1);
}

TEST(rebuilding_splits_where_two_outlines_cross)
{
    // Two squares overlapping. Going through the arrangement has to cut both
    // outlines at the crossings, so the result has more edges than it started
    // with.
    std::unique_ptr<Shape> shape =
        shapeFromEdges("!0 0|2000 0|2000 2000|0 2000|0 0", -1, 1, -1);

    PathParser parser;
    fla::Edge* second = parser.parse("!1000 1000|3000 1000|3000 3000|1000 3000|1000 1000",
        shape.get());
    CHECK(second != nullptr);
    if (!second)
        return;
    second->fillStyle0 = -1;
    second->fillStyle1 = 1;
    shape->edges.push_back(second);

    const size_t before = shape->edges.size();
    roundTripThroughMap(*shape);

    CHECK(shape->edges.size() > before);

    // Every surviving edge draws something.
    for (const fla::Edge* edge : shape->edges)
    {
        const bool draws = edge->fillStyle0 != -1 || edge->fillStyle1 != -1 ||
            edge->strokeStyle != -1;
        CHECK(draws);
    }
}

TEST(real_shapes_survive_a_trip_through_the_arrangement)
{
    // Taking a shape apart into an arrangement and putting it back together must
    // not change what it paints.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }

    int checked = 0;
    int mismatched = 0;
    int documents = 0;

    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        if (documents >= 6)
            break;

        std::string path;
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "DOMDocument.xml"))
            path = entry.path().string();
        else if (entry.is_regular_file() && entry.path().extension() == ".fla")
            path = entry.path().string();
        else
            continue;

        FLAParser parser;
        std::unique_ptr<fla::FLADocument> document(parser.parse(path));
        if (!document || !document->document)
            continue;

        ++documents;

        std::vector<const Shape*> shapes;
        for (const fla::Timeline* timeline : document->document->timelines)
            collectShapes(timeline, shapes);

        for (const Shape* original : shapes)
        {
            const std::vector<ShapeCurve> curves = fla::shapeCurves(*original);
            if (curves.empty() || curves.size() > 40)
                continue;

            // Work on a copy so the document is left alone.
            Shape rebuilt(nullptr);
            for (const ShapeCurve& source : curves)
            {
                fla::Edge* edge = new fla::Edge(&rebuilt);
                edge->fillStyle0 = source.fillStyle0;
                edge->fillStyle1 = source.fillStyle1;
                edge->strokeStyle = source.strokeStyle;

                fla::Path* p = new fla::Path(edge);
                p->segments.push_back(new fla::PathSegment(
                    fla::PathSegment::Command::Move, {source.curve.start()}, p));
                if (source.curve.isLine())
                {
                    p->segments.push_back(new fla::PathSegment(
                        fla::PathSegment::Command::Line, {source.curve.end()}, p));
                }
                else
                {
                    p->segments.push_back(new fla::PathSegment(
                        fla::PathSegment::Command::Cubic,
                        {source.curve.controlPoint(1), source.curve.controlPoint(2),
                         source.curve.end()}, p));
                }
                edge->paths.push_back(p);
                rebuilt.edges.push_back(edge);
            }

            const std::vector<int> before = mentionedFills(rebuilt);
            roundTripThroughMap(rebuilt);
            const std::vector<int> after = mentionedFills(rebuilt);

            ++checked;
            if (before != after)
                ++mismatched;
        }
    }

    std::printf("    %d documents, %d shapes: %d changed which fills they use\n",
        documents, checked, mismatched);

    CHECK(checked > 0);
    if (checked == 0)
        return;

    // The same fills have to come out as went in. A shape that loses one has had
    // a region misattributed.
    const double kept = 1.0 - static_cast<double>(mismatched) / checked;
    std::printf("    fills preserved for %.0f%% of shapes\n", kept * 100.0);
    CHECK(kept >= 0.9);
}

namespace {

using Polygon = std::vector<Point>;

/// A loop of curves as a fine polyline.
Polygon flattenLoop(const std::vector<fla::Curve>& loop)
{
    Polygon points;
    for (const fla::Curve& curve : loop)
    {
        const int steps = curve.isLine() ? 1 : 32;
        for (int i = points.empty() ? 0 : 1; i <= steps; ++i)
            points.push_back(curve.pointAt(static_cast<double>(i) / steps));
    }
    return points;
}

/// The curves of a path, walked forward or backward.
std::vector<fla::Curve> pathCurves(const fla::Path& path, bool reversed)
{
    std::vector<fla::Curve> curves;
    Point at;
    for (const fla::PathSegment* segment : path.segments)
    {
        using Cmd = fla::PathSegment::Command;
        if (segment->command == Cmd::Move && !segment->points.empty())
            at = segment->points[0];
        else if (segment->command == Cmd::Line && !segment->points.empty())
            curves.push_back(fla::Curve::line(at, segment->points[0]));
        else if (segment->command == Cmd::Quad && segment->points.size() >= 2)
            curves.push_back(fla::Curve::quadratic(at, segment->points[0], segment->points[1]));
        else if (segment->command == Cmd::Cubic && segment->points.size() >= 3)
            curves.push_back(fla::Curve::cubic(at, segment->points[0], segment->points[1], segment->points[2]));
        if (!curves.empty())
            at = curves.back().end();
    }
    if (reversed)
    {
        std::reverse(curves.begin(), curves.end());
        for (fla::Curve& curve : curves)
        {
            curve = curve.isLine() ? fla::Curve::line(curve.end(), curve.start())
                : fla::Curve::cubic(curve.end(), curve.controlPoint(2), curve.controlPoint(1), curve.start());
        }
    }
    return curves;
}

/// Each fill's outlines as polylines, the way the stage traces them.
std::map<int, std::vector<Polygon>> tracedPolygons(const Shape& shape)
{
    std::map<int, std::vector<Polygon>> polygons;
    for (const auto& [fill, loops] : fla::traceFills(shape))
    {
        for (const fla::TracedLoop& loop : loops)
        {
            std::vector<fla::Curve> curves;
            for (const fla::TracedLeg& leg : loop.legs)
            {
                for (const fla::Curve& curve : pathCurves(*leg.path, leg.reversed))
                    curves.push_back(curve);
            }
            if (!curves.empty())
                polygons[fill].push_back(flattenLoop(curves));
        }
    }
    return polygons;
}

/// The same read off the planar map, or nothing when the map declines.
bool mapPolygons(const Shape& shape, std::map<int, std::vector<Polygon>>& polygons)
{
    std::map<int, std::vector<std::vector<fla::Curve>>> outlines;
    if (!fla::fillOutlinesFromMap(shape, outlines))
        return false;
    for (const auto& [fill, loops] : outlines)
    {
        for (const std::vector<fla::Curve>& loop : loops)
            polygons[fill].push_back(flattenLoop(loop));
    }
    return true;
}

/// The non-zero winding rule over a fill's polylines, each closed.
bool windsRound(const std::vector<Polygon>& polygons, const Point& point)
{
    int winding = 0;
    for (const Polygon& polygon : polygons)
    {
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Point& a = polygon[i];
            const Point& b = polygon[(i + 1) % polygon.size()];
            if ((a.y <= point.y) == (b.y <= point.y))
                continue;
            const double x = a.x + (b.x - a.x) * (point.y - a.y) / (b.y - a.y);
            if (x > point.x)
                winding += b.y > a.y ? 1 : -1;
        }
    }
    return winding != 0;
}

/// Whether a point is within \a reach of any of the polylines.
bool nearOutline(const std::map<int, std::vector<Polygon>>& polygons, const Point& point, double reach)
{
    for (const auto& entry : polygons)
    {
        for (const Polygon& polygon : entry.second)
        {
            for (size_t i = 0; i < polygon.size(); ++i)
            {
                const Point& a = polygon[i];
                const Point& b = polygon[(i + 1) % polygon.size()];
                const double dx = b.x - a.x;
                const double dy = b.y - a.y;
                const double lengthSquared = dx * dx + dy * dy;
                double t = lengthSquared > 0.0
                    ? ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                if (std::hypot(a.x + dx * t - point.x, a.y + dy * t - point.y) < reach)
                    return true;
            }
        }
    }
    return false;
}

/// Compares what the two ways of drawing paint on a grid over the shape,
/// away from the outlines where only antialiasing differs. Returns how many
/// points disagree about some fill.
int disagreements(const Shape& shape, const std::map<int, std::vector<Polygon>>& fromMap,
    int grid = 24)
{
    const std::map<int, std::vector<Polygon>> traced = tracedPolygons(shape);
    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    for (const auto& entry : traced)
    {
        for (const Polygon& polygon : entry.second)
        {
            for (const Point& p : polygon)
            {
                left = std::min(left, p.x);
                right = std::max(right, p.x);
                top = std::min(top, p.y);
                bottom = std::max(bottom, p.y);
            }
        }
    }
    if (left > right)
        return 0;

    std::vector<int> fills;
    for (const auto& entry : traced)
        fills.push_back(entry.first);
    for (const auto& entry : fromMap)
        fills.push_back(entry.first);

    int count = 0;
    for (int i = 0; i <= grid; ++i)
    {
        for (int j = 0; j <= grid; ++j)
        {
            const Point point(left + (right - left) * (i + 0.37) / (grid + 1),
                top + (bottom - top) * (j + 0.61) / (grid + 1));
            if (nearOutline(traced, point, 0.25) || nearOutline(fromMap, point, 0.25))
                continue;
            for (const int fill : fills)
            {
                const auto a = traced.find(fill);
                const auto b = fromMap.find(fill);
                const bool tracedPaints = a != traced.end() && windsRound(a->second, point);
                const bool mapPaints = b != fromMap.end() && windsRound(b->second, point);
                if (tracedPaints != mapPaints)
                {
                    ++count;
                    break;
                }
            }
        }
    }
    return count;
}

/// A shape of several filled pieces as merge drawing leaves them: two
/// colors meeting along a shared edge, an island of a third inside one, and
/// a stroke running through the other.
std::unique_ptr<Shape> mergedArtwork()
{
    std::unique_ptr<Shape> shape(new Shape(nullptr));
    PathParser parser;
    const auto add = [&](const std::string& data, int fill0, int fill1, int stroke) {
        fla::Edge* edge = parser.parse(data, shape.get());
        if (!edge)
            return;
        edge->fillStyle0 = fill0;
        edge->fillStyle1 = fill1;
        edge->strokeStyle = stroke;
        shape->edges.push_back(edge);
    };
    // Left square red (1), right square blue (2), sharing x = 2000. Walked
    // clockwise on screen, a region lies on the fillStyle1 side.
    add("!0 0|2000 0", -1, 1, -1);
    add("!2000 0|2000 2000", 2, 1, -1);
    add("!2000 2000|0 2000|0 0", -1, 1, -1);
    add("!2000 0|4000 0|4000 2000|2000 2000", -1, 2, -1);
    // A curved island of green (3) in the red square.
    add("!500 1000[500 500 1000 500[1500 500 1500 1000[1500 1500 1000 1500[500 1500 500 1000", 1, 3, -1);
    // A stroke through the blue square: blue on both sides.
    add("!2500 200|3500 1800", 2, 2, 1);
    return shape;
}

} // namespace

TEST(fills_drawn_from_the_map_match_the_traced_fills)
{
    std::unique_ptr<Shape> shape = mergedArtwork();
    std::map<int, std::vector<Polygon>> fromMap;
    CHECK(mapPolygons(*shape, fromMap));
    CHECK(fromMap.size() == 3);
    CHECK(fromMap[1].size() == 2);
    // The blue square's outline, and the stroke inside it walked out and
    // back, which winds round nothing.
    CHECK(fromMap[2].size() == 2);
    CHECK(fromMap[3].size() == 1);
    CHECK(disagreements(*shape, fromMap, 40) == 0);

    // The island is a hole in the red, not painted red as well.
    CHECK(!windsRound(fromMap[1], Point(50.0, 50.0)));
    CHECK(windsRound(fromMap[3], Point(50.0, 50.0)));
    CHECK(windsRound(fromMap[1], Point(10.0, 10.0)));
}

TEST(fills_drawn_from_the_map_leave_inconsistent_shapes_to_the_tracing)
{
    // An outline that does not close: the map finds no face for the fill
    // its edges record, and declines.
    std::unique_ptr<Shape> open = shapeFromEdges("!0 0|2000 0|2000 2000|0 2000", -1, 1, -1);
    std::map<int, std::vector<std::vector<fla::Curve>>> outlines;
    CHECK(!fla::fillOutlinesFromMap(*open, outlines));

    // Two edges of one square disagreeing about its fill.
    std::unique_ptr<Shape> mixed = shapeFromEdges("!0 0|2000 0|2000 2000", -1, 1, -1);
    PathParser parser;
    fla::Edge* other = parser.parse("!2000 2000|0 2000|0 0", mixed.get());
    other->fillStyle1 = 2;
    mixed->edges.push_back(other);
    CHECK(!fla::fillOutlinesFromMap(*mixed, outlines));

    // A hidden edge is the tracing's to handle.
    std::unique_ptr<Shape> hidden = shapeFromEdges("!0 0|2000 0|2000 2000|0 2000|0 0", -1, 1, -1);
    hidden->edges.front()->visible = false;
    CHECK(!fla::fillOutlinesFromMap(*hidden, outlines));

    // The same square closed is drawn from the map.
    std::unique_ptr<Shape> closed = shapeFromEdges("!0 0|2000 0|2000 2000|0 2000|0 0", -1, 1, -1);
    CHECK(fla::fillOutlinesFromMap(*closed, outlines));
    CHECK(outlines.size() == 1 && outlines[1].size() == 1);
}

TEST(a_sliver_drawn_from_the_map_goes_with_its_neighbor)
{
    // Red below a straight edge, blue above a curve bowing a hair above it:
    // the hairline between them, painted nothing, would show the stage
    // through. Drawn from the map it is filled with the blue it borders most.
    std::unique_ptr<Shape> shape(new Shape(nullptr));
    PathParser parser;
    const auto add = [&](const std::string& data, int fill0, int fill1) {
        fla::Edge* edge = parser.parse(data, shape.get());
        edge->fillStyle0 = fill0;
        edge->fillStyle1 = fill1;
        shape->edges.push_back(edge);
    };
    add("!0 0|2000 0", -1, 1);
    add("!2000 0|2000 2000|0 2000|0 0", -1, 1);
    // A curve between the same ends bowing three hundredths of a pixel up.
    fla::Edge* curved = new fla::Edge(shape.get());
    curved->fillStyle0 = 2;
    fla::Path* path = new fla::Path(curved);
    path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Move, {Point(0.0, 0.0)}, path));
    path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Cubic,
        {Point(33.0, -0.04), Point(66.0, -0.04), Point(100.0, 0.0)}, path));
    curved->paths.push_back(path);
    shape->edges.push_back(curved);
    add("!2000 0|2000 -1000|0 -1000|0 0", 2, -1);

    std::map<int, std::vector<std::vector<fla::Curve>>> outlines;
    CHECK(fla::fillOutlinesFromMap(*shape, outlines));
    // The blue's outlines now include the sliver.
    double blueArea = 0.0;
    for (const std::vector<fla::Curve>& loop : outlines[2])
    {
        const Polygon polygon = flattenLoop(loop);
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Point& a = polygon[i];
            const Point& b = polygon[(i + 1) % polygon.size()];
            blueArea += (a.x * b.y - b.x * a.y) * 0.5;
        }
    }
    CHECK(std::fabs(std::fabs(blueArea) - 5000.0) < 0.01);
}

TEST(real_shapes_drawn_from_the_map_match_the_traced_fills)
{
    // Every shape of a few real documents the map agrees with is painted the
    // same both ways, away from the outlines.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }

    int documents = 0;
    int shapes = 0;
    int drawnFromMap = 0;
    int differing = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        if (documents >= 8)
            break;
        if (!entry.is_regular_file() || entry.path().extension() != ".fla")
            continue;

        FLAParser parser;
        std::unique_ptr<fla::FLADocument> document(parser.parse(entry.path().string()));
        if (!document || !document->document)
            continue;
        ++documents;

        std::vector<const Shape*> found;
        for (const fla::Timeline* timeline : document->document->timelines)
            collectShapes(timeline, found);

        for (const Shape* shape : found)
        {
            if (fla::shapeCurves(*shape).size() > 300)
                continue;
            ++shapes;
            std::map<int, std::vector<Polygon>> fromMap;
            if (!mapPolygons(*shape, fromMap))
                continue;
            ++drawnFromMap;
            if (disagreements(*shape, fromMap, 12) != 0)
                ++differing;
        }
    }

    std::printf("    %d documents, %d shapes, %d drawn from the map, %d painted differently\n",
        documents, shapes, drawnFromMap, differing);
    CHECK(differing == 0);
}
