#include "paper_cleanup.h"

#include "trace_bitmap.h"

#include "../data/shape.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fla {

namespace paper {

namespace {

/// The value below which \a fraction of a histogram's \a total lies.
int percentile(const std::array<int, 256>& histogram, long long total, double fraction)
{
    const long long wanted = static_cast<long long>(fraction * static_cast<double>(total));
    long long seen = 0;
    for (int v = 0; v < 256; ++v)
    {
        seen += histogram[static_cast<size_t>(v)];
        if (seen > wanted)
            return v;
    }
    return 255;
}

/// A box blur of \a radius pixels, across then down.
std::vector<uint8_t> boxBlur(const std::vector<uint8_t>& in, int width, int height, int radius)
{
    if (radius <= 0)
        return in;
    std::vector<uint8_t> across(in.size());
    std::vector<uint8_t> out(in.size());
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int sum = 0;
            int count = 0;
            for (int k = std::max(0, x - radius); k <= std::min(width - 1, x + radius); ++k, ++count)
                sum += in[static_cast<size_t>(y) * width + k];
            across[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(sum / count);
        }
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int sum = 0;
            int count = 0;
            for (int k = std::max(0, y - radius); k <= std::min(height - 1, y + radius); ++k, ++count)
                sum += across[static_cast<size_t>(k) * width + x];
            out[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(sum / count);
        }
    }
    return out;
}

/// A binary mask grown (\a grow) or shrunk by \a radius pixels, square.
std::vector<uint8_t> morph(const std::vector<uint8_t>& in, int width, int height, int radius, bool grow)
{
    std::vector<uint8_t> across(in.size());
    std::vector<uint8_t> out(in.size());
    const uint8_t want = grow ? 1 : 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t v = static_cast<uint8_t>(1 - want);
            for (int k = std::max(0, x - radius); k <= std::min(width - 1, x + radius); ++k)
                if (in[static_cast<size_t>(y) * width + k] == want)
                {
                    v = want;
                    break;
                }
            across[static_cast<size_t>(y) * width + x] = v;
        }
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t v = static_cast<uint8_t>(1 - want);
            for (int k = std::max(0, y - radius); k <= std::min(height - 1, y + radius); ++k)
                if (across[static_cast<size_t>(k) * width + x] == want)
                {
                    v = want;
                    break;
                }
            out[static_cast<size_t>(y) * width + x] = v;
        }
    }
    return out;
}

/// Turns each patch of \a value smaller than \a minimum pixels into the
/// other value. With \a keepBorder, a patch touching the picture's edge is
/// left: paper round the outside is not a hole.
void removeSmall(std::vector<uint8_t>& mask, int width, int height, uint8_t value, int minimum, bool keepBorder)
{
    std::vector<uint8_t> seen(mask.size(), 0);
    std::vector<int> patch;
    std::vector<int> stack;
    for (int start = 0; start < width * height; ++start)
    {
        if (seen[static_cast<size_t>(start)] || mask[static_cast<size_t>(start)] != value)
            continue;
        patch.clear();
        stack.assign(1, start);
        seen[static_cast<size_t>(start)] = 1;
        bool border = false;
        while (!stack.empty())
        {
            const int at = stack.back();
            stack.pop_back();
            patch.push_back(at);
            const int x = at % width;
            const int y = at / width;
            if (x == 0 || y == 0 || x == width - 1 || y == height - 1)
                border = true;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    // Lines join through corners; paper only side by side.
                    if ((dx == 0 && dy == 0) || (value == 0 && dx != 0 && dy != 0))
                        continue;
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    const int next = ny * width + nx;
                    if (!seen[static_cast<size_t>(next)] && mask[static_cast<size_t>(next)] == value)
                    {
                        seen[static_cast<size_t>(next)] = 1;
                        stack.push_back(next);
                    }
                }
            }
        }
        if (static_cast<int>(patch.size()) < minimum && !(keepBorder && border))
            for (const int at : patch)
                mask[static_cast<size_t>(at)] = static_cast<uint8_t>(1 - value);
    }
}

} // namespace

