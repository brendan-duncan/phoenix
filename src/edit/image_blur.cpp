#include "image_blur.h"

#include "../util/parallel.h"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace fla {

namespace {

/// The average of a box of \a window values from their sum, rounded: a
/// table, since the same divisor serves every pixel of a pass.
std::vector<uint8_t> averages(int window)
{
    std::vector<uint8_t> table(static_cast<size_t>(255) * window + 1);
    for (size_t sum = 0; sum < table.size(); ++sum)
        table[sum] = static_cast<uint8_t>((sum + window / 2) / window);
    return table;
}

/// One pass of a box of radius r along a row of pixels, through a running
/// sum of each channel. Pixels past the ends are taken to be the end ones,
/// which keeps an object's edge from darkening.
void blurRow(const uint8_t* in, uint8_t* out, int count, int radius, const uint8_t* table)
{
    int sum[4] = {0, 0, 0, 0};
    const auto at = [in, count](int i) { return in + static_cast<size_t>(std::clamp(i, 0, count - 1)) * 4; };
    for (int i = -radius; i <= radius; ++i)
    {
        const uint8_t* p = at(i);
        for (int c = 0; c < 4; ++c)
            sum[c] += p[c];
    }
    for (int i = 0; i < count; ++i)
    {
        uint8_t* o = out + static_cast<size_t>(i) * 4;
        for (int c = 0; c < 4; ++c)
            o[c] = table[sum[c]];
        const uint8_t* leaving = at(i - radius);
        const uint8_t* arriving = at(i + radius + 1);
        for (int c = 0; c < 4; ++c)
            sum[c] += arriving[c] - leaving[c];
    }
}

/// One pass of a box of radius r down columns \a from to \a to: the sums of
/// a whole strip of the row at a time, moving down a row at a time, so the
/// memory is read in order and the sums of neighboring channels add up
/// together.
void blurColumns(const uint8_t* in, uint8_t* out, int width, int height, int from, int to, int radius,
    const uint8_t* table)
{
    const size_t stride = static_cast<size_t>(width) * 4;
    const int span = (to - from) * 4;
    std::vector<int> sum(static_cast<size_t>(span), 0);
    const auto row = [in, stride, from, height](int y) {
        return in + static_cast<size_t>(std::clamp(y, 0, height - 1)) * stride + static_cast<size_t>(from) * 4;
    };
    for (int y = -radius; y <= radius; ++y)
    {
        const uint8_t* r = row(y);
        for (int k = 0; k < span; ++k)
            sum[static_cast<size_t>(k)] += r[k];
    }
    for (int y = 0; y < height; ++y)
    {
        uint8_t* o = out + static_cast<size_t>(y) * stride + static_cast<size_t>(from) * 4;
        for (int k = 0; k < span; ++k)
            o[k] = table[sum[static_cast<size_t>(k)]];
        const uint8_t* leaving = row(y - radius);
        const uint8_t* arriving = row(y + radius + 1);
        for (int k = 0; k < span; ++k)
            sum[static_cast<size_t>(k)] += arriving[k] - leaving[k];
    }
}

} // namespace

void boxBlur(uint32_t* pixels, int width, int height, double widthX, double widthY, int passes)
{
    if (!pixels || width <= 0 || height <= 0)
        return;

    // A box of width N reaches N/2 either side, rounded down: a width of
    // five is the five pixels centered on each one.
    // Kept to a width that still means something (a file or a setting may
    // ask for any), so the sums and tables stay sound; not a number is none.
    constexpr double kWidest = 4096.0;
    const int radiusX = static_cast<int>(std::floor(std::clamp(std::isfinite(widthX) ? widthX : 0.0, 0.0, kWidest) / 2.0));
    const int radiusY = static_cast<int>(std::floor(std::clamp(std::isfinite(widthY) ? widthY : 0.0, 0.0, kWidest) / 2.0));
    passes = std::max(1, std::min(15, passes));
    if (radiusX == 0 && radiusY == 0)
        return;

    std::vector<uint32_t> scratch(static_cast<size_t>(width) * height);
    uint8_t* image = reinterpret_cast<uint8_t*>(pixels);
    uint8_t* other = reinterpret_cast<uint8_t*>(scratch.data());
    const size_t stride = static_cast<size_t>(width) * 4;
    const std::vector<uint8_t> acrossTable = averages(2 * radiusX + 1);
    const std::vector<uint8_t> downTable = averages(2 * radiusY + 1);
    // Strips of columns for the downward passes, each its own thread's.
    // Narrower on a narrow image, so every thread gets some.
    const int kStrip = std::clamp(width / std::max(1, parallelThreads()) / 16 * 16, 16, 64);
    const int strips = (width + kStrip - 1) / kStrip;

    for (int pass = 0; pass < passes; ++pass)
    {
        if (radiusX > 0)
        {
            parallelRows(height, width, [&](int y) {
                blurRow(image + y * stride, other + y * stride, width, radiusX, acrossTable.data());
            });
            std::memcpy(image, other, stride * height);
        }
        if (radiusY > 0)
        {
            parallelFor(static_cast<size_t>(strips), std::max<size_t>(1, 8192 / (static_cast<size_t>(kStrip) * height)), [&](size_t first, size_t last) {
                for (size_t s = first; s < last; ++s)
                {
                    const int from = static_cast<int>(s) * kStrip;
                    blurColumns(image, other, width, height, from, std::min(width, from + kStrip), radiusY, downTable.data());
                }
            });
            std::memcpy(image, other, stride * height);
        }
    }
}

} // namespace fla
