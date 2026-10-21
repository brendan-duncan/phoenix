#include "shape_tween.h"

#include "feather.h"
#include "shape_merge.h"
#include "transform_blend.h"

#include "../data/edge.h"
#include "../data/frame.h"
#include "../data/linear_gradient.h"
#include "../data/morph_shape.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../geom/curve.h"
#include "../geom/shape_geometry.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <cstdlib>

namespace fla {

namespace {

double mix(double a, double b, double t)
{
    return a + (b - a) * t;
}

uint8_t mixByte(uint8_t a, uint8_t b, double t)
{
    return static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(mix(a, b, t)))));
}

Point apply(const Transform& m, const Point& p)
{
    return Point(m.m11 * p.x + m.m21 * p.y + m.tx, m.m12 * p.x + m.m22 * p.y + m.ty);
}

template <typename Entry>
void blendEntries(std::vector<Entry>& entries, const std::vector<Entry>& others, double t)
{
    for (size_t i = 0; i < entries.size() && i < others.size(); ++i)
    {
        entries[i].ratio = mix(entries[i].ratio, others[i].ratio, t);
        for (int c = 0; c < 4; ++c)
            entries[i].color[c] = mixByte(entries[i].color[c], others[i].color[c], t);
    }
}

} // namespace

namespace {

FillStyle* blendFillContents(const FillStyle* a, const FillStyle* b, double t, DOMElement* parent)
{
    switch (a->type())
    {
    case FillStyle::Type::SolidColor:
    {
        const SolidColor* from = static_cast<const SolidColor*>(a);
        const SolidColor* to = static_cast<const SolidColor*>(b);
        SolidColor* result = new SolidColor(parent);
        for (int c = 0; c < 4; ++c)
            result->color[c] = mixByte(from->color[c], to->color[c], t);
        result->extraAttributes = from->extraAttributes;
        return result;
    }

    case FillStyle::Type::LinearGradient:
    {
        const LinearGradient* from = static_cast<const LinearGradient*>(a);
        const LinearGradient* to = static_cast<const LinearGradient*>(b);
        LinearGradient* result = static_cast<LinearGradient*>(cloneFill(a, parent));
        if (from->entries.size() == to->entries.size())
        {
            blendEntries(result->entries, to->entries, t);
            result->transform = blendTransforms(from->transform, to->transform, t);
        }
        return result;
    }

    case FillStyle::Type::RadialGradient:
    {
        const RadialGradient* from = static_cast<const RadialGradient*>(a);
        const RadialGradient* to = static_cast<const RadialGradient*>(b);
        RadialGradient* result = static_cast<RadialGradient*>(cloneFill(a, parent));
        if (from->entries.size() == to->entries.size())
        {
            blendEntries(result->entries, to->entries, t);
            result->transform = blendTransforms(from->transform, to->transform, t);
            result->focalPointRatio = mix(from->focalPointRatio, to->focalPointRatio, t);
        }
        return result;
    }

    case FillStyle::Type::BitmapFill:
        break;
    }
    return cloneFill(a, parent);
}

} // namespace

FillStyle* blendFills(const FillStyle* a, const FillStyle* b, double t, DOMElement* parent)
{
    if (!a)
        return b ? cloneFill(b, parent) : nullptr;
    if (!b || b->type() != a->type())
        return cloneFill(a, parent);
    FillStyle* result = blendFillContents(a, b, t, parent);
    // A soft edge softens or sharpens through the tween.
    if (result)
        feather::set(*result, mix(feather::of(*a), feather::of(*b), t));
    return result;
}

StrokeStyle* blendStrokes(const StrokeStyle* a, const StrokeStyle* b, double t, DOMElement* parent)
{
    if (!a)
        return b ? cloneStroke(b, parent) : nullptr;
    StrokeStyle* result = cloneStroke(a, parent);
    if (!result || !b)
        return result;

    result->weight = mix(a->weight, b->weight, t);
    feather::set(*result, mix(feather::of(*a), feather::of(*b), t));
    if (a->fill)
    {
        delete result->fill;
        result->fill = blendFills(a->fill, b->fill, t, result);
    }
    return result;
}

