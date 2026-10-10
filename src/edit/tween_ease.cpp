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

} // namespace

double easeProgress(double progress, int acceleration, const std::vector<Point>& customEase)
{
    const double t = std::max(0.0, std::min(1.0, progress));

    if (customEase.size() >= 4 && (customEase.size() - 1) % 3 == 0)
        return std::max(0.0, std::min(1.0, customEaseAt(customEase, t)));

    const double e = std::max(-1.0, std::min(1.0, acceleration / 100.0));
    if (e == 0.0)
        return t;

    // Ease in leans on t squared, which starts slowly; ease out on its
    // mirror, which finishes slowly. The amount says how far to lean.
    const double eased = e < 0.0 ? t * t : 2.0 * t - t * t;
    return t * (1.0 - std::fabs(e)) + eased * std::fabs(e);
}

} // namespace fla
