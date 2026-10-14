#include "trace_bitmap.h"

#include "../data/edge.h"
#include "../data/rect.h"
#include "../data/shape.h"
#include "../data/solid_color.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <queue>
#include <unordered_map>

namespace fla {

namespace trace {

const CurveFit kCurveFits[6] = {{"Pixels", "pixels", 0.25}, {"Very Tight", "very tight", 0.5},
    {"Tight", "tight", 1.0}, {"Normal", "normal", 2.0}, {"Smooth", "smooth", 3.0}, {"Very Smooth", "very smooth", 5.0}};

namespace {

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
    if (pixels == 0)
        return label;
    const auto opaque = [&](size_t i) { return rgba[i * 4 + 3] >= 128; };

    // 1. Small areas to start from: each pixel joins a neighbor's area when
    // it is close to the area's average so far, well inside the threshold.
    const int tight = std::max(1, options.colorThreshold / 2);
    struct Area
    {
        double sum[3] = {0.0, 0.0, 0.0};
        double pixels = 0.0;
        int distanceTo(const uint8_t* p) const
        {
            return static_cast<int>(std::lround(std::fabs(sum[0] / pixels - p[0]) + std::fabs(sum[1] / pixels - p[1]) +
                std::fabs(sum[2] / pixels - p[2])));
        }
        int distanceTo(const Area& other) const
        {
            double d = 0.0;
            for (int c = 0; c < 3; ++c)
                d += std::fabs(sum[c] / pixels - other.sum[c] / other.pixels);
            return static_cast<int>(std::lround(d));
        }
        void add(const uint8_t* p)
        {
            for (int c = 0; c < 3; ++c)
                sum[c] += p[c];
            pixels += 1.0;
        }
    };
    std::vector<Area> areas;
    std::vector<size_t> stack;
    for (size_t start = 0; start < pixels; ++start)
    {
        if (label[start] != -2)
            continue;
        if (!opaque(start))
        {
            label[start] = -1;
            continue;
        }
        const int area = static_cast<int>(areas.size());
        areas.emplace_back();
        label[start] = area;
        areas.back().add(rgba + start * 4);
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
                if (label[next] != -2 || !opaque(next) || areas[static_cast<size_t>(area)].distanceTo(rgba + next * 4) > tight)
                    continue;
                label[next] = area;
                areas[static_cast<size_t>(area)].add(rgba + next * 4);
                stack.push_back(next);
            }
        }
    }

