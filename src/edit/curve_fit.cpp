#include "curve_fit.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

/// Below this a vector is treated as having no direction.
constexpr double kEpsilon = 1.0e-9;

/// How deep the recursive splitting may go. Freehand input with a lot of jitter
/// could otherwise keep splitting until every point is its own segment.
constexpr int kMaxDepth = 12;

struct Vector
{
    double x = 0.0;
    double y = 0.0;
};

Vector subtract(const Point& a, const Point& b)
{
    return Vector{a.x - b.x, a.y - b.y};
}

Vector scale(const Vector& v, double s)
{
    return Vector{v.x * s, v.y * s};
}

double dot(const Vector& a, const Vector& b)
{
    return a.x * b.x + a.y * b.y;
}

Vector normalize(const Vector& v)
{
    const double length = std::hypot(v.x, v.y);
    if (length <= kEpsilon)
        return Vector{0.0, 0.0};
    return Vector{v.x / length, v.y / length};
}

Point offset(const Point& p, const Vector& v)
{
    return Point(p.x + v.x, p.y + v.y);
}

/// The four Bernstein terms, which is all the cubic maths below needs.
double bernstein0(double t) { const double u = 1.0 - t; return u * u * u; }
double bernstein1(double t) { const double u = 1.0 - t; return 3.0 * t * u * u; }
double bernstein2(double t) { const double u = 1.0 - t; return 3.0 * t * t * u; }
double bernstein3(double t) { return t * t * t; }

Point evaluate(const Point bezier[4], double t)
{
    return Point(
        bezier[0].x * bernstein0(t) + bezier[1].x * bernstein1(t) +
        bezier[2].x * bernstein2(t) + bezier[3].x * bernstein3(t),
        bezier[0].y * bernstein0(t) + bezier[1].y * bernstein1(t) +
        bezier[2].y * bernstein2(t) + bezier[3].y * bernstein3(t));
}

/// Parameterises a run by how far along it each point sits, which is a much
/// better first guess than spacing them evenly.
std::vector<double> chordLengthParameters(const std::vector<Point>& points,
    size_t first, size_t last)
{
    std::vector<double> parameters(last - first + 1, 0.0);

    for (size_t i = first + 1; i <= last; ++i)
    {
        parameters[i - first] = parameters[i - first - 1] +
            std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    }

    const double total = parameters.back();
    if (total <= kEpsilon)
    {
        // Every point in the same place; spread them evenly rather than divide
        // by nothing.
        for (size_t i = 0; i < parameters.size(); ++i)
            parameters[i] = static_cast<double>(i) / (parameters.size() - 1);
        return parameters;
    }

    for (double& parameter : parameters)
        parameter /= total;

    return parameters;
}

/// Least-squares fit of one cubic to a run, with the end points and end tangents
/// fixed. Only the two handle lengths are free.
void fitCubic(const std::vector<Point>& points, size_t first, size_t last,
    const Vector& startTangent, const Vector& endTangent,
    const std::vector<double>& parameters, Point bezier[4])
{
    bezier[0] = points[first];
    bezier[3] = points[last];

    const size_t count = last - first + 1;

    // The normal equations for the two unknown handle lengths.
    double c00 = 0.0;
    double c01 = 0.0;
    double c11 = 0.0;
    double x0 = 0.0;
    double x1 = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        const double t = parameters[i];
        const Vector a0 = scale(startTangent, bernstein1(t));
        const Vector a1 = scale(endTangent, bernstein2(t));

        c00 += dot(a0, a0);
        c01 += dot(a0, a1);
        c11 += dot(a1, a1);

        Point onLine = points[first];
        onLine.x = bezier[0].x * (bernstein0(t) + bernstein1(t)) +
                   bezier[3].x * (bernstein2(t) + bernstein3(t));
        onLine.y = bezier[0].y * (bernstein0(t) + bernstein1(t)) +
                   bezier[3].y * (bernstein2(t) + bernstein3(t));

        const Vector difference = subtract(points[first + i], onLine);
        x0 += dot(a0, difference);
        x1 += dot(a1, difference);
    }

    const double determinant = c00 * c11 - c01 * c01;

    double alpha0 = 0.0;
    double alpha1 = 0.0;
    if (std::fabs(determinant) > kEpsilon)
    {
        alpha0 = (x0 * c11 - x1 * c01) / determinant;
        alpha1 = (c00 * x1 - c01 * x0) / determinant;
    }

    // A negative or vanishing length means the fit has gone wrong; fall back to
    // Wu and Barsky's heuristic of a third of the chord.
    const double chord = std::hypot(bezier[3].x - bezier[0].x, bezier[3].y - bezier[0].y);
    if (alpha0 < kEpsilon || alpha1 < kEpsilon)
    {
        alpha0 = chord / 3.0;
        alpha1 = chord / 3.0;
    }

    bezier[1] = offset(bezier[0], scale(startTangent, alpha0));
    bezier[2] = offset(bezier[3], scale(endTangent, alpha1));
}

