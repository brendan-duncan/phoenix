#include "brush_stroke.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// The finest step the line is worked on at, in screen pixels.
constexpr double kStep = 0.5;

/// The most samples a stroke is worked on at; a longer one is worked on at
/// a coarser step.
constexpr size_t kMostSamples = 20000;

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

BrushSample lerp(const BrushSample& a, const BrushSample& b, double t)
{
    BrushSample out;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.pressure = a.pressure + (b.pressure - a.pressure) * t;
    out.tiltX = a.tiltX + (b.tiltX - a.tiltX) * t;
    out.tiltY = a.tiltY + (b.tiltY - a.tiltY) * t;
    out.time = a.time + (b.time - a.time) * t;
    return out;
}

double distance(const BrushSample& a, const BrushSample& b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

/// The stabilizer: the line's point is pulled along by the pen on a string
/// of \a length, moving only when the pen is further away than that. With
/// \a finished the line runs on to where the pen lifted, so the stroke ends
/// where it was meant to.
std::vector<BrushSample> stabilize(const std::vector<BrushSample>& raw, double length, bool finished)
{
    if (length <= 0.0 || raw.size() < 2)
        return raw;
    std::vector<BrushSample> out;
    out.reserve(raw.size() + 1);
    BrushSample point = raw.front();
    out.push_back(point);
    for (size_t i = 1; i < raw.size(); ++i)
    {
        const BrushSample& pen = raw[i];
        const double d = distance(point, pen);
        if (d <= length)
            continue;
        const double t = 1.0 - length / d;
        BrushSample moved = pen;
        moved.x = point.x + (pen.x - point.x) * t;
        moved.y = point.y + (pen.y - point.y) * t;
        point = moved;
        out.push_back(point);
    }
    if (finished && distance(out.back(), raw.back()) > 1.0e-9)
        out.push_back(raw.back());
    return out;
}

/// The samples laid out \a step apart along the line they trace, each
/// reading interpolated between the two it falls between.
std::vector<BrushSample> resample(const std::vector<BrushSample>& in, double step)
{
    std::vector<BrushSample> out;
    if (in.empty())
        return out;
    out.push_back(in.front());
    double carried = 0.0;
    for (size_t i = 1; i < in.size(); ++i)
    {
        const BrushSample& a = in[i - 1];
        const BrushSample& b = in[i];
        const double length = distance(a, b);
        if (length <= 1.0e-12)
            continue;
        double at = step - carried;
        while (at <= length)
        {
            out.push_back(lerp(a, b, at / length));
            at += step;
        }
        carried = length - (at - step);
    }
    if (in.size() > 1 && distance(out.back(), in.back()) > step * 0.05)
        out.push_back(in.back());
    return out;
}

/// A Gaussian of \a sigma samples run over one reading of evenly spaced
/// samples. The window shrinks evenly towards the ends, so the first and
/// last readings stay where they are and the line keeps its ends.
template <typename Get, typename Set>
void smoothAlong(std::vector<BrushSample>& samples, double sigma, Get get, Set set)
{
    const int n = static_cast<int>(samples.size());
    if (sigma < 0.3 || n < 3)
        return;
    std::vector<double> values(n);
    for (int i = 0; i < n; ++i)
        values[i] = get(samples[i]);
    const int reachMost = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    std::vector<double> weights(reachMost + 1);
    for (int k = 0; k <= reachMost; ++k)
        weights[k] = std::exp(-(k * k) / (2.0 * sigma * sigma));
    for (int i = 0; i < n; ++i)
    {
        const int reach = std::min({reachMost, i, n - 1 - i});
        if (reach == 0)
            continue;
        double sum = values[i] * weights[0];
        double total = weights[0];
        for (int k = 1; k <= reach; ++k)
        {
            sum += (values[i - k] + values[i + k]) * weights[k];
            total += 2.0 * weights[k];
        }
        set(samples[i], sum / total);
    }
}

/// The tip's width factor for the speed at each sample, from 0.35 to 1.
std::vector<double> velocityFactors(const std::vector<BrushSample>& samples, double step, double zoom,
    BrushSettings::Velocity velocity)
{
    const size_t n = samples.size();
    std::vector<double> factors(n, 1.0);
    if (velocity == BrushSettings::Velocity::None || n < 3)
        return factors;
    // The speed over about ten screen pixels either side, in screen pixels
    // per millisecond; past about 2.5 (2500 a second) counts as fast.
    const int reach = std::max(1, static_cast<int>(10.0 / std::max(step * zoom, 1.0e-9)));
    for (size_t i = 0; i < n; ++i)
    {
        const size_t a = i >= static_cast<size_t>(reach) ? i - reach : 0;
        const size_t b = std::min(n - 1, i + reach);
        const double elapsed = samples[b].time - samples[a].time;
        if (elapsed <= 0.0)
            continue;
        const double speed = (b - a) * step * zoom / elapsed;
        const double fast = clamp01(speed / 2.5);
        factors[i] = velocity == BrushSettings::Velocity::ThinToThick ? 0.35 + 0.65 * fast : 1.0 - 0.65 * fast;
    }
    return factors;
}

/// How far a taper of \a length lets the width out at \a along: rising
/// quickly and then easing, so the end comes to a point.
double taper(double along, double length)
{
    if (length <= 0.0 || along >= length)
        return 1.0;
    const double t = clamp01(along / length);
    return std::max(0.03, t * (2.0 - t));
}

} // namespace

