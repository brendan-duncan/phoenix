#include "shape_operators.h"

#include "../data/shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace fla::shapeops {

const char* const kAttribute = "phoenixShapeOperators";

namespace {

using behaviors::Applied;
using behaviors::Definition;
using behaviors::ParamSpec;

ParamSpec number(const char* name, const char* label, double value, double min, double max, double step)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "number";
    spec.value = value;
    spec.min = min;
    spec.max = max;
    spec.step = step;
    char text[32];
    std::snprintf(text, sizeof(text), "%.10g", value);
    spec.text = text;
    return spec;
}

ParamSpec checkbox(const char* name, const char* label, bool value)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "checkbox";
    spec.value = value ? 1.0 : 0.0;
    spec.text = value ? "true" : "false";
    return spec;
}

ParamSpec seed()
{
    ParamSpec spec = number("seed", "Seed", 1, 0, 9999, 1);
    spec.type = "seed";
    return spec;
}

double param(const Applied& applied, const std::string& name, int frame, double fallback)
{
    const std::string text = applied.valueAt(name, frame, std::string());
    if (text.empty())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : value;
}

bool flag(const Applied& applied, const std::string& name, int frame, bool fallback)
{
    const std::string text = applied.valueAt(name, frame, std::string());
    return text.empty() ? fallback : text == "true" || text == "1";
}

/// A number from 0 to 1 for \a seed and \a n, as behaviors' random().
double hash(int seed, int64_t n)
{
    uint32_t h = static_cast<uint32_t>((static_cast<uint32_t>(seed) ^ 0x9e3779b9u) * 0x85ebca6bu) ^
        static_cast<uint32_t>(static_cast<uint64_t>(n) * 0xc2b2ae35u);
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    h ^= h >> 16;
    return h / 4294967296.0;
}

/// Smooth noise from -1 to 1 over the plane.
double noise(int seed, double x, double y)
{
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const auto corner = [seed](double cx, double cy) {
        return hash(seed, static_cast<int64_t>(cx) * 73856093 + static_cast<int64_t>(cy) * 19349663) * 2.0 - 1.0;
    };
    const double sx = x - fx;
    const double sy = y - fy;
    const double u = sx * sx * (3 - 2 * sx);
    const double v = sy * sy * (3 - 2 * sy);
    const double a = corner(fx, fy) + (corner(fx + 1, fy) - corner(fx, fy)) * u;
    const double b = corner(fx, fy + 1) + (corner(fx + 1, fy + 1) - corner(fx, fy + 1)) * u;
    return a + (b - a) * v;
}

