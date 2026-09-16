#include "planar_map.h"

#include "intersect.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace fla {

namespace {

/// How many points to sample a curve at when measuring the area it encloses.
/// Areas are only used to tell bounded faces from the unbounded one and to
/// report a rough size, so this does not need to be exact.
constexpr int kAreaSamples = 8;

/// A piece shorter than this in twips is a rounding artefact rather than
/// geometry, and welding has already pulled its ends together.
constexpr double kMinimumPieceLength = PlanarMap::kTwip * 0.5;

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

std::vector<PlanarMap::Input> PlanarMap::splitAtCrossings(double tolerance) const
{
    // Parameters along each input curve where it has to be cut.
    std::vector<std::vector<double>> cuts(_inputs.size());

    for (size_t i = 0; i < _inputs.size(); ++i)
    {
        for (const CurveIntersection& hit : selfIntersections(_inputs[i].curve, tolerance))
        {
            cuts[i].push_back(hit.t1);
            cuts[i].push_back(hit.t2);
        }

        for (size_t j = i + 1; j < _inputs.size(); ++j)
        {
            // Every pair is tested. A sweep line would avoid the quadratic cost,
            // but shapes here hold tens of curves, not thousands.
            const std::vector<CurveIntersection> hits =
                intersectCurves(_inputs[i].curve, _inputs[j].curve, tolerance);

            for (const CurveIntersection& hit : hits)
            {
                cuts[i].push_back(hit.t1);
                cuts[j].push_back(hit.t2);
            }
        }
    }

    std::vector<Input> pieces;

    for (size_t i = 0; i < _inputs.size(); ++i)
    {
        std::vector<double>& parameters = cuts[i];
        parameters.push_back(0.0);
        parameters.push_back(1.0);
        std::sort(parameters.begin(), parameters.end());

        for (size_t k = 1; k < parameters.size(); ++k)
        {
            const double from = parameters[k - 1];
            const double to = parameters[k];

            // Cuts that land on top of each other produce nothing worth keeping.
            if (to - from <= 1.0e-9)
                continue;

            Input piece;
            piece.curve = _inputs[i].curve.subcurve(from, to);
            piece.source = _inputs[i].source;
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
    std::map<std::tuple<int, int, int64_t, int64_t>, bool> seen;

    for (const Input& piece : pieces)
    {
        const Point start = snapToTwips(piece.curve.start());
        const Point end = snapToTwips(piece.curve.end());

        const int from = vertexAt(start);
        const int to = vertexAt(end);

        // Welding collapsed the piece to a point, so there is no edge here.
        if (from == to && piece.curve.chordLength() < kMinimumPieceLength)
            continue;

        // Two pieces count as the same edge when they join the same vertices and
        // bulge the same way, which the midpoint captures.
        const Point middle = snapToTwips(piece.curve.pointAt(0.5));
        const std::tuple<int, int, int64_t, int64_t> key(
            std::min(from, to), std::max(from, to),
            toTwips(middle.x), toTwips(middle.y));

        if (!seen.emplace(key, true).second)
            continue;

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
        _halfEdges.push_back(a);

        HalfEdge b;
        b.from = to;
        b.to = from;
        b.twin = forward;
        b.curve = reversed;
        b.source = piece.source;
        b.forward = false;
        _halfEdges.push_back(b);

        _vertices[from].outgoing.push_back(forward);
        _vertices[to].outgoing.push_back(backward);
    }
}

void PlanarMap::sortOutgoingEdges()
{
    for (MapVertex& vertex : _vertices)
    {
        std::sort(vertex.outgoing.begin(), vertex.outgoing.end(),
            [this](int left, int right) {
                const Point a = _halfEdges[left].curve.tangentAt(0.0);
                const Point b = _halfEdges[right].curve.tangentAt(0.0);
                return std::atan2(a.y, a.x) < std::atan2(b.y, b.x);
            });
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

            current = _halfEdges[current].next;
            if (current == static_cast<int>(i))
                break;
        }

        cycle.area = doubleArea * 0.5;
        cycles.push_back(std::move(cycle));
    }

    return cycles;
}

bool PlanarMap::cycleContains(const Cycle& cycle, const Point& point) const
{
    // Flatten the boundary and cast a ray. Faces here are small and this runs
    // once per nesting question, so the sampling cost does not matter.
    std::vector<Point> outline;
    for (int edge : cycle.edges)
    {
        const Curve& curve = _halfEdges[edge].curve;
        for (int i = 0; i < kAreaSamples; ++i)
            outline.push_back(curve.pointAt(static_cast<double>(i) / kAreaSamples));
    }

    if (outline.size() < 3)
        return false;

    bool inside = false;
    for (size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
    {
        const Point& a = outline[i];
        const Point& b = outline[j];

        if ((a.y > point.y) == (b.y > point.y))
            continue;

        const double x = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
        if (point.x < x)
            inside = !inside;
    }

    return inside;
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
    // anything else in the drawing, so it cannot stray into a neighbour.
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
            Cycle cycle;
            cycle.edges = cycleFrom(hole);
            if (cycleContains(cycle, point))
                return false;
        }
        return true;
    }

    Cycle outer;
    outer.edges = cycleFrom(target.halfEdge);
    if (!cycleContains(outer, point))
        return false;

    // Inside the outer boundary but inside a hole is not inside the face.
    for (int hole : target.holes)
    {
        Cycle cycle;
        cycle.edges = cycleFrom(hole);
        if (cycleContains(cycle, point))
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
    // is usually enough; a sliver of a face may need another.
    const std::vector<int> outer = cycleFrom(target.halfEdge);
    const double step = kTwip * 0.25;

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
    for (HalfEdge& edge : _halfEdges)
    {
        if (edge.face == face)
            edge.leftFill = fillStyle;
    }
}

void PlanarMap::build(double tolerance)
{
    _vertices.clear();
    _halfEdges.clear();
    _faces.clear();
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
