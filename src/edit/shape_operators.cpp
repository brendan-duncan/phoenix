#include "shape_operators.h"

#include "shape_merge.h"
#include "vector_import.h"

#include "../data/shape.h"
#include "../data/stroke_style.h"

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

ParamSpec choice(const char* name, const char* label, std::vector<std::string> options)
{
    ParamSpec spec;
    spec.name = name;
    spec.label = label;
    spec.type = "choice";
    spec.choices = std::move(options);
    spec.text = spec.choices.empty() ? std::string() : spec.choices.front();
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

/// \a lines cut into dashes \a dash long with gaps of \a gap between,
/// starting \a offset along: each line on its own, a closed one round.
std::vector<Line> dashed(const std::vector<Line>& lines, double dash, double gap, double offset)
{
    std::vector<Line> out;
    const double period = dash + gap;
    if (dash <= 0.0 || period <= 0.0)
        return out;
    for (const Line& line : lines)
    {
        const std::vector<Point> points = walked(line);
        const double length = lengthOf(points);
        if (length <= 0.0)
            continue;
        Line open;
        open.points = points;
        // Where the first dash starts, at or before the line's start.
        double start = std::fmod(offset, period);
        if (start > 0.0)
            start -= period;
        for (double at = start; at < length; at += period)
        {
            const std::vector<Line> piece = between({open}, std::max(0.0, at), std::min(length, at + dash));
            out.insert(out.end(), piece.begin(), piece.end());
        }
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


constexpr double kPi = 3.14159265358979323846;

/// Twice the signed area of a closed line: positive when it runs one way
/// round, negative the other.
double signedArea(const std::vector<Point>& points)
{
    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area;
}

/// Whether \a point is inside the closed \a line (even-odd).
bool inside(const Point& point, const Line& line)
{
    bool in = false;
    const std::vector<Point>& p = line.points;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if (((p[i].y > point.y) != (p[j].y > point.y)) &&
            point.x < (p[j].x - p[i].x) * (point.y - p[i].y) / (p[j].y - p[i].y) + p[i].x)
            in = !in;
    return in;
}

/// The points of \a line without repeats (a closed line without its end
/// back at the start).
std::vector<Point> distinctPoints(const Line& line)
{
    std::vector<Point> out;
    for (const Point& point : line.points)
        if (out.empty() || distance(out.back(), point) > 1e-6)
            out.push_back(point);
    if (line.closed && out.size() > 1 && distance(out.front(), out.back()) <= 1e-6)
        out.pop_back();
    return out;
}

/// How far the line turns at point \a i (radians, 0 for straight on).
double turnAt(const std::vector<Point>& p, size_t i, bool closed)
{
    const size_t n = p.size();
    if (n < 3 || (!closed && (i == 0 || i + 1 >= n)))
        return 0.0;
    const Point& a = p[(i + n - 1) % n];
    const Point& b = p[i];
    const Point& c = p[(i + 1) % n];
    const double a1 = std::atan2(b.y - a.y, b.x - a.x);
    const double a2 = std::atan2(c.y - b.y, c.x - b.x);
    double d = a2 - a1;
    while (d > kPi)
        d -= 2.0 * kPi;
    while (d < -kPi)
        d += 2.0 * kPi;
    return std::fabs(d);
}

/// The lines grown outward by \a by (shrunk with less than 0): closed
/// outlines away from their insides, holes toward theirs; an open line
/// moved across.
std::vector<Line> offsetLines(const std::vector<Line>& lines, double by)
{
    std::vector<Line> out;
    for (size_t l = 0; l < lines.size(); ++l)
    {
        const std::vector<Point> p = distinctPoints(lines[l]);
        const size_t n = p.size();
        if (n < 2)
        {
            out.push_back(lines[l]);
            continue;
        }
        double side = 1.0;
        if (lines[l].closed && n > 2)
        {
            // The normal (-dy, dx) points to the left of the way it runs:
            // outward for one winding, inward for the other; a hole the
            // other way again.
            side = signedArea(p) > 0.0 ? -1.0 : 1.0;
            int around = 0;
            for (size_t o = 0; o < lines.size(); ++o)
                if (o != l && lines[o].closed && lines[o].points.size() > 2 && inside(p.front(), lines[o]))
                    ++around;
            if (around % 2 == 1)
                side = -side;
        }
        const auto normalOf = [&](const Point& a, const Point& b) {
            const double length = std::max(1e-12, distance(a, b));
            return Point(-(b.y - a.y) / length * side, (b.x - a.x) / length * side);
        };
        Line moved;
        moved.closed = lines[l].closed;
        for (size_t i = 0; i < n; ++i)
        {
            const bool first = i == 0 && !moved.closed;
            const bool last = i + 1 == n && !moved.closed;
            const Point before = first ? normalOf(p[0], p[1]) : normalOf(p[(i + n - 1) % n], p[i]);
            const Point after = last ? normalOf(p[n - 2], p[n - 1]) : normalOf(p[i], p[(i + 1) % n]);
            // A mitered corner: along the two normals' middle, far enough
            // to keep both sides at the distance, within a limit.
            Point mid(before.x + after.x, before.y + after.y);
            const double length = std::hypot(mid.x, mid.y);
            if (length < 1e-9)
                mid = before;
            else
                mid = Point(mid.x / length, mid.y / length);
            const double cosHalf = std::max(0.25, mid.x * after.x + mid.y * after.y);
            moved.points.push_back(Point(p[i].x + mid.x * by / cosHalf, p[i].y + mid.y * by / cosHalf));
        }
        out.push_back(moved);
    }
    return out;
}

/// Each corner sharper than a slight bend made round, cut back up to
/// \a radius along each side.
std::vector<Line> roundCorners(const std::vector<Line>& lines, double radius)
{
    std::vector<Line> out;
    for (const Line& line : lines)
    {
        const std::vector<Point> p = distinctPoints(line);
        const size_t n = p.size();
        if (n < 3 || radius <= 0.0)
        {
            out.push_back(line);
            continue;
        }
        Line rounded;
        rounded.closed = line.closed;
        for (size_t i = 0; i < n; ++i)
        {
            if (turnAt(p, i, line.closed) < 0.3)
            {
                rounded.points.push_back(p[i]);
                continue;
            }
            const Point& a = p[(i + n - 1) % n];
            const Point& b = p[i];
            const Point& c = p[(i + 1) % n];
            const double inLength = distance(a, b);
            const double outLength = distance(b, c);
            const double cut = std::min({radius, inLength / 2.0, outLength / 2.0});
            const Point from(b.x + (a.x - b.x) / inLength * cut, b.y + (a.y - b.y) / inLength * cut);
            const Point to(b.x + (c.x - b.x) / outLength * cut, b.y + (c.y - b.y) / outLength * cut);
            // A curve from one cut to the other, bent toward the corner.
            constexpr int kSteps = 8;
            for (int s = 0; s <= kSteps; ++s)
            {
                const double u = static_cast<double>(s) / kSteps;
                const double w0 = (1 - u) * (1 - u), w1 = 2 * u * (1 - u), w2 = u * u;
                rounded.points.push_back(Point(w0 * from.x + w1 * b.x + w2 * to.x, w0 * from.y + w1 * b.y + w2 * to.y));
            }
        }
        out.push_back(rounded);
    }
    return out;
}

/// The middle of all the lines' points.
Point centerOf(const std::vector<Line>& lines)
{
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const Line& line : lines)
        for (const Point& point : line.points)
        {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x);
            maxY = std::max(maxY, point.y);
        }
    return minX > maxX ? Point(0, 0) : Point((minX + maxX) / 2.0, (minY + maxY) / 2.0);
}

/// Pucker (less than 0) and Bloat: the stretches between the corners drawn
/// toward the middle or pushed out from it, most at their halfway points.
std::vector<Line> puckerBloat(const std::vector<Line>& lines, double amount)
{
    const Point center = centerOf(lines);
    std::vector<Line> out;
    for (const Line& line : lines)
    {
        const std::vector<Point> p = distinctPoints(line);
        const size_t n = p.size();
        if (n < 2)
        {
            out.push_back(line);
            continue;
        }
        // The corners; a smooth outline has four, evenly round it.
        std::vector<size_t> corners;
        for (size_t i = 0; i < n; ++i)
            if ((!line.closed && (i == 0 || i + 1 == n)) || turnAt(p, i, line.closed) >= 0.3)
                corners.push_back(i);
        if (corners.size() < 2)
        {
            corners.clear();
            const size_t parts = line.closed ? 4 : 1;
            for (size_t k = 0; k < parts; ++k)
                corners.push_back(k * n / parts);
            if (!line.closed)
                corners.push_back(n - 1);
        }
        Line moved;
        moved.closed = line.closed;
        const size_t stretches = line.closed ? corners.size() : corners.size() - 1;
        for (size_t s = 0; s < stretches; ++s)
        {
            // The stretch's points, closely spaced, from one corner to the next.
            std::vector<Point> run;
            for (size_t i = corners[s];; i = (i + 1) % n)
            {
                run.push_back(p[i]);
                if (i == corners[(s + 1) % corners.size()] && run.size() > 1)
                    break;
            }
            Line stretch;
            stretch.points = run;
            const std::vector<Sample> samples = resample(stretch, 3.0);
            const double length = samples.empty() ? 0.0 : samples.back().along;
            for (size_t k = 0; k + 1 < samples.size() || (k + 1 == samples.size() && s + 1 == stretches && !line.closed); ++k)
            {
                const double u = length > 0.0 ? samples[k].along / length : 0.0;
                const double push = amount / 100.0 * std::sin(u * kPi);
                const Point& at = samples[k].at;
                moved.points.push_back(Point(at.x + (at.x - center.x) * push, at.y + (at.y - center.y) * push));
            }
        }
        out.push_back(moved);
    }
    return out;
}

/// The lines and \a copies more of them, each moved, turned and scaled
/// from the one before, about the middle of them all.
std::vector<Line> repeated(const std::vector<Line>& lines, int copies, double moveX, double moveY, double turn, double scale)
{
    const Point center = centerOf(lines);
    std::vector<Line> out = lines;
    for (int k = 1; k <= copies; ++k)
    {
        const double angle = turn * k * kPi / 180.0;
        const double size = std::pow(scale, k);
        const double c = std::cos(angle) * size;
        const double s = std::sin(angle) * size;
        for (const Line& line : lines)
        {
            Line copy;
            copy.closed = line.closed;
            for (const Point& point : line.points)
            {
                const double x = point.x - center.x;
                const double y = point.y - center.y;
                copy.points.push_back(Point(center.x + x * c - y * s + moveX * k, center.y + x * s + y * c + moveY * k));
            }
            out.push_back(copy);
        }
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

        Definition offsetDef;
        offsetDef.id = "offset";
        offsetDef.name = "Offset Path";
        offsetDef.description = "Grows the shape's outlines outward, or shrinks them with less than 0, keeping their "
                                "corners: an outline that swells, a shape that shrinks away.";
        offsetDef.builtIn = true;
        offsetDef.params = {number("amount", "Amount (pixels)", 6, -500, 500, 0.5)};
        made.push_back(offsetDef);

        Definition roundDef;
        roundDef.id = "round";
        roundDef.name = "Round Corners";
        roundDef.description = "Rounds the shape's sharp corners, up to a radius.";
        roundDef.builtIn = true;
        roundDef.params = {number("radius", "Radius (pixels)", 12, 0, 1000, 0.5)};
        made.push_back(roundDef);

        Definition bloatDef;
        bloatDef.id = "puckerbloat";
        bloatDef.name = "Pucker and Bloat";
        bloatDef.description = "Pushes the sides between the corners out from the shape's middle (bloat), or draws "
                               "them in (pucker, less than 0): a square that puffs up, a star that sharpens.";
        bloatDef.builtIn = true;
        bloatDef.params = {number("amount", "Amount (percent)", 30, -200, 200, 1)};
        made.push_back(bloatDef);

        Definition dashesDef;
        dashesDef.id = "dashes";
        dashesDef.name = "Dashes";
        dashesDef.description = "Cuts the shape's lines into dashes, each ended as the stroke's caps end it; key the "
                                "Offset to send them marching along.";
        dashesDef.builtIn = true;
        dashesDef.params = {number("dash", "Dash (pixels)", 20, 0, 2000, 0.5), number("gap", "Gap (pixels)", 10, 0, 2000, 0.5),
            number("offset", "Offset (pixels)", 0, -100000, 100000, 0.5)};
        made.push_back(dashesDef);

        Definition taperDef;
        taperDef.id = "taper";
        taperDef.name = "Taper";
        taperDef.description = "Thins or swells the shape's lines along their length, from the start's width to the "
                               "end's, with a wave that can travel down them: a brush stroke, a tail, a rope.";
        taperDef.builtIn = true;
        taperDef.params = {number("start", "Start width (percent)", 100, 0, 1000, 1),
            number("end", "End width (percent)", 0, 0, 1000, 1), number("wave", "Wave (percent)", 0, 0, 1000, 1),
            number("wavelength", "Wave length (pixels)", 40, 1, 5000, 1),
            number("speed", "Waves a second", 0, -60, 60, 0.1)};
        made.push_back(taperDef);

        Definition twistDef;
        twistDef.id = "twist";
        twistDef.name = "Twist";
        twistDef.description = "Turns the shape about its middle, more near the middle than at the edge, curling "
                               "straight sides into a swirl.";
        twistDef.builtIn = true;
        twistDef.params = {number("angle", "Angle (degrees)", 90, -3600, 3600, 1), number("centerX", "Center across (percent)", 50, -100, 200, 1),
            number("centerY", "Center down (percent)", 50, -100, 200, 1)};
        made.push_back(twistDef);

        Definition mergeDef;
        mergeDef.id = "merge";
        mergeDef.name = "Merge Paths";
        mergeDef.description = "Combines the shape's outlines into one: added together, the later ones cut out of the "
                               "first, only where they overlap, or only where they do not. Key the other operators to "
                               "make shapes meld and part.";
        mergeDef.builtIn = true;
        mergeDef.params = {choice("mode", "Merge", {"Add", "Subtract", "Intersect", "Exclude"})};
        made.push_back(mergeDef);

        Definition repeaterDef;
        repeaterDef.id = "repeater";
        repeaterDef.name = "Repeater";
        repeaterDef.description = "Draws the shape again and again, each copy moved, turned and scaled from the one "
                                  "before: a trail, a spiral, a stack of rings.";
        repeaterDef.builtIn = true;
        repeaterDef.params = {number("copies", "Copies", 3, 0, 200, 1), number("moveX", "Move across (pixels)", 20, -2000, 2000, 1),
            number("moveY", "Move down (pixels)", 0, -2000, 2000, 1), number("turn", "Turn (degrees)", 0, -360, 360, 1),
            number("scale", "Scale (percent)", 100, 1, 400, 1)};
        made.push_back(repeaterDef);
        return made;
    }();
    return list;
}

std::string mergeOf(const std::vector<Applied>& list, int frame)
{
    std::string mode;
    for (const Applied& applied : list)
        if (applied.enabled && applied.type == "merge")
        {
            mode = applied.valueAt("mode", frame, "Add");
            if (mode.empty())
                mode = "Add";
        }
    return mode;
}

Taper taperOf(const std::vector<Applied>& list, int frame)
{
    Taper taper;
    for (const Applied& applied : list)
    {
        if (!applied.enabled || applied.type != "taper")
            continue;
        taper.on = true;
        taper.start = std::max(0.0, param(applied, "start", frame, 100.0));
        taper.end = std::max(0.0, param(applied, "end", frame, 0.0));
        taper.wave = std::max(0.0, param(applied, "wave", frame, 0.0));
        taper.wavelength = std::max(1.0, param(applied, "wavelength", frame, 40.0));
        taper.speed = param(applied, "speed", frame, 0.0);
    }
    return taper;
}

std::vector<Point> taperedOutline(const Line& line, double weight, const Taper& taper, double seconds)
{
    std::vector<Point> corners = walked(line);
    if (line.closed && corners.size() > 1)
        corners.push_back(corners.front());
    // Points no more than two pixels apart, so a wave swells smoothly.
    std::vector<Point> points;
    for (size_t i = 0; i < corners.size(); ++i)
    {
        if (i > 0)
        {
            const int steps = static_cast<int>(std::ceil(distance(corners[i - 1], corners[i]) / 2.0));
            for (int s = 1; s < steps; ++s)
            {
                const double f = static_cast<double>(s) / steps;
                points.push_back(Point(corners[i - 1].x + (corners[i].x - corners[i - 1].x) * f,
                    corners[i - 1].y + (corners[i].y - corners[i - 1].y) * f));
            }
        }
        points.push_back(corners[i]);
    }
    std::vector<Point> outline;
    const double length = lengthOf(points);
    if (points.size() < 2 || length <= 0.0)
        return outline;
    // Each point's half width, and its side, from the pieces either side.
    std::vector<Point> left;
    std::vector<Point> right;
    double along = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (i > 0)
            along += distance(points[i - 1], points[i]);
        const Point& before = points[i > 0 ? i - 1 : i];
        const Point& after = points[i + 1 < points.size() ? i + 1 : i];
        const double dx = after.x - before.x;
        const double dy = after.y - before.y;
        const double span = std::max(1e-12, std::hypot(dx, dy));
        const Point normal(-dy / span, dx / span);
        const double t = along / length;
        double percent = taper.start + (taper.end - taper.start) * t;
        if (taper.wave > 0.0)
            percent += taper.wave * std::sin(2.0 * 3.14159265358979323846 * (along / taper.wavelength - taper.speed * seconds));
        const double half = std::max(0.0, weight * percent / 100.0) / 2.0;
        left.push_back(Point(points[i].x + normal.x * half, points[i].y + normal.y * half));
        right.push_back(Point(points[i].x - normal.x * half, points[i].y - normal.y * half));
    }
    outline = left;
    outline.insert(outline.end(), right.rbegin(), right.rend());
    return outline;
}

