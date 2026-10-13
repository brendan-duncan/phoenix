#include "editable_path.h"

#include "curve_fit.h"

#include "../data/edge.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

void EditablePath::applyTo(Edge& edge, size_t pathIndex) const
{
    // Only the path being edited is replaced. An edge can carry several, and
    // rewriting one must not throw the others away.
    if (pathIndex < edge.paths.size())
    {
        delete edge.paths[pathIndex];
        edge.paths.erase(edge.paths.begin() + static_cast<std::ptrdiff_t>(pathIndex));
    }
    else
    {
        pathIndex = edge.paths.size();
    }

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

    edge.paths.insert(edge.paths.begin() + static_cast<std::ptrdiff_t>(pathIndex), path);
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

/// The real roots of a t^3 + b t^2 + c t + d, falling back to the quadratic
/// or the line when the leading terms vanish.
std::vector<double> realRoots(double a, double b, double c, double d)
{
    std::vector<double> roots;
    const double scale = std::max({std::fabs(a), std::fabs(b), std::fabs(c), std::fabs(d)});
    if (scale == 0.0)
        return roots;

    if (std::fabs(a) <= 1.0e-12 * scale)
    {
        if (std::fabs(b) <= 1.0e-12 * scale)
        {
            if (std::fabs(c) > 1.0e-12 * scale)
                roots.push_back(-d / c);
            return roots;
        }
        const double discriminant = c * c - 4.0 * b * d;
        if (discriminant < 0.0)
            return roots;
        // The form that never subtracts two nearly equal numbers.
        const double q = -0.5 * (c + std::copysign(std::sqrt(discriminant), c));
        roots.push_back(q / b);
        if (q != 0.0)
            roots.push_back(d / q);
        return roots;
    }

    // Depressed to t = u - b/3a, then solved by Cardano for one real root or
    // by the trigonometric form for three.
    const double B = b / a;
    const double C = c / a;
    const double D = d / a;
    const double p = C - B * B / 3.0;
    const double q = 2.0 * B * B * B / 27.0 - B * C / 3.0 + D;
    const double shift = -B / 3.0;
    const double discriminant = q * q / 4.0 + p * p * p / 27.0;

    if (discriminant > 0.0)
    {
        const double root = std::sqrt(discriminant);
        roots.push_back(std::cbrt(-q / 2.0 + root) + std::cbrt(-q / 2.0 - root) + shift);
    }
    else if (p == 0.0)
    {
        roots.push_back(shift);
    }
    else
    {
        const double r = 2.0 * std::sqrt(-p / 3.0);
        const double cosine = std::clamp(3.0 * q / (p * r), -1.0, 1.0);
        const double phi = std::acos(cosine) / 3.0;
        constexpr double kTwoThirdsPi = 2.0943951023931954923;
        for (int k = 0; k < 3; ++k)
            roots.push_back(r * std::cos(phi - k * kTwoThirdsPi) + shift);
    }

    // A Newton step or two on the original polynomial mops up what the
    // closed forms lose to cancellation.
    for (double& t : roots)
    {
        for (int i = 0; i < 2; ++i)
        {
            const double value = ((a * t + b) * t + c) * t + d;
            const double slope = (3.0 * a * t + 2.0 * b) * t + c;
            if (slope == 0.0)
                break;
            t -= value / slope;
        }
    }
    return roots;
}

/// The parameter of the point on a quadratic nearest \a point. Where the
/// curve's tangent is square to the line to the point is a cubic in t; its
/// roots inside the segment and the two ends are the only candidates.
double closestOnQuadratic(const Point& p0, const Point& control, const Point& p2, const Point& point)
{
    // B(t) - point = m + 2t a + t^2 b, and B'(t) = 2 (a + t b).
    const Point a(control.x - p0.x, control.y - p0.y);
    const Point b(p2.x - 2.0 * control.x + p0.x, p2.y - 2.0 * control.y + p0.y);
    const Point m(p0.x - point.x, p0.y - point.y);
    const auto dot = [](const Point& u, const Point& v) { return u.x * v.x + u.y * v.y; };

    const auto at = [&](double t) {
        return Point(p0.x + 2.0 * t * a.x + t * t * b.x, p0.y + 2.0 * t * a.y + t * t * b.y);
    };

    double bestT = 0.0;
    double bestDistance = distanceBetween(p0, point);
    const auto consider = [&](double t) {
        const double distance = distanceBetween(at(t), point);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestT = t;
        }
    };

    consider(1.0);
    for (double t : realRoots(dot(b, b), 3.0 * dot(a, b), 2.0 * dot(a, a) + dot(m, b), dot(m, a)))
    {
        if (t >= 0.0 && t <= 1.0)
            consider(t);
    }
    return bestT;
}

