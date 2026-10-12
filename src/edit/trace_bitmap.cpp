#include "trace_bitmap.h"

#include "../data/edge.h"
#include "../data/rect.h"
#include "../data/shape.h"
#include "../data/solid_color.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>

namespace fla {

namespace trace {

namespace {

int distance(const uint8_t* a, const uint8_t* b)
{
    return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
}

/// Points the polyline needs within \a tolerance of it (Ramer, Douglas and
/// Peucker); the ends are kept.
void simplify(const std::vector<Point>& points, size_t first, size_t last, double tolerance, std::vector<bool>& keep)
{
    if (last <= first + 1)
        return;
    const Point& a = points[first];
    const Point& b = points[last];
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = std::hypot(dx, dy);
    double farthest = -1.0;
    size_t index = first;
    for (size_t i = first + 1; i < last; ++i)
    {
        const double d = length > 1.0e-12 ? std::fabs((points[i].x - a.x) * dy - (points[i].y - a.y) * dx) / length
                                          : std::hypot(points[i].x - a.x, points[i].y - a.y);
        if (d > farthest)
        {
            farthest = d;
            index = i;
        }
    }
    if (farthest > tolerance)
    {
        keep[index] = true;
        simplify(points, first, index, tolerance, keep);
        simplify(points, index, last, tolerance, keep);
    }
}

/// A lattice point as a key.
long long key(int x, int y, int width)
{
    return static_cast<long long>(y) * (width + 1) + x;
}

} // namespace

std::vector<int> regions(const uint8_t* rgba, int width, int height, const Options& options, int& count)
{
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<int> label(pixels, -2);
    count = 0;
    std::vector<size_t> stack;
    // Flood each unlabeled opaque pixel's like neighbors, against the
    // color of the pixel the area started from.
    for (size_t start = 0; start < pixels; ++start)
    {
        if (label[start] != -2)
            continue;
        if (rgba[start * 4 + 3] < 128)
        {
            label[start] = -1;
            continue;
        }
        const int area = count++;
        const uint8_t* seed = rgba + start * 4;
        label[start] = area;
        stack.assign(1, start);
        while (!stack.empty())
        {
            const size_t at = stack.back();
            stack.pop_back();
            const int x = static_cast<int>(at % static_cast<size_t>(width));
            const int y = static_cast<int>(at / static_cast<size_t>(width));
            const int nx[4] = {x - 1, x + 1, x, x};
            const int ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k)
            {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= width || ny[k] >= height)
                    continue;
                const size_t next = static_cast<size_t>(ny[k]) * static_cast<size_t>(width) + static_cast<size_t>(nx[k]);
                if (label[next] != -2 || rgba[next * 4 + 3] < 128 || distance(rgba + next * 4, seed) > options.colorThreshold)
                    continue;
                label[next] = area;
                stack.push_back(next);
            }
        }
    }

    // Small areas go into the neighbor they share the most edge with, a
    // few rounds, smallest first.
    for (int round = 0; round < 8; ++round)
    {
        std::vector<int> size(static_cast<size_t>(count), 0);
        for (int l : label)
            if (l >= 0)
                ++size[static_cast<size_t>(l)];
        std::vector<std::map<int, int>> borders(static_cast<size_t>(count));
        bool any = false;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const int l = label[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
                if (l < 0 || size[static_cast<size_t>(l)] >= options.minimumArea)
                    continue;
                any = true;
                const int nx[4] = {x - 1, x + 1, x, x};
                const int ny[4] = {y, y, y - 1, y + 1};
                for (int k = 0; k < 4; ++k)
                {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= width || ny[k] >= height)
                        continue;
                    const int other = label[static_cast<size_t>(ny[k]) * static_cast<size_t>(width) + static_cast<size_t>(nx[k])];
                    if (other >= 0 && other != l)
                        ++borders[static_cast<size_t>(l)][other];
                }
            }
        if (!any)
            break;
        std::vector<int> into(static_cast<size_t>(count));
        for (int l = 0; l < count; ++l)
        {
            into[static_cast<size_t>(l)] = l;
            const auto& border = borders[static_cast<size_t>(l)];
            if (size[static_cast<size_t>(l)] >= options.minimumArea || border.empty())
                continue;
            int best = -1, most = -1;
            for (const auto& [other, shared] : border)
            {
                // Into a larger one, so two small ones do not trade places.
                if (shared > most && size[static_cast<size_t>(other)] >= size[static_cast<size_t>(l)])
                {
                    most = shared;
                    best = other;
                }
            }
            if (best >= 0)
                into[static_cast<size_t>(l)] = best;
        }
        bool changed = false;
        for (int& l : label)
            if (l >= 0 && into[static_cast<size_t>(l)] != l)
            {
                l = into[static_cast<size_t>(l)];
                changed = true;
            }
        if (!changed)
            break;
    }
    // Numbered again from 0, with no gaps.
    std::vector<int> renumber(static_cast<size_t>(count), -1);
    int next = 0;
    for (int& l : label)
    {
        if (l < 0)
            continue;
        if (renumber[static_cast<size_t>(l)] < 0)
            renumber[static_cast<size_t>(l)] = next++;
        l = renumber[static_cast<size_t>(l)];
    }
    count = next;
    return label;
}