namespace {

// \a lines as the subpaths of a drawing to bake, closed or open.
vectors::Painted paintedOf(const std::vector<Line>& lines, bool closed)
{
    vectors::Painted painted;
    for (const Line& line : lines)
    {
        if (line.points.size() < (closed ? 3u : 2u))
            continue;
        vectors::Subpath subpath;
        subpath.segments.push_back({vectors::Segment::Kind::Move, {line.points.front()}});
        for (size_t i = 1; i < line.points.size(); ++i)
            subpath.segments.push_back({vectors::Segment::Kind::Line, {line.points[i]}});
        subpath.closed = closed || line.closed;
        painted.subpaths.push_back(subpath);
    }
    return painted;
}

} // namespace

Shape* bakedFill(const std::vector<Line>& outlines, const FillStyle& paint)
{
    vectors::Painted painted = paintedOf(outlines, true);
    // Any color: the shape's one fill is swapped for a copy of the paint.
    painted.fill = vectors::Color{0, 0, 0, 255};
    Shape* shape = vectors::toShape(painted);
    if (!shape)
        return nullptr;
    FillStyle* copy = cloneFill(&paint, shape);
    if (!copy)
        return shape;
    for (FillStyle*& fill : shape->fills)
        if (fill == shape->fillsMap[1])
        {
            delete fill;
            fill = copy;
        }
    shape->fillsMap[1] = copy;
    return shape;
}

