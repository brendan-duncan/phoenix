#include "shape_geometry.h"

#include "../data/shape.h"

#include <cmath>

namespace fla {

namespace {

bool samePoint(const Point& a, const Point& b)
{
    return std::fabs(a.x - b.x) < 1.0e-9 && std::fabs(a.y - b.y) < 1.0e-9;
}

} // namespace

std::vector<ShapeCurve> shapeCurves(const Shape& shape)
{
    std::vector<ShapeCurve> curves;

    for (size_t edgeIndex = 0; edgeIndex < shape.edges.size(); ++edgeIndex)
    {
        const Edge* edge = shape.edges[edgeIndex];
        if (!edge)
            continue;

        for (const Path* path : edge->paths)
        {
            if (!path)
                continue;

            // A style set on the path overrides the one on the edge, which is
            // how the format lets one edge carry several styles.
            ShapeCurve prototype;
            prototype.edgeIndex = static_cast<int>(edgeIndex);
            prototype.fillStyle0 = edge->fillStyle0;
            prototype.fillStyle1 = path->fillStyleIndex != -1
                ? path->fillStyleIndex : edge->fillStyle1;
            prototype.strokeStyle = path->lineStyleIndex != -1
                ? path->lineStyleIndex : edge->strokeStyle;

            Point current;
            Point subpathStart;
            bool started = false;

            for (const PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;

                switch (segment->command)
                {
                case PathSegment::Command::Move:
                    if (!segment->points.empty())
                    {
                        current = segment->points[0];
                        subpathStart = current;
                        started = true;
                    }
                    break;

                case PathSegment::Command::Line:
                    if (started && !segment->points.empty())
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::line(current, segment->points[0]);
                        curves.push_back(piece);
                        current = segment->points[0];
                    }
                    break;

                case PathSegment::Command::Quad:
                    if (started && segment->points.size() >= 2)
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::quadratic(current, segment->points[0],
                            segment->points[1]);
                        curves.push_back(piece);
                        current = segment->points[1];
                    }
                    break;

                case PathSegment::Command::Cubic:
                    if (started && segment->points.size() >= 3)
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::cubic(current, segment->points[0],
                            segment->points[1], segment->points[2]);
                        curves.push_back(piece);
                        current = segment->points[2];
                    }
                    break;

                case PathSegment::Command::Close:
                    // Closing runs back to where the subpath began, unless it is
                    // already there.
                    if (started && !samePoint(current, subpathStart))
                    {
                        ShapeCurve piece = prototype;
                        piece.curve = Curve::line(current, subpathStart);
                        curves.push_back(piece);
                        current = subpathStart;
                    }
                    break;
                }
            }
        }
    }

    return curves;
}

void attributeFillsFromSource(PlanarMap& map, const std::vector<ShapeCurve>& sources)
{
    for (size_t face = 0; face < map.faces().size(); ++face)
    {
        const int index = static_cast<int>(face);

        if (map.faces()[face].unbounded)
        {
            map.setFaceFill(index, -1);
            continue;
        }

        // Any half-edge bordering the face answers the question; they all agree
        // when the arrangement matches the file it came from.
        int fill = -1;
        for (const HalfEdge& edge : map.halfEdges())
        {
            if (edge.face != index)
                continue;
            if (edge.source < 0 || edge.source >= static_cast<int>(sources.size()))
                continue;

            const ShapeCurve& source = sources[edge.source];
            fill = edge.forward ? source.fillStyle1 : source.fillStyle0;
            break;
        }

        map.setFaceFill(index, fill);
    }
}

bool faceFillFromSources(const PlanarMap& map, int face, const std::vector<ShapeCurve>& sources,
    size_t first, size_t last, int& fill)
{
    for (const HalfEdge& edge : map.halfEdges())
    {
        if (edge.face != face || edge.source < 0)
            continue;
        const size_t source = static_cast<size_t>(edge.source);
        if (source < first || source >= last || source >= sources.size())
            continue;
        const ShapeCurve& shapeCurve = sources[source];
        fill = edge.forward ? shapeCurve.fillStyle1 : shapeCurve.fillStyle0;
        return true;
    }
    return false;
}

