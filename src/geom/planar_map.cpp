#include "planar_map.h"

#include "intersect.h"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_map>

namespace fla {

namespace {

/// How many points to sample a curve at when measuring the area it encloses.
/// Areas are only used to tell bounded faces from the unbounded one and to
/// report a rough size, so this does not need to be exact.
constexpr int kAreaSamples = 8;

/// A piece shorter than this in twips is a rounding artifact rather than
/// geometry, and welding has already pulled its ends together.
constexpr double kMinimumPieceLength = PlanarMap::kTwip * 0.5;

/// Coordinates past this many pixels are far beyond anything the format can
/// store, and would overflow the twip grid's integers.
constexpr double kLargestCoordinate = 1.0e12;

int64_t toTwips(double value)
{
    return static_cast<int64_t>(std::llround(value / PlanarMap::kTwip));
}

/// Twice the signed area contributed by a curve, sampled along its length.
/// Summed around a closed boundary this gives twice the enclosed area, positive
/// for a counter-clockwise walk in a y-down coordinate system.
double signedAreaContribution(const Curve& curve)
{
    double total = 0.0;

    Point previous = curve.pointAt(0.0);
    for (int i = 1; i <= kAreaSamples; ++i)
    {
        const Point current = curve.pointAt(static_cast<double>(i) / kAreaSamples);
        total += previous.x * current.y - current.x * previous.y;
        previous = current;
    }

    return total;
}

} // namespace

Point PlanarMap::snapToTwips(const Point& point)
{
    return Point(std::llround(point.x / kTwip) * kTwip,
                 std::llround(point.y / kTwip) * kTwip);
}

void PlanarMap::addCurve(const Curve& curve, int source)
{
    // A NaN would break every comparison the arrangement sorts and welds by.
    for (int i = 0; i < 4; ++i)
    {
        const Point& point = curve.controlPoint(i);
        if (!(std::fabs(point.x) <= kLargestCoordinate && std::fabs(point.y) <= kLargestCoordinate))
            return;
    }

    Input input;
    input.curve = curve;
    input.source = source;
    _inputs.push_back(input);
}

void PlanarMap::clear()
{
    _inputs.clear();
    _vertices.clear();
    _halfEdges.clear();
    _faces.clear();
    _faceHalfEdges.clear();
    _faceBounds.clear();
    _vertexLookup.clear();
    _unboundedFace = -1;
}

int PlanarMap::vertexAt(const Point& snapped)
{
    const std::pair<int64_t, int64_t> key(toTwips(snapped.x), toTwips(snapped.y));

    const auto it = _vertexLookup.find(key);
    if (it != _vertexLookup.end())
        return it->second;

    MapVertex vertex;
    vertex.position = snapped;
    _vertices.push_back(vertex);

    const int index = static_cast<int>(_vertices.size()) - 1;
    _vertexLookup.emplace(key, index);
    return index;
}

namespace {

/// The parameter on \a curve nearest to \a point, and how far away it is.
/// A line is solved by projection; a cubic by a coarse sweep refined by
/// bisection, which is as exact as picking needs.
double nearestParameter(const Curve& curve, const Point& point, double& distance)
{
    const auto distanceAt = [&](double t) {
        const Point at = curve.pointAt(t);
        return std::hypot(at.x - point.x, at.y - point.y);
    };

    if (curve.isLine())
    {
        const Point& a = curve.start();
        const Point& b = curve.end();
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double lengthSquared = dx * dx + dy * dy;
        double t = 0.0;
        if (lengthSquared > 1.0e-18)
            t = std::max(0.0, std::min(1.0, ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared));
        distance = distanceAt(t);
        return t;
    }

    constexpr int kSamples = 32;
    double best = 0.0;
    double bestDistance = distanceAt(0.0);
    for (int i = 1; i <= kSamples; ++i)
    {
        const double t = static_cast<double>(i) / kSamples;
        const double d = distanceAt(t);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = t;
        }
    }

    double step = 1.0 / kSamples;
    for (int i = 0; i < 24; ++i)
    {
        step *= 0.5;
        for (const double t : {best - step, best + step})
        {
            if (t < 0.0 || t > 1.0)
                continue;
            const double d = distanceAt(t);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = t;
            }
        }
    }