/// Worst distance from the fitted curve to the points, and where it happens.
double maximumError(const std::vector<Point>& points, size_t first, size_t last,
    const Point bezier[4], const std::vector<double>& parameters, size_t& splitAt)
{
    splitAt = (first + last) / 2;
    double worst = 0.0;

    for (size_t i = first + 1; i < last; ++i)
    {
        const Point onCurve = evaluate(bezier, parameters[i - first]);
        const double distance = std::hypot(onCurve.x - points[i].x, onCurve.y - points[i].y);
        if (distance > worst)
        {
            worst = distance;
            splitAt = i;
        }
    }

    return worst;
}

/// One Newton-Raphson step toward the parameter where the curve is closest to
/// the point, which makes the next least-squares fit noticeably better.
double refineParameter(const Point bezier[4], const Point& point, double t)
{
    // First and second derivative control points.
    Point d1[3];
    for (int i = 0; i < 3; ++i)
    {
        d1[i] = Point((bezier[i + 1].x - bezier[i].x) * 3.0,
                      (bezier[i + 1].y - bezier[i].y) * 3.0);
    }

    Point d2[2];
    for (int i = 0; i < 2; ++i)
    {
        d2[i] = Point((d1[i + 1].x - d1[i].x) * 2.0,
                      (d1[i + 1].y - d1[i].y) * 2.0);
    }

    const Point onCurve = evaluate(bezier, t);

    const double u = 1.0 - t;
    const Point firstDerivative(
        d1[0].x * u * u + d1[1].x * 2.0 * t * u + d1[2].x * t * t,
        d1[0].y * u * u + d1[1].y * 2.0 * t * u + d1[2].y * t * t);
    const Point secondDerivative(
        d2[0].x * u + d2[1].x * t,
        d2[0].y * u + d2[1].y * t);

    const double numerator = (onCurve.x - point.x) * firstDerivative.x +
                             (onCurve.y - point.y) * firstDerivative.y;
    const double denominator = firstDerivative.x * firstDerivative.x +
                               firstDerivative.y * firstDerivative.y +
                               (onCurve.x - point.x) * secondDerivative.x +
                               (onCurve.y - point.y) * secondDerivative.y;

    if (std::fabs(denominator) <= kEpsilon)
        return t;

    return t - numerator / denominator;
}

