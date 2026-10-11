#include "magic_wand.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fla {

namespace MagicWand {

namespace {

int channel(uint32_t color, int shift)
{
    return static_cast<int>((color >> shift) & 0xFF);
}

bool alike(uint32_t a, uint32_t b, int threshold)
{
    for (int shift = 0; shift <= 24; shift += 8)
        if (std::abs(channel(a, shift) - channel(b, shift)) > threshold)
            return false;
    return true;
}

double distanceToSegment(const Point& p, const Point& a, const Point& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = dx * dx + dy * dy;
    if (length <= 1.0e-18)
        return std::hypot(p.x - a.x, p.y - a.y);
    const double t = std::max(0.0, std::min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length));
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

/// Douglas-Peucker on points[first..last], keeping both ends.
void simplifyRun(const std::vector<Point>& points, size_t first, size_t last, double tolerance,
    std::vector<bool>& keep)
{
    if (last <= first + 1)
        return;
    double farthest = -1.0;
    size_t at = first;
    for (size_t i = first + 1; i < last; ++i)
    {
        const double d = distanceToSegment(points[i], points[first], points[last]);
        if (d > farthest)
        {
            farthest = d;
            at = i;
        }
    }
    if (farthest <= tolerance)
        return;
    keep[at] = true;
    simplifyRun(points, first, at, tolerance, keep);
    simplifyRun(points, at, last, tolerance, keep);
}

/// A closed loop straightened to within \a tolerance: split at the point
/// furthest from the first, each half simplified with its ends kept.
std::vector<Point> simplifyLoop(const std::vector<Point>& loop, double tolerance)
{
    const size_t n = loop.size();
    if (n < 4)
        return loop;
    size_t opposite = 0;
    double farthest = -1.0;
    for (size_t i = 1; i < n; ++i)
    {
        const double d = std::hypot(loop[i].x - loop[0].x, loop[i].y - loop[0].y);
        if (d > farthest)
        {
            farthest = d;
            opposite = i;
        }
    }
    std::vector<Point> closed = loop;
    closed.push_back(loop.front());
    std::vector<bool> keep(closed.size(), false);
    keep[0] = keep[opposite] = keep[closed.size() - 1] = true;
    simplifyRun(closed, 0, opposite, tolerance, keep);
    simplifyRun(closed, opposite, closed.size() - 1, tolerance, keep);
    std::vector<Point> out;
    for (size_t i = 0; i + 1 < closed.size(); ++i)
        if (keep[i])
            out.push_back(closed[i]);
    return out.size() >= 3 ? out : loop;
}

/// Each corner cut off along both its sides by \a reach, or half the side
/// when that is shorter: a pixel step becomes a diagonal across its middle,
/// while a long side loses only its ends.
std::vector<Point> cutCorners(const std::vector<Point>& loop, double reach)
{
    std::vector<Point> out;
    out.reserve(loop.size() * 2);
    const size_t n = loop.size();
    for (size_t i = 0; i < n; ++i)
    {
        const Point& a = loop[(i + n - 1) % n];
        const Point& b = loop[i];
        const Point& c = loop[(i + 1) % n];
        const double toA = std::hypot(a.x - b.x, a.y - b.y);
        const double toC = std::hypot(c.x - b.x, c.y - b.y);
        const double ta = toA > 0.0 ? std::min(0.5, reach / toA) : 0.0;
        const double tc = toC > 0.0 ? std::min(0.5, reach / toC) : 0.0;
        out.push_back(Point(b.x + (a.x - b.x) * ta, b.y + (a.y - b.y) * ta));
        out.push_back(Point(b.x + (c.x - b.x) * tc, b.y + (c.y - b.y) * tc));
    }
    // Where both cuts met at a side's middle, the point is there twice.
    std::vector<Point> unique;
    for (const Point& p : out)
        if (unique.empty() || std::hypot(p.x - unique.back().x, p.y - unique.back().y) > 1.0e-9)
            unique.push_back(p);
    while (unique.size() > 1 &&
        std::hypot(unique.front().x - unique.back().x, unique.front().y - unique.back().y) <= 1.0e-9)
        unique.pop_back();
    return unique;
}

/// Whether two sides cross, touching ends aside.
bool sidesCross(const Point& a, const Point& b, const Point& c, const Point& d)
{
    const auto side = [](const Point& p, const Point& q, const Point& r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };
    const double d1 = side(c, d, a);
    const double d2 = side(c, d, b);
    const double d3 = side(a, b, c);
    const double d4 = side(a, b, d);
    return ((d1 > 1.0e-12 && d2 < -1.0e-12) || (d1 < -1.0e-12 && d2 > 1.0e-12)) &&
        ((d3 > 1.0e-12 && d4 < -1.0e-12) || (d3 < -1.0e-12 && d4 > 1.0e-12));
}

} // namespace

bool selfIntersects(const std::vector<Point>& loop)
{
    const size_t n = loop.size();
    if (n < 4)
        return false;
    // Each side against every other side but its neighbors, with a box test
    // first so the common case is quick.
    for (size_t i = 0; i < n; ++i)
    {
        const Point& a = loop[i];
        const Point& b = loop[(i + 1) % n];
        const double minX = std::min(a.x, b.x);
        const double maxX = std::max(a.x, b.x);
        const double minY = std::min(a.y, b.y);
        const double maxY = std::max(a.y, b.y);
        for (size_t j = i + 2; j < n; ++j)
        {
            if (i == 0 && j == n - 1)
                continue;
            const Point& c = loop[j];
            const Point& d = loop[(j + 1) % n];
            if (std::max(c.x, d.x) < minX || std::min(c.x, d.x) > maxX ||
                std::max(c.y, d.y) < minY || std::min(c.y, d.y) > maxY)
                continue;
            if (sidesCross(a, b, c, d))
                return true;
        }
    }
    return false;
}

namespace {
} // namespace

std::vector<uint8_t> region(const uint32_t* pixels, int width, int height, int x, int y, int threshold)
{
    if (!pixels || width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height)
        return {};
    std::vector<uint8_t> mask(static_cast<size_t>(width) * height, 0);
    const uint32_t seed = pixels[static_cast<size_t>(y) * width + x];
    std::vector<int> stack;
    stack.push_back(y * width + x);
    mask[static_cast<size_t>(y) * width + x] = 1;
    while (!stack.empty())
    {
        const int at = stack.back();
        stack.pop_back();
        const int px = at % width;
        const int py = at / width;
        const int neighbors[4][2] = {{px - 1, py}, {px + 1, py}, {px, py - 1}, {px, py + 1}};
        for (const auto& n : neighbors)
        {
            if (n[0] < 0 || n[1] < 0 || n[0] >= width || n[1] >= height)
                continue;
            const size_t index = static_cast<size_t>(n[1]) * width + n[0];
            if (mask[index] || !alike(pixels[index], seed, threshold))
                continue;
            mask[index] = 1;
            stack.push_back(static_cast<int>(index));
        }
    }
    return mask;
}

namespace {

/// A square of side 2 * radius + 1 run over the mask: the least of it
/// (erode) or the most (dilate), one axis at a time.
std::vector<uint8_t> morph(const std::vector<uint8_t>& mask, int width, int height, int radius, bool dilate)
{
    std::vector<uint8_t> across(mask.size());
    std::vector<uint8_t> out(mask.size());
    const auto pick = [dilate](uint8_t a, uint8_t b) { return dilate ? std::max(a, b) : std::min(a, b); };
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t value = mask[static_cast<size_t>(y) * width + x];
            for (int k = -radius; k <= radius; ++k)
            {
                const int at = std::max(0, std::min(width - 1, x + k));
                value = pick(value, mask[static_cast<size_t>(y) * width + at]);
            }
            across[static_cast<size_t>(y) * width + x] = value;
        }
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t value = across[static_cast<size_t>(y) * width + x];
            for (int k = -radius; k <= radius; ++k)
            {
                const int at = std::max(0, std::min(height - 1, y + k));
                value = pick(value, across[static_cast<size_t>(at) * width + x]);
            }
            out[static_cast<size_t>(y) * width + x] = value;
        }
    }
    return out;
}

} // namespace

