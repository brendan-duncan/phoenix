#include "region_split.h"

#include "shape_merge.h"

#include "../data/shape.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fla {

namespace {

double distanceToSegment(const Point& p, const Point& a, const Point& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSquared > 1.0e-12)
        t = std::max(0.0, std::min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared));
    const double x = a.x + dx * t;
    const double y = a.y + dy * t;
    return std::hypot(p.x - x, p.y - y);
}

/// How far a point is from a curve, measured against a fine polyline. Picking
/// needs only pixel accuracy.
double distanceToCurve(const Point& p, const Curve& curve)
{
    if (curve.isLine())
        return distanceToSegment(p, curve.start(), curve.end());

    constexpr int kSamples = 24;
    double best = 1.0e300;
    Point previous = curve.start();
    for (int i = 1; i <= kSamples; ++i)
    {
        const Point next = curve.pointAt(static_cast<double>(i) / kSamples);
        best = std::min(best, distanceToSegment(p, previous, next));
        previous = next;
    }
    return best;
}

Path* pathFor(Edge* edge, const Curve& curve)
{
    Path* path = new Path(edge);
    path->segments.push_back(new PathSegment(
        PathSegment::Command::Move, {curve.start()}, path));

    if (curve.isLine())
    {
        path->segments.push_back(new PathSegment(
            PathSegment::Command::Line, {curve.end()}, path));
    }
    else
    {
        path->segments.push_back(new PathSegment(
            PathSegment::Command::Cubic,
            {curve.controlPoint(1), curve.controlPoint(2), curve.end()}, path));
    }
    return path;
}

} // namespace

RegionSplitter::RegionSplitter(const Shape& shape)
    : _shape(shape)
    , _curves(shapeCurves(shape))
{
    if (_curves.empty())
        return;

    for (size_t i = 0; i < _curves.size(); ++i)
        _map.addCurve(_curves[i].curve, static_cast<int>(i));
    _map.build();
    attributeFillsFromSource(_map, _curves);
}

RegionSplitter::RegionSplitter(const Shape& shape, const std::vector<Curve>& cutters)
    : _shape(shape)
    , _curves(shapeCurves(shape))
{
    if (_curves.empty())
        return;

    // What the shape paints, on its own, to ask about each divided face.
    PlanarMap painted;
    for (size_t i = 0; i < _curves.size(); ++i)
        painted.addCurve(_curves[i].curve, static_cast<int>(i));
    painted.build();
    attributeFillsFromSource(painted, _curves);

    // The cutters join the arrangement as curves with no styles of their own.
    for (const Curve& curve : cutters)
    {
        ShapeCurve cutter;
        cutter.curve = curve;
        _curves.push_back(cutter);
    }

    for (size_t i = 0; i < _curves.size(); ++i)
        _map.addCurve(_curves[i].curve, static_cast<int>(i));
    _map.build();

    // Every face, cut or not, is painted with whatever the shape painted
    // where it lies. Sampling rather than reading the cutters' sides is what
    // keeps a face bordered only by cutter pieces correct.
    for (size_t face = 0; face < _map.faces().size(); ++face)
    {
        const int index = static_cast<int>(face);
        Point inside;
        if (_map.faces()[face].unbounded || !_map.interiorPoint(index, inside))
        {
            _map.setFaceFill(index, -1);
            continue;
        }
        _map.setFaceFill(index, painted.fillAt(inside));
    }
}

namespace {

/// The nearest point of a curve to \a target, by sampling and then closing
/// in on the best sample. Returns the distance.
double closestOnCurve(const Curve& curve, const Point& target, Point& nearest)
{
    const auto distanceAt = [&](double t) {
        const Point p = curve.pointAt(t);
        return std::hypot(p.x - target.x, p.y - target.y);
    };

    const int samples = 32;
    double bestT = 0.0;
    double best = distanceAt(0.0);
    for (int i = 1; i <= samples; ++i)
    {
        const double t = static_cast<double>(i) / samples;
        const double d = distanceAt(t);
        if (d < best)
        {
            best = d;
            bestT = t;
        }
    }

    double low = std::max(0.0, bestT - 1.0 / samples);
    double high = std::min(1.0, bestT + 1.0 / samples);
    for (int i = 0; i < 24; ++i)
    {
        const double a = low + (high - low) / 3.0;
        const double b = high - (high - low) / 3.0;
        if (distanceAt(a) < distanceAt(b))
            high = b;
        else
            low = a;
    }
    const double t = (low + high) * 0.5;
    const double d = distanceAt(t);
    if (d < best)
    {
        best = d;
        bestT = t;
    }
    nearest = curve.pointAt(bestT);
    return best;
}

} // namespace

