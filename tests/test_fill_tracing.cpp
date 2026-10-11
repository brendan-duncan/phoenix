#include "test_util.h"

#include "../src/data/edge.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/edit/magic_wand.h"
#include "../src/edit/region_split.h"
#include "../src/geom/curve.h"
#include "../src/geom/fill_tracing.h"
#include "../src/parser/path_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using fla::Point;

namespace {

/// A 100 pixel square of fill 1, as one closed edge.
std::unique_ptr<fla::Shape> square()
{
    std::unique_ptr<fla::Shape> shape(new fla::Shape(nullptr));
    fla::SolidColor* fill = new fla::SolidColor(shape.get());
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|2000 0|2000 2000|0 2000|0 0", shape.get());
    edge->fillStyle1 = 1;
    edge->fillStyle0 = -1;
    edge->strokeStyle = -1;
    shape->edges.push_back(edge);
    return shape;
}

/// The points along a path, walked forward or backward, curves sampled.
std::vector<Point> walk(const fla::Path* path, bool reversed)
{
    std::vector<Point> points;
    Point at;
    for (const fla::PathSegment* segment : path->segments)
    {
        if (segment->points.empty())
            continue;
        switch (segment->command)
        {
        case fla::PathSegment::Command::Move:
            at = segment->points[0];
            points.push_back(at);
            break;
        case fla::PathSegment::Command::Line:
            at = segment->points[0];
            points.push_back(at);
            break;
        case fla::PathSegment::Command::Quad:
        case fla::PathSegment::Command::Cubic:
        {
            const fla::Curve curve = segment->command == fla::PathSegment::Command::Quad
                ? fla::Curve::quadratic(at, segment->points[0], segment->points[1])
                : fla::Curve::cubic(at, segment->points[0], segment->points[1], segment->points[2]);
            for (int i = 1; i <= 16; ++i)
                points.push_back(curve.pointAt(i / 16.0));
            at = segment->points.back();
            break;
        }
        default:
            break;
        }
    }
    if (reversed)
        std::reverse(points.begin(), points.end());
    return points;
}

/// A fill's traced outlines as polygons, curves sampled.
std::vector<std::vector<Point>> tracedPolygons(const fla::Shape& shape, int fill)
{
    std::vector<std::vector<Point>> polygons;
    const auto traced = fla::traceFills(shape);
    const auto found = traced.find(fill);
    if (found == traced.end())
        return polygons;
    for (const fla::TracedLoop& loop : found->second)
    {
        std::vector<Point> points;
        for (const fla::TracedLeg& leg : loop.legs)
        {
            const std::vector<Point> legPoints = walk(leg.path, leg.reversed);
            points.insert(points.end(), legPoints.begin(), legPoints.end());
        }
        polygons.push_back(points);
    }
    return polygons;
}

/// Whether \a p is painted by \a polygons drawn as the renderer draws a fill:
/// nonzero winding.
bool painted(const std::vector<std::vector<Point>>& polygons, const Point& p)
{
    int winding = 0;
    for (const std::vector<Point>& polygon : polygons)
    {
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Point& a = polygon[i];
            const Point& b = polygon[(i + 1) % polygon.size()];
            if ((a.y <= p.y) == (b.y <= p.y))
                continue;
            const double x = a.x + (b.x - a.x) * (p.y - a.y) / (b.y - a.y);
            if (x > p.x)
                winding += b.y > a.y ? 1 : -1;
        }
    }
    return winding != 0;
}

/// The area a fill paints within a box, sampled on a grid of \a step.
double tracedArea(const fla::Shape& shape, int fill, double left = 0.0, double top = 0.0, double right = 100.0,
    double bottom = 100.0, double step = 0.25)
{
    const std::vector<std::vector<Point>> polygons = tracedPolygons(shape, fill);
    double area = 0.0;
    for (double y = top + step / 2; y < bottom; y += step)
        for (double x = left + step / 2; x < right; x += step)
            if (painted(polygons, Point(x, y)))
                area += step * step;
    return area;
}

bool insideLoops(const std::vector<std::vector<Point>>& loops, const Point& p)
{
    bool inside = false;
    for (const std::vector<Point>& loop : loops)
    {
        for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
        {
            if ((loop[i].y > p.y) != (loop[j].y > p.y) &&
                p.x < (loop[j].x - loop[i].x) * (p.y - loop[i].y) / (loop[j].y - loop[i].y) + loop[i].x)
            {
                inside = !inside;
            }
        }
    }
    return inside;
}

} // namespace

