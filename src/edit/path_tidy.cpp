#include "path_tidy.h"

#include "curve_fit.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

Point lerp(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

Point cubicAt(const Point& p0, const Point& p1, const Point& p2, const Point& p3, double t)
{
    const Point q0 = lerp(p0, p1, t);
    const Point q1 = lerp(p1, p2, t);
    const Point q2 = lerp(p2, p3, t);
    return lerp(lerp(q0, q1, t), lerp(q1, q2, t), t);
}

double distance(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// The fitter leaves the seam of a closed path as a repeated last anchor,
/// which the closing segment covers.
void closeUp(EditablePath& path, bool closed)
{
    path.closed = closed;
    if (closed && path.anchors.size() > 2 &&
        distance(path.anchors.front().position, path.anchors.back().position) < 1.0e-6)
    {
        path.anchors.pop_back();
    }
}

} // namespace

std::vector<Point> samplePath(const EditablePath& path, double spacing)
{
    std::vector<Point> samples;
    if (path.anchors.empty())
        return samples;
    spacing = std::max(spacing, 1.0e-3);

    samples.push_back(path.anchors.front().position);
    for (size_t segment = 0; segment < path.segmentCount(); ++segment)
    {
        size_t from = 0;
        size_t to = 0;
        if (!path.segmentAnchors(segment, from, to))
            continue;
        const Anchor& a = path.anchors[from];
        const Anchor& b = path.anchors[to];
        const bool straight = !a.hasOutCurve() && !b.hasInCurve();

        // Enough steps that neighboring samples sit about the spacing
        // apart along the control polygon, which bounds the curve's length.
        double length = distance(a.position, b.position);
        if (!straight)
        {
            length = distance(a.position, a.outHandle) + distance(a.outHandle, b.inHandle) +
                distance(b.inHandle, b.position);
        }
        const int steps = std::max(1, static_cast<int>(std::ceil(length / spacing)));
        for (int i = 1; i <= steps; ++i)
        {
            const double t = static_cast<double>(i) / steps;
            samples.push_back(straight ? lerp(a.position, b.position, t)
                : cubicAt(a.position, a.outHandle, b.inHandle, b.position, t));
        }
    }
    return samples;
}

void smoothPath(EditablePath& path, double amount)
{
    amount = std::max(0.0, std::min(1.0, amount));
    const size_t count = path.anchors.size();
    if (count < 2 || amount <= 0.0)
        return;

    const std::vector<Anchor> before = path.anchors;
    for (size_t i = 0; i < count; ++i)
    {
        const bool hasPrevious = path.closed || i > 0;
        const bool hasNext = path.closed || i + 1 < count;
        const Anchor& here = before[i];
        Anchor& anchor = path.anchors[i];

        // The tangent runs from the previous anchor to the next; at an open
        // end it runs along the one chord there is.
        const Point& previous = before[hasPrevious ? (i + count - 1) % count : i].position;
        const Point& next = before[hasNext ? (i + 1) % count : i].position;
        double dx = next.x - previous.x;
        double dy = next.y - previous.y;
        const double length = std::hypot(dx, dy);
        if (length <= 1.0e-9)
            continue;
        dx /= length;
        dy /= length;

        if (hasNext)
        {
            const double reach = distance(here.position, next) / 3.0;
            const Point target(here.position.x + dx * reach, here.position.y + dy * reach);
            anchor.outHandle = lerp(here.outHandle, target, amount);
        }
        if (hasPrevious)
        {
            const double reach = distance(here.position, previous) / 3.0;
            const Point target(here.position.x - dx * reach, here.position.y - dy * reach);
            anchor.inHandle = lerp(here.inHandle, target, amount);
        }
        anchor.smooth = hasPrevious && hasNext;
    }
}

void straightenPath(EditablePath& path, double tolerance)
{
    if (path.anchors.size() < 2)
        return;
    const bool closed = path.closed;
    const std::vector<Point> samples = samplePath(path, std::max(tolerance, 0.5) / 2.0);

    CurveFit::Options options;
    options.straighten = true;
    options.tolerance = std::max(0.01, tolerance);
    options.minimumSpacing = 0.0;
    EditablePath fitted = CurveFit::fit(samples, options);
    if (fitted.anchors.size() < 2)
        return;
    closeUp(fitted, closed);
    path = fitted;
}

void optimizePath(EditablePath& path, double tolerance)
{
    if (path.anchors.size() < 2)
        return;
    const bool closed = path.closed;
    const std::vector<Point> samples = samplePath(path, std::max(tolerance, 0.5) / 2.0);

    CurveFit::Options options;
    options.tolerance = std::max(0.01, tolerance);
    options.minimumSpacing = 0.0;
    EditablePath fitted = CurveFit::fit(samples, options);
    if (fitted.anchors.size() < 2)
        return;
    closeUp(fitted, closed);
    // Keep the original where fitting it again would only add anchors.
    if (fitted.anchors.size() >= path.anchors.size())
        return;
    path = fitted;
}

} // namespace fla
