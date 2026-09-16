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

/// Fits a run and appends the result to \a anchors, splitting where it cannot
/// keep within tolerance.
void fitRun(const std::vector<Point>& points, size_t first, size_t last,
    const Vector& startTangent, const Vector& endTangent, double tolerance,
    int depth, std::vector<Anchor>& anchors)
{
    if (last <= first)
        return;

    // Two points can only be a straight line, which is also the base case that
    // stops the recursion.
    if (last - first == 1)
    {
        Anchor anchor(points[last]);
        anchors.push_back(anchor);
        return;
    }

    std::vector<double> parameters = chordLengthParameters(points, first, last);

    Point bezier[4];
    fitCubic(points, first, last, startTangent, endTangent, parameters, bezier);

    size_t splitAt = 0;
    double error = maximumError(points, first, last, bezier, parameters, splitAt);

    if (error > tolerance && depth < kMaxDepth)
    {
        // Reparameterise and try once more before giving up and splitting: a
        // single pass usually rescues a fit that is only slightly out.
        for (size_t i = 0; i < parameters.size(); ++i)
            parameters[i] = refineParameter(bezier, points[first + i], parameters[i]);

        fitCubic(points, first, last, startTangent, endTangent, parameters, bezier);
        error = maximumError(points, first, last, bezier, parameters, splitAt);
    }

    if (error <= tolerance || depth >= kMaxDepth)
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

    if (options.straighten)
    {
        // Straighten mode keeps the corners and throws away the curve: fit
        // straight runs by dropping points that already lie close to the line
        // between their neighbours.
        path.anchors.push_back(Anchor(cleaned.front()));

        size_t anchorIndex = 0;
        for (size_t i = 1; i + 1 < cleaned.size(); ++i)
        {
            const Point& start = cleaned[anchorIndex];
            const Point& end = cleaned[i + 1];
            const Vector along = subtract(end, start);
            const double length = std::hypot(along.x, along.y);
            if (length <= kEpsilon)
                continue;

            // Distance from the candidate point to the line start-end.
            const double deviation = std::fabs(
                along.x * (start.y - cleaned[i].y) - (start.x - cleaned[i].x) * along.y) / length;

            if (deviation > options.tolerance)
            {
                path.anchors.push_back(Anchor(cleaned[i]));
                anchorIndex = i;
            }
        }

        path.anchors.push_back(Anchor(cleaned.back()));
        return path;
    }

    path.anchors.push_back(Anchor(cleaned.front()));

    const Vector startTangent = normalize(subtract(cleaned[1], cleaned[0]));
    const Vector endTangent = normalize(
        subtract(cleaned[cleaned.size() - 2], cleaned.back()));

    fitRun(cleaned, 0, cleaned.size() - 1, startTangent, endTangent,
        std::max(0.01, options.tolerance), 0, path.anchors);

    return path;
}

} // namespace fla