TEST(traced_fill_of_a_square_is_its_area)
{
    std::unique_ptr<fla::Shape> shape = square();
    CHECK(std::fabs(tracedArea(*shape, 1) - 10000.0) < 1.0e-6);
}

TEST(a_ragged_cut_leaves_both_parts_whole)
{
    // A blob with one-pixel strands and a speckled rim, as the magic wand
    // finds in a noisy bitmap, placed across the square's right side. Its
    // outline, smoothed but not tidied, has sides running a pixel or less
    // apart: cutting along it once left a wedge of the piece undrawn.
    const int size = 40;
    std::vector<uint8_t> mask(size * size, 0);
    unsigned seed = 12345;
    const auto random = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7fff;
    };
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const double d = std::hypot(x - 20.0, y - 20.0);
            bool in = d < 12.0 || (d < 14.0 && random() % 3 == 0);
            // Strands a pixel wide.
            if ((y == 20 && x >= 20 && x < 38) || (x == 8 && y >= 4 && y < 20) || (x == y && x < 20))
                in = true;
            mask[y * size + x] = in ? 1 : 0;
        }
    }

    // At a stage pixel or more to the bitmap's, and at a fraction of one, as
    // when the bitmap of a fill is scaled down: then the outline's sides run
    // within a twip or two of each other.
    for (double scale : {2.0, 0.5, 0.2, 0.1})
    for (fla::MagicWand::Smoothing smoothing : {fla::MagicWand::Smoothing::Pixels, fla::MagicWand::Smoothing::Rough,
             fla::MagicWand::Smoothing::Normal, fla::MagicWand::Smoothing::Smooth})
    {
        // Across the square's right side, half in.
        const double originX = 100.0 - 20.0 * scale;
        const double originY = 50.0 - 20.0 * scale;
        std::vector<std::vector<Point>> loops;
        for (const std::vector<Point>& loop : fla::MagicWand::outline(mask, size, size))
        {
            std::vector<Point> placed;
            for (const Point& p : fla::MagicWand::smooth(loop, smoothing))
                placed.push_back(Point(originX + p.x * scale, originY + p.y * scale));
            loops.push_back(placed);
        }
        std::vector<fla::Curve> cutters;
        for (const std::vector<Point>& loop : loops)
        {
            for (size_t i = 0; i < loop.size(); ++i)
                cutters.push_back(fla::Curve::line(loop[i], loop[(i + 1) % loop.size()]));
        }

        std::unique_ptr<fla::Shape> shape = square();
        fla::RegionSplitter regions(*shape, cutters);
        const auto inside = [&](const Point& p) { return insideLoops(loops, p); };
        const std::vector<int> faces = regions.facesWhere(inside);
        fla::Shape* piece = nullptr;
        fla::Shape* remainder = nullptr;
        CHECK(regions.splitSet(faces, {}, piece, remainder));
        std::unique_ptr<fla::Shape> pieceOwner(piece);
        std::unique_ptr<fla::Shape> remainderOwner(remainder);
        if (!piece || !remainder)
            continue;

        // Over the blob's box, sampled finely enough for its strands: what
        // should be cut out is what is inside both.
        const double left = originX;
        const double top = originY;
        const double right = std::min(100.0, originX + size * scale);
        const double bottom = originY + size * scale;
        const double step = scale / 8.0;
        double expected = 0.0;
        for (double y = top + step / 2; y < bottom; y += step)
            for (double x = left + step / 2; x < right; x += step)
                if (inside(Point(x, y)))
                    expected += step * step;
        const double box = (right - left) * (bottom - top);

        const double cut = tracedArea(*piece, 1, left, top, right, bottom, step);
        const double rest = tracedArea(*remainder, 1, left, top, right, bottom, step);
        const double pixel = scale * scale;
        std::printf("    scale %.1f smoothing %d: piece %.3f (expected %.3f), rest %.3f, box %.3f\n", scale,
            static_cast<int>(smoothing), cut, expected, rest, box);
        CHECK(std::fabs(cut - expected) < expected * 0.01 + pixel);
        CHECK(std::fabs(cut + rest - box) < pixel);
    }
}
