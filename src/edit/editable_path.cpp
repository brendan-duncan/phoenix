#include "editable_path.h"

#include "../data/edge.h"

#include <cmath>

namespace fla {

namespace {

/// Handles closer than this to their anchor count as sitting on it, so a curve
/// that is straight in all but rounding is written out as a line.
constexpr double kHandleEpsilon = 1.0e-6;

bool samePoint(const Point& a, const Point& b)
{
    return std::fabs(a.x - b.x) <= kHandleEpsilon &&
           std::fabs(a.y - b.y) <= kHandleEpsilon;
}

/// Raises a quadratic to the cubic that draws the same curve.
void quadToCubic(const Point& start, const Point& control, const Point& end,
    Point& outControl1, Point& outControl2)
{
    outControl1 = Point(start.x + (2.0 / 3.0) * (control.x - start.x),
                        start.y + (2.0 / 3.0) * (control.y - start.y));
    outControl2 = Point(end.x + (2.0 / 3.0) * (control.x - end.x),
                        end.y + (2.0 / 3.0) * (control.y - end.y));
}

/// Adds the segment joining two anchors: a line when neither side curves, a
/// cubic otherwise.
void appendSegment(Path* path, const Anchor& from, const Anchor& to)
{
    if (!from.hasOutCurve() && !to.hasInCurve())
    {
        path->segments.push_back(new PathSegment(
            PathSegment::Command::Line, {to.position}, path));
        return;
    }

    path->segments.push_back(new PathSegment(
        PathSegment::Command::Cubic,
        {from.outHandle, to.inHandle, to.position}, path));
}

} // namespace

bool Anchor::hasInCurve() const
{
    return !samePoint(inHandle, position);
}

bool Anchor::hasOutCurve() const
{
    return !samePoint(outHandle, position);
}

void Anchor::translate(double dx, double dy)
{
    position.x += dx;
    position.y += dy;
    inHandle.x += dx;
    inHandle.y += dy;
    outHandle.x += dx;
    outHandle.y += dy;
}

void Anchor::setOutHandle(const Point& handle)
{
    outHandle = handle;

    // A smooth point keeps one straight tangent through it, so the other handle
    // mirrors this one at its own length.
    if (!smooth)
        return;

    const double dx = handle.x - position.x;
    const double dy = handle.y - position.y;
    const double length = std::hypot(dx, dy);
    if (length <= kHandleEpsilon)
        return;

    const double inLength = std::hypot(inHandle.x - position.x, inHandle.y - position.y);
    const double scale = (inLength > kHandleEpsilon ? inLength : length) / length;
    inHandle = Point(position.x - dx * scale, position.y - dy * scale);
}

void Anchor::setInHandle(const Point& handle)
{
    inHandle = handle;

    if (!smooth)
        return;

    const double dx = handle.x - position.x;
    const double dy = handle.y - position.y;
    const double length = std::hypot(dx, dy);
    if (length <= kHandleEpsilon)
        return;

    const double outLength = std::hypot(outHandle.x - position.x, outHandle.y - position.y);
    const double scale = (outLength > kHandleEpsilon ? outLength : length) / length;
    outHandle = Point(position.x - dx * scale, position.y - dy * scale);
}

EditablePath EditablePath::fromPath(const Path& path)
{
    EditablePath result;

    for (const PathSegment* segment : path.segments)
    {
        if (!segment)
            continue;

        switch (segment->command)
        {
        case PathSegment::Command::Move:
            if (!segment->points.empty())
                result.anchors.push_back(Anchor(segment->points[0]));
            break;

        case PathSegment::Command::Line:
            if (!segment->points.empty())
                result.anchors.push_back(Anchor(segment->points[0]));
            break;

        case PathSegment::Command::Quad:
            if (segment->points.size() >= 2 && !result.anchors.empty())
            {
                Point control1;
                Point control2;
                quadToCubic(result.anchors.back().position, segment->points[0],
                    segment->points[1], control1, control2);

                result.anchors.back().outHandle = control1;

                Anchor anchor(segment->points[1]);
                anchor.inHandle = control2;
                result.anchors.push_back(anchor);
            }
            break;

        case PathSegment::Command::Cubic:
            if (segment->points.size() >= 3 && !result.anchors.empty())
            {
                // A cubic carries the previous anchor's out handle and the new
                // anchor's in handle, which is exactly the split this model
                // undoes.
                result.anchors.back().outHandle = segment->points[0];

                Anchor anchor(segment->points[2]);
                anchor.inHandle = segment->points[1];
                result.anchors.push_back(anchor);
            }
            break;

        case PathSegment::Command::Close:
            result.closed = true;
            break;
        }
    }

    // A path that ends where it began is closed, however it was recorded. The
    // duplicated anchor would otherwise sit exactly on top of the first.
    if (result.anchors.size() > 1 &&
        samePoint(result.anchors.front().position, result.anchors.back().position))
    {
        result.closed = true;
        result.anchors.front().inHandle = result.anchors.back().inHandle;
        result.anchors.pop_back();
    }

    // Anything with a handle on both sides is treated as smooth, so dragging one
    // side keeps the curve running through rather than instantly cornering.
    for (Anchor& anchor : result.anchors)
        anchor.smooth = anchor.hasInCurve() && anchor.hasOutCurve();

    return result;
}

void EditablePath::applyTo(Edge& edge) const
{
    for (Path* path : edge.paths)
        delete path;
    edge.paths.clear();

    if (anchors.empty())
        return;

    Path* path = new Path(&edge);

    path->segments.push_back(new PathSegment(
        PathSegment::Command::Move, {anchors[0].position}, path));

    for (size_t i = 1; i < anchors.size(); ++i)
        appendSegment(path, anchors[i - 1], anchors[i]);

    if (closed && anchors.size() > 1)
    {
        // Run the final segment back to the start. Ending on the first point is
        // what tells the fill reconstruction the outline encloses an area.
        appendSegment(path, anchors.back(), anchors.front());
    }

    edge.paths.push_back(path);
}

} // namespace fla