Shape* morphedShape(const MorphShape& morph, const Shape& from, const Shape& to, double t,
    const Transform& fromToDocument, const Transform& toToDocument,
    const Transform& documentToLocal)
{
    Shape* shape = new Shape(nullptr);

    // Styles by index: the first keyframe's table, each blended with the
    // last keyframe's style of the same index.
    for (const auto& entry : from.fillsMap)
    {
        FillStyle* fill = blendFills(entry.second, to.getFillStyleByIndex(entry.first), t, shape);
        if (!fill)
            continue;
        shape->fills.push_back(fill);
        shape->fillsMap[entry.first] = fill;
    }
    for (const auto& entry : from.strokesMap)
    {
        StrokeStyle* stroke = blendStrokes(entry.second, to.getStrokeStyleByIndex(entry.first), t, shape);
        if (!stroke)
            continue;
        shape->strokes.push_back(stroke);
        shape->strokesMap[entry.first] = stroke;
    }

    // Some files leave every segment unstyled; those draw with the shape's
    // first fill and stroke, which is what they have.
    bool anyStyled = false;
    for (const MorphSegment* segment : morph.segments)
    {
        if (segment && (segment->fillIndex1 || segment->fillIndex2 ||
                        segment->strokeIndex1 || segment->strokeIndex2))
        {
            anyStyled = true;
        }
    }
    const int firstFill = shape->fillsMap.empty() ? -1 : shape->fillsMap.begin()->first;
    const int firstStroke = shape->strokesMap.empty() ? -1 : shape->strokesMap.begin()->first;

    const auto at = [&](const Point& a, const Point& b) {
        const Point pa = apply(fromToDocument, a);
        const Point pb = apply(toToDocument, b);
        return apply(documentToLocal, Point(mix(pa.x, pb.x, t), mix(pa.y, pb.y, t)));
    };

    bool first = true;
    Rect bounds;
    const auto include = [&](const Point& p) {
        if (first)
        {
            bounds = Rect(p, p);
            first = false;
            return;
        }
        bounds.topLeft.x = std::min(bounds.topLeft.x, p.x);
        bounds.topLeft.y = std::min(bounds.topLeft.y, p.y);
        bounds.bottomRight.x = std::max(bounds.bottomRight.x, p.x);
        bounds.bottomRight.y = std::max(bounds.bottomRight.y, p.y);
    };

    for (const MorphSegment* segment : morph.segments)
    {
        if (!segment)
            continue;

        Edge* edge = new Edge(shape);
        if (anyStyled)
        {
            // Index 1 is the fill on the right of the segment's direction,
            // index 2 the one on the left, as the file's own edges number
            // their sides; zero is none.
            edge->fillStyle0 = segment->fillIndex1 > 0 ? segment->fillIndex1 : -1;
            edge->fillStyle1 = segment->fillIndex2 > 0 ? segment->fillIndex2 : -1;
            const int stroke = segment->strokeIndex1 > 0 ? segment->strokeIndex1 : segment->strokeIndex2;
            edge->strokeStyle = stroke > 0 ? stroke : -1;
        }
        else
        {
            edge->fillStyle1 = firstFill;
            edge->strokeStyle = firstStroke;
        }

        Path* path = new Path(edge);
        const Point start = at(segment->startPointA, segment->startPointB);
        include(start);
        path->segments.push_back(new PathSegment(PathSegment::Command::Move, {start}, path));
        for (const MorphCurves* curve : segment->curves)
        {
            if (!curve)
                continue;
            const Point control = at(curve->controlPointA, curve->controlPointB);
            const Point anchor = at(curve->anchorPointA, curve->anchorPointB);
            include(control);
            include(anchor);
            path->segments.push_back(new PathSegment(PathSegment::Command::Quad, {control, anchor}, path));
        }
        edge->paths.push_back(path);
        shape->edges.push_back(edge);
    }

    shape->localBounds = bounds;
    shape->bounds = bounds;
    return shape;
}

// ---------------------------------------------------------------------------
// Building a morph