/// Improves a fit by reparameterising and fitting again, up to a few times,
/// keeping the best of them: a pass that makes things worse is thrown away
/// rather than accepted. Parameters are kept inside [0, 1] and the handles
/// are capped at four chords, beyond which a fit has gone wild. Returns the
/// error of the best fit, with \a bezier, \a parameters and \a splitAt set
/// for it.
double refit(const std::vector<Point>& points, size_t first, size_t last,
    const Vector& startTangent, const Vector& endTangent,
    std::vector<double>& parameters, Point bezier[4], size_t& splitAt)
{
    constexpr int kPasses = 6;

    double bestError = maximumError(points, first, last, bezier, parameters, splitAt);
    std::vector<double> bestParameters = parameters;
    Point bestBezier[4] = {bezier[0], bezier[1], bezier[2], bezier[3]};
    size_t bestSplit = splitAt;

    const double chord = std::hypot(bezier[3].x - bezier[0].x, bezier[3].y - bezier[0].y);
    const double longest = std::max(chord * 4.0, kEpsilon);

    std::vector<double> trial = parameters;
    Point trialBezier[4] = {bezier[0], bezier[1], bezier[2], bezier[3]};
    for (int pass = 0; pass < kPasses; ++pass)
    {
        for (size_t i = 0; i < trial.size(); ++i)
        {
            const double t = refineParameter(trialBezier, points[first + i], trial[i]);
            trial[i] = std::max(0.0, std::min(1.0, t));
        }
        trial.front() = 0.0;
        trial.back() = 1.0;

        fitCubic(points, first, last, startTangent, endTangent, trial, trialBezier);
        for (int handle = 1; handle <= 2; ++handle)
        {
            const Point& from = handle == 1 ? trialBezier[0] : trialBezier[3];
            const Vector reach = subtract(trialBezier[handle], from);
            const double length = std::hypot(reach.x, reach.y);
            if (length > longest)
                trialBezier[handle] = offset(from, scale(reach, longest / length));
        }

        size_t trialSplit = 0;
        const double error = maximumError(points, first, last, trialBezier, trial, trialSplit);
        if (error >= bestError)
            break;

        bestError = error;
        bestParameters = trial;
        bestSplit = trialSplit;
        for (int i = 0; i < 4; ++i)
            bestBezier[i] = trialBezier[i];
    }

    parameters = bestParameters;
    splitAt = bestSplit;
    for (int i = 0; i < 4; ++i)
        bezier[i] = bestBezier[i];
    return bestError;
}

/// Fits a run and appends the result to \a anchors, splitting where it cannot
/// keep within tolerance.
void fitRun(const std::vector<Point>& points, size_t first, size_t last,
    const Vector& startTangent, const Vector& endTangent, double tolerance,
    int depth, std::vector<Anchor>& anchors)
{
    if (last <= first)
        return;

    // Two points are the base case that stops the recursion. They get
    // handles a third of the chord along the tangents, so a split mid-stroke
    // does not leave a straight stub between two curves.
    if (last - first == 1)
    {
        const double chord = std::hypot(points[last].x - points[first].x,
            points[last].y - points[first].y);
        if (!anchors.empty())
            anchors.back().outHandle = offset(points[first], scale(startTangent, chord / 3.0));

        Anchor anchor(points[last]);
        anchor.inHandle = offset(points[last], scale(endTangent, chord / 3.0));
        anchor.smooth = true;
        anchors.push_back(anchor);
        return;
    }

    std::vector<double> parameters = chordLengthParameters(points, first, last);

    Point bezier[4];
    fitCubic(points, first, last, startTangent, endTangent, parameters, bezier);

    size_t splitAt = 0;
    double error = maximumError(points, first, last, bezier, parameters, splitAt);

    if (error > tolerance && depth < kMaxDepth)
        error = refit(points, first, last, startTangent, endTangent, parameters, bezier, splitAt);

    if (error <= tolerance)
    {
        // Accepted. The previous anchor keeps its out handle and this one gets
        // its in handle, which is how a cubic maps onto two anchors.
        if (!anchors.empty())
            anchors.back().outHandle = bezier[1];

        Anchor anchor(points[last]);
        anchor.inHandle = bezier[2];
        anchor.smooth = true;
        anchors.push_back(anchor);
        return;
    }

    if (depth >= kMaxDepth)
    {
        // The splitting has gone as deep as it may and the run still will
        // not fit. Rather than quietly keep a curve that strays, keep the
        // points themselves: a polyline is within any tolerance.
        for (size_t i = first + 1; i <= last; ++i)
            anchors.push_back(Anchor(points[i]));
        return;
    }

    // Too far out: split at the worst point and fit each half. The tangent at
    // the split is the direction the stroke is heading there, so the two halves
    // meet smoothly.
    if (splitAt <= first || splitAt >= last)
        splitAt = (first + last) / 2;

    const Vector centre = normalize(subtract(points[splitAt - 1], points[splitAt + 1]));
    const Vector leftEnd = centre;
    const Vector rightStart = Vector{-centre.x, -centre.y};

    fitRun(points, first, splitAt, startTangent, leftEnd, tolerance, depth + 1, anchors);
    fitRun(points, splitAt, last, rightStart, endTangent, tolerance, depth + 1, anchors);
}

} // namespace

