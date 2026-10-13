#include "shape_transform.h"

#include "../data/shape.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fla {

namespace {

bool isIdentity(const Transform& t)
{
    return t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 && t.m22 == 1.0 &&
        t.tx == 0.0 && t.ty == 0.0;
}

} // namespace

void bakeTransform(Shape& shape)
{
    const Transform t = shape.transform;
    if (isIdentity(t))
        return;

    for (Edge* edge : shape.edges)
    {
        if (!edge)
            continue;

        for (Path* path : edge->paths)
        {
            if (!path)
                continue;

            for (PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;

                for (Point& point : segment->points)
                    point.transform(t);
            }
        }

        // The writer prefers the text an edge was read with, so leaving it in
        // place would save the geometry this just moved.
        edge->data.clear();
    }

    // Bounds are kept in the shape's own space, which is what just changed.
    shape.localBounds.transform(t);
    shape.bounds = shape.localBounds;

    shape.transformationPoint = shape.transformationPoint.transformed(t);

    shape.transform.reset();
}

namespace {

Point lerp(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

/// Splits a cubic at t, giving the two halves' control points.
void splitCubic(const Point p[4], double t, Point left[4], Point right[4])
{
    const Point p01 = lerp(p[0], p[1], t);
    const Point p12 = lerp(p[1], p[2], t);
    const Point p23 = lerp(p[2], p[3], t);
    const Point p012 = lerp(p01, p12, t);
    const Point p123 = lerp(p12, p23, t);
    const Point mid = lerp(p012, p123, t);
    left[0] = p[0];
    left[1] = p01;
    left[2] = p012;
    left[3] = mid;
    right[0] = mid;
    right[1] = p123;
    right[2] = p23;
    right[3] = p[3];
}

double polylineLength(const Point p[4])
{
    double length = 0.0;
    for (int i = 1; i < 4; ++i)
        length += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return length;
}

/// Appends a cubic as pieces short enough for the warp.
void appendPieces(std::vector<PathSegment*>& out, Path* path, const Point p[4], double pieceLength)
{
    // Clamped before it becomes an int: a length that is not a number, or
    // absurdly long, would otherwise be undefined or ask for billions of
    // pieces.
    constexpr double kMostPieces = 10000.0;
    const double pieces = std::ceil(polylineLength(p) / pieceLength);
    const int count = pieces > 1.0 ? static_cast<int>(std::min(pieces, kMostPieces)) : 1;
    Point rest[4] = {p[0], p[1], p[2], p[3]};
    for (int i = 0; i < count; ++i)
    {
        if (i == count - 1)
        {
            out.push_back(new PathSegment(PathSegment::Command::Cubic,
                {rest[1], rest[2], rest[3]}, path));
            break;
        }
        Point left[4];
        Point right[4];
        splitCubic(rest, 1.0 / (count - i), left, right);
        out.push_back(new PathSegment(PathSegment::Command::Cubic, {left[1], left[2], left[3]}, path));
        for (int k = 0; k < 4; ++k)
            rest[k] = right[k];
    }
}

} // namespace

void prepareForWarp(Shape& shape, double pieceLength)
{
    pieceLength = std::max(pieceLength, 1.0);
    for (Edge* edge : shape.edges)
    {
        if (!edge)
            continue;
        for (Path* path : edge->paths)
        {
            if (!path)
                continue;

            std::vector<PathSegment*> rebuilt;
            Point at;
            for (PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;
                switch (segment->command)
                {
                case PathSegment::Command::Move:
                    at = segment->points[0];
                    rebuilt.push_back(segment);
                    continue;
                case PathSegment::Command::Line:
                {
                    const Point& to = segment->points[0];
                    const Point p[4] = {at, lerp(at, to, 1.0 / 3.0), lerp(at, to, 2.0 / 3.0), to};
                    appendPieces(rebuilt, path, p, pieceLength);
                    at = to;
                    break;
                }
                case PathSegment::Command::Quad:
                {
                    const Point& c = segment->points[0];
                    const Point& to = segment->points[1];
                    const Point p[4] = {at, lerp(at, c, 2.0 / 3.0), lerp(to, c, 2.0 / 3.0), to};
                    appendPieces(rebuilt, path, p, pieceLength);
                    at = to;
                    break;
                }
                case PathSegment::Command::Cubic:
                {
                    const Point p[4] = {at, segment->points[0], segment->points[1], segment->points[2]};
                    appendPieces(rebuilt, path, p, pieceLength);
                    at = segment->points[2];
                    break;
                }
                case PathSegment::Command::Close:
                    rebuilt.push_back(segment);
                    continue;
                }
                delete segment;
            }
            path->segments = rebuilt;
        }
        edge->data.clear();
    }
}

void warpShape(Shape& shape, const std::function<Point(const Point&)>& map)
{
    bool any = false;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    for (Edge* edge : shape.edges)
    {
        if (!edge)
            continue;

        for (Path* path : edge->paths)
        {
            if (!path)
                continue;

            for (PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;

                for (Point& point : segment->points)
                {
                    point = map(point);
                    if (!any)
                    {
                        left = right = point.x;
                        top = bottom = point.y;
                        any = true;
                    }
                    else
                    {
                        left = point.x < left ? point.x : left;
                        right = point.x > right ? point.x : right;
                        top = point.y < top ? point.y : top;
                        bottom = point.y > bottom ? point.y : bottom;
                    }
                }
            }
        }

        edge->data.clear();
    }

    if (any)
    {
        shape.localBounds = Rect(Point(left, top), Point(right, bottom));
        shape.bounds = shape.localBounds;
    }
}

} // namespace fla