/// The parameter of the point on a cubic nearest \a point: Newton's method on
/// the tangent condition, started from every sample nearer than both its
/// neighbors so a second dip in the curve is not missed, kept inside the
/// segment, and checked against the ends.
double closestOnCubic(const Point& p0, const Point& p1, const Point& p2, const Point& p3, const Point& point)
{
    constexpr int kSamples = 16;
    constexpr int kIterations = 40;

    const auto squaredDistance = [&](double t) {
        const Point at = cubicAt(p0, p1, p2, p3, t);
        return (at.x - point.x) * (at.x - point.x) + (at.y - point.y) * (at.y - point.y);
    };

    double samples[kSamples + 1];
    for (int i = 0; i <= kSamples; ++i)
        samples[i] = squaredDistance(static_cast<double>(i) / kSamples);

    double bestT = 0.0;
    double best = samples[0];
    if (samples[kSamples] < best)
    {
        best = samples[kSamples];
        bestT = 1.0;
    }

    for (int i = 0; i <= kSamples; ++i)
    {
        const bool dip = (i == 0 || samples[i] <= samples[i - 1]) &&
            (i == kSamples || samples[i] <= samples[i + 1]);
        if (!dip)
            continue;

        double t = static_cast<double>(i) / kSamples;
        double current = samples[i];
        for (int iteration = 0; iteration < kIterations; ++iteration)
        {
            // f = (B - point) . B' and its slope f' = B' . B' + (B - point) . B''.
            const double u = 1.0 - t;
            const Point at = cubicAt(p0, p1, p2, p3, t);
            const double dx = 3.0 * (u * u * (p1.x - p0.x) + 2.0 * u * t * (p2.x - p1.x) + t * t * (p3.x - p2.x));
            const double dy = 3.0 * (u * u * (p1.y - p0.y) + 2.0 * u * t * (p2.y - p1.y) + t * t * (p3.y - p2.y));
            const double ddx = 6.0 * (u * (p2.x - 2.0 * p1.x + p0.x) + t * (p3.x - 2.0 * p2.x + p1.x));
            const double ddy = 6.0 * (u * (p2.y - 2.0 * p1.y + p0.y) + t * (p3.y - 2.0 * p2.y + p1.y));
            const double ex = at.x - point.x;
            const double ey = at.y - point.y;
            const double f = ex * dx + ey * dy;
            const double slope = dx * dx + dy * dy + ex * ddx + ey * ddy;

            // Where the distance curves the wrong way Newton would head for a
            // maximum, so a plain step downhill stands in.
            double step = slope > 0.0 ? -f / slope : (f > 0.0 ? -1.0 : 1.0) / kSamples;
            double next = std::clamp(t + step, 0.0, 1.0);
            double nextDistance = squaredDistance(next);
            for (int halve = 0; halve < 30 && nextDistance > current; ++halve)
            {
                step *= 0.5;
                next = std::clamp(t + step, 0.0, 1.0);
                nextDistance = squaredDistance(next);
            }
            if (nextDistance > current)
                break;

            const bool settled = std::fabs(next - t) <= 1.0e-15;
            t = next;
            current = nextDistance;
            if (settled)
                break;
        }

        if (current < best)
        {
            best = current;
            bestT = t;
        }
    }
    return bestT;
}