    // 2. Neighboring areas whose average colors are within the threshold
    // join, the closest pair first, so how the picture was walked does not
    // decide where the borders fall.
    const int areaCount = static_cast<int>(areas.size());
    std::vector<int> parent(static_cast<size_t>(areaCount));
    for (int i = 0; i < areaCount; ++i)
        parent[static_cast<size_t>(i)] = i;
    const auto find = [&parent](int a) {
        while (parent[static_cast<size_t>(a)] != a)
        {
            parent[static_cast<size_t>(a)] = parent[static_cast<size_t>(parent[static_cast<size_t>(a)])];
            a = parent[static_cast<size_t>(a)];
        }
        return a;
    };
    std::vector<std::vector<int>> neighbors(static_cast<size_t>(areaCount));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int here = label[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
            if (here < 0)
                continue;
            const int right = x + 1 < width ? label[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x + 1)] : -1;
            const int below = y + 1 < height ? label[static_cast<size_t>(y + 1) * static_cast<size_t>(width) + static_cast<size_t>(x)] : -1;
            for (int other : {right, below})
                if (other >= 0 && other != here)
                {
                    neighbors[static_cast<size_t>(here)].push_back(other);
                    neighbors[static_cast<size_t>(other)].push_back(here);
                }
        }
    for (auto& list : neighbors)
    {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    struct Pair
    {
        int distance, a, b;
        bool operator>(const Pair& other) const { return distance > other.distance; }
    };
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> queue;
    for (int a = 0; a < areaCount; ++a)
        for (int b : neighbors[static_cast<size_t>(a)])
            if (a < b)
            {
                const int d = areas[static_cast<size_t>(a)].distanceTo(areas[static_cast<size_t>(b)]);
                if (d <= options.colorThreshold)
                    queue.push({d, a, b});
            }
    // Lists of neighbors, joined as their areas join; compacted (roots
    // only, once each) when one has doubled since it last was.
    std::vector<size_t> compacted(static_cast<size_t>(areaCount));
    for (int a = 0; a < areaCount; ++a)
        compacted[static_cast<size_t>(a)] = neighbors[static_cast<size_t>(a)].size();
    const auto compact = [&](int a) {
        std::vector<int>& list = neighbors[static_cast<size_t>(a)];
        for (int& other : list)
            other = find(other);
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        list.erase(std::remove(list.begin(), list.end(), a), list.end());
        compacted[static_cast<size_t>(a)] = list.size();
    };
    bool joinedAny = true;
    while (joinedAny)
    {
        joinedAny = false;
        while (!queue.empty())
        {
            Pair pair = queue.top();
            queue.pop();
            const int a = find(pair.a);
            const int b = find(pair.b);
            if (a == b)
                continue;
            // Either side may have grown since: measured again, and put
            // back when farther now.
            const int d = areas[static_cast<size_t>(a)].distanceTo(areas[static_cast<size_t>(b)]);
            if (d > options.colorThreshold)
                continue;
            if (d > pair.distance)
            {
                queue.push({d, a, b});
                continue;
            }
            // The smaller joins the larger, its neighbors with it. Only the
            // pairs it brings are queued here; the larger one's others are
            // measured again by the sweep below.
            const int into = areas[static_cast<size_t>(a)].pixels >= areas[static_cast<size_t>(b)].pixels ? a : b;
            const int from = into == a ? b : a;
            Area& kept = areas[static_cast<size_t>(into)];
            for (int c = 0; c < 3; ++c)
                kept.sum[c] += areas[static_cast<size_t>(from)].sum[c];
            kept.pixels += areas[static_cast<size_t>(from)].pixels;
            parent[static_cast<size_t>(from)] = into;
            joinedAny = true;
            std::vector<int>& list = neighbors[static_cast<size_t>(into)];
            for (int other : neighbors[static_cast<size_t>(from)])
            {
                const int root = find(other);
                if (root == into)
                    continue;
                list.push_back(root);
                const int between = kept.distanceTo(areas[static_cast<size_t>(root)]);
                if (between <= options.colorThreshold)
                    queue.push({between, into, root});
            }
            std::vector<int>().swap(neighbors[static_cast<size_t>(from)]);
            if (list.size() > 2 * compacted[static_cast<size_t>(into)] + 16)
                compact(into);
        }
        if (!joinedAny)
            break;
        // Areas that have grown may have come within the threshold of
        // neighbors measured before they did.
        joinedAny = false;
        for (int a = 0; a < areaCount; ++a)
        {
            if (find(a) != a)
                continue;
            compact(a);
            for (int b : neighbors[static_cast<size_t>(a)])
            {
                if (a >= b)
                    continue;
                const int d = areas[static_cast<size_t>(a)].distanceTo(areas[static_cast<size_t>(b)]);
                if (d <= options.colorThreshold)
                {
                    queue.push({d, a, b});
                    joinedAny = true;
                }
            }
        }
    }
    for (int& l : label)
        if (l >= 0)
            l = find(l);

    // A strip with no pixel inside it, every one on a border, whose color
    // is a blend of two areas beside it, is the soft edge between them
    // rather than an area of its own: it goes whole to the nearer of the
    // two, which keeps the edge straight. A drawn line is not a blend of
    // what is either side of it, and stays.
    {
        std::vector<char> inside(static_cast<size_t>(areaCount), 0);
        for (int y = 1; y + 1 < height; ++y)
            for (int x = 1; x + 1 < width; ++x)
            {
                const size_t at = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                const int l = label[at];
                if (l >= 0 && label[at - 1] == l && label[at + 1] == l && label[at - static_cast<size_t>(width)] == l &&
                    label[at + static_cast<size_t>(width)] == l)
                    inside[static_cast<size_t>(l)] = 1;
            }
        std::vector<int> into(static_cast<size_t>(areaCount), -1);
        for (int s = 0; s < areaCount; ++s)
        {
            if (find(s) != s || inside[static_cast<size_t>(s)])
                continue;
            compact(s);
            const std::vector<int>& around = neighbors[static_cast<size_t>(s)];
            if (around.size() < 2 || around.size() > 12)
                continue;
            const Area& strip = areas[static_cast<size_t>(s)];
            double mean[3];
            for (int c = 0; c < 3; ++c)
                mean[c] = strip.sum[c] / strip.pixels;
            double best = static_cast<double>(tight) + 1.0;
            int target = -1;
            for (size_t i = 0; i < around.size(); ++i)
                for (size_t j = i + 1; j < around.size(); ++j)
                {
                    const Area& a = areas[static_cast<size_t>(around[i])];
                    const Area& b = areas[static_cast<size_t>(around[j])];
                    double ca[3], cb[3], along = 0.0, length = 0.0;
                    for (int c = 0; c < 3; ++c)
                    {
                        ca[c] = a.sum[c] / a.pixels;
                        cb[c] = b.sum[c] / b.pixels;
                        along += (mean[c] - cb[c]) * (ca[c] - cb[c]);
                        length += (ca[c] - cb[c]) * (ca[c] - cb[c]);
                    }
                    if (length < 1.0)
                        continue;
                    const double mix = std::clamp(along / length, 0.0, 1.0);
                    double off = 0.0;
                    for (int c = 0; c < 3; ++c)
                        off += std::fabs(mean[c] - (cb[c] + mix * (ca[c] - cb[c])));
                    if (off < best)
                    {
                        best = off;
                        target = mix >= 0.5 ? around[i] : around[j];
                    }
                }
            into[static_cast<size_t>(s)] = target;
        }
        // A strip may go into another strip: followed a few steps.
        for (int& l : label)
            for (int step = 0; step < 4 && l >= 0 && into[static_cast<size_t>(l)] >= 0; ++step)
                l = into[static_cast<size_t>(l)];
    }

    // 3. A pixel on a border goes to whichever area beside it has the
    // clearly nearer color, so a soft edge is split where its colors cross
    // over rather than wherever the walk happened to reach first; noise in a
    // pixel halfway between is not enough to move it.
    const int margin = std::max(1, tight / 2);
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<int> moved = label;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const size_t at = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                const int here = label[at];
                if (here < 0)
                    continue;
                const uint8_t* p = rgba + at * 4;
                int best = here;
                int nearest = areas[static_cast<size_t>(here)].distanceTo(p);
                const int nx[4] = {x - 1, x + 1, x, x};
                const int ny[4] = {y, y, y - 1, y + 1};
                for (int k = 0; k < 4; ++k)
                {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= width || ny[k] >= height)
                        continue;
                    const int other = label[static_cast<size_t>(ny[k]) * static_cast<size_t>(width) + static_cast<size_t>(nx[k])];
                    if (other < 0 || other == here)
                        continue;
                    const int d = areas[static_cast<size_t>(other)].distanceTo(p) + margin;
                    if (d < nearest)
                    {
                        nearest = d;
                        best = other;
                    }
                }
                moved[at] = best;
            }
        label.swap(moved);
    }

    // Each connected piece an area of its own, as a pixel moved in step 3
    // may have cut one off.
    std::vector<int> piece(pixels, -1);
    count = 0;
    for (size_t start = 0; start < pixels; ++start)
    {
        if (label[start] < 0 || piece[start] >= 0)
            continue;
        const int id = count++;
        piece[start] = id;
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
                if (piece[next] >= 0 || label[next] != label[at])
                    continue;
                piece[next] = id;
                stack.push_back(next);
            }
        }
    }
    for (size_t i = 0; i < pixels; ++i)
        if (label[i] >= 0)
            label[i] = piece[i];

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
