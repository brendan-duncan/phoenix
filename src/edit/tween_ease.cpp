#include "tween_ease.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

double cubic(double p0, double p1, double p2, double p3, double t)
{
    const double u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}

/// The custom ease read at x: the segment whose x range holds it, the
/// parameter found by bisection (x is monotone along a valid ease curve),
/// and its y there.
double customEaseAt(const std::vector<Point>& points, double x)
{
    const size_t segments = (points.size() - 1) / 3;
    for (size_t s = 0; s < segments; ++s)
    {
        const Point& p0 = points[s * 3];
        const Point& p1 = points[s * 3 + 1];
        const Point& p2 = points[s * 3 + 2];
        const Point& p3 = points[s * 3 + 3];
        const bool last = s + 1 == segments;
        if (x > p3.x && !last)
            continue;

        double low = 0.0;
        double high = 1.0;
        for (int i = 0; i < 40; ++i)
        {
            const double mid = (low + high) * 0.5;
            if (cubic(p0.x, p1.x, p2.x, p3.x, mid) < x)
                low = mid;
            else
                high = mid;
        }
        const double t = (low + high) * 0.5;
        return cubic(p0.y, p1.y, p2.y, p3.y, t);
    }
    return x;
}

const double kPi = 3.14159265358979323846;

double bounceOut(double t)
{
    const double n = 7.5625;
    const double d = 2.75;
    if (t < 1.0 / d)
        return n * t * t;
    if (t < 2.0 / d)
    {
        t -= 1.5 / d;
        return n * t * t + 0.75;
    }
    if (t < 2.5 / d)
    {
        t -= 2.25 / d;
        return n * t * t + 0.9375;
    }
    t -= 2.625 / d;
    return n * t * t + 0.984375;
}

/// An ease that starts slowly, given as \a in, as itself (In), its mirror
/// (Out) or both halves (InOut).
double shaped(double (*in)(double), const std::string& kind, double t)
{
    if (kind == "In")
        return in(t);
    if (kind == "Out")
        return 1.0 - in(1.0 - t);
    return t < 0.5 ? in(2.0 * t) / 2.0 : 1.0 - in(2.0 - 2.0 * t) / 2.0;
}

} // namespace

double presetEase(const std::string& method, double t)
{
    // Animate's names: a curve (quad, cubic, quart, quint, sine, back, circ,
    // bounce, elastic) and a kind (In, Out, InOut).
    std::string curve;
    std::string kind;
    for (const std::string k : {"InOut", "In", "Out"})
    {
        if (method.size() > k.size() && method.compare(method.size() - k.size(), k.size(), k) == 0)
        {
            curve = method.substr(0, method.size() - k.size());
            kind = k;
            break;
        }
    }
    if (curve == "quad")
        return shaped([](double x) { return x * x; }, kind, t);
    if (curve == "cubic")
        return shaped([](double x) { return x * x * x; }, kind, t);
    if (curve == "quart")
        return shaped([](double x) { return x * x * x * x; }, kind, t);
    if (curve == "quint")
        return shaped([](double x) { return x * x * x * x * x; }, kind, t);
    if (curve == "sine")
        return shaped([](double x) { return 1.0 - std::cos(x * kPi / 2.0); }, kind, t);
    if (curve == "circ")
        return shaped([](double x) { return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x)); }, kind, t);
    if (curve == "back")
        return shaped([](double x) { return 2.70158 * x * x * x - 1.70158 * x * x; }, kind, t);
    if (curve == "bounce")
        return shaped([](double x) { return 1.0 - bounceOut(1.0 - x); }, kind, t);
    if (curve == "elastic")
    {
        return shaped([](double x) {
            if (x <= 0.0 || x >= 1.0)
                return x <= 0.0 ? 0.0 : 1.0;
            return -std::pow(2.0, 10.0 * x - 10.0) * std::sin((10.0 * x - 10.75) * (2.0 * kPi / 3.0));
        }, kind, t);
    }
    return -1.0;
}

double easeProgress(double progress, int acceleration, const std::vector<Point>& customEase,
    const std::string& method)
{
    const double t = std::max(0.0, std::min(1.0, progress));

    if (customEase.size() >= 4 && (customEase.size() - 1) % 3 == 0)
        return std::max(0.0, std::min(1.0, customEaseAt(customEase, t)));

    if (!method.empty() && method != "none" && method != "classic")
    {
        const double eased = presetEase(method, t);
        if (eased != -1.0)
            return eased;
    }

    const double e = std::max(-1.0, std::min(1.0, acceleration / 100.0));
    if (e == 0.0)
        return t;

    // Ease in leans on t squared, which starts slowly; ease out on its
    // mirror, which finishes slowly. The amount says how far to lean.
    const double eased = e < 0.0 ? t * t : 2.0 * t - t * t;
    return t * (1.0 - std::fabs(e)) + eased * std::fabs(e);
}

} // namespace fla
