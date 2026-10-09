#include "intersect.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

/// How deep the subdivision may go. Each level halves both parameter ranges, so
/// this is far more than enough to reach the tolerances that matter here.
constexpr int kMaxDepth = 34;

constexpr double kEpsilon = 1.0e-12;

struct Range
{
    double from = 0.0;
    double to = 1.0;

    double middle() const { return (from + to) * 0.5; }
    double span() const { return to - from; }
};

bool boxesOverlap(const Curve& a, const Curve& b, double tolerance)
{
    double al, at, ar, ab;
    double bl, bt, br, bb;
    a.controlBounds(al, at, ar, ab);
    b.controlBounds(bl, bt, br, bb);

    // Grown by the tolerance so a touch counts as a hit rather than being lost
    // to rounding.
    return ar >= bl - tolerance && br >= al - tolerance &&
           ab >= bt - tolerance && bb >= at - tolerance;
}

/// Solves the crossing of two straight segments.
///
/// Returns false for parallel pairs, including collinear ones: an overlap along
/// a stretch is not a crossing, and the caller reports its ends separately.
bool intersectSegments(const Point& p0, const Point& p1, const Point& q0, const Point& q1,
    double& t, double& u)
{
    const double rx = p1.x - p0.x;
    const double ry = p1.y - p0.y;
    const double sx = q1.x - q0.x;
    const double sy = q1.y - q0.y;

    // Parallel when the cross product is negligible against the segments'
    // lengths, not against an absolute figure: a pair of long collinear
    // lines turned through an angle still has to count as parallel.
    const double denominator = rx * sy - ry * sx;
    const double scale = std::hypot(rx, ry) * std::hypot(sx, sy);
    if (std::fabs(denominator) <= std::max(kEpsilon, scale * 1.0e-10))
        return false;

    const double dx = q0.x - p0.x;
    const double dy = q0.y - p0.y;

    t = (dx * sy - dy * sx) / denominator;
    u = (dx * ry - dy * rx) / denominator;

    return true;
}

void addHit(std::vector<CurveIntersection>& hits, const Curve& a,
    double t1, double t2, double tolerance)
{
    t1 = std::max(0.0, std::min(1.0, t1));
    t2 = std::max(0.0, std::min(1.0, t2));

    const Point position = a.pointAt(t1);

    // Subdivision reaches the same crossing down several branches, so the same
    // point arrives more than once.
    for (const CurveIntersection& existing : hits)
    {
        if (std::hypot(existing.position.x - position.x,
                       existing.position.y - position.y) <= tolerance)
        {
            return;
        }
    }

    CurveIntersection hit;
    hit.t1 = t1;
    hit.t2 = t2;
    hit.position = position;
    hits.push_back(hit);
}

/// Maps a parameter found on a subdivided piece back to the whole curve.
double toWhole(const Range& range, double local)
{
    return range.from + local * range.span();
}

void searchMapped(const Curve& wholeA, const Curve& wholeB,
    const Curve& a, const Curve& b, Range ra, Range rb,
    double tolerance, int depth, std::vector<CurveIntersection>& hits)
{
    if (!boxesOverlap(a, b, tolerance))
        return;

    // A pair of real curves crosses at most nine times; far more means the
    // subdivision is chasing noise along an overlap, and the ends of that
    // are reported by the caller anyway.
    if (hits.size() > 1024)
        return;

    const bool aStraight = a.flatness() <= tolerance;
    const bool bStraight = b.flatness() <= tolerance;

    if ((aStraight && bStraight) || depth >= kMaxDepth)
    {
        double t = 0.0;
        double u = 0.0;
        if (!intersectSegments(a.start(), a.end(), b.start(), b.end(), t, u))
            return;

        const double slack = 1.0e-6;
        if (t < -slack || t > 1.0 + slack || u < -slack || u > 1.0 + slack)
            return;

        addHit(hits, wholeA, toWhole(ra, t), toWhole(rb, u), tolerance);
        return;
    }

    Curve firstHalf;
    Curve secondHalf;

    if (!aStraight && (bStraight || ra.span() >= rb.span()))
    {
        a.split(0.5, firstHalf, secondHalf);
        const double middle = ra.middle();
        searchMapped(wholeA, wholeB, firstHalf, b, Range{ra.from, middle}, rb,
            tolerance, depth + 1, hits);
        searchMapped(wholeA, wholeB, secondHalf, b, Range{middle, ra.to}, rb,
            tolerance, depth + 1, hits);
        return;
    }

    b.split(0.5, firstHalf, secondHalf);
    const double middle = rb.middle();
    searchMapped(wholeA, wholeB, a, firstHalf, ra, Range{rb.from, middle},
        tolerance, depth + 1, hits);
    searchMapped(wholeA, wholeB, a, secondHalf, ra, Range{middle, rb.to},
        tolerance, depth + 1, hits);
}

} // namespace