bool CurveFit::fitSingleCubic(const std::vector<Point>& points,
    const Point& startTangent, const Point& endTangent, Point bezier[4])
{
    if (points.size() < 2)
        return false;

    const Vector start = normalize(Vector{startTangent.x, startTangent.y});
    const Vector end = normalize(Vector{endTangent.x, endTangent.y});
    const size_t last = points.size() - 1;

    std::vector<double> parameters = chordLengthParameters(points, 0, last);
    fitCubic(points, 0, last, start, end, parameters, bezier);
    size_t splitAt = 0;
    refit(points, 0, last, start, end, parameters, bezier, splitAt);
    return true;
}

namespace {

/// The direction the stroke leaves \a from in, judged over \a window rather
/// than from the next sample alone, so the jitter of a single sample does
/// not hook the end of the fit. \a step is +1 looking forward, -1 back.
Vector tangentOver(const std::vector<Point>& points, size_t from, int step,
    size_t stop, double window)
{
    size_t at = from;
    for (;;)
    {
        const size_t next = static_cast<size_t>(static_cast<long long>(at) + step);
        if ((step > 0 && at >= stop) || (step < 0 && at <= stop))
            break;
        at = next;
        const Vector reach = subtract(points[at], points[from]);
        if (std::hypot(reach.x, reach.y) >= window)
            break;
    }
    if (at == from)
        return Vector{0.0, 0.0};
    return normalize(subtract(points[at], points[from]));
}

/// Indices of the points where the stroke turns sharply: the direction it
/// came from, taken over \a window, and the direction it goes on in differ
/// by more than \a cornerAngle. Of a cluster of neighbouring candidates the
/// sharpest is kept.
std::vector<size_t> findCorners(const std::vector<Point>& points, double window,
    double cornerAngle)
{
    std::vector<size_t> corners;
    if (points.size() < 3)
        return corners;

    const double threshold = std::cos(cornerAngle * 3.14159265358979323846 / 180.0);
    const size_t last = points.size() - 1;

    size_t bestInCluster = 0;
    double bestTurn = 2.0;
    bool inCluster = false;

    for (size_t i = 1; i < last; ++i)
    {
        // Too close to either end there is not a window's worth of stroke
        // to judge the turn by, and a jittered sample there would read as
        // a corner.
        const double fromStart = std::hypot(points[i].x - points[0].x, points[i].y - points[0].y);
        const double fromEnd = std::hypot(points[i].x - points[last].x, points[i].y - points[last].y);
        const bool nearEnd = fromStart < window || fromEnd < window;

        const Vector in = tangentOver(points, i, -1, 0, window);
        const Vector out = tangentOver(points, i, +1, last, window);
        const bool measurable = !nearEnd &&
            (in.x != 0.0 || in.y != 0.0) && (out.x != 0.0 || out.y != 0.0);
        // The direction in is from i back, so the turn is between -in and out.
        const double cosine = measurable ? -dot(in, out) : 1.0;
        const bool corner = measurable && cosine < threshold;

        if (corner)
        {
            if (!inCluster || cosine < bestTurn)
            {
                bestInCluster = i;
                bestTurn = cosine;
            }
            inCluster = true;
        }
        else if (inCluster)
        {
            corners.push_back(bestInCluster);
            inCluster = false;
            bestTurn = 2.0;
        }
    }
    if (inCluster)
        corners.push_back(bestInCluster);

    return corners;
}

/// Ramer-Douglas-Peucker: keep the end points, and the point furthest from
/// the line between them if it is too far off, and recurse either side of
/// it.
void simplifyRun(const std::vector<Point>& points, size_t first, size_t last,
    double tolerance, std::vector<size_t>& kept)
{
    if (last <= first + 1)
        return;

    const Vector along = subtract(points[last], points[first]);
    const double length = std::hypot(along.x, along.y);

    size_t worst = first;
    double worstDistance = 0.0;
    for (size_t i = first + 1; i < last; ++i)
    {
        const double distance = length <= kEpsilon
            ? std::hypot(points[i].x - points[first].x, points[i].y - points[first].y)
            : std::fabs(along.x * (points[first].y - points[i].y) -
                        (points[first].x - points[i].x) * along.y) / length;
        if (distance > worstDistance)
        {
            worstDistance = distance;
            worst = i;
        }
    }

    if (worstDistance <= tolerance)
        return;

    simplifyRun(points, first, worst, tolerance, kept);
    kept.push_back(worst);
    simplifyRun(points, worst, last, tolerance, kept);
}

} // namespace

