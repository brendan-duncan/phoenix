#include "brush_stroke.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

/// \a value, rounded down, as a count from \a least to \a most. Not a
/// number counts as \a least; casting one, or anything past the range of an
/// int, would be undefined.
int clampedCount(double value, int least, int most)
{
    if (!(value > least))
        return least;
    if (value >= most)
        return most;
    return static_cast<int>(value);
}

/// The most an int can hold of a size, for clamping a count against it.
int countLimit(size_t size)
{
    return static_cast<int>(std::min<size_t>(size, 1000000000));
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
    out.rotation = a.rotation + (b.rotation - a.rotation) * t;
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
    if (!(step > 0.0) || !std::isfinite(step))
        return in;
    out.push_back(in.front());
    double carried = 0.0;
    for (size_t i = 1; i < in.size(); ++i)
    {
        const BrushSample& a = in[i - 1];
        const BrushSample& b = in[i];
        const double length = distance(a, b);
        // An infinite length would never be walked to its end.
        if (length <= 1.0e-12 || !std::isfinite(length))
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
    // No sample reaches further than the ends, so neither does the window.
    const int reachMost = clampedCount(std::ceil(sigma * 3.0), 1, countLimit(samples.size()));
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
    BrushSettings::Velocity velocity, double sensitivity)
{
    const size_t n = samples.size();
    std::vector<double> factors(n, 1.0);
    if (velocity == BrushSettings::Velocity::None || n < 3)
        return factors;
    // The speed over about ten screen pixels either side, in screen pixels
    // per millisecond; past about 2.5 (2500 a second) counts as fast.
    const int reach = clampedCount(10.0 / std::max(step * zoom, 1.0e-9), 1, countLimit(n));
    for (size_t i = 0; i < n; ++i)
    {
        const size_t a = i >= static_cast<size_t>(reach) ? i - reach : 0;
        const size_t b = std::min(n - 1, i + reach);
        const double elapsed = samples[b].time - samples[a].time;
        if (elapsed <= 0.0)
            continue;
        const double speed = (b - a) * step * zoom / elapsed;
        // At a sensitivity of 50 a speed of 2.5 has the full effect; higher
        // reaches it sooner.
        const double full = std::max(0.25, 5.0 * (1.0 - std::clamp(sensitivity, 1.0, 100.0) / 100.0));
        const double fast = clamp01(speed / full);
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

BrushSettings::AngleFunction BrushSettings::effectiveAngleFunction() const
{
    if (angleFunction == AngleFunction::Constant && useTilt && tiltEffect == TiltEffect::Angle)
        return AngleFunction::Azimuth;
    return angleFunction;
}

std::vector<Point> tipPolygon(const BrushSettings& settings)
{
    std::vector<Point> polygon;
    switch (settings.tip)
    {
    case BrushSettings::Tip::Ellipse:
    case BrushSettings::Tip::Rectangle:
        break;
    case BrushSettings::Tip::Triangle:
        // Pointing along the width, so Heading leads with the point.
        polygon = {Point(0.5, 0.0), Point(-0.5, 0.5), Point(-0.5, -0.5)};
        break;
    case BrushSettings::Tip::Diamond:
        polygon = {Point(0.5, 0.0), Point(0.0, 0.5), Point(-0.5, 0.0), Point(0.0, -0.5)};
        break;
    case BrushSettings::Tip::Hexagon:
        for (int k = 0; k < 6; ++k)
        {
            const double theta = 2.0 * kPi * k / 6.0;
            polygon.push_back(Point(0.5 * std::cos(theta), 0.5 * std::sin(theta)));
        }
        break;
    case BrushSettings::Tip::Star:
        for (int k = 0; k < 10; ++k)
        {
            const double theta = 2.0 * kPi * k / 10.0 - kPi / 2.0;
            const double radius = k % 2 == 0 ? 0.5 : 0.2;
            polygon.push_back(Point(radius * std::cos(theta), radius * std::sin(theta)));
        }
        break;
    case BrushSettings::Tip::Custom:
        polygon = settings.customTip;
        break;
    }
    return polygon;
}

bool isConvex(const std::vector<Point>& polygon)
{
    const size_t n = polygon.size();
    if (n < 4)
        return true;
    int sign = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const Point& a = polygon[i];
        const Point& b = polygon[(i + 1) % n];
        const Point& c = polygon[(i + 2) % n];
        const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::fabs(cross) < 1.0e-12)
            continue;
        const int here = cross > 0.0 ? 1 : -1;
        if (sign != 0 && here != sign)
            return false;
        sign = here;
    }
    return true;
}

namespace {

/// Douglas and Peucker: the points of \a ring that keep it within \a
/// tolerance, as a closed ring.
std::vector<Point> simplifyRing(const std::vector<Point>& ring, double tolerance)
{
    if (ring.size() < 4)
        return ring;
    std::vector<bool> keep(ring.size(), false);
    keep.front() = keep.back() = true;
    // The point furthest from the first, so the ring is split in two.
    size_t far = 0;
    double best = -1.0;
    for (size_t i = 1; i < ring.size(); ++i)
    {
        const double d = std::hypot(ring[i].x - ring[0].x, ring[i].y - ring[0].y);
        if (d > best)
        {
            best = d;
            far = i;
        }
    }
    keep[far] = true;
    std::vector<std::pair<size_t, size_t>> spans = {{0, far}, {far, ring.size() - 1}};
    while (!spans.empty())
    {
        const auto [a, b] = spans.back();
        spans.pop_back();
        if (b <= a + 1)
            continue;
        const Point& p = ring[a];
        const Point& q = ring[b];
        const double length = std::max(std::hypot(q.x - p.x, q.y - p.y), 1.0e-12);
        size_t worst = a;
        double most = 0.0;
        for (size_t i = a + 1; i < b; ++i)
        {
            const double d = std::fabs((q.x - p.x) * (p.y - ring[i].y) - (p.x - ring[i].x) * (q.y - p.y)) / length;
            if (d > most)
            {
                most = d;
                worst = i;
            }
        }
        if (most > tolerance)
        {
            keep[worst] = true;
            spans.push_back({a, worst});
            spans.push_back({worst, b});
        }
    }
    std::vector<Point> out;
    for (size_t i = 0; i + 1 < ring.size(); ++i)
        if (keep[i])
            out.push_back(ring[i]);
    return out;
}

} // namespace

std::vector<Point> traceTip(const std::vector<uint8_t>& alpha, int width, int height)
{
    std::vector<Point> polygon;
    if (width <= 0 || height <= 0 || alpha.size() < static_cast<size_t>(width) * height)
        return polygon;
    const auto covered = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && alpha[static_cast<size_t>(y) * width + x] >= 128;
    };
    // The largest covered area, found by flooding each in turn.
    std::vector<int> label(static_cast<size_t>(width) * height, 0);
    int bestLabel = 0;
    size_t bestSize = 0;
    int next = 0;
    std::vector<std::pair<int, int>> stack;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (!covered(x, y) || label[static_cast<size_t>(y) * width + x])
                continue;
            ++next;
            size_t size = 0;
            stack.push_back({x, y});
            label[static_cast<size_t>(y) * width + x] = next;
            while (!stack.empty())
            {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                ++size;
                const int around[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& step : around)
                {
                    const int nx = cx + step[0];
                    const int ny = cy + step[1];
                    if (covered(nx, ny) && !label[static_cast<size_t>(ny) * width + nx])
                    {
                        label[static_cast<size_t>(ny) * width + nx] = next;
                        stack.push_back({nx, ny});
                    }
                }
            }
            if (size > bestSize)
            {
                bestSize = size;
                bestLabel = next;
            }
        }
    }
    if (bestLabel == 0)
        return polygon;
    const auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && label[static_cast<size_t>(y) * width + x] == bestLabel;
    };
    // Its outer boundary, walked along pixel corners with the area on the
    // right: start at the top-left corner of its topmost, leftmost pixel.
    int sx = -1, sy = -1;
    for (int y = 0; y < height && sx < 0; ++y)
        for (int x = 0; x < width && sx < 0; ++x)
            if (inside(x, y))
            {
                sx = x;
                sy = y;
            }
    // Directions: 0 right, 1 down, 2 left, 3 up, along corner points.
    const int dx[4] = {1, 0, -1, 0};
    const int dy[4] = {0, 1, 0, -1};
    // The pixel to the right of a move from corner (x, y) in direction d,
    // and the one to its left.
    const auto rightOf = [&](int x, int y, int d) {
        switch (d)
        {
        case 0: return inside(x, y);
        case 1: return inside(x - 1, y);
        case 2: return inside(x - 1, y - 1);
        default: return inside(x, y - 1);
        }
    };
    const auto leftOf = [&](int x, int y, int d) {
        switch (d)
        {
        case 0: return inside(x, y - 1);
        case 1: return inside(x, y);
        case 2: return inside(x - 1, y);
        default: return inside(x - 1, y - 1);
        }
    };
    std::vector<Point> ring;
    int x = sx, y = sy, d = 0;
    const size_t limit = static_cast<size_t>(width + 1) * (height + 1) * 4;
    do
    {
        ring.push_back(Point(x, y));
        x += dx[d];
        y += dy[d];
        // Keep the area on the right: turn left if the way ahead's left is
        // covered, right if its right is not, else straight on.
        const int left = (d + 3) % 4;
        const int right = (d + 1) % 4;
        if (leftOf(x, y, d))
            d = left;
        else if (!rightOf(x, y, d))
            d = right;
    } while ((x != sx || y != sy || d != 0) && ring.size() < limit);
    ring.push_back(ring.front());
    std::vector<Point> simple = simplifyRing(ring, 0.75);
    if (simple.size() < 3)
        return polygon;
    double left = simple[0].x, right = simple[0].x, top = simple[0].y, bottom = simple[0].y;
    for (const Point& p : simple)
    {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    const double span = std::max(right - left, 1.0e-9);
    const double cx = (left + right) / 2.0;
    const double cy = (top + bottom) / 2.0;
    for (const Point& p : simple)
        polygon.push_back(Point((p.x - cx) / span, (p.y - cy) / span));
    return polygon;
}