    distance = bestDistance;
    return best;
}

/// Where a curve has to be cut, and the exact point the cut lands on when one
/// is known. Every crossing is given to both curves as the same point, and a
/// curve cut where another's endpoint touches it takes that endpoint, so the
/// pieces weld to one vertex instead of two neighboring ones.
struct Cut
{
    double t = 0.0;
    bool hasPoint = false;
    Point point;
};

} // namespace

std::vector<PlanarMap::Input> PlanarMap::splitAtCrossings(double tolerance) const
{
    std::vector<std::vector<Cut>> cuts(_inputs.size());

    // An endpoint has to sit clear of the other curve's own ends to count as a
    // T-junction; at the ends, welding does the job.
    const double touch = std::max(tolerance, kTwip / 2.0);

    const auto addTouch = [&](size_t onto, const Point& point) {
        const Curve& curve = _inputs[onto].curve;

        // A point clear of the curve's control box cannot touch it; this is
        // what keeps the pair loop cheap, since nearly every pair misses.
        double left, top, right, bottom;
        curve.controlBounds(left, top, right, bottom);
        if (point.x < left - touch || point.x > right + touch ||
            point.y < top - touch || point.y > bottom + touch)
        {
            return;
        }

        for (const Point& end : {curve.start(), curve.end()})
        {
            if (std::hypot(end.x - point.x, end.y - point.y) <= touch)
                return;
        }

        double distance = 0.0;
        const double t = nearestParameter(curve, point, distance);
        if (distance > touch || t <= 1.0e-6 || t >= 1.0 - 1.0e-6)
            return;

        cuts[onto].push_back({t, true, point});
    };

    // Only curves whose control boxes come within touching distance of each
    // other can cut each other. A sweep across the boxes, ordered by their
    // left sides, finds those pairs without testing every pair; each curve's
    // partners are then visited in index order, which is the order the cuts
    // have always been recorded in.
    const size_t count = _inputs.size();
    struct Box
    {
        double left, top, right, bottom;
    };
    std::vector<Box> boxes(count);
    for (size_t i = 0; i < count; ++i)
        _inputs[i].curve.controlBounds(boxes[i].left, boxes[i].top, boxes[i].right, boxes[i].bottom);

    const auto overlap = [&](size_t i, size_t j) {
        const Box& a = boxes[i];
        const Box& b = boxes[j];
        return !(a.right < b.left - touch || b.right < a.left - touch ||
            a.bottom < b.top - touch || b.bottom < a.top - touch);
    };

    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return boxes[a].left < boxes[b].left || (boxes[a].left == boxes[b].left && a < b);
    });

    std::vector<std::vector<size_t>> partners(count);
    for (size_t p = 0; p < count; ++p)
    {
        const size_t a = order[p];
        // Twice the reach, so rounding in the overlap test never loses a pair
        // the sweep should have offered it.
        const double reach = boxes[a].right + 2.0 * touch;
        for (size_t q = p + 1; q < count && boxes[order[q]].left <= reach; ++q)
        {
            const size_t b = order[q];
            const size_t i = std::min(a, b);
            const size_t j = std::max(a, b);
            if (overlap(i, j))
                partners[i].push_back(j);
        }
    }

    for (size_t i = 0; i < count; ++i)
    {
        for (const CurveIntersection& hit : selfIntersections(_inputs[i].curve, tolerance))
        {
            cuts[i].push_back({hit.t1, true, hit.position});
            cuts[i].push_back({hit.t2, true, hit.position});
        }

        std::vector<size_t>& others = partners[i];
        std::sort(others.begin(), others.end());

        for (const size_t j : others)
        {
            const std::vector<CurveIntersection> hits =
                intersectCurves(_inputs[i].curve, _inputs[j].curve, tolerance);

            for (const CurveIntersection& hit : hits)
            {
                cuts[i].push_back({hit.t1, true, hit.position});
                cuts[j].push_back({hit.t2, true, hit.position});
            }

            // T-junctions and collinear overlaps: an end of one curve lying on
            // the other splits it there. Running along together is not a
            // crossing, so the pair test above leaves these alone, and without
            // the split a piece would run straight through a vertex it ought
            // to stop at.
            addTouch(j, _inputs[i].curve.start());
            addTouch(j, _inputs[i].curve.end());
            addTouch(i, _inputs[j].curve.start());
            addTouch(i, _inputs[j].curve.end());
        }
    }

    std::vector<Input> pieces;

    for (size_t i = 0; i < _inputs.size(); ++i)
    {
        std::vector<Cut>& parameters = cuts[i];
        parameters.push_back({0.0, false, Point()});
        parameters.push_back({1.0, false, Point()});
        std::sort(parameters.begin(), parameters.end(),
            [](const Cut& a, const Cut& b) { return a.t < b.t; });

        for (size_t k = 1; k < parameters.size(); ++k)
        {
            const Cut& from = parameters[k - 1];
            const Cut& to = parameters[k];

            // Cuts that land on top of each other produce nothing worth keeping.
            if (to.t - from.t <= 1.0e-9)
                continue;

            Input piece;
            piece.curve = _inputs[i].curve.subcurve(from.t, to.t);
            piece.source = _inputs[i].source;
            piece.t0 = from.t;
            piece.t1 = to.t;

            // Pin the ends to the points the cuts were made at, so the curve
            // this piece meets there ends on exactly the same point.
            if (from.hasPoint || to.hasPoint)
            {
                const Point start = from.hasPoint ? from.point : piece.curve.start();
                const Point end = to.hasPoint ? to.point : piece.curve.end();
                piece.curve = piece.curve.isLine()
                    ? Curve::line(start, end)
                    : Curve::cubic(start, piece.curve.controlPoint(1),
                        piece.curve.controlPoint(2), end);
            }

            pieces.push_back(piece);
        }
    }

    return pieces;
}