std::vector<uint8_t> tidy(const std::vector<uint8_t>& mask, int width, int height, int radius)
{
    if (radius <= 0 || width <= 0 || height <= 0 || mask.size() != static_cast<size_t>(width) * height)
        return mask;
    // Gaps are closed first, so the walls round a small hole are not worn
    // away with the strands.
    const std::vector<uint8_t> closed = morph(morph(mask, width, height, radius, true), width, height, radius, false);
    return morph(morph(closed, width, height, radius, false), width, height, radius, true);
}

std::vector<std::vector<Point>> outline(const std::vector<uint8_t>& mask, int width, int height)
{
    std::vector<std::vector<Point>> loops;
    if (width <= 0 || height <= 0 || mask.size() != static_cast<size_t>(width) * height)
        return loops;
    const auto in = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height && mask[static_cast<size_t>(y) * width + x] != 0;
    };
    const int stride = width + 1;
    const auto vertex = [stride](int x, int y) { return y * stride + x; };

    // Every pixel side between in and out, directed so the inside is on its
    // right as it runs (the stage runs y down): round each pixel clockwise.
    struct Side
    {
        int from;
        int to;
        bool used;
    };
    std::vector<Side> sides;
    std::vector<std::vector<int>> leaving(static_cast<size_t>(stride) * (height + 1));
    const auto add = [&](int x0, int y0, int x1, int y1) {
        leaving[vertex(x0, y0)].push_back(static_cast<int>(sides.size()));
        sides.push_back({vertex(x0, y0), vertex(x1, y1), false});
    };
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (!in(x, y))
                continue;
            if (!in(x, y - 1))
                add(x, y, x + 1, y);
            if (!in(x + 1, y))
                add(x + 1, y, x + 1, y + 1);
            if (!in(x, y + 1))
                add(x + 1, y + 1, x, y + 1);
            if (!in(x - 1, y))
                add(x, y + 1, x, y);
        }
    }

    const auto direction = [stride](const Side& side) {
        const int dx = side.to % stride - side.from % stride;
        const int dy = side.to / stride - side.from / stride;
        return std::make_pair(dx, dy);
    };

    for (size_t start = 0; start < sides.size(); ++start)
    {
        if (sides[start].used)
            continue;
        std::vector<Point> loop;
        size_t current = start;
        while (!sides[current].used)
        {
            sides[current].used = true;
            const int from = sides[current].from;
            loop.push_back(Point(from % stride, from / stride));
            // Where two sides leave a corner (pixels touching only at it),
            // the one turning right hugs this piece: pieces joined only at
            // a corner stay apart, as the region does.
            const std::vector<int>& next = leaving[sides[current].to];
            const std::pair<int, int> d = direction(sides[current]);
            int chosen = -1;
            int best = 4;
            for (int candidate : next)
            {
                if (sides[candidate].used && candidate != static_cast<int>(start))
                    continue;
                const std::pair<int, int> e = direction(sides[candidate]);
                int rank = 1;
                if (e.first == -d.second && e.second == d.first)
                    rank = 0; // right
                else if (e.first == d.first && e.second == d.second)
                    rank = 1; // straight on
                else
                    rank = 2; // left
                if (rank < best)
                {
                    best = rank;
                    chosen = candidate;
                }
            }
            if (chosen < 0 || chosen == static_cast<int>(start))
                break;
            current = static_cast<size_t>(chosen);
        }

        // Corners only: a point where the loop runs straight on goes.
        std::vector<Point> corners;
        const size_t n = loop.size();
        for (size_t i = 0; i < n; ++i)
        {
            const Point& a = loop[(i + n - 1) % n];
            const Point& b = loop[i];
            const Point& c = loop[(i + 1) % n];
            const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
            if (std::fabs(cross) > 1.0e-9)
                corners.push_back(b);
        }
        if (corners.size() >= 3)
            loops.push_back(corners);
    }
    return loops;
}

std::vector<Point> smooth(const std::vector<Point>& loop, Smoothing smoothing)
{
    if (loop.size() < 3)
        return loop;
    // Straightening can make the two sides of a strand a pixel wide cross,
    // and a loop that crosses itself leaves part of the area out; such a
    // loop is tidied less, down to its pixel edges.
    std::vector<Point> tidied;
    std::vector<Point> gentler;
    switch (smoothing)
    {
    case Smoothing::Pixels:
        return loop;
    case Smoothing::Rough:
        tidied = simplifyLoop(loop, 1.5);
        break;
    case Smoothing::Normal:
        gentler = cutCorners(loop, 0.5);
        tidied = simplifyLoop(gentler, 0.35);
        break;
    case Smoothing::Smooth:
        gentler = cutCorners(cutCorners(loop, 1.5), 0.75);
        tidied = simplifyLoop(gentler, 0.25);
        break;
    }
    if (tidied.size() >= 3 && !selfIntersects(tidied))
        return tidied;
    if (gentler.size() >= 3 && !selfIntersects(gentler))
        return gentler;
    return loop;
}

} // namespace MagicWand

} // namespace fla