/// The parameter of the point on a line segment nearest \a point.
double closestOnLine(const Point& a, const Point& b, const Point& point)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0)
        return 0.0;
    return std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
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

    for (size_t segment = 0; segment < segmentCount(); ++segment)
    {
        size_t from = 0;
        size_t to = 0;
        if (!segmentAnchors(segment, from, to))
            continue;

        const Anchor& a = anchors[from];
        const Anchor& b = anchors[to];
        const Point& p0 = a.position;
        const Point& p1 = a.outHandle;
        const Point& p2 = b.inHandle;
        const Point& p3 = b.position;

        double t = 0.0;
        if (!a.hasOutCurve() && !b.hasInCurve())
        {
            t = closestOnLine(p0, p3, point);
        }
        else
        {
            // A quadratic read from a file comes in raised to a cubic; its
            // cubic term is nothing, and the quadratic's nearest point has an
            // exact answer.
            const double span = std::max({std::fabs(p3.x - p0.x), std::fabs(p3.y - p0.y),
                std::fabs(p1.x - p0.x), std::fabs(p1.y - p0.y), std::fabs(p2.x - p3.x), std::fabs(p2.y - p3.y)});
            const double cubicX = p3.x - 3.0 * p2.x + 3.0 * p1.x - p0.x;
            const double cubicY = p3.y - 3.0 * p2.y + 3.0 * p1.y - p0.y;
            if (std::fabs(cubicX) <= 1.0e-9 * span && std::fabs(cubicY) <= 1.0e-9 * span)
            {
                const Point control((3.0 * p1.x - p0.x) / 2.0, (3.0 * p1.y - p0.y) / 2.0);
                t = closestOnQuadratic(p0, control, p3, point);
            }
            else
            {
                t = closestOnCubic(p0, p1, p2, p3, point);
            }
        }

        const Point position = (!a.hasOutCurve() && !b.hasInCurve())
            ? lerp(p0, p3, t)
            : cubicAt(p0, p1, p2, p3, t);
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

    const size_t count = anchors.size();
    const bool hasPrevious = closed || index > 0;
    const bool hasNext = closed || index + 1 < count;
    if (hasPrevious && hasNext)
    {
        Anchor& previous = anchors[(index + count - 1) % count];
        Anchor& next = anchors[(index + 1) % count];
        const Anchor& gone = anchors[index];

        const bool curved = previous.hasOutCurve() || gone.hasInCurve() ||
            gone.hasOutCurve() || next.hasInCurve();
        if (curved)
        {
            // Both segments sampled, then one cubic fitted through the
            // samples with the outer tangents kept.
            constexpr int kSamples = 16;
            std::vector<Point> samples;
            const auto sampleSegment = [&](const Anchor& a, const Anchor& b, bool includeStart) {
                const bool straight = !a.hasOutCurve() && !b.hasInCurve();
                for (int i = includeStart ? 0 : 1; i <= kSamples; ++i)
                {
                    const double t = static_cast<double>(i) / kSamples;
                    samples.push_back(straight
                        ? lerp(a.position, b.position, t)
                        : cubicAt(a.position, a.outHandle, b.inHandle, b.position, t));
                }
            };
            sampleSegment(previous, gone, true);
            sampleSegment(gone, next, false);

            const auto direction = [](const Point& from, const Point& to) {
                return Point(to.x - from.x, to.y - from.y);
            };
            const Point startTangent = previous.hasOutCurve()
                ? direction(previous.position, previous.outHandle)
                : direction(samples[0], samples[1]);
            const Point endTangent = next.hasInCurve()
                ? direction(next.position, next.inHandle)
                : direction(samples[samples.size() - 1], samples[samples.size() - 2]);

            Point bezier[4];
            if (CurveFit::fitSingleCubic(samples, startTangent, endTangent, bezier))
            {
                previous.outHandle = bezier[1];
                next.inHandle = bezier[2];
            }
        }
    }

    anchors.erase(anchors.begin() + static_cast<long long>(index));
    return true;
}

EditablePath EditablePath::reversed() const
{
    EditablePath result;
    result.closed = closed;
    for (auto it = anchors.rbegin(); it != anchors.rend(); ++it)
    {
        Anchor anchor = *it;
        std::swap(anchor.inHandle, anchor.outHandle);
        result.anchors.push_back(anchor);
    }
    return result;
}

EditablePath EditablePath::transformed(const Transform& transform) const
{
    const auto map = [&](const Point& p) {
        return Point(transform.m11 * p.x + transform.m21 * p.y + transform.tx,
                     transform.m12 * p.x + transform.m22 * p.y + transform.ty);
    };
    EditablePath result = *this;
    for (Anchor& anchor : result.anchors)
    {
        anchor.position = map(anchor.position);
        anchor.inHandle = map(anchor.inHandle);
        anchor.outHandle = map(anchor.outHandle);
    }
    return result;
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
    // neighbors, which is the direction that makes the curve pass through
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

    // A third of the way to the closer neighbor is the usual choice: long
    // enough to round the corner, short enough not to overshoot.
    const double reach = std::min(distanceBetween(anchor.position, previous),
        distanceBetween(anchor.position, next)) / 3.0;

    anchor.smooth = true;
    anchor.outHandle = Point(anchor.position.x + dx * reach, anchor.position.y + dy * reach);
    anchor.inHandle = Point(anchor.position.x - dx * reach, anchor.position.y - dy * reach);
    return true;
}

} // namespace fla