double distance(const Point& a, const Point& b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

/// \a line's points with the closing segment back to its start, for a
/// closed one.
std::vector<Point> walked(const Line& line)
{
    std::vector<Point> points = line.points;
    if (line.closed && points.size() > 1 && distance(points.front(), points.back()) > 1e-9)
        points.push_back(points.front());
    return points;
}

double lengthOf(const std::vector<Point>& points)
{
    double length = 0.0;
    for (size_t i = 1; i < points.size(); ++i)
        length += distance(points[i - 1], points[i]);
    return length;
}

/// \a line's points every \a spacing along it, with the direction across it
/// at each (a unit normal), and how far along each is.
struct Sample
{
    Point at;
    Point normal;
    double along = 0.0;
};

std::vector<Sample> resample(const Line& line, double spacing)
{
    std::vector<Sample> samples;
    const std::vector<Point> points = walked(line);
    const double length = lengthOf(points);
    if (points.size() < 2 || length <= 0.0)
        return samples;
    const int count = std::max(line.closed ? 3 : 2, static_cast<int>(std::ceil(length / std::max(0.5, spacing))));
    const int steps = line.closed ? count : count - 1;
    size_t segment = 1;
    double walkedSoFar = 0.0;
    for (int i = 0; i <= (line.closed ? count - 1 : steps); ++i)
    {
        const double want = length * i / steps;
        while (segment + 1 < points.size() && walkedSoFar + distance(points[segment - 1], points[segment]) < want)
        {
            walkedSoFar += distance(points[segment - 1], points[segment]);
            ++segment;
        }
        const Point& a = points[segment - 1];
        const Point& b = points[segment];
        const double piece = std::max(1e-12, distance(a, b));
        const double t = std::clamp((want - walkedSoFar) / piece, 0.0, 1.0);
        Sample sample;
        sample.at = Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        sample.normal = Point(-(b.y - a.y) / piece, (b.x - a.x) / piece);
        sample.along = want;
        samples.push_back(sample);
    }
    return samples;
}

/// The parts of \a lines between \a from and \a to along all of them, one
/// after another (distances from the first line's start).
std::vector<Line> between(const std::vector<Line>& lines, double from, double to)
{
    std::vector<Line> out;
    double offset = 0.0;
    for (const Line& line : lines)
    {
        const std::vector<Point> points = walked(line);
        const double length = lengthOf(points);
        const double a = std::max(from, offset);
        const double b = std::min(to, offset + length);
        if (b > a && points.size() > 1)
        {
            Line part;
            double here = offset;
            for (size_t i = 1; i < points.size(); ++i)
            {
                const double piece = distance(points[i - 1], points[i]);
                const double next = here + piece;
                const auto pointAt = [&](double d) {
                    const double t = piece > 0 ? (d - here) / piece : 0.0;
                    return Point(points[i - 1].x + (points[i].x - points[i - 1].x) * t,
                        points[i - 1].y + (points[i].y - points[i - 1].y) * t);
                };
                if (next >= a && here <= b)
                {
                    if (part.points.empty())
                        part.points.push_back(pointAt(std::max(a, here)));
                    part.points.push_back(pointAt(std::min(b, next)));
                }
                here = next;
            }
            if (part.points.size() > 1)
                out.push_back(part);
        }
        offset += length;
    }
    return out;
}

std::vector<Line> trim(const std::vector<Line>& lines, const Applied& applied, int frame)
{
    double total = 0.0;
    for (const Line& line : lines)
        total += lengthOf(walked(line));
    if (total <= 0.0)
        return lines;
    const double start = std::clamp(param(applied, "start", frame, 0.0), 0.0, 100.0);
    const double end = std::clamp(param(applied, "end", frame, 100.0), 0.0, 100.0);
    const double offset = param(applied, "offset", frame, 0.0);
    if (end <= start)
        return {};
    if (start <= 0.0 && end >= 100.0)
        return lines;
    // The window slides with the offset, round past the end to the start.
    double a = start + offset;
    a -= 100.0 * std::floor(a / 100.0);
    const double b = a + (end - start);
    std::vector<Line> out = between(lines, a / 100.0 * total, std::min(b, 100.0) / 100.0 * total);
    if (b > 100.0)
    {
        const std::vector<Line> wrapped = between(lines, 0.0, (b - 100.0) / 100.0 * total);
        out.insert(out.end(), wrapped.begin(), wrapped.end());
    }
    return out;
}

/// Each line's points moved across it by \a offset(sample, line index).
template <typename F>
std::vector<Line> displaced(const std::vector<Line>& lines, double spacing, F offset)
{
    std::vector<Line> out;
    for (size_t l = 0; l < lines.size(); ++l)
    {
        const std::vector<Sample> samples = resample(lines[l], spacing);
        if (samples.empty())
        {
            out.push_back(lines[l]);
            continue;
        }
        Line moved;
        moved.closed = lines[l].closed;
        for (const Sample& sample : samples)
        {
            const double by = offset(sample, l);
            moved.points.push_back(Point(sample.at.x + sample.normal.x * by, sample.at.y + sample.normal.y * by));
        }
        out.push_back(moved);
    }
    return out;
}

} // namespace