std::vector<Curve> RegionSplitter::gapJoins(double gap) const
{
    std::vector<Curve> joins;
    if (!valid() || gap <= 0.0)
        return joins;

    const std::vector<MapVertex>& vertices = _map.vertices();
    const std::vector<HalfEdge>& halves = _map.halfEdges();

    // A loose end is a vertex with one piece leaving it; fills cannot dangle,
    // so these are stroke ends.
    std::vector<int> loose;
    for (size_t v = 0; v < vertices.size(); ++v)
    {
        if (vertices[v].outgoing.size() == 1)
            loose.push_back(static_cast<int>(v));
    }

    std::vector<bool> joined(vertices.size(), false);
    for (const int v : loose)
    {
        if (joined[v])
            continue;
        const Point& from = vertices[v].position;

        // Another loose end first: two strokes that nearly meet.
        int bestVertex = -1;
        double bestDistance = gap;
        for (const int other : loose)
        {
            if (other == v || joined[other])
                continue;
            const Point& to = vertices[other].position;
            const double d = std::hypot(to.x - from.x, to.y - from.y);
            if (d <= bestDistance)
            {
                bestDistance = d;
                bestVertex = other;
            }
        }
        if (bestVertex >= 0)
        {
            joins.push_back(Curve::line(from, vertices[bestVertex].position));
            joined[v] = true;
            joined[bestVertex] = true;
            continue;
        }

        // Otherwise the side of a piece that the stroke stops short of.
        Point bestPoint;
        bool found = false;
        bestDistance = gap;
        for (const HalfEdge& half : halves)
        {
            if (!half.forward || half.from == v || half.to == v)
                continue;
            Point nearest;
            const double d = closestOnCurve(half.curve, from, nearest);
            if (d <= bestDistance)
            {
                bestDistance = d;
                bestPoint = nearest;
                found = true;
            }
        }
        if (found && bestDistance > 1.0e-9)
        {
            joins.push_back(Curve::line(from, bestPoint));
            joined[v] = true;
        }
    }
    return joins;
}

int RegionSplitter::strokeOf(const HalfEdge& half) const
{
    if (half.source < 0 || half.source >= static_cast<int>(_curves.size()))
        return -1;
    return _curves[half.source].strokeStyle;
}

int RegionSplitter::forwardOf(int half) const
{
    const std::vector<HalfEdge>& halves = _map.halfEdges();
    if (half < 0 || half >= static_cast<int>(halves.size()))
        return -1;
    if (halves[half].forward)
        return half;
    return halves[half].twin;
}

std::vector<int> RegionSplitter::faceBoundary(int face) const
{
    std::vector<int> boundary;
    if (face < 0 || face >= static_cast<int>(_map.faces().size()))
        return boundary;

    const MapFace& mapFace = _map.faces()[face];
    boundary = _map.cycleFrom(mapFace.halfEdge);
    for (const int hole : mapFace.holes)
    {
        const std::vector<int> cycle = _map.cycleFrom(hole);
        boundary.insert(boundary.end(), cycle.begin(), cycle.end());
    }
    return boundary;
}

std::vector<int> RegionSplitter::connectedStrokes(int half) const
{
    const std::vector<HalfEdge>& halves = _map.halfEdges();
    const std::vector<MapVertex>& vertices = _map.vertices();

    std::vector<int> result;
    const int start = forwardOf(half);
    if (start < 0)
        return result;

    const int stroke = strokeOf(halves[start]);
    std::set<int> seen;
    std::vector<int> pending{start};

    while (!pending.empty())
    {
        const int current = pending.back();
        pending.pop_back();
        if (!seen.insert(current).second)
            continue;
        result.push_back(current);

        // At either end, carry on only where exactly two pieces meet: a
        // corner that nothing else touches. Where three or more meet the run
        // stops, as it does in Animate.
        const HalfEdge& edge = halves[current];
        for (const int vertex : {edge.from, edge.to})
        {
            if (vertex < 0 || vertex >= static_cast<int>(vertices.size()))
                continue;
            const std::vector<int>& outgoing = vertices[vertex].outgoing;
            if (outgoing.size() != 2)
                continue;

            for (const int leaving : outgoing)
            {
                const int candidate = forwardOf(leaving);
                if (candidate < 0 || candidate == current)
                    continue;
                if (strokeOf(halves[candidate]) != stroke)
                    continue;
                pending.push_back(candidate);
            }
        }
    }

    return result;
}