std::vector<CurveIntersection> intersectCurves(const Curve& a, const Curve& b,
    double tolerance)
{
    std::vector<CurveIntersection> hits;

    tolerance = std::max(tolerance, 1.0e-9);

    if (a.isLine() && b.isLine())
    {
        // The common case, and the only one that can be answered exactly.
        double t = 0.0;
        double u = 0.0;
        if (intersectSegments(a.start(), a.end(), b.start(), b.end(), t, u))
        {
            if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0)
                addHit(hits, a, t, u, tolerance);
        }
        return hits;
    }

    searchMapped(a, b, a, b, Range{}, Range{}, tolerance, 0, hits);

    std::sort(hits.begin(), hits.end(),
        [](const CurveIntersection& left, const CurveIntersection& right) {
            return left.t1 < right.t1;
        });

    return hits;
}

std::vector<CurveIntersection> selfIntersections(const Curve& curve, double tolerance)
{
    std::vector<CurveIntersection> hits;

    // A straight piece cannot cross itself.
    if (curve.isLine())
        return hits;

    tolerance = std::max(tolerance, 1.0e-9);

    // Cut the curve where it turns back in x or y. Each piece is then
    // monotone in both and cannot cross itself, so every crossing lies
    // between two different pieces, which the pair search finds. Halving at
    // the midpoint alone missed a loop lying wholly in one half.
    std::vector<double> splits = {0.0};
    for (const double t : curve.extremaParameters())
        splits.push_back(t);
    splits.push_back(1.0);

    const size_t pieceCount = splits.size() - 1;
    std::vector<Curve> pieces;
    for (size_t i = 0; i < pieceCount; ++i)
        pieces.push_back(curve.subcurve(splits[i], splits[i + 1]));

    for (size_t i = 0; i < pieceCount; ++i)
    {
        for (size_t j = i + 1; j < pieceCount; ++j)
        {
            std::vector<CurveIntersection> found;
            searchMapped(pieces[i], pieces[j], pieces[i], pieces[j], Range{}, Range{},
                tolerance, 0, found);

            for (const CurveIntersection& hit : found)
            {
                const double t1 = splits[i] + hit.t1 * (splits[i + 1] - splits[i]);
                const double t2 = splits[j] + hit.t2 * (splits[j + 1] - splits[j]);

                // Neighbouring pieces share their join, and the curve's two
                // ends may coincide: neither is a crossing.
                if (j == i + 1)
                {
                    const Point join = curve.pointAt(splits[j]);
                    if (std::hypot(hit.position.x - join.x, hit.position.y - join.y) <= tolerance)
                        continue;
                }
                if (i == 0 && j == pieceCount - 1)
                {
                    const Point& start = curve.start();
                    if (std::hypot(hit.position.x - start.x, hit.position.y - start.y) <= tolerance)
                        continue;
                }

                bool known = false;
                for (const CurveIntersection& existing : hits)
                {
                    if (std::hypot(existing.position.x - hit.position.x,
                        existing.position.y - hit.position.y) <= tolerance)
                    {
                        known = true;
                        break;
                    }
                }
                if (known)
                    continue;

                CurveIntersection mapped;
                mapped.t1 = t1;
                mapped.t2 = t2;
                mapped.position = hit.position;
                hits.push_back(mapped);
            }
        }
    }

    std::sort(hits.begin(), hits.end(),
        [](const CurveIntersection& left, const CurveIntersection& right) {
            return left.t1 < right.t1;
        });

    return hits;
}

} // namespace fla