const std::vector<Definition>& definitions()
{
    static const std::vector<Definition> list = [] {
        std::vector<Definition> made;
        Definition trimDef;
        trimDef.id = "trim";
        trimDef.name = "Trim Paths";
        trimDef.description = "Draws only part of the shape's lines: key Start or End from one to the other and the "
                              "line draws itself on, or key the Offset to send a stretch of it round.";
        trimDef.builtIn = true;
        trimDef.params = {number("start", "Start (percent)", 0, 0, 100, 0.5), number("end", "End (percent)", 100, 0, 100, 0.5),
            number("offset", "Offset (percent)", 0, -1000, 1000, 0.5)};
        made.push_back(trimDef);

        Definition wiggleDef;
        wiggleDef.id = "wigglepath";
        wiggleDef.name = "Wiggle Path";
        wiggleDef.description = "Shakes the shape's outlines and lines smoothly at random, for a boiling, hand-drawn "
                                "look or a wobbling jelly.";
        wiggleDef.builtIn = true;
        wiggleDef.params = {number("amount", "Amount (pixels)", 3, 0, 200, 0.5),
            number("wavelength", "Wave length (pixels)", 30, 2, 1000, 1),
            number("speed", "Changes per second", 4, 0, 60, 0.5), number("detail", "Detail (pixels between points)", 4, 1, 100, 1),
            checkbox("steps", "Change on whole frames only (boil)", false), seed()};
        made.push_back(wiggleDef);

        Definition zigzagDef;
        zigzagDef.id = "zigzag";
        zigzagDef.name = "Zig-zag";
        zigzagDef.description = "Puts teeth or waves along the shape's outlines and lines; key the shift to make them "
                                "travel.";
        zigzagDef.builtIn = true;
        zigzagDef.params = {number("size", "Size (pixels)", 5, -200, 200, 0.5), number("ridges", "Ridges per 100 pixels", 8, 0.1, 200, 0.1),
            number("shift", "Shift (ridges)", 0, -1000, 1000, 0.05), checkbox("smooth", "Smooth waves", false)};
        made.push_back(zigzagDef);
        return made;
    }();
    return list;
}

const Definition* find(const std::string& id)
{
    for (const Definition& definition : definitions())
        if (definition.id == id)
            return &definition;
    return nullptr;
}

Applied defaults(const std::string& id, int seedValue)
{
    Applied applied;
    applied.type = id;
    if (const Definition* definition = find(id))
        for (const ParamSpec& spec : definition->params)
            applied.params[spec.name] = spec.type == "seed" ? std::to_string(seedValue) : spec.text;
    return applied;
}

std::vector<Applied> of(const Shape& shape)
{
    return behaviors::listOf(shape, kAttribute);
}

bool any(const Shape& shape)
{
    for (const Applied& applied : of(shape))
        if (applied.enabled && find(applied.type))
            return true;
    return false;
}

std::vector<Line> apply(const std::vector<Line>& lines, bool stroke, const std::vector<Applied>& list, int frame,
    double frameRate)
{
    std::vector<Line> out = lines;
    const double rate = frameRate > 0.0 ? frameRate : 24.0;
    for (const Applied& applied : list)
    {
        if (!applied.enabled)
            continue;
        if (applied.type == "trim")
        {
            if (stroke)
                out = trim(out, applied, frame);
        }
        else if (applied.type == "wigglepath")
        {
            const double amount = param(applied, "amount", frame, 3.0);
            const double wavelength = std::max(1.0, param(applied, "wavelength", frame, 30.0));
            const double speed = param(applied, "speed", frame, 4.0);
            const double detail = std::max(1.0, param(applied, "detail", frame, 4.0));
            const int seedValue = static_cast<int>(param(applied, "seed", frame, 1.0));
            // Boiling: the wiggle holds for a frame, then jumps.
            const double time = flag(applied, "steps", frame, false)
                ? std::floor(frame * speed / rate)
                : frame * speed / rate;
            out = displaced(out, detail, [&](const Sample& sample, size_t line) {
                return amount * noise(seedValue + static_cast<int>(line) * 101, sample.along / wavelength, time);
            });
        }
        else if (applied.type == "zigzag")
        {
            const double size = param(applied, "size", frame, 5.0);
            const double ridges = std::max(0.01, param(applied, "ridges", frame, 8.0));
            const double shift = param(applied, "shift", frame, 0.0);
            const bool smooth = flag(applied, "smooth", frame, false);
            const double spacing = smooth ? std::max(1.0, 100.0 / ridges / 8.0) : 100.0 / ridges / 2.0;
            out = displaced(out, spacing, [&](const Sample& sample, size_t) {
                const double phase = sample.along * ridges / 100.0 + shift;
                if (smooth)
                    return size * std::sin(phase * 2.0 * 3.14159265358979323846);
                // Teeth: up and down at each half ridge.
                return std::fmod(std::floor(phase * 2.0 + 1e-9), 2.0) == 0.0 ? size : -size;
            });
        }
    }
    return out;
}

} // namespace fla::shapeops