void PlanarMap::buildHalfEdges(const std::vector<Input>& pieces)
{
    // Two shapes that share an edge each contribute their own copy of it.
    // Running along together is not a crossing, so the intersection pass leaves
    // both, and keeping both would put a zero-width sliver between them where
    // there is nothing. One piece of geometry means one edge.
    //
    // Two pieces are the same edge when they join the same vertices and bulge
    // the same way, which the midpoint captures. The comparison is tolerant: a
    // midpoint can sit on a half-twip, where the two copies' rounding noise
    // would send it to different grid points and let both copies through.
    struct Seen
    {
        Point middle;
        /// The forward half-edge the piece became.
        int halfEdge;
    };
    struct PairHash
    {
        size_t operator()(const std::pair<int, int>& key) const
        {
            return std::hash<int>()(key.first) * 1000003u ^ std::hash<int>()(key.second);
        }
    };
    std::unordered_map<std::pair<int, int>, std::vector<Seen>, PairHash> seen;
    _halfEdges.reserve(pieces.size() * 2);

    for (const Input& piece : pieces)
    {
        const Point start = snapToTwips(piece.curve.start());
        const Point end = snapToTwips(piece.curve.end());

        const int from = vertexAt(start);
        const int to = vertexAt(end);

        // Welding collapsed the piece to a point, so there is no edge here.
        // A loop back to its own start is not collapsed, which only the
        // control polygon tells: its chord is zero too.
        if (from == to && piece.curve.controlPolygonLength() < kMinimumPieceLength)
            continue;

        const Point middle = piece.curve.pointAt(0.5);
        std::vector<Seen>& middles = seen[{std::min(from, to), std::max(from, to)}];
        bool duplicate = false;
        for (const Seen& other : middles)
        {
            if (std::hypot(other.middle.x - middle.x, other.middle.y - middle.y) > kTwip)
                continue;
            duplicate = true;

            // The edge is kept once, but the later source claims it: a shape
            // drawn over another along a shared edge is the one whose stroke
            // shows there. The direction flags follow the new source's own
            // direction.
            HalfEdge& forward = _halfEdges[other.halfEdge];
            HalfEdge& backward = _halfEdges[forward.twin];
            if (piece.source > forward.source)
            {
                const bool sameWay = forward.from == from;
                forward.source = piece.source;
                backward.source = piece.source;
                forward.forward = sameWay;
                backward.forward = !sameWay;
                forward.t0 = backward.t0 = piece.t0;
                forward.t1 = backward.t1 = piece.t1;
            }
            break;
        }
        if (duplicate)
            continue;
        middles.push_back({middle, static_cast<int>(_halfEdges.size())});

        // Rebuild the geometry on the welded endpoints. The shift is under half
        // a twip, which is below what the format can store anyway.
        Curve curve = piece.curve.isLine()
            ? Curve::line(start, end)
            : Curve::cubic(start, piece.curve.controlPoint(1),
                piece.curve.controlPoint(2), end);

        Curve reversed = piece.curve.isLine()
            ? Curve::line(end, start)
            : Curve::cubic(end, piece.curve.controlPoint(2),
                piece.curve.controlPoint(1), start);

        const int forward = static_cast<int>(_halfEdges.size());
        const int backward = forward + 1;

        HalfEdge a;
        a.from = from;
        a.to = to;
        a.twin = backward;
        a.curve = curve;
        a.source = piece.source;
        a.forward = true;
        a.t0 = piece.t0;
        a.t1 = piece.t1;
        _halfEdges.push_back(a);

        HalfEdge b;
        b.from = to;
        b.to = from;
        b.twin = forward;
        b.curve = reversed;
        b.source = piece.source;
        b.forward = false;
        b.t0 = piece.t0;
        b.t1 = piece.t1;
        _halfEdges.push_back(b);

        _vertices[from].outgoing.push_back(forward);
        _vertices[to].outgoing.push_back(backward);
    }
}