namespace {

Point plus(const Point& a, const Point& b)
{
    return Point(a.x + b.x, a.y + b.y);
}

Point minus(const Point& a, const Point& b)
{
    return Point(a.x - b.x, a.y - b.y);
}

Point times(const Point& a, double s)
{
    return Point(a.x * s, a.y * s);
}

double distance(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// Whether two ends meet: within a twip.
bool meets(const Point& a, const Point& b)
{
    return distance(a, b) < 0.05;
}

Curve reversed(const Curve& curve)
{
    if (curve.isLine())
        return Curve::line(curve.end(), curve.start());
    return Curve::cubic(curve.end(), curve.controlPoint(2), curve.controlPoint(1), curve.start());
}

/// One outline: pieces end to end, with the style it carries.
struct Outline
{
    std::vector<Curve> pieces;
    bool closed = true;
    int fill = 0;
    int stroke = 0;
};

/// An outline measured along its length, so points can be found by the
/// fraction of the way round they are.
class Measured
{
public:
    static constexpr int kSteps = 16;

    explicit Measured(const Outline& outline)
        : _outline(outline)
    {
        for (const Curve& piece : outline.pieces)
        {
            std::vector<double> table(kSteps + 1, 0.0);
            Point previous = piece.start();
            for (int i = 1; i <= kSteps; ++i)
            {
                const Point p = piece.pointAt(static_cast<double>(i) / kSteps);
                table[i] = table[i - 1] + distance(previous, p);
                previous = p;
            }
            _starts.push_back(_total);
            _total += table[kSteps];
            _tables.push_back(std::move(table));
        }
    }

    double total() const { return _total; }

    bool isPoint() const { return _total < 1.0e-9; }

    /// Where each piece starts, as a fraction of the way round.
    std::vector<double> anchors() const
    {
        std::vector<double> at;
        for (double start : _starts)
            at.push_back(isPoint() ? 0.0 : start / _total);
        return at;
    }

    /// The fraction of the way round of the point nearest \a p.
    double nearest(const Point& p) const
    {
        if (isPoint() || _outline.pieces.empty())
            return 0.0;
        constexpr int kCoarse = 256;
        double best = 0.0;
        double bestDistance = -1.0;
        for (int i = 0; i <= kCoarse; ++i)
        {
            const double u = static_cast<double>(i) / kCoarse;
            const double d = distance(at(u), p);
            if (bestDistance < 0.0 || d < bestDistance)
            {
                bestDistance = d;
                best = u;
            }
        }
        // Closer, a step either side at a time.
        double step = 1.0 / kCoarse;
        for (int round = 0; round < 20; ++round)
        {
            step *= 0.5;
            for (double u : {best - step, best + step})
            {
                const double d = distance(at(u), p);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = u;
                }
            }
        }
        return _outline.closed ? best - std::floor(best) : std::max(0.0, std::min(1.0, best));
    }

    /// The point a fraction \a u of the way round, wrapping for a closed one.
    Point at(double u) const
    {
        if (_outline.pieces.empty())
            return Point();
        size_t piece = 0;
        double t = 0.0;
        locate(wrap(u) * _total, piece, t);
        return _outline.pieces[piece].pointAt(t);
    }

    /// The part between fractions \a from and \a to (\a to beyond \a from,
    /// past 1 for a closed outline's last part), which lies in one piece.
    Curve part(double from, double to) const
    {
        if (isPoint())
        {
            const Point p = _outline.pieces.empty() ? Point() : _outline.pieces.front().start();
            return Curve::line(p, p);
        }
        const double middle = wrap(0.5 * (from + to)) * _total;
        const double half = 0.5 * (to - from) * _total;
        size_t piece = 0;
        double t = 0.0;
        locate(middle, piece, t);
        const double begin = parameter(piece, middle - half - _starts[piece]);
        const double end = parameter(piece, middle + half - _starts[piece]);
        return _outline.pieces[piece].subcurve(begin, end);
    }

private:
    double wrap(double u) const
    {
        if (!_outline.closed)
            return std::max(0.0, std::min(1.0, u));
        u -= std::floor(u);
        return u;
    }

    /// The piece \a length along the outline and the parameter there.
    void locate(double length, size_t& piece, double& t) const
    {
        // The last piece starting at or before the length; the starts only
        // grow, so it is found by halving rather than walking from the first.
        const auto after = std::upper_bound(_starts.begin() + 1, _starts.end(), length);
        piece = static_cast<size_t>(after - _starts.begin()) - 1;
        t = parameter(piece, length - _starts[piece]);
    }

    /// The parameter \a length along one piece.
    double parameter(size_t piece, double length) const
    {
        const std::vector<double>& table = _tables[piece];
        if (length <= 0.0 || table[kSteps] <= 0.0)
            return 0.0;
        if (length >= table[kSteps])
            return 1.0;
        int i = 0;
        while (i + 1 < kSteps && table[i + 1] < length)
            ++i;
        const double span = table[i + 1] - table[i];
        const double fraction = span > 0.0 ? (length - table[i]) / span : 0.0;
        return (i + fraction) / kSteps;
    }

    const Outline& _outline;
    std::vector<std::vector<double>> _tables;
    std::vector<double> _starts;
    double _total = 0.0;
};

/// Points spread evenly round an outline.
std::vector<Point> samples(const Measured& measured, double offset, int count)
{
    std::vector<Point> points;
    for (int i = 0; i < count; ++i)
        points.push_back(measured.at(offset + static_cast<double>(i) / count));
    return points;
}

Point centroid(const std::vector<Point>& points)
{
    Point sum;
    for (const Point& p : points)
        sum = plus(sum, p);
    return points.empty() ? sum : times(sum, 1.0 / points.size());
}

/// The area an outline encloses, signed by the way it runs.
double signedArea(const Outline& outline)
{
    double area = 0.0;
    for (const Curve& piece : outline.pieces)
    {
        Point previous = piece.start();
        for (int i = 1; i <= 8; ++i)
        {
            const Point p = piece.pointAt(i / 8.0);
            area += previous.x * p.y - p.x * previous.y;
            previous = p;
        }
    }
    return 0.5 * area;
}

double length(const Outline& outline)
{
    return Measured(outline).total();
}

/// Pieces filed by the pixel one of their ends falls in, so the pieces with
/// an end meeting a point are found without trying every piece.
class EndIndex
{
public:
    void add(const Point& at, size_t piece) { _cells[cellOf(at)].push_back(piece); }

    /// Every piece not yet \a used filed within a pixel of \a at: all those
    /// that can meet it, and some that do not.
    void around(const Point& at, const std::vector<bool>& used, std::vector<size_t>& into)
    {
        const std::pair<long long, long long> cell = cellOf(at);
        for (long long dy = -1; dy <= 1; ++dy)
        {
            for (long long dx = -1; dx <= 1; ++dx)
            {
                const auto it = _cells.find({cell.first + dx, cell.second + dy});
                if (it == _cells.end())
                    continue;
                std::vector<size_t>& filed = it->second;
                filed.erase(std::remove_if(filed.begin(), filed.end(),
                    [&](size_t piece) { return used[piece]; }), filed.end());
                into.insert(into.end(), filed.begin(), filed.end());
            }
        }
    }

private:
    struct CellHash
    {
        size_t operator()(const std::pair<long long, long long>& cell) const
        {
            return std::hash<long long>()(cell.first) * 1000003u ^ std::hash<long long>()(cell.second);
        }
    };

