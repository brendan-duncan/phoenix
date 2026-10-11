#include "fill_tracing.h"

#include "../data/edge.h"
#include "../data/shape.h"

#include <cmath>

namespace fla {

namespace {

struct DirectedPath
{
    Point start;
    Point end;
    const Path* path = nullptr;
    bool reversed = false;
    bool used = false;
};

void endpoints(const Path* path, Point& start, Point& end)
{
    for (const PathSegment* segment : path->segments)
    {
        if (segment->points.empty())
            continue;
        if (segment->command == PathSegment::Command::Move)
        {
            start = segment->points[0];
            end = start;
        }
        else if (segment->command != PathSegment::Command::Close)
        {
            end = segment->points.back();
        }
    }
}

/// The direction a path sets off in, read from its first leg.
Point forwardStartTangent(const Path* path)
{
    Point at;
    bool started = false;
    for (const PathSegment* segment : path->segments)
    {
        if (segment->command == PathSegment::Command::Move)
        {
            at = segment->points[0];
            started = true;
            continue;
        }
        if (!started || segment->points.empty())
            continue;
        for (const Point& point : segment->points)
        {
            const Point d(point.x - at.x, point.y - at.y);
            if (std::hypot(d.x, d.y) > 1.0e-9)
                return d;
        }
        at = segment->points.back();
    }
    return Point(0.0, 0.0);
}

/// The direction a path arrives with, read from its last leg.
Point forwardEndTangent(const Path* path)
{
    Point at;
    bool started = false;
    Point tangent(0.0, 0.0);
    for (const PathSegment* segment : path->segments)
    {
        if (segment->command == PathSegment::Command::Move)
        {
            at = segment->points[0];
            started = true;
            continue;
        }
        if (!started || segment->points.empty())
            continue;
        // From the last point before the end, back to the segment's start
        // when the controls sit on the end.
        const Point& end = segment->points.back();
        Point from = at;
        for (size_t i = segment->points.size() - 1; i > 0; --i)
        {
            const Point& candidate = segment->points[i - 1];
            if (std::hypot(end.x - candidate.x, end.y - candidate.y) > 1.0e-9)
            {
                from = candidate;
                break;
            }
        }
        const Point d(end.x - from.x, end.y - from.y);
        if (std::hypot(d.x, d.y) > 1.0e-9)
            tangent = d;
        at = end;
    }
    return tangent;
}

Point startTangent(const DirectedPath& directed)
{
    if (!directed.reversed)
        return forwardStartTangent(directed.path);
    const Point t = forwardEndTangent(directed.path);
    return Point(-t.x, -t.y);
}

Point endTangent(const DirectedPath& directed)
{
    if (!directed.reversed)
        return forwardEndTangent(directed.path);
    const Point t = forwardStartTangent(directed.path);
    return Point(-t.x, -t.y);
}

/// How far round to the right \a out turns from \a in, on screen (y down):
/// pi for the sharpest right turn, -pi for the sharpest left, and a straight
/// reversal counted as the sharpest left, since doubling back along the edge
/// just walked is never the way round a region.
double rightTurn(const Point& in, const Point& out)
{
    const double cross = in.x * out.y - in.y * out.x;
    const double dot = in.x * out.x + in.y * out.y;
    double angle = std::atan2(cross, dot);
    const double pi = 3.14159265358979323846;
    if (angle > pi - 1.0e-6)
        angle = -pi;
    return angle;
}

/// Whether two points are the same to a twip.
bool sameTwip(const Point& a, const Point& b)
{
    return std::lround(a.x * 20.0) == std::lround(b.x * 20.0) && std::lround(a.y * 20.0) == std::lround(b.y * 20.0);
}

/// Whether two points are within half a pixel, near enough to join when
/// nothing meets exactly.
bool near(const Point& a, const Point& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy < 0.25;
}

TracedLoop single(const DirectedPath& directed, bool closed)
{
    TracedLoop loop;
    loop.legs.push_back({directed.path, directed.reversed});
    loop.closed = closed;
    return loop;
}

} // namespace

std::map<int, std::vector<TracedLoop>> traceFills(const Shape& shape)
{
    std::map<int, std::vector<DirectedPath>> byFill;
    for (const Edge* edge : shape.edges)
    {
        if (!edge || !edge->visible)
            continue;
        for (const Path* path : edge->paths)
        {
            if (!path || !path->visible || path->segments.empty())
                continue;
            Point start;
            Point end;
            endpoints(path, start, end);
            const int fill1 = path->fillStyleIndex != -1 ? path->fillStyleIndex : edge->fillStyle1;
            const int fill0 = edge->fillStyle0;
            if (fill1 != -1)
                byFill[fill1].push_back({start, end, path, false, false});
            // A piece with the same fill on both sides -- a stroke running
            // through a fill -- bounds a region on each side, so it goes in
            // both ways; only a loop closed on itself with one fill inside
            // and out is taken once.
            if (fill0 != -1 && (fill0 != fill1 || !sameTwip(start, end)))
                byFill[fill0].push_back({end, start, path, true, false});
        }
    }

    std::map<int, std::vector<TracedLoop>> traced;
    for (auto& entry : byFill)
    {
        std::vector<DirectedPath>& paths = entry.second;
        std::vector<TracedLoop>& loops = traced[entry.first];

        // A fill of one or two pieces, as small symbol shapes often are, is
        // joined first come first served.
        if (paths.size() <= 2)
        {
            for (DirectedPath& directed : paths)
            {
                if (directed.used || sameTwip(directed.start, directed.end))
                    continue;
                TracedLoop loop = single(directed, false);
                directed.used = true;
                Point end = directed.end;
                for (size_t step = 0; step < paths.size() && !sameTwip(end, directed.start); ++step)
                {
                    bool found = false;
                    for (DirectedPath& next : paths)
                    {
                        if (!next.used && sameTwip(end, next.start))
                        {
                            loop.legs.push_back({next.path, next.reversed});
                            next.used = true;
                            end = next.end;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        break;
                }
                loop.closed = sameTwip(end, directed.start);
                loops.push_back(loop);
            }
            for (DirectedPath& directed : paths)
            {
                if (directed.used)
                    continue;
                directed.used = true;
                loops.push_back(single(directed, sameTwip(directed.start, directed.end)));
            }
            continue;
        }

        // Paths closed on themselves are outlines already.
        for (DirectedPath& directed : paths)
        {
            if (!directed.used && sameTwip(directed.start, directed.end))
            {
                directed.used = true;
                loops.push_back(single(directed, true));
            }
        }

        // The rest are joined into outlines. Every directed path has its fill
        // on the right, so the boundary of one region is traced by keeping
        // right: at a junction, of the paths that set off from here, take the
        // one that turns furthest to the right. Taking whichever came first
        // would wander into the neighboring region and leave this one half
        // drawn.
        for (DirectedPath& first : paths)
        {
            if (first.used)
                continue;
            TracedLoop loop = single(first, false);
            first.used = true;
            Point end = first.end;
            Point arriving = endTangent(first);
            const size_t limit = paths.size() * 2;
            for (size_t step = 0; step < limit; ++step)
            {
                if (sameTwip(end, first.start))
                    break;
                // Starts at the very point first, and only failing those any
                // within half a pixel: where cut edges crowd together, a
                // neighbor's start can be nearer than that without being the
                // way on.
                DirectedPath* best = nullptr;
                double bestTurn = 0.0;
                for (int pass = 0; pass < 2 && !best; ++pass)
                {
                    for (DirectedPath& next : paths)
                    {
                        if (next.used)
                            continue;
                        if (pass == 0 ? !sameTwip(end, next.start) : !near(end, next.start))
                            continue;
                        const double turn = rightTurn(arriving, startTangent(next));
                        if (!best || turn > bestTurn)
                        {
                            best = &next;
                            bestTurn = turn;
                        }
                    }
                }
                if (!best)
                    break;
                loop.legs.push_back({best->path, best->reversed});
                best->used = true;
                end = best->end;
                arriving = endTangent(*best);
            }
            loop.closed = sameTwip(end, first.start);
            loops.push_back(loop);
        }
    }
    return traced;
}

} // namespace fla