RegionPick RegionSplitter::pickAt(const Point& localPos, double tolerance, bool extended) const
{
    RegionPick pick;
    if (!valid())
        return pick;

    const std::vector<HalfEdge>& halves = _map.halfEdges();

    // Strokes draw over fills, so a stroke within reach is what was clicked.
    int nearest = -1;
    double nearestDistance = tolerance;
    for (size_t i = 0; i < halves.size(); ++i)
    {
        const HalfEdge& half = halves[i];
        if (!half.forward || strokeOf(half) == -1)
            continue;

        const double distance = distanceToCurve(localPos, half.curve);
        if (distance <= nearestDistance)
        {
            nearestDistance = distance;
            nearest = static_cast<int>(i);
        }
    }

    if (nearest >= 0)
    {
        pick.kind = RegionPick::Kind::Stroke;
        pick.strokeHalfEdges = extended ? connectedStrokes(nearest) : std::vector<int>{nearest};
        return pick;
    }

    const int face = _map.faceAt(localPos);
    if (face < 0 || _map.faceFill(face) == -1)
        return pick;

    pick.kind = RegionPick::Kind::Fill;
    pick.face = face;

    if (extended)
    {
        // A double-click takes the strokes round the fill with it.
        for (const int half : faceBoundary(face))
        {
            const int forward = forwardOf(half);
            if (forward >= 0 && strokeOf(halves[forward]) != -1 &&
                std::find(pick.strokeHalfEdges.begin(), pick.strokeHalfEdges.end(), forward) ==
                    pick.strokeHalfEdges.end())
            {
                pick.strokeHalfEdges.push_back(forward);
            }
        }
    }

    return pick;
}

std::vector<int> RegionSplitter::facesWhere(const std::function<bool(const Point&)>& inside) const
{
    std::vector<int> faces;
    if (!valid())
        return faces;

    for (size_t face = 0; face < _map.faces().size(); ++face)
    {
        const int index = static_cast<int>(face);
        if (_map.faces()[face].unbounded || _map.faceFill(index) == -1)
            continue;

        Point point;
        if (_map.interiorPoint(index, point) && inside(point))
            faces.push_back(index);
    }
    return faces;
}

std::vector<int> RegionSplitter::strokesWhere(const std::function<bool(const Point&)>& inside) const
{
    std::vector<int> strokes;
    if (!valid())
        return strokes;

    const std::vector<HalfEdge>& halves = _map.halfEdges();
    for (size_t i = 0; i < halves.size(); ++i)
    {
        const HalfEdge& half = halves[i];
        if (!half.forward || strokeOf(half) == -1)
            continue;

        // Ends and middle all inside: a piece the area's outline cut has its
        // ends on that outline, and the middle decides which side it is on.
        if (inside(half.curve.pointAt(0.5)) &&
            inside(half.curve.pointAt(0.25)) && inside(half.curve.pointAt(0.75)))
        {
            strokes.push_back(static_cast<int>(i));
        }
    }
    return strokes;
}

bool RegionSplitter::splitSet(const std::vector<int>& faces, const std::vector<int>& strokes,
    Shape*& piece, Shape*& remainder) const
{
    piece = nullptr;
    remainder = nullptr;
    if (!valid() || (faces.empty() && strokes.empty()))
        return false;

    const std::set<int> chosen(faces.begin(), faces.end());
    const std::set<int> lifted(strokes.begin(), strokes.end());

    const auto isLifted = [&](const HalfEdge& half, const std::vector<HalfEdge>& halves) {
        const int index = static_cast<int>(&half - &halves[0]);
        return lifted.count(index) > 0 || (half.twin >= 0 && lifted.count(half.twin) > 0);
    };

    // The piece: the chosen fills and strokes, nothing else.
    PlanarMap pieceMap = _map;
    for (size_t face = 0; face < pieceMap.faces().size(); ++face)
    {
        if (chosen.count(static_cast<int>(face)) == 0)
            pieceMap.setFaceFill(static_cast<int>(face), -1);
    }
    piece = cloneShape(_shape, nullptr);
    const std::vector<HalfEdge>& pieceHalves = pieceMap.halfEdges();
    rebuildShapeEdges(*piece, pieceMap, _curves,
        [&](const HalfEdge& half) { return isLifted(half, pieceHalves); });

    // The remainder: everything but.
    PlanarMap restMap = _map;
    for (const int face : chosen)
        restMap.setFaceFill(face, -1);
    remainder = cloneShape(_shape, nullptr);
    const std::vector<HalfEdge>& restHalves = restMap.halfEdges();
    rebuildShapeEdges(*remainder, restMap, _curves,
        [&](const HalfEdge& half) { return !isLifted(half, restHalves); });

    return true;
}

int RegionSplitter::faceAt(const Point& localPos) const
{
    return valid() ? _map.faceAt(localPos) : -1;
}

