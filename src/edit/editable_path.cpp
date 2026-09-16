#include "editable_path.h"

#include "../data/edge.h"

#include <algorithm>
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

namespace {

Point lerp(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

double distanceBetween(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// Evaluates the cubic through the four control points.
Point cubicAt(const Point& p0, const Point& p1, const Point& p2, const Point& p3, double t)
{
    const Point a = lerp(p0, p1, t);
    const Point b = lerp(p1, p2, t);
    const Point c = lerp(p2, p3, t);
    return lerp(lerp(a, b, t), lerp(b, c, t), t);
}

} // namespace

size_t EditablePath::segmentCount() const
{
    if (anchors.size() < 2)
        return 0;
    return closed ? anchors.size() : anchors.size() - 1;
}

bool EditablePath::segmentAnchors(size_t segment, size_t& from, size_t& to) const
{
    if (segment >= segmentCount())
        return false;

    from = segment;
    to = (segment + 1) % anchors.size();
    return true;
}

EditablePath::PathPoint EditablePath::closestPoint(const Point& point) const
{
    PathPoint best;

    // Sampling avoids solving for the true nearest point on a cubic. A coarse
    // sweep followed by a local refinement gets close enough for both picking a
    // click and measuring how well a curve fits.
    constexpr int kSamples = 32;
    constexpr int kRefinements = 20;

    for (size_t segment = 0; segment < segmentCount(); ++segment)
    {
        size_t from = 0;
        size_t to = 0;
        if (!segmentAnchors(segment, from, to))
            continue;

        const Anchor& a = anchors[from];
        const Anchor& b = anchors[to];

        for (int i = 0; i <= kSamples; ++i)
        {
            const double t = static_cast<double>(i) / kSamples;
            const Point position = (!a.hasOutCurve() && !b.hasInCurve())
                ? lerp(a.position, b.position, t)
                : cubicAt(a.position, a.outHandle, b.inHandle, b.position, t);

            const double distance = distanceBetween(position, point);
            if (!best.valid || distance < best.distance)
            {
                best.valid = true;
                best.segment = segment;
                best.t = t;
                best.position = position;
                best.distance = distance;
            }
        }
    }

    if (!best.valid)
        return best;

    // Narrow in on the winning sample by repeatedly halving the interval around
    // it, which costs little and removes the sweep's spacing from the answer.
    size_t from = 0;
    size_t to = 0;
    if (!segmentAnchors(best.segment, from, to))
        return best;

    const Anchor& a = anchors[from];
    const Anchor& b = anchors[to];
    const bool straight = !a.hasOutCurve() && !b.hasInCurve();

    const auto at = [&](double t) {
        return straight ? lerp(a.position, b.position, t)
                        : cubicAt(a.position, a.outHandle, b.inHandle, b.position, t);
    };

    double step = 1.0 / kSamples;
    for (int i = 0; i < kRefinements; ++i)
    {
        step *= 0.5;

        const double lower = std::max(0.0, best.t - step);
        const double upper = std::min(1.0, best.t + step);

        for (double t : {lower, upper})
        {
            const Point position = at(t);
            const double distance = distanceBetween(position, point);
            if (distance < best.distance)
            {
                best.t = t;
                best.position = position;
                best.distance = distance;
            }
        }
    }

    return best;
}

int EditablePath::splitSegment(size_t segment, double t)
{
    size_t from = 0;
    size_t to = 0;
    if (!segmentAnchors(segment, from, to))
        return -1;

    t = std::max(0.0, std::min(1.0, t));

    Anchor& a = anchors[from];
    Anchor& b = anchors[to];

    Anchor inserted;

    if (!a.hasOutCurve() && !b.hasInCurve())
    {
        // A straight segment just gains a point on the line.
        inserted = Anchor(lerp(a.position, b.position, t));
    }
    else
    {
        // De Casteljau: subdividing at t gives both halves exactly, so the curve
        // through the new anchor is the one that was already there.
        const Point p0 = a.position;
        const Point p1 = a.outHandle;
        const Point p2 = b.inHandle;
        const Point p3 = b.position;

        const Point q0 = lerp(p0, p1, t);
        const Point q1 = lerp(p1, p2, t);
        const Point q2 = lerp(p2, p3, t);
        const Point r0 = lerp(q0, q1, t);
        const Point r1 = lerp(q1, q2, t);
        const Point s = lerp(r0, r1, t);

        inserted.position = s;
        inserted.inHandle = r0;
        inserted.outHandle = r1;
        inserted.smooth = true;

        a.outHandle = q0;
        b.inHandle = q2;
    }

    // Inserting after `from` works for the closing segment too, where `to` wraps
    // to zero and the new anchor belongs at the end.
    const size_t insertAt = from + 1;
    anchors.insert(anchors.begin() + static_cast<long long>(insertAt), inserted);
    return static_cast<int>(insertAt);
}

bool EditablePath::removeAnchor(size_t index)
{
    // Two anchors is the least that still draws something.
    if (index >= anchors.size() || anchors.size() <= 2)
        return false;

    anchors.erase(anchors.begin() + static_cast<long long>(index));
    return true;
}

bool EditablePath::toggleAnchorSmooth(size_t index)
{
    if (index >= anchors.size())
        return false;

    Anchor& anchor = anchors[index];

    if (anchor.smooth || anchor.hasInCurve() || anchor.hasOutCurve())
    {
        // Becoming a corner means losing the handles, leaving straight sides.
        anchor.smooth = false;
        anchor.inHandle = anchor.position;
        anchor.outHandle = anchor.position;
        return true;
    }

    // Becoming smooth means growing handles along the line through the
    // neighbours, which is the direction that makes the curve pass through
    // without a kink.
    const size_t count = anchors.size();
    if (count < 2)
        return false;

    const bool hasPrevious = closed || index > 0;
    const bool hasNext = closed || index + 1 < count;
    if (!hasPrevious && !hasNext)
        return false;

    const Point& previous = anchors[hasPrevious ? (index + count - 1) % count : index + 1].position;
    const Point& next = anchors[hasNext ? (index + 1) % count : index - 1].position;

    double dx = next.x - previous.x;
    double dy = next.y - previous.y;
    const double length = std::hypot(dx, dy);
    if (length <= kHandleEpsilon)
        return false;

    dx /= length;
    dy /= length;

    // A third of the way to the closer neighbour is the usual choice: long
    // enough to round the corner, short enough not to overshoot.
    const double reach = std::min(distanceBetween(anchor.position, previous),
        distanceBetween(anchor.position, next)) / 3.0;

    anchor.smooth = true;
    anchor.outHandle = Point(anchor.position.x + dx * reach, anchor.position.y + dy * reach);
    anchor.inHandle = Point(anchor.position.x - dx * reach, anchor.position.y - dy * reach);
    return true;
}

} // namespace fla