BrushSettings BrushSettings::fluidDefaults()
{
    BrushSettings settings;
    settings.size = 12.0;
    settings.minSize = 20.0;
    settings.smoothing = 50;
    settings.precision = 50;
    return settings;
}

BrushSettings BrushSettings::classicDefaults()
{
    BrushSettings settings;
    settings.size = 12.0;
    settings.minSize = 10.0;
    settings.tiltEffect = TiltEffect::Angle;
    settings.smoothing = 30;
    settings.precision = 60;
    return settings;
}

BrushSettings BrushSettings::inkDefaults()
{
    BrushSettings settings;
    settings.size = 8.0;
    settings.minSize = 5.0;
    settings.useTilt = true;
    settings.tiltEffect = TiltEffect::Width;
    settings.smoothing = 40;
    settings.precision = 40;
    settings.stabilizer = 10.0;
    return settings;
}

namespace BrushStroke {

std::vector<BrushDab> dabs(const std::vector<BrushSample>& input, const BrushSettings& settings,
    double zoom, bool finished)
{
    std::vector<BrushDab> result;
    if (input.empty())
        return result;
    zoom = std::max(zoom, 1.0e-6);
    const double pixel = 1.0 / zoom;

    // Readings at the same spot add nothing but a pause: the latest stands.
    std::vector<BrushSample> raw;
    raw.reserve(input.size());
    for (const BrushSample& sample : input)
    {
        if (!raw.empty() && distance(raw.back(), sample) < pixel * 0.02)
        {
            const double x = raw.back().x;
            const double y = raw.back().y;
            raw.back() = sample;
            raw.back().x = x;
            raw.back().y = y;
            continue;
        }
        raw.push_back(sample);
    }

    // A mouse reports full pressure throughout; then pressure plays no part.
    bool pressureVaries = false;
    for (const BrushSample& sample : raw)
        pressureVaries = pressureVaries || std::fabs(sample.pressure - 1.0) > 0.01;
    const bool usePressure = settings.usePressure && pressureVaries;
    if (!usePressure)
    {
        for (BrushSample& sample : raw)
            sample.pressure = 1.0;
    }

    raw = stabilize(raw, settings.stabilizer * pixel, finished);

    double length = 0.0;
    for (size_t i = 1; i < raw.size(); ++i)
        length += distance(raw[i - 1], raw[i]);
    double step = kStep * pixel;
    if (length / step > kMostSamples)
        step = length / kMostSamples;
    std::vector<BrushSample> line = resample(raw, step);

    // Smoothing takes the wobble out of the line, up to about eight screen
    // pixels' worth at 100. Pressure and tilt are always eased a little:
    // a pen's readings flicker from one to the next.
    const double smoothing = std::max(0, std::min(100, settings.smoothing)) / 100.0;
    const double stepPixels = step * zoom;
    const double sigma = std::pow(smoothing, 1.5) * 8.0 / stepPixels;
    smoothAlong(line, sigma, [](const BrushSample& s) { return s.x; }, [](BrushSample& s, double v) { s.x = v; });
    smoothAlong(line, sigma, [](const BrushSample& s) { return s.y; }, [](BrushSample& s, double v) { s.y = v; });
    const double readingSigma = (2.0 + 4.0 * smoothing) / stepPixels;
    smoothAlong(line, readingSigma, [](const BrushSample& s) { return s.pressure; },
        [](BrushSample& s, double v) { s.pressure = v; });
    smoothAlong(line, readingSigma, [](const BrushSample& s) { return s.tiltX; },
        [](BrushSample& s, double v) { s.tiltX = v; });
    smoothAlong(line, readingSigma, [](const BrushSample& s) { return s.tiltY; },
        [](BrushSample& s, double v) { s.tiltY = v; });

    std::vector<double> along(line.size(), 0.0);
    for (size_t i = 1; i < line.size(); ++i)
        along[i] = along[i - 1] + distance(line[i - 1], line[i]);
    const double total = along.back();

    std::vector<double> speedFactors = velocityFactors(line, step, zoom, settings.velocity);
    if (settings.velocity != BrushSettings::Velocity::None)
    {
        // Speed is eased too, so a hitch in the hand is not a lump.
        std::vector<BrushSample> carrier(line.size());
        for (size_t i = 0; i < line.size(); ++i)
            carrier[i].pressure = speedFactors[i];
        smoothAlong(carrier, 8.0 / stepPixels, [](const BrushSample& s) { return s.pressure; },
            [](BrushSample& s, double v) { s.pressure = v; });
        for (size_t i = 0; i < line.size(); ++i)
            speedFactors[i] = carrier[i].pressure;
    }

    const double startTaper = std::min(0.5, std::max(0.0, settings.taperStart) / 100.0) * total;
    const double endTaper = std::min(0.5, std::max(0.0, settings.taperEnd) / 100.0) * total;
    const double minimum = clamp01(settings.minSize / 100.0);
    const double curve = std::max(0.1, settings.pressureCurve);
    const double roundness = std::max(1.0, std::min(100.0, settings.roundness)) / 100.0;
    const double baseAngle = settings.angle * kPi / 180.0;
    const bool rectangular = settings.tip == BrushSettings::Tip::Rectangle;

    const auto dabAt = [&](size_t i) {
        const BrushSample& s = line[i];
        double width = std::max(settings.size, 0.1);
        if (usePressure)
            width *= minimum + (1.0 - minimum) * std::pow(clamp01(s.pressure), curve);
        width *= speedFactors[i];
        width *= taper(along[i], startTaper);
        width *= taper(total - along[i], endTaper);
        double angle = baseAngle;
        const double lean = clamp01(std::hypot(s.tiltX, s.tiltY) / 60.0);
        if (settings.useTilt && settings.tiltEffect == BrushSettings::TiltEffect::Width)
            width *= 1.0 + 1.5 * lean;
        else if (settings.useTilt && lean > 0.08)
            angle += std::atan2(s.tiltY, s.tiltX);
        width = std::max(width, 0.1 * pixel);
        BrushDab dab;
        dab.x = s.x;
        dab.y = s.y;
        dab.width = width;
        dab.height = std::max(width * roundness, 0.1 * pixel);
        dab.angle = angle;
        dab.rectangular = rectangular;
        return dab;
    };

    // Dabs close enough that the sweep between two follows the line and its
    // changes of width: a quarter of the tip's wide side, between half a
    // screen pixel and three. The hull between two dabs fills the gap
    // whatever the narrow side is, so a thin tip needs no more of them.
    result.push_back(dabAt(0));
    double since = 0.0;
    for (size_t i = 1; i < line.size(); ++i)
    {
        since += along[i] - along[i - 1];
        const BrushDab& last = result.back();
        const double spacing = std::max(0.5 * pixel, std::min(3.0 * pixel,
            0.25 * std::max(last.width, last.height)));
        if (since >= spacing || i + 1 == line.size())
        {
            result.push_back(dabAt(i));
            since = 0.0;
        }
    }
    return result;
}

std::vector<Point> dabOutline(const BrushDab& dab, double zoom)
{
    std::vector<Point> points;
    const double c = std::cos(dab.angle);
    const double s = std::sin(dab.angle);
    const auto place = [&](double u, double v) {
        points.push_back(Point(dab.x + u * c - v * s, dab.y + u * s + v * c));
    };
    const double a = dab.width / 2.0;
    const double b = dab.height / 2.0;
    if (dab.rectangular)
    {
        place(-a, -b);
        place(a, -b);
        place(a, b);
        place(-a, b);
        return points;
    }
    const int count = std::max(8, std::min(64, static_cast<int>(std::ceil(std::max(dab.width, dab.height) *
        std::max(zoom, 1.0e-6) * 0.6))));
    for (int k = 0; k < count; ++k)
    {
        const double theta = 2.0 * kPi * k / count;
        place(a * std::cos(theta), b * std::sin(theta));
    }
    return points;
}

std::vector<Point> convexHull(std::vector<Point> points)
{
    if (points.size() < 3)
        return points;
    std::sort(points.begin(), points.end(), [](const Point& p, const Point& q) {
        return p.x < q.x || (p.x == q.x && p.y < q.y);
    });
    const auto cross = [](const Point& o, const Point& a, const Point& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<Point> hull(points.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0)
            --k;
        hull[k++] = points[i];
    }
    for (size_t i = points.size() - 1, lower = k + 1; i > 0; --i)
    {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0)
            --k;
        hull[k++] = points[i - 1];
    }
    hull.resize(k > 1 ? k - 1 : k);
    return hull;
}

std::vector<std::vector<Point>> sweep(const std::vector<BrushDab>& dabs, double zoom)
{
    std::vector<std::vector<Point>> polygons;
    if (dabs.empty())
        return polygons;
    std::vector<Point> previous = dabOutline(dabs.front(), zoom);
    if (dabs.size() == 1)
    {
        polygons.push_back(convexHull(previous));
        return polygons;
    }
    polygons.reserve(dabs.size() - 1);
    for (size_t i = 1; i < dabs.size(); ++i)
    {
        std::vector<Point> current = dabOutline(dabs[i], zoom);
        std::vector<Point> both = previous;
        both.insert(both.end(), current.begin(), current.end());
        polygons.push_back(convexHull(std::move(both)));
        previous = std::move(current);
    }
    return polygons;
}

} // namespace BrushStroke

} // namespace fla