void PlanarMap::sortOutgoingEdges()
{
    for (MapVertex& vertex : _vertices)
    {
        if (vertex.outgoing.size() < 2)
            continue;

        // The direction an edge leaves in is where it first crosses a small
        // circle round the vertex, not its tangent at the vertex: a pen
        // handle pulled back onto its anchor has no tangent there, and two
        // curves tangent at the vertex but bending apart are told apart by
        // where they have got to a little way out. The circle is half the
        // shortest chord here, so every edge reaches it.
        double radius = 1.0e300;
        for (const int edge : vertex.outgoing)
            radius = std::min(radius, _halfEdges[edge].curve.chordLength());
        radius = std::max(radius * 0.5, kTwip * 0.5);

        const Point& center = vertex.position;
        const auto departure = [&](int edge) {
            const Curve& curve = _halfEdges[edge].curve;
            const auto distanceAt = [&](double t) {
                const Point p = curve.pointAt(t);
                return std::hypot(p.x - center.x, p.y - center.y);
            };

            double low = 0.0;
            double high = 1.0;
            bool found = false;
            double previous = 0.0;
            for (int i = 1; i <= 32; ++i)
            {
                const double t = static_cast<double>(i) / 32.0;
                if (distanceAt(t) >= radius)
                {
                    low = previous;
                    high = t;
                    found = true;
                    break;
                }
                previous = t;
            }

            Point direction;
            if (found)
            {
                for (int i = 0; i < 30; ++i)
                {
                    const double mid = (low + high) * 0.5;
                    if (distanceAt(mid) >= radius)
                        high = mid;
                    else
                        low = mid;
                }
                const Point p = curve.pointAt(high);
                direction = Point(p.x - center.x, p.y - center.y);
            }
            else
            {
                direction = Point(curve.end().x - center.x, curve.end().y - center.y);
            }
            if (std::hypot(direction.x, direction.y) <= 1.0e-12)
                direction = curve.tangentAt(0.0);
            return std::atan2(direction.y, direction.x);
        };

        std::vector<std::pair<double, int>> keyed;
        for (const int edge : vertex.outgoing)
            keyed.push_back({departure(edge), edge});
        std::sort(keyed.begin(), keyed.end());
        for (size_t i = 0; i < keyed.size(); ++i)
            vertex.outgoing[i] = keyed[i].second;
    }
}