Shape* bakedStroke(const std::vector<Line>& lines, const StrokeStyle& stroke)
{
    vectors::Painted painted = paintedOf(lines, false);
    painted.stroke = vectors::Color{0, 0, 0, 255};
    painted.strokeWidth = stroke.weight;
    Shape* shape = vectors::toShape(painted);
    if (!shape)
        return nullptr;
    StrokeStyle* copy = cloneStroke(&stroke, shape);
    if (!copy)
        return shape;
    for (StrokeStyle*& each : shape->strokes)
        if (each == shape->strokesMap[1])
        {
            delete each;
            each = copy;
        }
    shape->strokesMap[1] = copy;
    return shape;
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
        else if (applied.type == "twist")
        {
            // Each point turned about the middle of all the lines, most at
            // the middle and none at the far edge.
            const double angle = param(applied, "angle", frame, 90.0) * 3.14159265358979323846 / 180.0;
            double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
            for (const Line& line : out)
                for (const Point& point : line.points)
                {
                    left = std::min(left, point.x);
                    right = std::max(right, point.x);
                    top = std::min(top, point.y);
                    bottom = std::max(bottom, point.y);
                }
            if (left > right)
                continue;
            const double cx = left + (right - left) * param(applied, "centerX", frame, 50.0) / 100.0;
            const double cy = top + (bottom - top) * param(applied, "centerY", frame, 50.0) / 100.0;
            const double reach = std::max(1e-9, std::hypot(std::max(cx - left, right - cx), std::max(cy - top, bottom - cy)));
            // Finely, so straight sides curl.
            out = displaced(out, 2.0, [](const Sample&, size_t) { return 0.0; });
            for (Line& line : out)
                for (Point& point : line.points)
                {
                    const double dx = point.x - cx, dy = point.y - cy;
                    const double turn = angle * (1.0 - std::min(1.0, std::hypot(dx, dy) / reach));
                    point = Point(cx + dx * std::cos(turn) - dy * std::sin(turn), cy + dx * std::sin(turn) + dy * std::cos(turn));
                }
        }
        else if (applied.type == "dashes")
        {
            if (stroke)
                out = dashed(out, std::max(0.0, param(applied, "dash", frame, 20.0)),
                    std::max(0.0, param(applied, "gap", frame, 10.0)), param(applied, "offset", frame, 0.0));
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
        else if (applied.type == "offset")
            out = offsetLines(out, param(applied, "amount", frame, 6.0));
        else if (applied.type == "round")
            out = roundCorners(out, param(applied, "radius", frame, 12.0));
        else if (applied.type == "puckerbloat")
            out = puckerBloat(out, param(applied, "amount", frame, 30.0));
        else if (applied.type == "repeater")
            out = repeated(out, std::clamp(static_cast<int>(std::lround(param(applied, "copies", frame, 3.0))), 0, 200),
                param(applied, "moveX", frame, 20.0), param(applied, "moveY", frame, 0.0), param(applied, "turn", frame, 0.0),
                std::max(0.01, param(applied, "scale", frame, 100.0) / 100.0));
    }
    return out;
}

} // namespace fla::shapeops