std::vector<Point> CurveFit::removeDuplicates(const std::vector<Point>& points,
    double minimumSpacing)
{
    std::vector<Point> cleaned;
    cleaned.reserve(points.size());

    for (const Point& point : points)
    {
        if (cleaned.empty())
        {
            cleaned.push_back(point);
            continue;
        }

        const Point& previous = cleaned.back();
        if (std::hypot(point.x - previous.x, point.y - previous.y) >= minimumSpacing)
            cleaned.push_back(point);
    }

    return cleaned;
}

EditablePath CurveFit::fit(const std::vector<Point>& points, const Options& options)
{
    EditablePath path;

    const std::vector<Point> cleaned = removeDuplicates(points,
        std::max(0.0, options.minimumSpacing));

    if (cleaned.size() < 2)
        return path;

    const double tolerance = std::max(0.01, options.tolerance);

    if (options.straighten)
    {
        // Straighten mode keeps the corners and throws away the curve: the
        // points that matter are the ones furthest from the straight runs
        // between them, which Ramer-Douglas-Peucker finds, so a gentle arc
        // becomes a few long segments rather than drifting off.
        std::vector<size_t> kept;
        kept.push_back(0);
        simplifyRun(cleaned, 0, cleaned.size() - 1, tolerance, kept);
        kept.push_back(cleaned.size() - 1);
        for (const size_t index : kept)
            path.anchors.push_back(Anchor(cleaned[index]));
        return path;
    }

    // End tangents, and the turn at a corner, are judged over a short
    // stretch rather than one sample, so jitter neither hooks the ends nor
    // makes corners of its own.
    const double window = std::max(3.0 * tolerance, 2.0 * std::max(0.0, options.minimumSpacing));

    std::vector<size_t> corners;
    if (options.detectCorners)
        corners = findCorners(cleaned, window, options.cornerAngle);

    path.anchors.push_back(Anchor(cleaned.front()));

    // Each run between corners is fitted on its own, with its own end
    // tangents, and the anchor at a corner is left a corner: its two handles
    // belong to different runs and need not line up.
    size_t runStart = 0;
    for (size_t c = 0; c <= corners.size(); ++c)
    {
        const size_t runEnd = c < corners.size() ? corners[c] : cleaned.size() - 1;
        if (runEnd <= runStart)
            continue;

        Vector startTangent = tangentOver(cleaned, runStart, +1, runEnd, window);
        Vector endTangent = tangentOver(cleaned, runEnd, -1, runStart, window);
        if (startTangent.x == 0.0 && startTangent.y == 0.0)
            startTangent = normalize(subtract(cleaned[runStart + 1], cleaned[runStart]));
        if (endTangent.x == 0.0 && endTangent.y == 0.0)
            endTangent = normalize(subtract(cleaned[runEnd - 1], cleaned[runEnd]));

        fitRun(cleaned, runStart, runEnd, startTangent, endTangent, tolerance, 0, path.anchors);

        if (c < corners.size() && !path.anchors.empty())
            path.anchors.back().smooth = false;

        runStart = runEnd;
    }

    // A cubic whose handles sit on its chord, between its ends, is a line
    // and is written as one, so a straight stretch of a stroke does not
    // carry handles that do nothing.
    for (size_t i = 1; i < path.anchors.size(); ++i)
    {
        Anchor& from = path.anchors[i - 1];
        Anchor& to = path.anchors[i];
        const Vector chord = subtract(to.position, from.position);
        const double length = std::hypot(chord.x, chord.y);
        if (length <= kEpsilon)
            continue;

        const auto onChord = [&](const Point& handle) {
            const Vector reach = subtract(handle, from.position);
            const double along = dot(reach, chord) / length;
            const double off = std::fabs(chord.x * reach.y - chord.y * reach.x) / length;
            return along >= -kEpsilon && along <= length + kEpsilon && off <= tolerance * 0.5;
        };
        if (onChord(from.outHandle) && onChord(to.inHandle))
        {
            from.outHandle = from.position;
            to.inHandle = to.position;
        }
    }

    return path;
}

} // namespace fla