int RegionSplitter::faceFill(int face) const
{
    return _map.faceFill(face);
}

int RegionSplitter::strokeOf(const RegionPick& pick) const
{
    if (pick.kind != RegionPick::Kind::Stroke || pick.strokeHalfEdges.empty())
        return -1;
    const int index = pick.strokeHalfEdges.front();
    if (index < 0 || index >= static_cast<int>(_map.halfEdges().size()))
        return -1;
    return strokeOf(_map.halfEdges()[index]);
}

Shape* RegionSplitter::withFaceFill(int face, int fillIndex) const
{
    if (!valid() || face < 0 || face >= static_cast<int>(_map.faces().size()))
        return nullptr;

    PlanarMap painted = _map;
    painted.setFaceFill(face, fillIndex);

    Shape* result = cloneShape(_shape, nullptr);
    rebuildShapeEdges(*result, painted, _curves);
    return result;
}

Shape* RegionSplitter::withStroke(const RegionPick& pick, int strokeIndex) const
{
    if (!valid() || pick.isEmpty())
        return nullptr;

    std::set<int> affected;
    if (pick.kind == RegionPick::Kind::Fill)
    {
        for (const int half : faceBoundary(pick.face))
        {
            const int forward = forwardOf(half);
            if (forward >= 0)
                affected.insert(forward);
        }
    }
    else
    {
        affected.insert(pick.strokeHalfEdges.begin(), pick.strokeHalfEdges.end());
    }

    const std::vector<HalfEdge>& halves = _map.halfEdges();
    Shape* result = cloneShape(_shape, nullptr);
    rebuildShapeEdges(*result, _map, _curves, nullptr, [&](const HalfEdge& half) {
        const int index = static_cast<int>(&half - &halves[0]);
        const bool hit = affected.count(index) > 0 ||
            (half.twin >= 0 && affected.count(half.twin) > 0);
        return hit ? strokeIndex : kKeepStroke;
    });
    return result;
}

bool RegionSplitter::split(const RegionPick& pick, Shape*& piece, Shape*& remainder) const
{
    piece = nullptr;
    remainder = nullptr;
    if (pick.isEmpty() || !valid())
        return false;

    const std::vector<HalfEdge>& halves = _map.halfEdges();

    // The lifted strokes, as forward half-edges, looked up by either direction.
    std::set<int> lifted(pick.strokeHalfEdges.begin(), pick.strokeHalfEdges.end());
    const auto isLifted = [&](const HalfEdge& half) {
        const int index = static_cast<int>(&half - &halves[0]);
        return lifted.count(index) > 0 || (half.twin >= 0 && lifted.count(half.twin) > 0);
    };

    // The piece keeps the source's whole style table so its indices still
    // mean the same thing; a merge renumbers what it uses anyway.
    piece = cloneShape(_shape, nullptr);
    for (Edge* edge : piece->edges)
        delete edge;
    piece->edges.clear();

    if (pick.kind == RegionPick::Kind::Fill)
    {
        const int fill = _map.faceFill(pick.face);
        for (const int index : faceBoundary(pick.face))
        {
            const HalfEdge& half = halves[index];

            // The half-edge belongs to the face, so the face is on its left:
            // fillStyle1 by the file's convention.
            Edge* edge = new Edge(piece);
            edge->fillStyle1 = fill;
            edge->fillStyle0 = -1;
            edge->strokeStyle = isLifted(half) ? strokeOf(half) : -1;
            edge->paths.push_back(pathFor(edge, half.curve));
            piece->edges.push_back(edge);
        }
    }
    else
    {
        for (const int index : pick.strokeHalfEdges)
        {
            if (index < 0 || index >= static_cast<int>(halves.size()))
                continue;
            const HalfEdge& half = halves[index];

            Edge* edge = new Edge(piece);
            edge->strokeStyle = strokeOf(half);
            edge->paths.push_back(pathFor(edge, half.curve));
            piece->edges.push_back(edge);
        }
    }

    // What is left: the same arrangement with the lifted fill emptied and the
    // lifted strokes dropped, written back out as edges.
    PlanarMap rest = _map;
    if (pick.kind == RegionPick::Kind::Fill)
        rest.setFaceFill(pick.face, -1);

    remainder = cloneShape(_shape, nullptr);
    const std::vector<HalfEdge>& restHalves = rest.halfEdges();
    rebuildShapeEdges(*remainder, rest, _curves, [&](const HalfEdge& half) {
        const int index = static_cast<int>(&half - &restHalves[0]);
        return lifted.count(index) == 0 &&
            !(half.twin >= 0 && lifted.count(half.twin) > 0);
    });

    return true;
}

} // namespace fla