std::string toText(const BrushSettings& settings)
{
    std::string out;
    const auto put = [&](const char* key, double value) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s=%.6g\n", key, value);
        out += buffer;
    };
    put("size", settings.size);
    put("tip", static_cast<int>(settings.tip));
    put("roundness", settings.roundness);
    put("angle", settings.angle);
    put("usePressure", settings.usePressure);
    put("minSize", settings.minSize);
    put("pressureCurve", settings.pressureCurve);
    put("useTilt", settings.useTilt);
    put("tiltEffect", static_cast<int>(settings.tiltEffect));
    put("velocity", static_cast<int>(settings.velocity));
    put("taperStart", settings.taperStart);
    put("taperEnd", settings.taperEnd);
    put("smoothing", settings.smoothing);
    put("precision", settings.precision);
    put("stabilizer", settings.stabilizer);
    put("invertPressure", settings.invertPressure);
    put("invertTilt", settings.invertTilt);
    put("velocitySensitivity", settings.velocitySensitivity);
    put("taperUnits", static_cast<int>(settings.taperUnits));
    put("fadeStart", settings.fadeStart);
    put("fadeEnd", settings.fadeEnd);
    put("sizeFollowsCamera", settings.sizeFollowsCamera);
    put("angleFunction", static_cast<int>(settings.angleFunction));
    put("tiltStretch", settings.tiltStretch);
    if (!settings.customTip.empty())
    {
        out += "customTip=";
        char buffer[64];
        for (size_t i = 0; i < settings.customTip.size(); ++i)
        {
            std::snprintf(buffer, sizeof(buffer), "%s%.5g,%.5g", i ? ";" : "", settings.customTip[i].x,
                settings.customTip[i].y);
            out += buffer;
        }
        out += "\n";
    }
    if (!settings.tipSource.empty())
        out += "tipSource=" + settings.tipSource + "\n";
    return out;
}