void rebuildShapeEdges(Shape& shape, const PlanarMap& map,
    const std::vector<ShapeCurve>& sources, const StrokeFilter& keepStroke,
    const StrokeOverride& strokeFor)
{
    for (Edge* edge : shape.edges)
        delete edge;
    shape.edges.clear();

    const std::vector<HalfEdge>& halves = map.halfEdges();
    const std::vector<MapVertex>& vertices = map.vertices();
    const size_t count = halves.size();

    // What each pair will be written with, decided once per forward half.
    struct Styled
    {
        int leftFill = -1;
        int rightFill = -1;
        int stroke = -1;
        bool drawn = false;
    };
    std::vector<Styled> styled(count);

    for (size_t i = 0; i < count; ++i)
    {
        const HalfEdge& half = halves[i];
        // One edge per pair, not per direction.
        if (!half.forward)
            continue;

        Styled& style = styled[i];
        style.leftFill = half.leftFill;
        style.rightFill = half.twin >= 0 && half.twin < static_cast<int>(count)
            ? halves[half.twin].leftFill : -1;

        if (half.source >= 0 && half.source < static_cast<int>(sources.size()))
            style.stroke = sources[half.source].strokeStyle;

        if (strokeFor)
        {
            const int chosen = strokeFor(half);
            if (chosen != kKeepStroke)
                style.stroke = chosen;
        }

        if (style.stroke != -1 && keepStroke && !keepStroke(half))
            style.stroke = -1;

        // An edge with the same thing on both sides separates nothing. Whether
        // that is two empty sides or the same fill on each, it draws nothing and
        // is left out -- which is exactly how the seam vanishes where two shapes
        // merged into one region.
        style.drawn = !(style.leftFill == style.rightFill && style.stroke == -1);
    }

    const auto sameStyle = [&](int a, int b) {
        return styled[a].drawn && styled[b].drawn &&
            styled[a].leftFill == styled[b].leftFill &&
            styled[a].rightFill == styled[b].rightFill &&
            styled[a].stroke == styled[b].stroke;
    };

    // Pieces of one input curve were cut apart at crossings. Where the other
    // curve at a cut has since dropped out, the cut is a vertex with nothing
    // else meeting there, and the two pieces run on along the same source
    // with the same styles: they are joined back into one stretch of the
    // original curve, so a merge does not leave the artwork chopped up.
    // The pair an outgoing half-edge belongs to, as its forward half.
    const auto pairOf = [&](int e) -> int {
        if (e < 0 || e >= static_cast<int>(count))
            return -1;
        return halves[e].forward ? e : halves[e].twin;
    };
    // The one other drawn edge leaving a vertex besides the given one, or -1
    // when there is not exactly one: edges that dropped out (same fill both
    // sides, no stroke) are not there any more and do not break a run.
    const auto onlyOtherDrawn = [&](const MapVertex& vertex, int except) -> int {
        int other = -1;
        for (const int e : vertex.outgoing)
        {
            if (e == except)
                continue;
            const int pair = pairOf(e);
            if (pair < 0 || !styled[pair].drawn)
                continue;
            if (other != -1)
                return -1;
            other = e;
        }
        return other;
    };

    // Whether the forward half b carries straight on from the forward half
    // a: the next stretch of the same input curve, or, for two straight
    // pieces, collinear and heading the same way. Lines are joined across
    // sources because an earlier operation may have already cut one line
    // into separate edges, which come back here as separate inputs.
    const auto continues = [&](int a, int b) {
        const HalfEdge& first = halves[a];
        const HalfEdge& second = halves[b];
        if (!first.forward || !second.forward || !sameStyle(a, b))
            return false;
        if (first.source >= 0 && first.source == second.source)
            return std::fabs(second.t0 - first.t1) <= 1.0e-9;
        if (!first.curve.isLine() || !second.curve.isLine())
            return false;
        const Point u(first.curve.end().x - first.curve.start().x,
            first.curve.end().y - first.curve.start().y);
        const Point v(second.curve.end().x - second.curve.start().x,
            second.curve.end().y - second.curve.start().y);
        const double lengths = std::hypot(u.x, u.y) * std::hypot(v.x, v.y);
        if (lengths <= 0.0)
            return false;
        const double cross = u.x * v.y - u.y * v.x;
        const double dot = u.x * v.x + u.y * v.y;
        return dot > 0.0 && std::fabs(cross) <= 1.0e-6 * lengths;
    };

    const auto nextInRun = [&](int h) -> int {
        const HalfEdge& half = halves[h];
        if (half.to < 0 || half.to >= static_cast<int>(vertices.size()))
            return -1;
        const int k = onlyOtherDrawn(vertices[half.to], half.twin);
        if (k < 0 || !continues(h, k))
            return -1;
        return k;
    };
    const auto previousInRun = [&](int h) -> int {
        const HalfEdge& half = halves[h];
        if (half.from < 0 || half.from >= static_cast<int>(vertices.size()))
            return -1;
        const int j = onlyOtherDrawn(vertices[half.from], h);
        if (j < 0)
            return -1;
        // The piece before arrives here, so it is the twin of what leaves.
        const int p = halves[j].twin;
        if (p < 0 || p >= static_cast<int>(count) || !continues(p, h))
            return -1;
        return p;
    };

    std::vector<bool> used(count, false);
    for (size_t i = 0; i < count; ++i)
    {
        if (!halves[i].forward || used[i] || !styled[i].drawn)
            continue;

        // Back to the head of the run this piece belongs to.
        int head = static_cast<int>(i);
        for (size_t guard = 0; guard < count; ++guard)
        {
            const int before = previousInRun(head);
            if (before < 0 || used[before] || before == static_cast<int>(i))
                break;
            head = before;
        }

        std::vector<int> run;
        for (int current = head; current >= 0 && !used[current];)
        {
            used[current] = true;
            run.push_back(current);
            current = nextInRun(current);
        }

        const HalfEdge& first = halves[run.front()];
        const HalfEdge& last = halves[run.back()];
        const Styled& style = styled[run.front()];

        Curve curve = first.curve;
        if (run.size() > 1)
        {
            if (first.source == last.source && first.source >= 0 &&
                first.source < static_cast<int>(sources.size()) &&
                !sources[first.source].curve.isLine())
            {
                // The stretch of the original between the run's ends, pinned
                // to the vertices the pieces were welded to.
                const Curve whole = sources[first.source].curve.subcurve(first.t0, last.t1);
                curve = Curve::cubic(first.curve.start(), whole.controlPoint(1),
                    whole.controlPoint(2), last.curve.end());
            }
            else
            {
                // A run of collinear lines is one line.
                curve = Curve::line(first.curve.start(), last.curve.end());
            }
        }

        Edge* edge = new Edge(&shape);
        edge->fillStyle1 = style.leftFill;
        edge->fillStyle0 = style.rightFill;
        edge->strokeStyle = style.stroke;

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
                {curve.controlPoint(1), curve.controlPoint(2), curve.end()},
                path));
        }

        edge->paths.push_back(path);
        shape.edges.push_back(edge);
    }
}

} // namespace fla
