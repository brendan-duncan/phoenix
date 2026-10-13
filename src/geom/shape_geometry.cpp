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
        for (const int half : map.faceHalfEdges(index))
        {
            const HalfEdge& edge = map.halfEdges()[half];
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
    for (const int half : map.faceHalfEdges(face))
    {
        const HalfEdge& edge = map.halfEdges()[half];
        if (edge.source < 0)
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

namespace {

/// The unit direction a curve leaves its start in, or arrives at its end in.
/// A handle pulled back onto its anchor leaves no tangent there, so the next
/// control point along gives the direction instead.
bool endDirection(const Curve& curve, bool atEnd, Point& direction)
{
    const Point candidates[3] = {
        atEnd ? curve.tangentAt(1.0) : curve.tangentAt(0.0),
        atEnd ? Point(curve.end().x - curve.controlPoint(1).x, curve.end().y - curve.controlPoint(1).y)
              : Point(curve.controlPoint(2).x - curve.start().x, curve.controlPoint(2).y - curve.start().y),
        Point(curve.end().x - curve.start().x, curve.end().y - curve.start().y)
    };
    for (const Point& candidate : candidates)
    {
        const double length = std::hypot(candidate.x, candidate.y);
        if (length > 1.0e-9)
        {
            direction = Point(candidate.x / length, candidate.y / length);
            return true;
        }
    }
    return false;
}

} // namespace

bool joinCurves(const std::vector<Curve>& pieces, double tolerance, Curve& joined)
{
    if (pieces.empty())
        return false;
    if (pieces.size() == 1)
    {
        joined = pieces.front();
        return true;
    }

    const Point p0 = pieces.front().start();
    const Point p3 = pieces.back().end();
    Point d0;
    Point d3;
    if (!endDirection(pieces.front(), false, d0) || !endDirection(pieces.back(), true, d3))
        return false;

    // Points along the pieces, parameterized by how far along the run they
    // are; the fit is asked to pass near each at its parameter.
    constexpr int kSamplesPerPiece = 16;
    std::vector<Point> samples;
    samples.push_back(p0);
    for (const Curve& piece : pieces)
    {
        for (int i = 1; i <= kSamplesPerPiece; ++i)
            samples.push_back(piece.pointAt(static_cast<double>(i) / kSamplesPerPiece));
    }

    // Where each piece starts on the joined curve. Cutting a cubic at s
    // scales the speed of the piece before by s and the one after by 1 - s,
    // so the ratio of the speeds either side of a joint gives the ratio of
    // the pieces' spans. For pieces truly cut from one cubic this is the
    // exact parameterization, and the first fit is the original; when a
    // speed is zero, how far along the run each point is will do instead.
    std::vector<double> parameters(samples.size(), 0.0);
    std::vector<double> spans(pieces.size(), 1.0);
    bool bySpeed = true;
    for (size_t i = 1; i < pieces.size() && bySpeed; ++i)
    {
        const Point before = pieces[i - 1].tangentAt(1.0);
        const Point after = pieces[i].tangentAt(0.0);
        const double arriving = std::hypot(before.x, before.y);
        const double leaving = std::hypot(after.x, after.y);
        bySpeed = arriving > 1.0e-9 && leaving > 1.0e-9;
        if (bySpeed)
            spans[i] = spans[i - 1] * leaving / arriving;
    }
    if (bySpeed)
    {
        double total = 0.0;
        for (const double span : spans)
            total += span;
        double start = 0.0;
        size_t k = 1;
        for (size_t i = 0; i < pieces.size(); ++i)
        {
            const double span = spans[i] / total;
            for (int j = 1; j <= kSamplesPerPiece; ++j, ++k)
                parameters[k] = start + span * j / kSamplesPerPiece;
            start += span;
        }
    }
    else
    {
        for (size_t k = 1; k < samples.size(); ++k)
        {
            parameters[k] = parameters[k - 1] +
                std::hypot(samples[k].x - samples[k - 1].x, samples[k].y - samples[k - 1].y);
        }
        const double total = parameters.back();
        if (total <= 1.0e-9)
            return false;
        for (double& u : parameters)
            u /= total;
    }
    parameters.back() = 1.0;

    // Each pass fits the handle lengths for the current parameters, then moves
    // each parameter to the point of the fit nearest its sample (a Newton
    // step), which is how Schneider's fit converges. Passes go on past the
    // first fit within tolerance, since a few more cost little and bring the
    // fit closer still; the closest is kept. A fit that has stopped getting
    // better is given up on.
    bool found = false;
    double closest = tolerance;
    double previous = 1.0e300;
    const double chord = std::hypot(p3.x - p0.x, p3.y - p0.y);
    for (int pass = 0; pass < 32; ++pass)
    {
        double c11 = 0.0, c12 = 0.0, c22 = 0.0, x1 = 0.0, x2 = 0.0;
        for (size_t k = 0; k < samples.size(); ++k)
        {
            const double u = parameters[k];
            const double v = 1.0 - u;
            const double b0 = v * v * v, b1 = 3.0 * u * v * v, b2 = 3.0 * u * u * v, b3 = u * u * u;
            const Point a1(b1 * d0.x, b1 * d0.y);
            const Point a2(-b2 * d3.x, -b2 * d3.y);
            const Point r(samples[k].x - (b0 + b1) * p0.x - (b2 + b3) * p3.x,
                samples[k].y - (b0 + b1) * p0.y - (b2 + b3) * p3.y);
            c11 += a1.x * a1.x + a1.y * a1.y;
            c12 += a1.x * a2.x + a1.y * a2.y;
            c22 += a2.x * a2.x + a2.y * a2.y;
            x1 += a1.x * r.x + a1.y * r.y;
            x2 += a2.x * r.x + a2.y * r.y;
        }
        const double determinant = c11 * c22 - c12 * c12;
        if (std::fabs(determinant) <= 1.0e-12 * std::max(1.0, c11 * c22))
            return false;
        double reach0 = (x1 * c22 - c12 * x2) / determinant;
        double reach3 = (c11 * x2 - c12 * x1) / determinant;
        // A handle pointing backwards is not the curve the pieces draw. Early
        // on that can be the parameters' fault, so the pass carries on with
        // a plain third of the chord to improve them, but it is not a fit.
        const bool usable = reach0 > 0.0 && reach3 > 0.0;
        if (!usable)
        {
            reach0 = chord / 3.0;
            reach3 = chord / 3.0;
        }

        const Curve fit = Curve::cubic(p0, Point(p0.x + reach0 * d0.x, p0.y + reach0 * d0.y),
            Point(p3.x - reach3 * d3.x, p3.y - reach3 * d3.y), p3);

        double worst = 0.0;
        for (size_t k = 0; k < samples.size(); ++k)
        {
            const Point at = fit.pointAt(parameters[k]);
            worst = std::max(worst, std::hypot(at.x - samples[k].x, at.y - samples[k].y));
        }
        if (usable && worst <= closest)
        {
            joined = fit;
            found = true;
            closest = worst;
            if (worst <= tolerance * 1.0e-4)
                break;
        }
        if (usable && pass >= 2 && worst > previous * 0.95 && (found || worst > tolerance * 4.0))
            break;
        if (usable)
            previous = worst;

        for (size_t k = 1; k + 1 < samples.size(); ++k)
        {
            const double u = parameters[k];
            const Point at = fit.pointAt(u);
            const Point first = fit.tangentAt(u);
            // The second derivative, from the control points' second differences.
            const double v = 1.0 - u;
            const Point& q0 = fit.controlPoint(0);
            const Point& q1 = fit.controlPoint(1);
            const Point& q2 = fit.controlPoint(2);
            const Point& q3 = fit.controlPoint(3);
            const Point second(6.0 * (v * (q2.x - 2.0 * q1.x + q0.x) + u * (q3.x - 2.0 * q2.x + q1.x)),
                6.0 * (v * (q2.y - 2.0 * q1.y + q0.y) + u * (q3.y - 2.0 * q2.y + q1.y)));
            const Point off(at.x - samples[k].x, at.y - samples[k].y);
            const double numerator = off.x * first.x + off.y * first.y;
            const double denominator = first.x * first.x + first.y * first.y +
                off.x * second.x + off.y * second.y;
            if (std::fabs(denominator) > 1.0e-12)
                parameters[k] = std::min(1.0, std::max(0.0, u - numerator / denominator));
        }
    }

    return found;
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

    // Whether the forward half b carries on smoothly from a without being
    // the next stretch of the same input: a curve an earlier merge cut into
    // separate edges comes back as several sources. Such a joint is only a
    // candidate; the pieces either side are joined by refitting them as one
    // cubic, which has to stay within tolerance of them. Segments of one
    // edge's path meet where whoever drew it put an anchor, and stay apart.
    const auto joinsSmoothly = [&](int a, int b) {
        const HalfEdge& first = halves[a];
        const HalfEdge& second = halves[b];
        if (!first.forward || !second.forward || !sameStyle(a, b))
            return false;
        if (first.curve.isLine() && second.curve.isLine())
            return false;
        if (first.source < 0 || second.source < 0 ||
            first.source >= static_cast<int>(sources.size()) ||
            second.source >= static_cast<int>(sources.size()) ||
            sources[first.source].edgeIndex == sources[second.source].edgeIndex)
        {
            return false;
        }
        Point arriving;
        Point leaving;
        if (!endDirection(first.curve, true, arriving) || !endDirection(second.curve, false, leaving))
            return false;
        const double cross = arriving.x * leaving.y - arriving.y * leaving.x;
        const double dot = arriving.x * leaving.x + arriving.y * leaving.y;
        // About three degrees: a curve cut in two meets itself exactly, give
        // or take welding moving a short piece's end by half a twip.
        return dot > 0.0 && std::fabs(cross) <= 0.05;
    };
    const auto linked = [&](int a, int b) {
        return continues(a, b) || joinsSmoothly(a, b);
    };

    const auto nextInRun = [&](int h) -> int {
        const HalfEdge& half = halves[h];
        if (half.to < 0 || half.to >= static_cast<int>(vertices.size()))
            return -1;
        const int k = onlyOtherDrawn(vertices[half.to], half.twin);
        if (k < 0 || !linked(h, k))
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
        if (p < 0 || p >= static_cast<int>(count) || !linked(p, h))
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

        const Styled& style = styled[run.front()];

        // The run in stretches that are exactly one curve: pieces of one
        // source, or collinear lines. Between stretches the run only carries
        // on smoothly, and the stretches are joined by refitting.
        std::vector<Curve> stretches;
        for (size_t begin = 0; begin < run.size();)
        {
            size_t end = begin + 1;
            while (end < run.size() && continues(run[end - 1], run[end]))
                ++end;

            const HalfEdge& first = halves[run[begin]];
            const HalfEdge& last = halves[run[end - 1]];
            Curve curve = first.curve;
            if (end - begin > 1)
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
            stretches.push_back(curve);
            begin = end;
        }

        // As many stretches at a time as one cubic follows to within half a
        // twip, which is finer than the file stores; where it cannot, the
        // stretches stay separate edges, exactly as they were.
        std::vector<Curve> curves;
        std::vector<size_t> firstStretch;
        for (size_t begin = 0; begin < stretches.size();)
        {
            Curve joined = stretches[begin];
            size_t end = begin + 1;
            while (end < stretches.size())
            {
                Curve attempt;
                const std::vector<Curve> span(stretches.begin() + begin, stretches.begin() + end + 1);
                if (!joinCurves(span, PlanarMap::kTwip * 0.5, attempt))
                    break;
                joined = attempt;
                ++end;
            }
            curves.push_back(joined);
            firstStretch.push_back(begin);
            begin = end;
        }

        // A run closed on itself starts wherever the search happened to
        // begin, which may be in the middle of a curve that was cut: the
        // last and first curves get a chance to join across that point.
        const bool closedRun = run.size() > 1 &&
            halves[run.back()].to == halves[run.front()].from && linked(run.back(), run.front());
        if (closedRun && curves.size() > 1)
        {
            std::vector<Curve> span(stretches.begin() + firstStretch.back(), stretches.end());
            const size_t firstEnd = firstStretch.size() > 1 ? firstStretch[1] : stretches.size();
            span.insert(span.end(), stretches.begin(), stretches.begin() + firstEnd);
            Curve attempt;
            if (joinCurves(span, PlanarMap::kTwip * 0.5, attempt))
            {
                curves.front() = attempt;
                curves.pop_back();
            }
        }

        for (const Curve& curve : curves)
        {
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
}

} // namespace fla