void fromText(const std::string& text, BrushSettings& into)
{
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        const double number = std::atof(value.c_str());
        // A setting that is not a number, or is infinite, is left as it was.
        if (!std::isfinite(number) && key != "tipSource" && key != "customTip")
            continue;
        const int whole = static_cast<int>(std::round(std::clamp(number, -1.0e9, 1.0e9)));
        if (key == "size") into.size = std::clamp(number, 0.1, 1000.0);
        else if (key == "tip") into.tip = static_cast<BrushSettings::Tip>(std::clamp(whole, 0, 6));
        else if (key == "roundness") into.roundness = std::clamp(number, 1.0, 100.0);
        else if (key == "angle") into.angle = number;
        else if (key == "usePressure") into.usePressure = whole != 0;
        else if (key == "minSize") into.minSize = std::clamp(number, 1.0, 100.0);
        else if (key == "pressureCurve") into.pressureCurve = std::max(0.1, number);
        else if (key == "useTilt") into.useTilt = whole != 0;
        else if (key == "tiltEffect") into.tiltEffect = static_cast<BrushSettings::TiltEffect>(std::clamp(whole, 0, 1));
        else if (key == "velocity") into.velocity = static_cast<BrushSettings::Velocity>(std::clamp(whole, 0, 2));
        else if (key == "taperStart") into.taperStart = std::max(0.0, number);
        else if (key == "taperEnd") into.taperEnd = std::max(0.0, number);
        else if (key == "smoothing") into.smoothing = std::clamp(whole, 0, 100);
        else if (key == "precision") into.precision = std::clamp(whole, 0, 100);
        else if (key == "stabilizer") into.stabilizer = std::clamp(number, 0.0, 60.0);
        else if (key == "invertPressure") into.invertPressure = whole != 0;
        else if (key == "invertTilt") into.invertTilt = whole != 0;
        else if (key == "velocitySensitivity") into.velocitySensitivity = std::clamp(number, 1.0, 100.0);
        else if (key == "taperUnits") into.taperUnits = static_cast<BrushSettings::TaperUnits>(std::clamp(whole, 0, 1));
        else if (key == "fadeStart") into.fadeStart = std::max(0.0, number);
        else if (key == "fadeEnd") into.fadeEnd = std::max(0.0, number);
        else if (key == "sizeFollowsCamera") into.sizeFollowsCamera = whole != 0;
        else if (key == "angleFunction")
            into.angleFunction = static_cast<BrushSettings::AngleFunction>(std::clamp(whole, 0, 3));
        else if (key == "tiltStretch") into.tiltStretch = std::clamp(number, 0.0, 500.0);
        else if (key == "tipSource") into.tipSource = value;
        else if (key == "customTip")
        {
            into.customTip.clear();
            size_t at = 0;
            while (at < value.size())
            {
                size_t stop = value.find(';', at);
                if (stop == std::string::npos)
                    stop = value.size();
                const std::string pair = value.substr(at, stop - at);
                const size_t comma = pair.find(',');
                if (comma != std::string::npos)
                {
                    const Point point(std::atof(pair.substr(0, comma).c_str()),
                        std::atof(pair.substr(comma + 1).c_str()));
                    if (std::isfinite(point.x) && std::isfinite(point.y))
                        into.customTip.push_back(point);
                }
                at = stop + 1;
            }
        }
    }
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
    for (BrushSample sample : input)
    {
        // A reading off the end of the numbers is a glitch, not a place: it
        // is dropped, and a reading the pen could not have meant falls back
        // to what it reads at rest.
        if (!std::isfinite(sample.x) || !std::isfinite(sample.y))
            continue;
        if (!std::isfinite(sample.pressure))
            sample.pressure = 1.0;
        if (!std::isfinite(sample.tiltX))
            sample.tiltX = 0.0;
        if (!std::isfinite(sample.tiltY))
            sample.tiltY = 0.0;
        if (!std::isfinite(sample.rotation))
            sample.rotation = 0.0;
        if (!std::isfinite(sample.time))
            sample.time = raw.empty() ? 0.0 : raw.back().time;
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
    if (raw.empty())
        return result;

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

    std::vector<double> speedFactors =
        velocityFactors(line, step, zoom, settings.velocity, settings.velocitySensitivity);
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

    const bool pixels = settings.taperUnits == BrushSettings::TaperUnits::Pixels;
    const double startTaper = pixels ? std::min(total * 0.5, std::max(0.0, settings.taperStart))
                                     : std::min(0.5, std::max(0.0, settings.taperStart) / 100.0) * total;
    const double endTaper = pixels ? std::min(total * 0.5, std::max(0.0, settings.taperEnd))
                                   : std::min(0.5, std::max(0.0, settings.taperEnd) / 100.0) * total;
    const double fadeIn = std::max(0.0, settings.fadeStart);
    const double fadeOut = std::max(0.0, settings.fadeEnd);
    const BrushSettings::AngleFunction angleFunction = settings.effectiveAngleFunction();
    // The library's shapes and custom tips, shared by every dab.
    std::shared_ptr<const std::vector<Point>> shape;
    {
        std::vector<Point> polygon = tipPolygon(settings);
        if (polygon.size() >= 3)
            shape = std::make_shared<const std::vector<Point>>(std::move(polygon));
    }
    // The direction of travel, over about two screen pixels either side.
    const int headingReach = clampedCount(std::round(2.0 / std::max(stepPixels, 1.0e-9)), 1, countLimit(line.size()));
    const double minimum = clamp01(settings.minSize / 100.0);
    const double curve = std::max(0.1, settings.pressureCurve);
    const double roundness = std::max(1.0, std::min(100.0, settings.roundness)) / 100.0;
    const double baseAngle = settings.angle * kPi / 180.0;
    const bool rectangular = settings.tip == BrushSettings::Tip::Rectangle;

    const auto dabAt = [&](size_t i) {
        const BrushSample& s = line[i];
        double width = std::max(settings.size, 0.1);
        const double pressure = settings.invertPressure ? 1.0 - clamp01(s.pressure) : clamp01(s.pressure);
        if (usePressure)
            width *= minimum + (1.0 - minimum) * std::pow(pressure, curve);
        width *= speedFactors[i];
        width *= taper(along[i], startTaper);
        width *= taper(total - along[i], endTaper);
        double angle = baseAngle;
        const double rawLean = clamp01(std::hypot(s.tiltX, s.tiltY) / 60.0);
        const double lean = settings.invertTilt ? 1.0 - rawLean : rawLean;
        if (settings.useTilt && settings.tiltEffect == BrushSettings::TiltEffect::Width)
            width *= 1.0 + 1.5 * lean;
        switch (angleFunction)
        {
        case BrushSettings::AngleFunction::Azimuth:
            if (rawLean > 0.08)
                angle += std::atan2(s.tiltY, s.tiltX);
            break;
        case BrushSettings::AngleFunction::Heading:
        {
            const size_t a = i >= static_cast<size_t>(headingReach) ? i - headingReach : 0;
            const size_t b = std::min(line.size() - 1, i + headingReach);
            if (b > a)
                angle += std::atan2(line[b].y - line[a].y, line[b].x - line[a].x);
            break;
        }
        case BrushSettings::AngleFunction::Twist:
            angle += s.rotation * kPi / 180.0;
            break;
        case BrushSettings::AngleFunction::Constant:
            break;
        }
        const double height = std::max(width * roundness, 0.1 * pixel);
        // A lean stretches the tip along its width, as a tilted marker's
        // footprint lengthens.
        if (settings.tiltStretch > 0.0)
            width *= 1.0 + settings.tiltStretch / 100.0 * rawLean;
        width = std::max(width, 0.1 * pixel);
        BrushDab dab;
        dab.x = s.x;
        dab.y = s.y;
        dab.width = width;
        dab.height = height;
        dab.angle = angle;
        dab.rectangular = rectangular;
        dab.shape = shape;
        dab.pressure = clamp01(s.pressure);
        double opacity = 1.0;
        if (fadeIn > 0.0 && along[i] < fadeIn)
            opacity *= along[i] / fadeIn;
        if (fadeOut > 0.0 && total - along[i] < fadeOut)
            opacity *= (total - along[i]) / fadeOut;
        dab.opacity = clamp01(opacity);
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
        const double spacing = std::max(0.5 * pixel, std::min(3.0 * pixel, 0.25 * std::max(last.width, last.height)));
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
    if (dab.shape && dab.shape->size() >= 3)
    {
        for (const Point& point : *dab.shape)
            place(point.x * dab.width, point.y * dab.height);
        return points;
    }
    if (dab.rectangular)
    {
        place(-a, -b);
        place(a, -b);
        place(a, b);
        place(-a, b);
        return points;
    }
    const int count = clampedCount(std::ceil(std::max(dab.width, dab.height) * std::max(zoom, 1.0e-6) * 0.6), 8, 64);
    for (int k = 0; k < count; ++k)
    {
        const double theta = 2.0 * kPi * k / count;
        place(a * std::cos(theta), b * std::sin(theta));
    }
    return points;
}

std::vector<Point> convexHull(std::vector<Point> points)
{
    // A point that is not a number cannot be ordered, and sorting with one
    // is undefined.
    points.erase(std::remove_if(points.begin(), points.end(), [](const Point& p) {
        return !std::isfinite(p.x) || !std::isfinite(p.y);
    }), points.end());
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

std::vector<std::vector<Point>> triangulate(const std::vector<Point>& given)
{
    // Ear clipping: a corner whose triangle turns the polygon's way and
    // holds no other corner is cut off, until three are left.
    std::vector<std::vector<Point>> triangles;
    std::vector<Point> polygon = given;
    if (polygon.size() < 3)
        return triangles;
    double area = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i)
    {
        const Point& a = polygon[i];
        const Point& b = polygon[(i + 1) % polygon.size()];
        area += a.x * b.y - b.x * a.y;
    }
    const double turn = area >= 0.0 ? 1.0 : -1.0;
    const auto cross = [](const Point& o, const Point& a, const Point& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    size_t guard = polygon.size() * polygon.size() + 8;
    while (polygon.size() > 3 && guard-- > 0)
    {
        bool cut = false;
        const size_t n = polygon.size();
        for (size_t i = 0; i < n; ++i)
        {
            const Point& a = polygon[(i + n - 1) % n];
            const Point& b = polygon[i];
            const Point& c = polygon[(i + 1) % n];
            if (cross(a, b, c) * turn <= 1.0e-12)
                continue;
            bool holds = false;
            for (size_t k = 0; k < n && !holds; ++k)
            {
                if (k == i || k == (i + n - 1) % n || k == (i + 1) % n)
                    continue;
                const Point& p = polygon[k];
                holds = cross(a, b, p) * turn >= 0.0 && cross(b, c, p) * turn >= 0.0 && cross(c, a, p) * turn >= 0.0;
            }
            if (holds)
                continue;
            triangles.push_back({a, b, c});
            polygon.erase(polygon.begin() + static_cast<long long>(i));
            cut = true;
            break;
        }
        if (!cut)
            break;
    }
    if (polygon.size() == 3)
        triangles.push_back(polygon);
    return triangles;
}

std::vector<std::vector<Point>> sweep(const std::vector<BrushDab>& dabs, double zoom)
{
    std::vector<std::vector<Point>> polygons;
    if (dabs.empty())
        return polygons;
    // A tip that is not convex would be filled in by the hulls. It is cut
    // into triangles, each swept on its own: the area a union sweeps is the
    // union of what its parts sweep.
    if (dabs.front().shape && !isConvex(*dabs.front().shape))
    {
        const std::vector<std::vector<Point>> parts = triangulate(*dabs.front().shape);
        for (const std::vector<Point>& part : parts)
        {
            std::vector<BrushDab> pieces = dabs;
            const auto piece = std::make_shared<const std::vector<Point>>(part);
            for (BrushDab& dab : pieces)
                dab.shape = piece;
            const std::vector<std::vector<Point>> swept = sweep(pieces, zoom);
            polygons.insert(polygons.end(), swept.begin(), swept.end());
        }
        return polygons;
    }
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
