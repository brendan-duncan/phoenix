#include "deformers.h"

#include "property_command.h"

#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstdlib>

namespace fla {

namespace deform {

namespace {

const char* const kAttribute = "phoenixDeformer";

struct V
{
    double x = 0.0;
    double y = 0.0;
};

V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
V operator*(V a, double s) { return {a.x * s, a.y * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y; }
double length(V a) { return std::hypot(a.x, a.y); }
V vec(const Point& point) { return {point.x, point.y}; }

/// A cubic Bezier's point, and its first and second derivatives, at \a t.
V cubic(const V* c, double t)
{
    const double u = 1.0 - t;
    return c[0] * (u * u * u) + c[1] * (3.0 * u * u * t) + c[2] * (3.0 * u * t * t) + c[3] * (t * t * t);
}
V cubicD1(const V* c, double t)
{
    const double u = 1.0 - t;
    return (c[1] - c[0]) * (3.0 * u * u) + (c[2] - c[1]) * (6.0 * u * t) + (c[3] - c[2]) * (3.0 * t * t);
}
V cubicD2(const V* c, double t)
{
    return (c[2] - c[1] * 2.0 + c[0]) * (6.0 * (1.0 - t)) + (c[3] - c[2] * 2.0 + c[1]) * (6.0 * t);
}

/// The frame of a curve at a point: where, which way along, which way out.
struct Frame
{
    V origin;
    V along;
    V out;
    double speed = 1.0;
};

Frame frameAt(const V* c, double t)
{
    Frame frame;
    frame.origin = cubic(c, t);
    V d = cubicD1(c, t);
    frame.speed = length(d);
    if (frame.speed < 1.0e-9)
    {
        // A handle sitting on its end point: the chord instead.
        d = c[3] - c[0];
        frame.speed = std::max(length(d), 1.0e-9);
    }
    frame.along = d * (1.0 / std::max(length(d), 1.0e-9));
    frame.out = {-frame.along.y, frame.along.x};
    return frame;
}

Point applyCurve(const Deformer& deformer, const Point& point)
{
    const V p = vec(point);
    const size_t segments = (deformer.rest.size() - 1) / 3;
    std::vector<V> rest(deformer.rest.size());
    std::vector<V> posed(deformer.posed.size());
    for (size_t i = 0; i < rest.size(); ++i)
    {
        rest[i] = vec(deformer.rest[i]);
        posed[i] = vec(deformer.posed[i]);
    }

    // The nearest point on the spine at rest: sampled, then refined.
    size_t best = 0;
    double bestT = 0.0;
    double bestDistance = 1.0e300;
    const int samples = 24;
    for (size_t s = 0; s < segments; ++s)
        for (int k = 0; k <= samples; ++k)
        {
            const double t = static_cast<double>(k) / samples;
            const double distance = length(cubic(&rest[3 * s], t) - p);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = s;
                bestT = t;
            }
        }
    const V* c = &rest[3 * best];
    for (int i = 0; i < 8; ++i)
    {
        const V offset = cubic(c, bestT) - p;
        const V d1 = cubicD1(c, bestT);
        const double slope = dot(d1, d1) + dot(offset, cubicD2(c, bestT));
        if (std::abs(slope) < 1.0e-12)
            break;
        bestT = std::clamp(bestT - dot(offset, d1) / slope, 0.0, 1.0);
    }

    // Its place in the spine's frame there, taken to the posed spine's.
    const Frame from = frameAt(c, bestT);
    const Frame to = frameAt(&posed[3 * best], bestT);
    const V relative = p - from.origin;
    const double along = dot(relative, from.along) * to.speed / from.speed;
    const double out = dot(relative, from.out);
    const V moved = to.origin + to.along * along + to.out * out;
    return Point(moved.x, moved.y);
}

double bernstein(int i, double t)
{
    const double u = 1.0 - t;
    switch (i)
    {
    case 0: return u * u * u;
    case 1: return 3.0 * u * u * t;
    case 2: return 3.0 * u * t * t;
    default: return t * t * t;
    }
}

double bernsteinD(int i, double t)
{
    const double u = 1.0 - t;
    switch (i)
    {
    case 0: return -3.0 * u * u;
    case 1: return 3.0 * u * u - 6.0 * u * t;
    case 2: return 6.0 * u * t - 3.0 * t * t;
    default: return 3.0 * t * t;
    }
}

Point applyEnvelope(const Deformer& deformer, const Point& point)
{
    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    for (const Point& handle : deformer.rest)
    {
        left = std::min(left, handle.x);
        right = std::max(right, handle.x);
        top = std::min(top, handle.y);
        bottom = std::max(bottom, handle.y);
    }
    if (right - left < 1.0e-9 || bottom - top < 1.0e-9)
        return point;
    const double u = (point.x - left) / (right - left);
    const double v = (point.y - top) / (bottom - top);
    // Outside the grid, straight on from its edge.
    const double cu = std::clamp(u, 0.0, 1.0);
    const double cv = std::clamp(v, 0.0, 1.0);
    V at, du, dv;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
        {
            const V handle = vec(deformer.posed[static_cast<size_t>(row * 4 + column)]);
            at = at + handle * (bernstein(row, cv) * bernstein(column, cu));
            du = du + handle * (bernstein(row, cv) * bernsteinD(column, cu));
            dv = dv + handle * (bernsteinD(row, cv) * bernstein(column, cu));
        }
    const V moved = at + du * (u - cu) + dv * (v - cv);
    return Point(moved.x, moved.y);
}

using Matrix = std::array<double, 9>;

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix m{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            for (int k = 0; k < 3; ++k)
                m[row * 3 + column] += a[row * 3 + k] * b[k * 3 + column];
    return m;
}

/// The map from the unit square's corners, (0, 0), (1, 0), (1, 1), (0, 1),
/// to \a quad's.
Matrix squareTo(const Point quad[4])
{
    const double dx1 = quad[1].x - quad[2].x, dx2 = quad[3].x - quad[2].x;
    const double dy1 = quad[1].y - quad[2].y, dy2 = quad[3].y - quad[2].y;
    const double sx = quad[0].x - quad[1].x + quad[2].x - quad[3].x;
    const double sy = quad[0].y - quad[1].y + quad[2].y - quad[3].y;
    double g = 0.0, h = 0.0;
    const double det = dx1 * dy2 - dx2 * dy1;
    if ((sx != 0.0 || sy != 0.0) && std::abs(det) > 1.0e-12)
    {
        g = (sx * dy2 - dx2 * sy) / det;
        h = (dx1 * sy - sx * dy1) / det;
    }
    return {quad[1].x - quad[0].x + g * quad[1].x, quad[3].x - quad[0].x + h * quad[3].x, quad[0].x,
        quad[1].y - quad[0].y + g * quad[1].y, quad[3].y - quad[0].y + h * quad[3].y, quad[0].y, g, h, 1.0};
}

bool invert(const Matrix& m, Matrix& inverse)
{
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1.0e-12)
        return false;
    inverse = {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
        (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
        (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    return true;
}

Point applyCornerPin(const Deformer& deformer, const Point& point)
{
    Matrix map;
    if (!cornerPinMap(deformer, map))
        return point;
    const double w = map[6] * point.x + map[7] * point.y + map[8];
    // Past the horizon a fold leaves nothing sensible: kept where it was.
    if (w <= 1.0e-9)
        return point;
    return Point((map[0] * point.x + map[1] * point.y + map[2]) / w, (map[3] * point.x + map[4] * point.y + map[5]) / w);
}

} // namespace

bool Deformer::valid() const
{
    if (rest.size() != posed.size())
        return false;
    if (kind == Kind::Curve)
        return rest.size() >= 4 && (rest.size() - 1) % 3 == 0;
    if (kind == Kind::CornerPin)
        return rest.size() == 4;
    return rest.size() == 16;
}

Deformer cornerPinAround(double left, double top, double right, double bottom)
{
    Deformer deformer;
    deformer.kind = Kind::CornerPin;
    deformer.rest = {Point(left, top), Point(right, top), Point(right, bottom), Point(left, bottom)};
    deformer.posed = deformer.rest;
    return deformer;
}

bool homography(const Point from[4], const Point to[4], std::array<double, 9>& map)
{
    // From the corners to a unit square, then from it to the new corners.
    Matrix back;
    if (!invert(squareTo(from), back))
        return false;
    map = multiply(squareTo(to), back);
    // Scaled so the last entry is 1, as an affine map's is.
    if (std::abs(map[8]) > 1.0e-12)
        for (double& value : map)
            value /= map[8];
    return true;
}

bool cornerPinMap(const Deformer& deformer, std::array<double, 9>& map)
{
    if (deformer.kind != Kind::CornerPin || !deformer.valid())
        return false;
    return homography(deformer.rest.data(), deformer.posed.data(), map);
}

Deformer curveAround(double left, double top, double right, double bottom)
{
    Deformer deformer;
    deformer.kind = Kind::Curve;
    const bool across = right - left >= bottom - top;
    for (int i = 0; i < 4; ++i)
    {
        const double t = i / 3.0;
        deformer.rest.push_back(across ? Point(left + (right - left) * t, (top + bottom) / 2.0)
                                       : Point((left + right) / 2.0, top + (bottom - top) * t));
    }
    deformer.posed = deformer.rest;
    return deformer;
}

Deformer envelopeAround(double left, double top, double right, double bottom)
{
    Deformer deformer;
    deformer.kind = Kind::Envelope;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            deformer.rest.emplace_back(left + (right - left) * column / 3.0, top + (bottom - top) * row / 3.0);
    deformer.posed = deformer.rest;
    return deformer;
}

std::optional<Deformer> of(const SymbolInstance& instance)
{
    for (const auto& [name, value] : instance.extraAttributes)
        if (name == kAttribute)
            return parse(value);
    return std::nullopt;
}

std::string text(const Deformer& deformer)
{
    std::ostringstream out;
    out.precision(10);
    out << (deformer.kind == Kind::Curve ? "curve" : deformer.kind == Kind::CornerPin ? "cornerpin" : "envelope");
    for (const Point& point : deformer.rest)
        out << ' ' << point.x << ' ' << point.y;
    out << " /";
    for (const Point& point : deformer.posed)
        out << ' ' << point.x << ' ' << point.y;
    return out.str();
}

std::optional<Deformer> parse(const std::string& text)
{
    std::istringstream in(text);
    std::string kind;
    in >> kind;
    Deformer deformer;
    if (kind == "curve")
        deformer.kind = Kind::Curve;
    else if (kind == "envelope")
        deformer.kind = Kind::Envelope;
    else if (kind == "cornerpin")
        deformer.kind = Kind::CornerPin;
    else
        return std::nullopt;
    std::vector<Point>* into = &deformer.rest;
    std::string word;
    double pending = 0.0;
    bool half = false;
    while (in >> word)
    {
        if (word == "/")
        {
            into = &deformer.posed;
            half = false;
            continue;
        }
        char* end = nullptr;
        const double value = std::strtod(word.c_str(), &end);
        if (!end || *end)
            return std::nullopt;
        if (half)
            into->emplace_back(pending, value);
        else
            pending = value;
        half = !half;
    }
    if (!deformer.valid())
        return std::nullopt;
    return deformer;
}

std::vector<std::pair<std::string, std::string>> withDeformer(const SymbolInstance& instance,
    const std::optional<Deformer>& deformer)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    for (const auto& attribute : instance.extraAttributes)
        if (attribute.first != kAttribute)
            attributes.push_back(attribute);
    if (deformer && deformer->valid())
        attributes.emplace_back(kAttribute, text(*deformer));
    return attributes;
}

CommandPtr setDeformer(SymbolInstance& instance, const std::optional<Deformer>& deformer, const std::string& name)
{
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    return CommandPtr(new SetPropertyCommand<Attributes>(instance.extraAttributes, instance.extraAttributes,
        withDeformer(instance, deformer), name));
}

Deformer blend(const Deformer& a, const Deformer& b, double t)
{
    if (a.kind != b.kind || a.posed.size() != b.posed.size())
        return a;
    Deformer between = a;
    for (size_t i = 0; i < between.posed.size(); ++i)
    {
        between.posed[i].x = a.posed[i].x + (b.posed[i].x - a.posed[i].x) * t;
        between.posed[i].y = a.posed[i].y + (b.posed[i].y - a.posed[i].y) * t;
    }
    return between;
}

bool atRest(const Deformer& deformer)
{
    for (size_t i = 0; i < deformer.rest.size() && i < deformer.posed.size(); ++i)
        if (deformer.rest[i].x != deformer.posed[i].x || deformer.rest[i].y != deformer.posed[i].y)
            return false;
    return true;
}

Point apply(const Deformer& deformer, const Point& point)
{
    if (!deformer.valid())
        return point;
    if (deformer.kind == Kind::CornerPin)
        return applyCornerPin(deformer, point);
    return deformer.kind == Kind::Curve ? applyCurve(deformer, point) : applyEnvelope(deformer, point);
}

Point unapply(const Deformer& deformer, const Point& shown)
{
    if (!deformer.valid() || atRest(deformer))
        return shown;
    // Newton's method from the point itself, the deformer's slope there
    // measured a half pixel each way.
    Point at = shown;
    const double h = 0.5;
    for (int i = 0; i < 30; ++i)
    {
        const Point now = apply(deformer, at);
        const double rx = shown.x - now.x;
        const double ry = shown.y - now.y;
        if (rx * rx + ry * ry < 1.0e-6)
            break;
        const Point dx = apply(deformer, Point(at.x + h, at.y));
        const Point dy = apply(deformer, Point(at.x, at.y + h));
        const double a = (dx.x - now.x) / h, b = (dy.x - now.x) / h;
        const double c = (dx.y - now.y) / h, d = (dy.y - now.y) / h;
        const double det = a * d - b * c;
        if (std::abs(det) < 1.0e-9)
            break;
        double stepX = (d * rx - b * ry) / det;
        double stepY = (a * ry - c * rx) / det;
        // Never more than a few pixels at once, so it settles on the
        // nearest place rather than leaping across a fold.
        const double size = std::hypot(stepX, stepY);
        const double most = 20.0;
        if (size > most)
        {
            stepX *= most / size;
            stepY *= most / size;
        }
        at = Point(at.x + stepX, at.y + stepY);
    }
    return at;
}

} // namespace deform

} // namespace fla