std::vector<PlanarMap::Cycle> PlanarMap::collectCycles() const
{
    std::vector<Cycle> cycles;
    std::vector<bool> visited(_halfEdges.size(), false);

    for (size_t i = 0; i < _halfEdges.size(); ++i)
    {
        if (visited[i])
            continue;

        Cycle cycle;
        double doubleArea = 0.0;

        int current = static_cast<int>(i);
        for (size_t step = 0; step <= _halfEdges.size(); ++step)
        {
            if (current < 0 || visited[current])
                break;

            visited[current] = true;
            cycle.edges.push_back(current);
            doubleArea += signedAreaContribution(_halfEdges[current].curve);
            cycle.bounds.add(_halfEdges[current].curve);

            current = _halfEdges[current].next;
            if (current == static_cast<int>(i))
                break;
        }

        cycle.area = doubleArea * 0.5;
        cycles.push_back(std::move(cycle));
    }

    return cycles;
}

void PlanarMap::Bounds::add(const Curve& curve)
{
    double l, t, r, b;
    curve.controlBounds(l, t, r, b);
    if (empty)
    {
        left = l;
        top = t;
        right = r;
        bottom = b;
        empty = false;
        return;
    }
    left = std::min(left, l);
    top = std::min(top, t);
    right = std::max(right, r);
    bottom = std::max(bottom, b);
}

bool PlanarMap::Bounds::rayCanHit(const Point& point) const
{
    if (empty)
        return false;

    // A curve stays inside its control box, give or take the rounding of
    // evaluating it, which is far below this.
    const double slack = 1.0e-6 * (1.0 + std::max(std::max(std::fabs(left), std::fabs(right)),
        std::max(std::fabs(top), std::fabs(bottom))));
    return !(point.y < top - slack || point.y > bottom + slack || point.x > right + slack);
}

int PlanarMap::rayCrossings(const Curve& curve, const Point& point)
{
    // A ray that misses the curve's box crosses nothing, and that is much
    // cheaper to find out than solving for where it would.
    Bounds bounds;
    bounds.add(curve);
    return bounds.rayCanHit(point) ? curve.horizontalRayCrossings(point) : 0;
}

bool PlanarMap::cycleContains(const Cycle& cycle, const Point& point) const
{
    // Cast a ray and count exactly where the boundary curves cross it. A
    // sampled outline sags inside a curve by more than the quarter twip an
    // interior point steps off the boundary, which put faces on the wrong
    // side of their own edges.
    if (!cycle.bounds.rayCanHit(point))
        return false;
    int crossings = 0;
    for (int edge : cycle.edges)
        crossings += rayCrossings(_halfEdges[edge].curve, point);
    return (crossings % 2) == 1;
}

bool PlanarMap::boundaryContains(int halfEdge, const Point& point) const
{
    // The walk cycleFrom() takes, counting as it goes.
    if (halfEdge < 0 || halfEdge >= static_cast<int>(_halfEdges.size()))
        return false;

    int crossings = 0;
    int current = halfEdge;
    for (size_t step = 0; step <= _halfEdges.size(); ++step)
    {
        crossings += rayCrossings(_halfEdges[current].curve, point);
        current = _halfEdges[current].next;
        if (current < 0 || current == halfEdge)
            break;
    }
    return (crossings % 2) == 1;
}

Point PlanarMap::sampleOutside(const Cycle& cycle) const
{
    if (cycle.edges.empty())
        return Point();

    const Curve& curve = _halfEdges[cycle.edges.front()].curve;
    const Point middle = curve.pointAt(0.5);
    const Point tangent = curve.tangentAt(0.5);

    const double length = std::hypot(tangent.x, tangent.y);
    if (length <= 1.0e-12)
        return middle;

    // A quarter twip off the edge is clear of the boundary but far closer than
    // anything else in the drawing, so it cannot stray into a neighbor.
    const double step = kTwip * 0.25;
    const Point offset(-tangent.y / length * step, tangent.x / length * step);

    const Point first(middle.x + offset.x, middle.y + offset.y);
    const Point second(middle.x - offset.x, middle.y - offset.y);

    return cycleContains(cycle, first) ? second : first;
}