std::vector<uint8_t> lightness(const uint8_t* rgba, int width, int height)
{
    const size_t pixels = static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height));
    std::vector<uint8_t> gray(pixels);
    for (size_t i = 0; i < pixels; ++i)
    {
        const uint8_t* p = rgba + i * 4;
        // Transparent counts as paper.
        const int luma = (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
        gray[i] = static_cast<uint8_t>(255 - (255 - luma) * p[3] / 255);
    }
    return gray;
}

std::vector<uint8_t> level(const std::vector<uint8_t>& gray, int width, int height, bool evenPaper, bool autoLevel)
{
    std::vector<uint8_t> out(gray);
    if (width <= 0 || height <= 0 || gray.size() != static_cast<size_t>(width) * height)
        return out;

    if (evenPaper)
    {
        // The paper's brightness block by block, from each block's lighter
        // pixels (the lines are few), smoothed, then spread over the page.
        const int block = std::max(8, std::min(width, height) / 12);
        const int columns = (width + block - 1) / block;
        const int rows = (height + block - 1) / block;
        std::vector<double> paper(static_cast<size_t>(columns) * rows);
        for (int by = 0; by < rows; ++by)
        {
            for (int bx = 0; bx < columns; ++bx)
            {
                std::array<int, 256> histogram{};
                long long count = 0;
                for (int y = by * block; y < std::min(height, (by + 1) * block); ++y)
                    for (int x = bx * block; x < std::min(width, (bx + 1) * block); ++x, ++count)
                        ++histogram[gray[static_cast<size_t>(y) * width + x]];
                paper[static_cast<size_t>(by) * columns + bx] = std::max(1, percentile(histogram, count, 0.8));
            }
        }
        std::vector<double> smooth(paper.size());
        for (int by = 0; by < rows; ++by)
        {
            for (int bx = 0; bx < columns; ++bx)
            {
                double sum = 0.0;
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int x = bx + dx;
                        const int y = by + dy;
                        if (x < 0 || y < 0 || x >= columns || y >= rows)
                            continue;
                        sum += paper[static_cast<size_t>(y) * columns + x];
                        ++count;
                    }
                smooth[static_cast<size_t>(by) * columns + bx] = sum / count;
            }
        }
        const auto paperAt = [&](int x, int y) {
            // Between the four nearest block centers.
            const double fx = std::clamp((x + 0.5) / block - 0.5, 0.0, static_cast<double>(columns - 1));
            const double fy = std::clamp((y + 0.5) / block - 0.5, 0.0, static_cast<double>(rows - 1));
            const int x0 = static_cast<int>(fx);
            const int y0 = static_cast<int>(fy);
            const int x1 = std::min(columns - 1, x0 + 1);
            const int y1 = std::min(rows - 1, y0 + 1);
            const double tx = fx - x0;
            const double ty = fy - y0;
            const auto at = [&](int cx, int cy) { return smooth[static_cast<size_t>(cy) * columns + cx]; };
            return (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
        };
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const size_t i = static_cast<size_t>(y) * width + x;
                out[i] = static_cast<uint8_t>(std::clamp(gray[i] * 255.0 / paperAt(x, y), 0.0, 255.0));
            }
    }

    if (autoLevel)
    {
        std::array<int, 256> histogram{};
        for (const uint8_t v : out)
            ++histogram[v];
        const long long total = static_cast<long long>(out.size());
        // The paper is most of the page; the darkest lines are a sliver.
        const int white = percentile(histogram, total, 0.5);
        const int black = percentile(histogram, total, 0.002);
        if (white - black >= 40)
            for (uint8_t& v : out)
                v = static_cast<uint8_t>(std::clamp((v - black) * 255 / (white - black), 0, 255));
    }
    return out;
}

