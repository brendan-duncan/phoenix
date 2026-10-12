#include "guide_path.h"

#include "../data/edge.h"
#include "../data/shape.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

constexpr double kPi = 3.14159265358979323846;
/// Ends this close (pixels) are joined.
constexpr double kJoin = 0.5;
/// Points a curve is flattened into.
constexpr int kSteps = 12;

double distance(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

Point place(const Transform& t, const Point& p)
{
    return Point(t.m11 * p.x + t.m21 * p.y + t.tx, t.m12 * p.x + t.m22 * p.y + t.ty);
}

double lineLength(const std::vector<Point>& line)
{
    double total = 0.0;
    for (size_t i = 1; i < line.size(); ++i)
        total += distance(line[i - 1], line[i]);
    return total;
}

} // namespace

void GuidePath::build(const std::vector<const Shape*>& shapes)
{
    std::vector<std::vector<Point>> lines;
    for (const Shape* shape : shapes)
    {
        if (!shape)
            continue;
        for (const Edge* edge : shape->edges)
        {
            if (!edge || edge->strokeStyle <= 0)
                continue;
            for (const Path* path : edge->paths)
            {
                if (!path)
                    continue;
                std::vector<Point> line;
                Point pen;
                for (const PathSegment* segment : path->segments)
                {
                    if (!segment)
                        continue;
                    const std::vector<Point>& p = segment->points;
                    switch (segment->command)
                    {
                    case PathSegment::Command::Move:
                        if (line.size() > 1)
                            lines.push_back(line);
                        line.clear();
                        if (!p.empty())
                        {
                            pen = p[0];
                            line.push_back(place(shape->transform, pen));
                        }
                        break;
                    case PathSegment::Command::Line:
                        if (!p.empty())
                        {
                            pen = p[0];
                            line.push_back(place(shape->transform, pen));
                        }
                        break;
                    case PathSegment::Command::Quad:
                        if (p.size() >= 2)
                        {
                            for (int step = 1; step <= kSteps; ++step)
                            {
                                const double s = static_cast<double>(step) / kSteps;
                                const double u = 1.0 - s;
                                const Point q(u * u * pen.x + 2.0 * u * s * p[0].x + s * s * p[1].x,
                                    u * u * pen.y + 2.0 * u * s * p[0].y + s * s * p[1].y);
                                line.push_back(place(shape->transform, q));
                            }
                            pen = p[1];
                        }
                        break;
                    case PathSegment::Command::Cubic:
                        if (p.size() >= 3)
                        {
                            for (int step = 1; step <= kSteps; ++step)
                            {
                                const double s = static_cast<double>(step) / kSteps;
                                const double u = 1.0 - s;
                                const Point q(
                                    u * u * u * pen.x + 3.0 * u * u * s * p[0].x + 3.0 * u * s * s * p[1].x + s * s * s * p[2].x,
                                    u * u * u * pen.y + 3.0 * u * u * s * p[0].y + 3.0 * u * s * s * p[1].y + s * s * s * p[2].y);
                                line.push_back(place(shape->transform, q));
                            }
                            pen = p[2];
                        }
                        break;
                    case PathSegment::Command::Close:
                        if (!line.empty())
                            line.push_back(line.front());
                        break;
                    }
                }
                if (line.size() > 1)
                    lines.push_back(line);
            }
        }
    }
    build(std::move(lines));
}

void GuidePath::build(std::vector<std::vector<Point>> lines)
{
    _points.clear();
    _lengths.clear();
    // Joined end to end, either way round, into as long a line as they make.
    std::vector<Point> best;
    double bestLength = -1.0;
    while (!lines.empty())
    {
        // From the first stroke drawn, the way it was drawn.
        std::vector<Point> chain = std::move(lines.front());
        lines.erase(lines.begin());
        bool grew = true;
        while (grew)
        {
            grew = false;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                std::vector<Point>& line = lines[i];
                if (distance(chain.back(), line.front()) < kJoin)
                    chain.insert(chain.end(), line.begin() + 1, line.end());
                else if (distance(chain.back(), line.back()) < kJoin)
                    chain.insert(chain.end(), line.rbegin() + 1, line.rend());
                else if (distance(chain.front(), line.back()) < kJoin)
                    chain.insert(chain.begin(), line.begin(), line.end() - 1);
                else if (distance(chain.front(), line.front()) < kJoin)
                    chain.insert(chain.begin(), line.rbegin(), line.rend() - 1);
                else
                    continue;
                lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
                grew = true;
                break;
            }
        }
        const double length = lineLength(chain);
        if (length > bestLength)
        {
            bestLength = length;
            best = std::move(chain);
        }
    }
    _points = std::move(best);
    double total = 0.0;
    for (size_t i = 0; i < _points.size(); ++i)
    {
        if (i > 0)
            total += distance(_points[i - 1], _points[i]);
        _lengths.push_back(total);
    }
}

double GuidePath::nearest(const Point& point) const
{
    double best = 0.0;
    double bestDistance = -1.0;
    for (size_t i = 1; i < _points.size(); ++i)
    {
        const Point& a = _points[i - 1];
        const Point& b = _points[i];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double span = dx * dx + dy * dy;
        double s = span > 0.0 ? ((point.x - a.x) * dx + (point.y - a.y) * dy) / span : 0.0;
        s = std::max(0.0, std::min(1.0, s));
        const double d = std::hypot(a.x + s * dx - point.x, a.y + s * dy - point.y);
        if (bestDistance < 0.0 || d < bestDistance - 1.0e-9)
        {
            bestDistance = d;
            best = _lengths[i - 1] + s * (_lengths[i] - _lengths[i - 1]);
        }
    }
    return best;
}

size_t GuidePath::segmentAt(double distance) const
{
    // The first segment reaching \a distance.
    const auto found = std::lower_bound(_lengths.begin(), _lengths.end(), distance);
    size_t i = static_cast<size_t>(found - _lengths.begin());
    return std::max<size_t>(1, std::min(i, _points.size() - 1));
}

Point GuidePath::at(double distance) const
{
    if (_points.empty())
        return Point();
    if (_points.size() == 1)
        return _points.front();
    distance = std::max(0.0, std::min(length(), distance));
    const size_t i = segmentAt(distance);
    const double span = _lengths[i] - _lengths[i - 1];
    const double s = span > 0.0 ? (distance - _lengths[i - 1]) / span : 0.0;
    return Point(_points[i - 1].x + s * (_points[i].x - _points[i - 1].x),
        _points[i - 1].y + s * (_points[i].y - _points[i - 1].y));
}

double GuidePath::angleAt(double distance) const
{
    if (_points.size() < 2)
        return 0.0;
    distance = std::max(0.0, std::min(length(), distance));
    size_t i = segmentAt(distance);
    // Past a point of no length, to one that has a direction.
    while (i + 1 < _points.size() && _lengths[i] - _lengths[i - 1] <= 0.0)
        ++i;
    return std::atan2(_points[i].y - _points[i - 1].y, _points[i].x - _points[i - 1].x) * 180.0 / kPi;
}

} // namespace fla