void PlanarMap::linkFaces()
{
    // Around the vertex an edge arrives at, the face continues along whichever
    // edge leaves just before the way back. That is what keeps the walk hugging
    // one side of the drawing rather than cutting across it.
    for (size_t i = 0; i < _halfEdges.size(); ++i)
    {
        HalfEdge& edge = _halfEdges[i];
        const MapVertex& vertex = _vertices[edge.to];
        const std::vector<int>& outgoing = vertex.outgoing;

        const auto it = std::find(outgoing.begin(), outgoing.end(), edge.twin);
        if (it == outgoing.end())
            continue;

        const size_t position = static_cast<size_t>(it - outgoing.begin());
        const size_t previous = (position + outgoing.size() - 1) % outgoing.size();
        edge.next = outgoing[previous];
    }

    const std::vector<Cycle> cycles = collectCycles();

    // A cycle walked clockwise encloses a region. A cycle walked the other way
    // is a boundary seen from outside: either a hole in some region, or the edge
    // of the drawing itself. Which one cannot be decided until the regions are
    // known, so the two kinds are separated first.
    std::vector<size_t> regions;
    std::vector<size_t> boundaries;

    for (size_t i = 0; i < cycles.size(); ++i)
    {
        if (cycles[i].area > 0.0)
            regions.push_back(i);
        else
            boundaries.push_back(i);
    }

    std::vector<int> faceOfCycle(cycles.size(), -1);

    for (size_t index : regions)
    {
        MapFace face;
        face.halfEdge = cycles[index].edges.empty() ? -1 : cycles[index].edges.front();
        face.area = cycles[index].area;

        faceOfCycle[index] = static_cast<int>(_faces.size());
        _faces.push_back(face);
    }

    // Everything not inside a region is outside the drawing, and they all belong
    // to the one unbounded face.
    MapFace unbounded;
    unbounded.unbounded = true;
    unbounded.area = 0.0;
    _unboundedFace = static_cast<int>(_faces.size());
    _faces.push_back(unbounded);

    for (size_t index : boundaries)
    {
        const Cycle& cycle = cycles[index];
        if (cycle.edges.empty())
            continue;

        const Point sample = sampleOutside(cycle);

        // The smallest region containing it is the one it is a hole in: a hole
        // inside a hole belongs to the innermost region around it.
        int best = -1;
        double bestArea = 0.0;
        for (size_t region : regions)
        {
            if (!cycleContains(cycles[region], sample))
                continue;

            if (best < 0 || cycles[region].area < bestArea)
            {
                best = static_cast<int>(region);
                bestArea = cycles[region].area;
            }
        }

        const int faceIndex = best < 0 ? _unboundedFace : faceOfCycle[best];
        faceOfCycle[index] = faceIndex;

        _faces[faceIndex].holes.push_back(cycle.edges.front());
        _faces[faceIndex].area += cycle.area;

        if (faceIndex == _unboundedFace && _faces[faceIndex].halfEdge < 0)
            _faces[faceIndex].halfEdge = cycle.edges.front();
    }

    for (size_t i = 0; i < cycles.size(); ++i)
    {
        const int faceIndex = faceOfCycle[i];
        for (int edge : cycles[i].edges)
            _halfEdges[edge].face = faceIndex;
    }

    _faceHalfEdges.assign(_faces.size(), std::vector<int>());
    for (size_t i = 0; i < _halfEdges.size(); ++i)
    {
        const int face = _halfEdges[i].face;
        if (face >= 0 && face < static_cast<int>(_faces.size()))
            _faceHalfEdges[face].push_back(static_cast<int>(i));
    }

    _faceBounds.assign(_faces.size(), Bounds());
    for (size_t index : regions)
        _faceBounds[faceOfCycle[index]] = cycles[index].bounds;
}

std::vector<int> PlanarMap::cycleFrom(int halfEdge) const
{
    std::vector<int> edges;
    if (halfEdge < 0 || halfEdge >= static_cast<int>(_halfEdges.size()))
        return edges;

    int current = halfEdge;
    for (size_t step = 0; step <= _halfEdges.size(); ++step)
    {
        edges.push_back(current);
        current = _halfEdges[current].next;
        if (current < 0 || current == halfEdge)
            break;
    }

    return edges;
}

