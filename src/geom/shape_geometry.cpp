#include "shape_geometry.h"

#include "../data/shape.h"

#include <cmath>

namespace fla {

namespace {

bool samePoint(const Point& a, const Point& b)
{
    return std::fabs(a.x - b.x) < 1.0e-9 && std::fabs(a.y - b.y) < 1.0e-9;
}

} // namespace

std::vector<ShapeCurve> shapeCurves(const Shape& shape)
{
    std::vector<ShapeCurve> curves;

    for (size_t edgeIndex = 0; edgeIndex < shape.edges.size(); ++edgeIndex)
    {
        const Edge* edge = shape.edges[edgeIndex];
        if (!edge)
            continue;

        for (const Path* path : edge->paths)
        {
            if (!path)
                continue;

            // A style set on the path overrides the one on the edge, which is
            // how the format lets one edge carry several styles.
            ShapeCurve prototype;
            prototype.edgeIndex = static_cast<int>(edgeIndex);
            prototype.fillStyle0 = edge->fillStyle0;
            prototype.fillStyle1 = path->fillStyleIndex != -1
                ? path->fillStyleIndex : edge->fillStyle1;
            prototype.strokeStyle = path->lineStyleIndex != -1
                ? path->lineStyleIndex : edge->strokeStyle;

            Point current;
            Point subpathStart;
            bool started = false;

            for (const PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;

                switch (segment->command)
                {
                case PathSegment::Command::Move:
                    if (!segment->points.empty())
                    {
                        current = segment->points[0];
                        subpathStart = current;
                        started = true;
                    }
                    break;

                case PathSegment::Command::Line:
                    if (started && !segment->points.empty())
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::line(current, segment->points[0]);
                        curves.push_back(piece);
                        current = segment->points[0];
                    }
                    break;

                case PathSegment::Command::Quad:
                    if (started && segment->points.size() >= 2)
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::quadratic(current, segment->points[0],
                            segment->points[1]);
                        curves.push_back(piece);
                        current = segment->points[1];
                    }
                    break;

                case PathSegment::Command::Cubic:
                    if (started && segment->points.size() >= 3)
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::cubic(current, segment->points[0],
                            segment->points[1], segment->points[2]);
                        curves.push_back(piece);
                        current = segment->points[2];
                    }
                    break;

                case PathSegment::Command::Close:
                    // Closing runs back to where the subpath began, unless it is
                    // already there.
                    if (started && !samePoint(current, subpathStart))
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::line(current, subpathStart);
                        curves.push_back(piece);
                        current = subpathStart;
                    }
                    break;
                }
            }
        }
    }

    return curves;
}

void attributeFillsFromSource(PlanarMap& map, const std::vector<ShapeCurve>& sources)
{
    for (size_t face = 0; face < map.faces().size(); ++face)
    {
        const int index = static_cast<int>(face);

        if (map.faces()[face].unbounded)
        {
            map.setFaceFill(index, -1);
            continue;
        }

        // Any half-edge bordering the face answers the question; they all agree
        // when the arrangement matches the file it came from.
        int fill = -1;
        for (const HalfEdge& edge : map.halfEdges())
        {
            if (edge.face != index)
                continue;
            if (edge.source < 0 || edge.source >= static_cast<int>(sources.size()))
                continue;

            const ShapeCurve& source = sources[edge.source];
            fill = edge.forward ? source.fillStyle1 : source.fillStyle0;
            break;
        }

        map.setFaceFill(index, fill);
    }
}

void rebuildShapeEdges(Shape& shape, const PlanarMap& map,
    const std::vector<ShapeCurve>& sources)
{
    for (Edge* edge : shape.edges)
        delete edge;
    shape.edges.clear();

    for (const HalfEdge& half : map.halfEdges())
    {
        // One edge per pair, not per direction.
        if (!half.forward)
            continue;

        const int leftFill = half.leftFill;
        const int rightFill = half.twin >= 0 &&
            half.twin < static_cast<int>(map.halfEdges().size())
            ? map.halfEdges()[half.twin].leftFill : -1;

        int strokeStyle = -1;
        if (half.source >= 0 && half.source < static_cast<int>(sources.size()))
            strokeStyle = sources[half.source].strokeStyle;

        // An edge with the same thing on both sides separates nothing. Whether
        // that is two empty sides or the same fill on each, it draws nothing and
        // is left out -- which is exactly how the seam vanishes where two shapes
        // merged into one region.
        if (leftFill == rightFill && strokeStyle == -1)
            continue;

        Edge* edge = new Edge(&shape);
        edge->fillStyle1 = leftFill;
        edge->fillStyle0 = rightFill;
        edge->strokeStyle = strokeStyle;

        Path* path = new Path(edge);

        path->segments.push_back(new PathSegment(
            PathSegment::Command::Move, {half.curve.start()}, path));

        if (half.curve.isLine())
        {
            path->segments.push_back(new PathSegment(
                PathSegment::Command::Line, {half.curve.end()}, path));
        }
        else
        {
            path->segments.push_back(new PathSegment(
                PathSegment::Command::Cubic,
                {half.curve.controlPoint(1), half.curve.controlPoint(2), half.curve.end()},
                path));
        }

        edge->paths.push_back(path);
        shape.edges.push_back(edge);
    }
}

} // namespace fla
