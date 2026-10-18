#include "path_bend.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace fla {

namespace {

/// Controls closer than this to their anchor give it no direction.
constexpr double kTiny = 1.0e-6;

/// Where two segments meet at more than this angle, the outline has a corner
/// and a bend stops there.
constexpr double kCornerAngle = 40.0 * 3.14159265358979323846 / 180.0;

/// How far the outline may turn, either way from the grab, before a bend
/// stops spreading. Past this the outline is heading somewhere else: round
/// the cap of a brush stroke and back along its other side.
constexpr double kMaximumTurn = 3.14159265358979323846 / 2.0;

/// Samples per segment for measuring length and turning.
constexpr int kSamples = 16;

/// The least share of the drag the grabbed point may take, so a grab right
/// at a corner does not fling the controls away.
constexpr double kMinimumGrabWeight = 0.15;

struct Cubic
{
    Point p[4];

    Point at(double t) const
    {
        const double u = 1.0 - t;
        const double a = u * u * u;
        const double b = 3.0 * u * u * t;
        const double c = 3.0 * u * t * t;
        const double d = t * t * t;
        return Point(a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x,
                          a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y);
    }

    Cubic reversed() const { return {{p[3], p[2], p[1], p[0]}}; }
};

bool isLine(const Anchor& from, const Anchor& to)
{
    return !from.hasOutCurve() && !to.hasInCurve();
}

Point onChord(const Point& a, const Point& b, double t)
{
    return Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

/// A segment's controls. A straight one gets its controls on the chord, which
/// draws the same line.
Cubic segmentCubic(const EditablePath& path, size_t segment)
{
    size_t from = 0;
    size_t to = 0;
    path.segmentAnchors(segment, from, to);
    const Anchor& a = path.anchors[from];
    const Anchor& b = path.anchors[to];
    if (isLine(a, b))
    {
        return {{a.position, onChord(a.position, b.position, 1.0 / 3.0),
                 onChord(a.position, b.position, 2.0 / 3.0), b.position}};
    }
    return {{a.position, a.outHandle, b.inHandle, b.position}};
}

double distanceBetween(const Point& a, const Point& b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

/// Arc length from the start of \a cubic to \a t.
double arcTo(const Cubic& cubic, double t)
{
    double total = 0.0;
    Point previous = cubic.p[0];
    for (int i = 1; i <= kSamples; ++i)
    {
        const Point point = cubic.at(t * i / kSamples);
        total += distanceBetween(previous, point);
        previous = point;
    }
    return total;
}

/// The direction a cubic leaves its start, or arrives at its end, skipping
/// controls that sit on the anchor.
bool endTangent(const Cubic& cubic, bool atEnd, double& dx, double& dy)
{
    for (int i = 1; i <= 3; ++i)
    {
        const Point& from = atEnd ? cubic.p[3 - i] : cubic.p[0];
        const Point& to = atEnd ? cubic.p[3] : cubic.p[i];
        dx = to.x - from.x;
        dy = to.y - from.y;
        if (std::hypot(dx, dy) > kTiny)
            return true;
    }
    return false;
}

double turnBetween(double ax, double ay, double bx, double by)
{
    return std::atan2(ax * by - ay * bx, ax * bx + ay * by);
}

/// Whether the outline turns sharply where \a in ends and \a out begins.
bool isCorner(const Cubic& in, const Cubic& out)
{
    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    if (!endTangent(in, true, ax, ay) || !endTangent(out, false, bx, by))
        return false;
    return std::fabs(turnBetween(ax, ay, bx, by)) > kCornerAngle;
}

/// How far a bend spreads one way along the outline from the grab.
struct Reach
{
    /// Arc length from the grab to where the bend stops, always an anchor.
    double distance = 0.0;
    /// Whether it stops on smooth outline rather than at a corner or the end
    /// of the path, in which case the bend eases off to nothing there so as
    /// not to leave a crease.
    bool smoothEnd = false;
    /// The segments it covers, with the arc length from the grab to each
    /// one's start as walked. The grabbed segment's start lies behind.
    std::vector<std::pair<size_t, double>> segments;
};

/// Follows the outline from the grab, forwards for \a direction 1 and
/// backwards for -1, until it turns too far, meets a corner, ends, or is
/// halfway round a loop.
Reach walk(const EditablePath& path, size_t segment, double t, int direction)
{
    Reach reach;
    const size_t count = path.segmentCount();

    double halfLoop = std::numeric_limits<double>::infinity();
    if (path.closed)
    {
        double total = 0.0;
        for (size_t i = 0; i < count; ++i)
            total += arcTo(segmentCubic(path, i), 1.0);
        halfLoop = total / 2.0;
    }

    const auto oriented = [&](size_t index) {
        const Cubic cubic = segmentCubic(path, index);
        return direction > 0 ? cubic : cubic.reversed();
    };

    size_t current = segment;
    Cubic cubic = oriented(current);
    double from = direction > 0 ? t : 1.0 - t;
    double start = -arcTo(cubic, from);
    double turning = 0.0;
    double dx = 0.0, dy = 0.0;
    bool haveDirection = false;

    for (;;)
    {
        // The turning along this segment, chord by chord, from the grab or
        // from its start.
        Point previous = cubic.at(from);
        for (int i = 1; i <= kSamples; ++i)
        {
            const double at = static_cast<double>(i) / kSamples;
            if (at <= from)
                continue;
            const Point point = cubic.at(at);
            const double cx = point.x - previous.x;
            const double cy = point.y - previous.y;
            if (std::hypot(cx, cy) > kTiny)
            {
                if (haveDirection)
                    turning += turnBetween(dx, dy, cx, cy);
                dx = cx;
                dy = cy;
                haveDirection = true;
            }
            previous = point;
        }

        const bool grabbed = reach.segments.empty();
        if (!grabbed && std::fabs(turning) > kMaximumTurn)
        {
            reach.distance = start;
            reach.smoothEnd = true;
            return reach;
        }

        reach.segments.emplace_back(current, start);
        const double end = start + arcTo(cubic, 1.0);
        reach.distance = end;

        if (grabbed && std::fabs(turning) > kMaximumTurn)
        {
            reach.smoothEnd = true;
            return reach;
        }

        const bool atPathEnd = !path.closed &&
            (direction > 0 ? current + 1 >= count : current == 0);
        if (atPathEnd)
            return reach;

        const size_t next = direction > 0 ? (current + 1) % count : (current + count - 1) % count;
        if (next == segment)
            return reach;

        const Cubic nextCubic = oriented(next);
        if (isCorner(cubic, nextCubic))
            return reach;

        if (end + arcTo(nextCubic, 1.0) > halfLoop)
        {
            reach.smoothEnd = true;
            return reach;
        }

        current = next;
        cubic = nextCubic;
        from = 0.0;
        start = end;
    }
}

/// How much of the drag a control \a distance along the outline from the
/// grab takes: all of it at the grab, easing to none where the bend stops.
/// Against a corner it falls as a parabola, so a lone segment bends as one
/// curve; on smooth outline it eases in as well, so the run joins the
/// untouched outline without a crease.
double falloff(double distance, const Reach& reach)
{
    if (reach.distance <= 0.0 || distance >= reach.distance)
        return 0.0;
    const double s = std::max(0.0, distance) / reach.distance;
    if (reach.smoothEnd)
        return 1.0 - s * s * (3.0 - 2.0 * s);
    return 1.0 - s * s;
}

/// How fast the falloff changes, per unit of distance along the outline.
double falloffSlope(double distance, const Reach& reach)
{
    if (reach.distance <= 0.0 || distance >= reach.distance)
        return 0.0;
    const double s = std::max(0.0, distance) / reach.distance;
    if (reach.smoothEnd)
        return 6.0 * s * (s - 1.0) / reach.distance;
    return -2.0 * s / reach.distance;
}

} // namespace

PathBend::PathBend(const EditablePath& path, size_t segment, double t)
    : _path(path)
{
    // The bend spreads along the smooth run of outline either side of the
    // grab rather than staying in the one segment, which on a fitted outline
    // is short and would fold into a crease.
    std::map<std::pair<size_t, Control>, double> weights;
    for (int direction : {1, -1})
    {
        const Reach reach = walk(path, segment, t, direction);
        // Handles facing back towards the grab and on along the walk.
        const Control back = direction > 0 ? Control::In : Control::Out;
        const Control ahead = direction > 0 ? Control::Out : Control::In;
        const auto weigh = [&](size_t anchor, Control control, double weight) {
            // Walks meet only where both stop, where either may speak.
            const auto [found, inserted] = weights.emplace(std::make_pair(anchor, control), weight);
            if (!inserted)
                found->second = std::max(found->second, weight);
        };

        for (size_t k = 0; k < reach.segments.size(); ++k)
        {
            const auto& [index, start] = reach.segments[k];
            size_t from = 0;
            size_t to = 0;
            if (!path.segmentAnchors(index, from, to))
                continue;
            if (isLine(path.anchors[from], path.anchors[to]))
                _lineSegments.push_back(index);

            // Each walk weighs the anchors it reaches, which are the far ends
            // of its segments; the grabbed segment's near end is the other
            // walk's.
            Cubic cubic = segmentCubic(path, index);
            if (direction < 0)
                cubic = cubic.reversed();
            const size_t anchor = direction > 0 ? to : from;
            const double distance = start + arcTo(cubic, 1.0);
            const double backLength = distanceBetween(cubic.p[2], cubic.p[3]);

            if (k + 1 == reach.segments.size())
            {
                // Where the bend stops. The anchor stays; at a corner the
                // handle facing back takes its share as a control of its own,
                // and on smooth outline the falloff is flat there, so it stays.
                if (!reach.smoothEnd)
                    weigh(anchor, back, falloff(distance - backLength, reach));
                continue;
            }

            // An anchor inside the run: its handles move with it, plus the
            // falloff's slope times their reach along the outline. Both lean
            // by the same amount per unit length, so a smooth anchor stays
            // smooth.
            const size_t next = reach.segments[k + 1].first;
            Cubic nextCubic = segmentCubic(path, next);
            if (direction < 0)
                nextCubic = nextCubic.reversed();
            const double aheadLength = distanceBetween(nextCubic.p[0], nextCubic.p[1]);

            const double weight = falloff(distance, reach);
            const double slope = falloffSlope(distance, reach);
            weigh(anchor, Control::Position, weight);
            weigh(anchor, back, weight - slope * backLength);
            weigh(anchor, ahead, weight + slope * aheadLength);
        }
    }

    std::sort(_lineSegments.begin(), _lineSegments.end());
    _lineSegments.erase(std::unique(_lineSegments.begin(), _lineSegments.end()), _lineSegments.end());

    for (const auto& [control, weight] : weights)
    {
        if (weight != 0.0)
            _influence.push_back({control.first, control.second, weight});
    }

    // The grabbed point moves by the blend of its segment's controls' shares.
    size_t from = 0;
    size_t to = 0;
    if (path.segmentAnchors(segment, from, to))
    {
        const auto share = [&](size_t anchor, Control control) {
            const auto found = weights.find({anchor, control});
            return found == weights.end() ? 0.0 : found->second;
        };
        const double u = 1.0 - t;
        const double grabWeight = u * u * u * share(from, Control::Position)
            + 3.0 * u * u * t * share(from, Control::Out)
            + 3.0 * u * t * t * share(to, Control::In)
            + t * t * t * share(to, Control::Position);
        _grabWeight = std::max(grabWeight, kMinimumGrabWeight);
    }
}

EditablePath PathBend::apply(double dx, double dy) const
{
    EditablePath result = _path;

    // Straight segments in the run take controls on their chords first,
    // which draw the same lines and give the bend something to move.
    for (size_t segment : _lineSegments)
    {
        size_t from = 0;
        size_t to = 0;
        if (!result.segmentAnchors(segment, from, to))
            continue;
        Anchor& a = result.anchors[from];
        Anchor& b = result.anchors[to];
        a.outHandle = onChord(a.position, b.position, 1.0 / 3.0);
        b.inHandle = onChord(a.position, b.position, 2.0 / 3.0);
    }

    // Dividing by the grabbed point's own share keeps it under the cursor.
    const double sx = dx / _grabWeight;
    const double sy = dy / _grabWeight;
    for (const Influence& influence : _influence)
    {
        if (influence.anchor >= result.anchors.size())
            continue;
        Anchor& anchor = result.anchors[influence.anchor];
        Point& point = influence.control == Control::Position ? anchor.position
            : influence.control == Control::In ? anchor.inHandle : anchor.outHandle;
        point = Point(point.x + sx * influence.weight, point.y + sy * influence.weight);
    }

    return result;
}

} // namespace fla
