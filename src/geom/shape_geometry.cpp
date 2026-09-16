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

} // namespace fla