const std::vector<int>& PlanarMap::faceHalfEdges(int face) const
{
    static const std::vector<int> none;
    if (face < 0 || face >= static_cast<int>(_faceHalfEdges.size()))
        return none;
    return _faceHalfEdges[face];
}

bool PlanarMap::faceContains(int face, const Point& point) const
{
    if (face < 0 || face >= static_cast<int>(_faces.size()))
        return false;

    const MapFace& target = _faces[face];

    // The unbounded face is everything the drawing does not cover, so it
    // contains whatever falls outside all of its boundaries.
    if (target.unbounded)
    {
        for (int hole : target.holes)
        {
            if (boundaryContains(hole, point))
                return false;
        }
        return true;
    }

    // A ray that misses the outer boundary's box crosses none of it.
    if (face < static_cast<int>(_faceBounds.size()) && !_faceBounds[face].rayCanHit(point))
        return false;

    if (!boundaryContains(target.halfEdge, point))
        return false;

    // Inside the outer boundary but inside a hole is not inside the face.
    for (int hole : target.holes)
    {
        if (boundaryContains(hole, point))
            return false;
    }

    return true;
}

bool PlanarMap::interiorPoint(int face, Point& result) const
{
    if (face < 0 || face >= static_cast<int>(_faces.size()))
        return false;

    const MapFace& target = _faces[face];
    if (target.unbounded || target.halfEdge < 0)
        return false;

    // Step off each boundary edge in turn until a point lands inside. One edge
    // is usually enough; a sliver of a face may need another, or a bigger
    // step. Every candidate is checked against the face, so a larger step
    // that lands in a neighbor is simply passed over.
    const std::vector<int> outer = cycleFrom(target.halfEdge);

    for (const double step : {kTwip * 0.25, kTwip, kTwip * 4.0})
    {
        for (int edge : outer)
        {
            const Curve& curve = _halfEdges[edge].curve;
            const Point middle = curve.pointAt(0.5);
            const Point tangent = curve.tangentAt(0.5);

            const double length = std::hypot(tangent.x, tangent.y);
            if (length <= 1.0e-12)
                continue;

            const Point offset(-tangent.y / length * step, tangent.x / length * step);

            const Point candidates[2] = {
                Point(middle.x + offset.x, middle.y + offset.y),
                Point(middle.x - offset.x, middle.y - offset.y)
            };

            for (const Point& candidate : candidates)
            {
                if (faceContains(face, candidate))
                {
                    result = candidate;
                    return true;
                }
            }
        }
    }

    return false;
}

int PlanarMap::faceAt(const Point& point) const
{
    // Faces do not overlap, so the first one containing the point is the answer.
    for (size_t i = 0; i < _faces.size(); ++i)
    {
        if (_faces[i].unbounded)
            continue;
        if (faceContains(static_cast<int>(i), point))
            return static_cast<int>(i);
    }

    return -1;
}

int PlanarMap::fillAt(const Point& point) const
{
    return faceFill(faceAt(point));
}

int PlanarMap::faceFill(int face) const
{
    if (face < 0 || face >= static_cast<int>(_faces.size()))
        return -1;
    return _faces[face].fillStyle;
}

void PlanarMap::setFaceFill(int face, int fillStyle)
{
    if (face < 0 || face >= static_cast<int>(_faces.size()))
        return;

    _faces[face].fillStyle = fillStyle;

    // A half-edge carries the fill of the face it borders, which is the fill on
    // its left.
    for (const int edge : faceHalfEdges(face))
        _halfEdges[edge].leftFill = fillStyle;
}

void PlanarMap::build(double tolerance)
{
    _vertices.clear();
    _halfEdges.clear();
    _faces.clear();
    _faceHalfEdges.clear();
    _faceBounds.clear();
    _vertexLookup.clear();
    _unboundedFace = -1;

    if (_inputs.empty())
        return;

    const std::vector<Input> pieces = splitAtCrossings(tolerance);

    buildHalfEdges(pieces);
    sortOutgoingEdges();
    linkFaces();
}

} // namespace fla