    static long long pixelOf(double value)
    {
        // Kept well inside the integers, so the neighbors either side exist.
        const double clamped = std::max(-4.0e18, std::min(4.0e18, std::floor(value)));
        return clamped == clamped ? static_cast<long long>(clamped) : 0;
    }

    static std::pair<long long, long long> cellOf(const Point& at)
    {
        return {pixelOf(at.x), pixelOf(at.y)};
    }

    std::unordered_map<std::pair<long long, long long>, std::vector<size_t>, CellHash> _cells;
};

/// Chains directed pieces end to start into outlines.
std::vector<Outline> chainDirected(std::vector<Curve> pieces)
{
    std::vector<Outline> outlines;
    std::vector<bool> used(pieces.size(), false);
    EndIndex starts;
    for (size_t i = 0; i < pieces.size(); ++i)
        starts.add(pieces[i].start(), i);
    std::vector<size_t> candidates;
    for (size_t first = 0; first < pieces.size(); ++first)
    {
        if (used[first])
            continue;
        used[first] = true;
        Outline outline;
        outline.pieces.push_back(pieces[first]);
        while (!meets(outline.pieces.back().end(), outline.pieces.front().start()))
        {
            // The first piece, in order, that starts where this one ends.
            const Point end = outline.pieces.back().end();
            size_t next = pieces.size();
            candidates.clear();
            starts.around(end, used, candidates);
            for (const size_t i : candidates)
            {
                if (i < next && meets(pieces[i].start(), end))
                    next = i;
            }
            if (next == pieces.size())
                break;
            used[next] = true;
            outline.pieces.push_back(pieces[next]);
        }
        outline.closed = true;
        outlines.push_back(std::move(outline));
    }
    return outlines;
}

/// Chains pieces into runs, either way round.
std::vector<Outline> chainUndirected(std::vector<Curve> pieces)
{
    std::vector<Outline> outlines;
    std::vector<bool> used(pieces.size(), false);
    EndIndex ends;
    for (size_t i = 0; i < pieces.size(); ++i)
    {
        ends.add(pieces[i].start(), i);
        ends.add(pieces[i].end(), i);
    }
    std::vector<size_t> candidates;
    // The first piece, in order, with either end at \a at, turned to run on
    // from there.
    const auto take = [&](const Point& at, Curve& found) {
        candidates.clear();
        ends.around(at, used, candidates);
        size_t best = pieces.size();
        for (const size_t i : candidates)
        {
            if (i < best && (meets(pieces[i].start(), at) || meets(pieces[i].end(), at)))
                best = i;
        }
        if (best == pieces.size())
            return false;
        used[best] = true;
        found = meets(pieces[best].start(), at) ? pieces[best] : reversed(pieces[best]);
        return true;
    };
    for (size_t first = 0; first < pieces.size(); ++first)
    {
        if (used[first])
            continue;
        used[first] = true;
        std::vector<Curve> run{pieces[first]};
        Curve found;
        while (!meets(run.back().end(), run.front().start()) && take(run.back().end(), found))
            run.push_back(found);
        while (!meets(run.back().end(), run.front().start()) && take(run.front().start(), found))
            run.insert(run.begin(), reversed(found));
        Outline outline;
        outline.closed = run.size() > 1 && meets(run.back().end(), run.front().start());
        outline.pieces = std::move(run);
        outlines.push_back(std::move(outline));
    }
    return outlines;
}

/// A shape's fill outlines, each running with its fill on the left, and its
/// stroke runs.
void outlinesOf(const Shape& shape, std::vector<Outline>& fills, std::vector<Outline>& strokes)
{
    std::map<int, std::vector<Curve>> byFill;
    std::map<int, std::vector<Curve>> byStroke;
    for (const ShapeCurve& source : shapeCurves(shape))
    {
        // Not a number, or too large to measure, would break the sorting and
        // measuring that pairing outlines rests on.
        if (!(source.curve.controlPolygonLength() >= 1.0e-9 &&
                source.curve.controlPolygonLength() < 1.0e15))
            continue;
        // A seam with one fill both sides bounds nothing.
        if (source.fillStyle0 != source.fillStyle1)
        {
            if (source.fillStyle1 > 0)
                byFill[source.fillStyle1].push_back(source.curve);
            if (source.fillStyle0 > 0)
                byFill[source.fillStyle0].push_back(reversed(source.curve));
        }
        if (source.strokeStyle > 0)
            byStroke[source.strokeStyle].push_back(source.curve);
    }
    for (auto& entry : byFill)
    {
        for (Outline& outline : chainDirected(std::move(entry.second)))
        {
            outline.fill = entry.first;
            fills.push_back(std::move(outline));
        }
    }
    for (auto& entry : byStroke)
    {
        for (Outline& outline : chainUndirected(std::move(entry.second)))
        {
            outline.stroke = entry.first;
            strokes.push_back(std::move(outline));
        }
    }
}

/// An outline of as many pieces as \a like, all at one point.
Outline pointOutline(const Outline& like, const Point& at)
{
    Outline outline;
    outline.closed = like.closed;
    outline.fill = like.fill;
    outline.stroke = like.stroke;
    outline.pieces.push_back(Curve::line(at, at));
    return outline;
}

Point middleOf(const Outline& outline)
{
    return centroid(samples(Measured(outline), 0.0, 32));
}

/// The quadratic nearest a cubic piece.
void toQuadratic(const Curve& curve, Point& control)
{
    if (curve.isLine())
    {
        control = times(plus(curve.start(), curve.end()), 0.5);
        return;
    }
    const Point inner = times(plus(curve.controlPoint(1), curve.controlPoint(2)), 3.0);
    control = times(minus(inner, plus(curve.start(), curve.end())), 0.25);
}

/// A hint as fractions of the way round its two outlines.
struct Knot
{
    double a;
    double b;
};

/// One segment of the morph: \a a and \a b cut to match, as quadratics.
/// \a hints pin fractions of \a a to fractions of \a b.
MorphSegment* pairSegment(const Outline& a, const Outline& b, MorphShape* morph,
    std::vector<Knot> hints = {})
{
    const Measured ma(a);
    const Measured mb(b);

    double offsetA = 0.0;
    double offsetB = 0.0;
    bool flipB = false;
    Outline flippedB;
    const Measured* measuredB = &mb;

    // Where the fractions of A (from its start) fall on B: straight across
    // unless hints pin them, piece by piece between the hints.
    std::vector<Knot> knots;
    const bool hinted = !hints.empty() && !ma.isPoint() && !mb.isPoint() && a.closed == b.closed;
    if (hinted)
    {
        std::sort(hints.begin(), hints.end(), [](const Knot& l, const Knot& r) { return l.a < r.a; });
        if (a.closed)
        {
            offsetA = hints.front().a;
            offsetB = hints.front().b;
        }
        knots.push_back({0.0, 0.0});
        for (const Knot& hint : hints)
        {
            double x = hint.a - offsetA;
            double y = hint.b - offsetB;
            x -= std::floor(x);
            y -= std::floor(y);
            // Hints that would fold the outline back on itself are left out.
            if (x > knots.back().a + 1.0e-6 && y > knots.back().b + 1.0e-6 && x < 1.0 - 1.0e-6 && y < 1.0 - 1.0e-6)
                knots.push_back({x, y});
        }
        knots.push_back({1.0, 1.0});
    }
    const auto toB = [&knots](double x) {
        if (knots.size() < 2)
            return x;
        for (size_t i = 0; i + 1 < knots.size(); ++i)
        {
            if (x <= knots[i + 1].a || i + 2 == knots.size())
            {
                const double span = knots[i + 1].a - knots[i].a;
                const double t = span > 0.0 ? (x - knots[i].a) / span : 0.0;
                return knots[i].b + t * (knots[i + 1].b - knots[i].b);
            }
        }
        return x;
    };
    const auto toA = [&knots](double y) {
        if (knots.size() < 2)
            return y;
        for (size_t i = 0; i + 1 < knots.size(); ++i)
        {
            if (y <= knots[i + 1].b || i + 2 == knots.size())
            {
                const double span = knots[i + 1].b - knots[i].b;
                const double t = span > 0.0 ? (y - knots[i].b) / span : 0.0;
                return knots[i].a + t * (knots[i + 1].a - knots[i].a);
            }
        }
        return y;
    };

    if (a.closed && b.closed && !hinted)
    {
        // A starts at its leftmost anchor (then topmost); B where it lines up best with that.
        const std::vector<double> anchorsA = ma.anchors();
        size_t start = 0;
        for (size_t i = 1; i < a.pieces.size(); ++i)
        {
            const Point& p = a.pieces[i].start();
            const Point& best = a.pieces[start].start();
            if (p.x < best.x - 1.0e-9 || (std::abs(p.x - best.x) <= 1.0e-9 && p.y < best.y))
                start = i;
        }
        offsetA = anchorsA.empty() ? 0.0 : anchorsA[start];

        if (!ma.isPoint() && !mb.isPoint())
        {
            constexpr int kSamples = 32;
            const std::vector<Point> pointsA = samples(ma, offsetA, kSamples);
            const Point middleA = centroid(pointsA);
            const Point middleB = centroid(samples(mb, 0.0, kSamples));
            std::vector<double> candidates = mb.anchors();
            for (int i = 0; i < 48; ++i)
                candidates.push_back(i / 48.0);
            double bestCost = -1.0;
            for (double candidate : candidates)
            {
                double cost = 0.0;
                for (int i = 0; i < kSamples; ++i)
                {
                    const Point pa = minus(pointsA[i], middleA);
                    const Point pb = minus(mb.at(candidate + static_cast<double>(i) / kSamples), middleB);
                    cost += (pa.x - pb.x) * (pa.x - pb.x) + (pa.y - pb.y) * (pa.y - pb.y);
                }
                if (bestCost < 0.0 || cost < bestCost)
                {
                    bestCost = cost;
                    offsetB = candidate;
                }
            }
        }
    }
    else if (!a.closed && !b.closed && !hinted)
    {
        // Open runs: B the way round that keeps the ends nearest.
        const double straight = distance(a.pieces.front().start(), b.pieces.front().start()) +
            distance(a.pieces.back().end(), b.pieces.back().end());
        const double crossed = distance(a.pieces.front().start(), b.pieces.back().end()) +
            distance(a.pieces.back().end(), b.pieces.front().start());
        if (crossed < straight)
        {
            flipB = true;
            flippedB = b;
            std::reverse(flippedB.pieces.begin(), flippedB.pieces.end());
            for (Curve& piece : flippedB.pieces)
                piece = reversed(piece);
        }
    }
    std::unique_ptr<Measured> flippedMeasure;
    if (flipB)
    {
        flippedMeasure = std::make_unique<Measured>(flippedB);
        measuredB = flippedMeasure.get();
    }

    // Both cut at every anchor either has, and at the hints, by fraction of
    // the way round A.
    std::vector<double> cuts{0.0};
    for (double at : ma.anchors())
    {
        double u = at - offsetA;
        u -= std::floor(u);
        cuts.push_back(u);
    }
    for (double at : measuredB->anchors())
    {
        double u = at - offsetB;
        u -= std::floor(u);
        cuts.push_back(toA(u));
    }
    for (const Knot& knot : knots)
        if (knot.a < 1.0)
            cuts.push_back(knot.a);
    std::sort(cuts.begin(), cuts.end());
    std::vector<double> unique;
    for (double cut : cuts)
    {
        if (unique.empty() || cut - unique.back() > 1.0e-7)
            unique.push_back(cut);
    }
    if (!unique.empty() && 1.0 - unique.back() <= 1.0e-7)
        unique.pop_back();

    MorphSegment* segment = new MorphSegment(morph);
    segment->fillIndex1 = 0;
    segment->fillIndex2 = a.fill;
    segment->strokeIndex1 = a.stroke;
    segment->strokeIndex2 = 0;
    bool first = true;
    for (size_t k = 0; k < unique.size(); ++k)
    {
        const double from = unique[k];
        const double to = k + 1 < unique.size() ? unique[k + 1] : 1.0;
        const Curve pa = ma.part(offsetA + from, offsetA + to);
        const Curve pb = measuredB->part(offsetB + toB(from), offsetB + toB(to));
        if (first)
        {
            segment->startPointA = pa.start();
            segment->startPointB = pb.start();
            first = false;
        }
        const auto emit = [&](const Curve& ca, const Curve& cb) {
            MorphCurves* curves = new MorphCurves(segment);
            toQuadratic(ca, curves->controlPointA);
            toQuadratic(cb, curves->controlPointB);
            curves->anchorPointA = ca.end();
            curves->anchorPointB = cb.end();
            segment->curves.push_back(curves);
        };
        if (pa.isLine() && pb.isLine())
        {
            emit(pa, pb);
            continue;
        }
        Curve a0, a1, b0, b1;
        pa.split(0.5, a0, a1);
        pb.split(0.5, b0, b1);
        emit(a0, b0);
        emit(a1, b1);
    }
    return segment;
}

/// Pairs one keyframe's outlines with the other's, biggest with biggest,
/// within groups that \a key draws; \a size ranks them.
template <typename Key, typename Size>
void pairOutlines(std::vector<Outline>& from, std::vector<Outline>& to, Key key, Size size, MorphShape* morph)
{
    std::map<long long, std::vector<Outline*>> groupsA;
    std::map<long long, std::vector<Outline*>> groupsB;
    for (Outline& outline : from)
        groupsA[key(outline, true)].push_back(&outline);
    for (Outline& outline : to)
        groupsB[key(outline, false)].push_back(&outline);
    const auto bySize = [&](const Outline* l, const Outline* r) { return size(*l) > size(*r); };

    for (auto& group : groupsA)
    {
        std::vector<Outline*>& as = group.second;
        std::vector<Outline*>& bs = groupsB[group.first];
        std::stable_sort(as.begin(), as.end(), bySize);
        std::stable_sort(bs.begin(), bs.end(), bySize);
        for (size_t i = 0; i < std::max(as.size(), bs.size()); ++i)
        {
            if (i < as.size() && i < bs.size())
            {
                Outline b = *bs[i];
                b.fill = as[i]->fill;
                b.stroke = as[i]->stroke;
                // An open run and a closed outline pair as open runs.
                if (as[i]->closed != b.closed)
                {
                    Outline a = *as[i];
                    a.closed = b.closed = false;
                    morph->segments.push_back(pairSegment(a, b, morph));
                }
                else
                {
                    morph->segments.push_back(pairSegment(*as[i], b, morph));
                }
            }
            else if (i < as.size())
            {
                morph->segments.push_back(pairSegment(*as[i], pointOutline(*as[i], middleOf(*as[i])), morph));
            }
            else
            {
                Outline b = *bs[i];
                b.fill = as.empty() ? b.fill : as.front()->fill;
                b.stroke = as.empty() ? b.stroke : as.front()->stroke;
                morph->segments.push_back(pairSegment(pointOutline(b, middleOf(b)), b, morph));
            }
        }
    }
}

void hashBytes(uint64_t& hash, const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

void hashShape(uint64_t& hash, const Shape& shape)
{
    const double matrix[6] = {shape.transform.m11, shape.transform.m12, shape.transform.m21,
        shape.transform.m22, shape.transform.tx, shape.transform.ty};
    hashBytes(hash, matrix, sizeof(matrix));
    for (const Edge* edge : shape.edges)
    {
        if (!edge)
            continue;
        const int styles[3] = {edge->fillStyle0, edge->fillStyle1, edge->strokeStyle};
        hashBytes(hash, styles, sizeof(styles));
        for (const Path* path : edge->paths)
        {
            if (!path)
                continue;
            const int pathStyles[2] = {path->fillStyleIndex, path->lineStyleIndex};
            hashBytes(hash, pathStyles, sizeof(pathStyles));
            for (const PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;
                const int command = static_cast<int>(segment->command);
                hashBytes(hash, &command, sizeof(command));
                for (const Point& p : segment->points)
                {
                    const double xy[2] = {p.x, p.y};
                    hashBytes(hash, xy, sizeof(xy));
                }
            }
        }
    }
    const size_t counts[2] = {shape.fills.size(), shape.strokes.size()};
    hashBytes(hash, counts, sizeof(counts));
}

const Shape* firstShape(const Frame& frame)
{
    for (const Element* element : frame.elements)
    {
        if (element && element->elementType() == Element::Type::Shape)
            return static_cast<const Shape*>(element);
    }
    return nullptr;
}

} // namespace

MorphShape* buildMorph(const Shape& from, const Shape& to, DOMElement* parent, const std::vector<ShapeHint>& hints)
{
    MorphShape* morph = new MorphShape(parent);
    std::vector<Outline> fillsA;
    std::vector<Outline> strokesA;
    std::vector<Outline> fillsB;
    std::vector<Outline> strokesB;
    outlinesOf(from, fillsA, strokesA);
    outlinesOf(to, fillsB, strokesB);

    // Hinted outlines first: each hint pairs the outline its start lies on
    // with the one its end lies on, and pins the two points together. The
    // fill outlines take the hints, and the strokes along them take them too,
    // so the outline keeps to the fill all the way.
    const auto local = [](const Shape& shape, const Point& p) {
        const Transform& m = shape.transform;
        const double det = m.m11 * m.m22 - m.m12 * m.m21;
        if (std::abs(det) < 1.0e-12)
            return p;
        const double x = p.x - m.tx;
        const double y = p.y - m.ty;
        return Point((m.m22 * x - m.m21 * y) / det, (-m.m12 * x + m.m11 * y) / det);
    };
    const auto nearestOutline = [](const std::vector<Outline>& outlines, const Point& p, double& fraction) {
        size_t best = 0;
        double bestDistance = -1.0;
        for (size_t i = 0; i < outlines.size(); ++i)
        {
            const Measured measured(outlines[i]);
            const double u = measured.nearest(p);
            const double d = distance(measured.at(u), p);
            if (bestDistance < 0.0 || d < bestDistance)
            {
                bestDistance = d;
                best = i;
                fraction = u;
            }
        }
        return best;
    };
    const auto hinted = [&](std::vector<Outline>& as, std::vector<Outline>& bs) {
        if (hints.empty() || as.empty() || bs.empty())
            return;
        std::map<std::pair<size_t, size_t>, std::vector<Knot>> pairs;
        for (const ShapeHint& hint : hints)
        {
            double ua = 0.0;
            double ub = 0.0;
            const size_t ia = nearestOutline(as, local(from, hint.start), ua);
            const size_t ib = nearestOutline(bs, local(to, hint.end), ub);
            pairs[{ia, ib}].push_back({ua, ub});
        }
        std::set<size_t> usedA;
        std::set<size_t> usedB;
        for (const auto& entry : pairs)
        {
            const size_t ia = entry.first.first;
            const size_t ib = entry.first.second;
            if (usedA.count(ia) || usedB.count(ib))
                continue;
            usedA.insert(ia);
            usedB.insert(ib);
            Outline b = bs[ib];
            b.fill = as[ia].fill;
            b.stroke = as[ia].stroke;
            morph->segments.push_back(pairSegment(as[ia], b, morph, entry.second));
        }
        const auto without = [](std::vector<Outline>& outlines, const std::set<size_t>& used) {
            std::vector<Outline> kept;
            for (size_t i = 0; i < outlines.size(); ++i)
                if (!used.count(i))
                    kept.push_back(outlines[i]);
            outlines = std::move(kept);
        };
        without(as, usedA);
        without(bs, usedB);
    };
    hinted(fillsA, fillsB);
    // A stroke runs whichever way its edges were chained; a closed one is
    // turned to go round the same way in both keyframes, so the hints'
    // fractions of the way round mean the same on each.
    if (!hints.empty())
        for (std::vector<Outline>* strokes : {&strokesA, &strokesB})
            for (Outline& outline : *strokes)
                if (outline.closed && signedArea(outline) < 0.0)
                {
                    std::vector<Curve> back;
                    for (auto piece = outline.pieces.rbegin(); piece != outline.pieces.rend(); ++piece)
                        back.push_back(reversed(*piece));
                    outline.pieces = std::move(back);
                }
    hinted(strokesA, strokesB);

    // Fills group by fill and by which way they run, so holes pair with
    // holes. A fill only the last keyframe has takes the first one's first
    // fill, since the morph's styles come from the first keyframe's table.
    const int firstFill = fillsA.empty() ? 0 : fillsA.front().fill;
    std::set<int> fillsOfA;
    for (const Outline& outline : fillsA)
        fillsOfA.insert(outline.fill);
    const auto fillKey = [&](const Outline& outline, bool isA) {
        int fill = outline.fill;
        if (!isA && !fillsOfA.count(fill) && firstFill > 0)
            fill = firstFill;
        return static_cast<long long>(fill) * 2 + (signedArea(outline) < 0.0 ? 1 : 0);
    };
    pairOutlines(fillsA, fillsB, fillKey, [](const Outline& o) { return std::abs(signedArea(o)); }, morph);

    const int firstStroke = strokesA.empty() ? 0 : strokesA.front().stroke;
    std::set<int> strokesOfA;
    for (const Outline& outline : strokesA)
        strokesOfA.insert(outline.stroke);
    const auto strokeKey = [&](const Outline& outline, bool isA) {
        int stroke = outline.stroke;
        if (!isA && !strokesOfA.count(stroke) && firstStroke > 0)
            stroke = firstStroke;
        return static_cast<long long>(stroke);
    };
    pairOutlines(strokesA, strokesB, strokeKey, [](const Outline& o) { return length(o); }, morph);
    return morph;
}

uint64_t morphSignature(const Shape& from, const Shape& to)
{
    uint64_t hash = 14695981039346656037ull;
    hashShape(hash, from);
    const char separator = '|';
    hashBytes(hash, &separator, 1);
    hashShape(hash, to);
    return hash ? hash : 1;
}

bool tweenShapes(const Frame& start, const Frame& end, const Shape*& from, const Shape*& to)
{
    from = firstShape(start);
    to = firstShape(end);
    return from && to;
}

namespace {

const char* const kHints = "phoenixShapeHints";

} // namespace

std::vector<ShapeHint> hintsOf(const Frame& start)
{
    std::vector<ShapeHint> hints;
    for (const auto& attribute : start.extraAttributes)
    {
        if (attribute.first != kHints)
            continue;
        // "ax ay bx by;ax ay bx by..."
        size_t at = 0;
        const std::string& text = attribute.second;
        while (at < text.size())
        {
            const size_t end = text.find(';', at);
            const std::string item = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            ShapeHint hint;
            if (std::sscanf(item.c_str(), "%lf %lf %lf %lf", &hint.start.x, &hint.start.y, &hint.end.x, &hint.end.y) == 4)
                hints.push_back(hint);
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
    }
    return hints;
}

void setHints(std::vector<std::pair<std::string, std::string>>& attributes, const std::vector<ShapeHint>& hints)
{
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& a) { return a.first == kHints; }),
        attributes.end());
    if (hints.empty())
        return;
    std::string text;
    char buffer[128];
    for (const ShapeHint& hint : hints)
    {
        std::snprintf(buffer, sizeof(buffer), "%s%.4f %.4f %.4f %.4f", text.empty() ? "" : ";", hint.start.x,
            hint.start.y, hint.end.x, hint.end.y);
        text += buffer;
    }
    attributes.emplace_back(kHints, text);
}

bool ensureMorph(Frame& start, const Frame& end)
{
    const Shape* from = nullptr;
    const Shape* to = nullptr;
    if (start.tweenType != TweenType::Shape || !tweenShapes(start, end, from, to))
        return false;
    // The hints are part of what the morph is made from.
    const std::vector<ShapeHint> hints = hintsOf(start);
    uint64_t signature = morphSignature(*from, *to);
    for (const ShapeHint& hint : hints)
    {
        const double values[4] = {hint.start.x, hint.start.y, hint.end.x, hint.end.y};
        hashBytes(signature, values, sizeof(values));
    }
    if (signature == 0)
        signature = 1;
    if (start.morphShape && start.morphSignature == 0)
    {
        start.morphSignature = signature;
        return false;
    }
    if (start.morphShape && start.morphSignature == signature)
        return false;
    delete start.morphShape;
    start.morphShape = buildMorph(*from, *to, &start, hints);
    start.morphSignature = signature;
    return true;
}

} // namespace fla