int suggestThreshold(const std::vector<uint8_t>& leveled)
{
    std::array<int, 256> histogram{};
    for (const uint8_t v : leveled)
        ++histogram[v];
    const double total = static_cast<double>(leveled.size());
    if (total <= 0.0)
        return 170;
    double sumAll = 0.0;
    for (int v = 0; v < 256; ++v)
        sumAll += static_cast<double>(v) * histogram[static_cast<size_t>(v)];
    // Otsu: the split with the most variance between the two sides.
    double best = -1.0;
    int split = 170;
    double weightDark = 0.0;
    double sumDark = 0.0;
    for (int v = 0; v < 255; ++v)
    {
        weightDark += histogram[static_cast<size_t>(v)];
        sumDark += static_cast<double>(v) * histogram[static_cast<size_t>(v)];
        const double weightLight = total - weightDark;
        if (weightDark <= 0.0 || weightLight <= 0.0)
            continue;
        const double meanDark = sumDark / weightDark;
        const double meanLight = (sumAll - sumDark) / weightLight;
        const double between = weightDark * weightLight * (meanDark - meanLight) * (meanDark - meanLight);
        if (between > best)
        {
            best = between;
            split = v + 1;
        }
    }
    return std::clamp(split, 60, 230);
}

std::vector<uint8_t> lineMask(const std::vector<uint8_t>& leveled, int width, int height, const Settings& settings)
{
    std::vector<uint8_t> mask(leveled.size(), 0);
    if (width <= 0 || height <= 0 || leveled.size() != static_cast<size_t>(width) * height)
        return mask;
    const std::vector<uint8_t> soft = boxBlur(leveled, width, height, std::clamp(settings.smoothing, 0, 3));
    for (size_t i = 0; i < soft.size(); ++i)
        mask[i] = soft[i] < settings.threshold ? 1 : 0;
    if (settings.closeGaps > 0)
    {
        // Grown over the gap, then shrunk back: what the growing joined
        // stays joined.
        const int radius = (settings.closeGaps + 1) / 2;
        mask = morph(morph(mask, width, height, radius, true), width, height, radius, false);
    }
    if (settings.despeckle > 0)
    {
        removeSmall(mask, width, height, 1, settings.despeckle, false);
        removeSmall(mask, width, height, 0, settings.despeckle, true);
    }
    return mask;
}

std::vector<uint8_t> cleanup(const uint8_t* rgba, int width, int height, const Settings& settings, int* linePixels)
{
    const std::vector<uint8_t> leveled =
        level(lightness(rgba, width, height), width, height, settings.evenPaper, settings.autoLevel);
    const std::vector<uint8_t> mask = lineMask(leveled, width, height, settings);
    std::vector<uint8_t> cleaned(mask.size() * 4, 0);
    int count = 0;
    for (size_t i = 0; i < mask.size(); ++i)
    {
        if (!mask[i])
            continue;
        ++count;
        std::copy(settings.lineColor, settings.lineColor + 4, cleaned.begin() + static_cast<std::ptrdiff_t>(i * 4));
        cleaned[i * 4 + 3] = 255;
    }
    if (linePixels)
        *linePixels = count;
    return cleaned;
}

bool lineBounds(const std::vector<uint8_t>& cleaned, int width, int height, int& left, int& top, int& right,
    int& bottom)
{
    left = width;
    top = height;
    right = -1;
    bottom = -1;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (cleaned[(static_cast<size_t>(y) * width + x) * 4 + 3] >= 128)
            {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    return right >= left;
}

Shape* vectorize(const std::vector<uint8_t>& cleaned, int width, int height, double curveFit)
{
    if (width <= 0 || height <= 0 || cleaned.size() != static_cast<size_t>(width) * height * 4)
        return nullptr;
    trace::Options options;
    // One line color: every line pixel is one area with its neighbors.
    options.colorThreshold = 60;
    options.minimumArea = 1;
    options.curveFit = curveFit;
    return trace::traceBitmap(cleaned.data(), width, height, options);
}

} // namespace paper

} // namespace fla