Shape* traceBitmap(const uint8_t* rgba, int width, int height, const Options& options)
{
    if (!rgba || width <= 0 || height <= 0)
        return nullptr;
    int count = 0;
    const std::vector<int> label = regions(rgba, width, height, options, count);
    if (count == 0)
        return nullptr;
    const auto at = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return -1;
        return label[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    };

    Shape* shape = new Shape(nullptr);
    // Each area's fill, its average color; fill style n + 1 for area n.
    std::vector<double> sum(static_cast<size_t>(count) * 4, 0.0);
    std::vector<int> pixels(static_cast<size_t>(count), 0);
    for (size_t i = 0; i < label.size(); ++i)
    {
        if (label[i] < 0)
            continue;
        const size_t l = static_cast<size_t>(label[i]);
        for (int c = 0; c < 4; ++c)
            sum[l * 4 + static_cast<size_t>(c)] += rgba[i * 4 + static_cast<size_t>(c)];
        ++pixels[l];
    }
    for (int l = 0; l < count; ++l)
    {
        SolidColor* fill = new SolidColor(shape);
        for (int c = 0; c < 4; ++c)
            fill->color[c] = static_cast<uint8_t>(std::lround(sum[static_cast<size_t>(l) * 4 + static_cast<size_t>(c)] /
                std::max(1, pixels[static_cast<size_t>(l)])));
        fill->color[3] = 255;
        shape->fills.push_back(fill);
        shape->fillsMap[l + 1] = fill;
    }

    // The unit edges between areas, each going east or south, with the
    // area on its left and on its right (the page's y runs down).
    struct Unit
    {
        int x, y;
        bool horizontal;
        int left, right;
        bool used = false;
    };
    std::vector<Unit> units;
    std::unordered_map<long long, std::vector<size_t>> at_vertex;
    const auto addUnit = [&](int x, int y, bool horizontal, int left, int right) {
        units.push_back({x, y, horizontal, left, right});
        const size_t index = units.size() - 1;
        at_vertex[key(x, y, width)].push_back(index);
        at_vertex[key(horizontal ? x + 1 : x, horizontal ? y : y + 1, width)].push_back(index);
    };
    for (int y = 0; y <= height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int above = at(x, y - 1);
            const int below = at(x, y);
            if (above != below)
                addUnit(x, y, true, above, below);
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x <= width; ++x)
        {
            const int west = at(x - 1, y);
            const int east = at(x, y);
            if (west != east)
                addUnit(x, y, false, east, west);
        }

    // A unit edge walked from \a from: its far end, and the areas on its
    // left and right that way round.
    struct Step
    {
        int x, y, left, right;
    };
    const auto walk = [&](const Unit& unit, int fromX, int fromY) {
        const int endX = unit.horizontal ? unit.x + 1 : unit.x;
        const int endY = unit.horizontal ? unit.y : unit.y + 1;
        if (fromX == unit.x && fromY == unit.y)
            return Step{endX, endY, unit.left, unit.right};
        return Step{unit.x, unit.y, unit.right, unit.left};
    };
    // Where a chain must stop: where other than two unit edges meet, or
    // where the two that meet part different areas.
    const auto breaks = [&](int x, int y) {
        const auto found = at_vertex.find(key(x, y, width));
        if (found == at_vertex.end() || found->second.size() != 2)
            return true;
        const Unit& a = units[found->second[0]];
        const Unit& b = units[found->second[1]];
        return std::minmax(a.left, a.right) != std::minmax(b.left, b.right);
    };

    const double tolerance = std::max(0.25, options.curveFit);
    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
    const auto chainFrom = [&](size_t first, int startX, int startY) {
        std::vector<Point> points{Point(startX, startY)};
        Step step = walk(units[first], startX, startY);
        const int chainLeft = step.left;
        const int chainRight = step.right;
        units[first].used = true;
        points.emplace_back(step.x, step.y);
        while (!breaks(step.x, step.y))
        {
            const auto& incident = at_vertex[key(step.x, step.y, width)];
            const size_t next = units[incident[0]].used ? incident[1] : incident[0];
            if (units[next].used)
                break;
            units[next].used = true;
            step = walk(units[next], step.x, step.y);
            points.emplace_back(step.x, step.y);
        }
        // Only the corners matter, then only those the fit needs.
        std::vector<Point> corners{points.front()};
        for (size_t i = 1; i + 1 < points.size(); ++i)
        {
            const Point& a = points[i - 1];
            const Point& b = points[i];
            const Point& c = points[i + 1];
            if ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) != 0.0)
                corners.push_back(b);
        }
        corners.push_back(points.back());
        std::vector<bool> keep(corners.size(), false);
        keep.front() = keep.back() = true;
        if (corners.size() > 2 && std::hypot(corners.front().x - corners.back().x, corners.front().y - corners.back().y) < 1.0e-9)
        {
            // A ring: split at its farthest point so both halves fit.
            size_t far = 1;
            double best = -1.0;
            for (size_t i = 1; i + 1 < corners.size(); ++i)
            {
                const double d = std::hypot(corners[i].x - corners.front().x, corners[i].y - corners.front().y);
                if (d > best)
                {
                    best = d;
                    far = i;
                }
            }
            keep[far] = true;
            simplify(corners, 0, far, tolerance, keep);
            simplify(corners, far, corners.size() - 1, tolerance, keep);
        }
        else
        {
            simplify(corners, 0, corners.size() - 1, tolerance, keep);
        }
        Edge* edge = new Edge(shape);
        edge->fillStyle0 = chainLeft + 1;
        edge->fillStyle1 = chainRight + 1;
        Path* path = new Path(edge);
        bool started = false;
        for (size_t i = 0; i < corners.size(); ++i)
        {
            if (!keep[i])
                continue;
            path->segments.push_back(new PathSegment(started ? PathSegment::Command::Line : PathSegment::Command::Move,
                {corners[i]}, path));
            started = true;
            left = std::min(left, corners[i].x);
            top = std::min(top, corners[i].y);
            right = std::max(right, corners[i].x);
            bottom = std::max(bottom, corners[i].y);
        }
        edge->paths.push_back(path);
        shape->edges.push_back(edge);
    };
    // Chains between break points, then rings with none.
    for (size_t i = 0; i < units.size(); ++i)
    {
        if (units[i].used)
            continue;
        const Unit& unit = units[i];
        const int endX = unit.horizontal ? unit.x + 1 : unit.x;
        const int endY = unit.horizontal ? unit.y : unit.y + 1;
        if (breaks(unit.x, unit.y))
            chainFrom(i, unit.x, unit.y);
        else if (breaks(endX, endY))
            chainFrom(i, endX, endY);
    }
    for (size_t i = 0; i < units.size(); ++i)
        if (!units[i].used)
            chainFrom(i, units[i].x, units[i].y);

    shape->localBounds = Rect(Point(left, top), Point(right, bottom));
    shape->bounds = shape->localBounds;
    return shape;
}

} // namespace trace

} // namespace fla
