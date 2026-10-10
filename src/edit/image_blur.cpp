#include "image_blur.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fla {

namespace {

/// One pass of a box of radius r along one line of pixels, one channel at a
/// time through the running sum. \a stride steps between neighbours, so the
/// same code serves rows and columns.
void blurLine(const uint32_t* in, uint32_t* out, int count, int stride, int radius)
{
    const int window = 2 * radius + 1;
    int sum[4] = {0, 0, 0, 0};

    const auto channel = [](uint32_t pixel, int c) {
        return static_cast<int>((pixel >> (8 * c)) & 0xff);
    };
    const auto at = [&](int i) {
        return in[std::max(0, std::min(count - 1, i)) * stride];
    };

    // Edge pixels are clamped, as the picture beyond the image is taken to
    // continue its border, which keeps an object's edge from darkening.
    for (int i = -radius; i <= radius; ++i)
        for (int c = 0; c < 4; ++c)
            sum[c] += channel(at(i), c);

    for (int i = 0; i < count; ++i)
    {
        uint32_t pixel = 0;
        for (int c = 0; c < 4; ++c)
            pixel |= static_cast<uint32_t>((sum[c] + window / 2) / window) << (8 * c);
        out[i * stride] = pixel;

        const uint32_t leaving = at(i - radius);
        const uint32_t arriving = at(i + radius + 1);
        for (int c = 0; c < 4; ++c)
            sum[c] += channel(arriving, c) - channel(leaving, c);
    }
}

} // namespace

void boxBlur(uint32_t* pixels, int width, int height, double widthX, double widthY, int passes)
{
    if (!pixels || width <= 0 || height <= 0)
        return;

    // A box of width N reaches N/2 either side, rounded down: a width of
    // five is the five pixels centred on each one.
    const int radiusX = static_cast<int>(std::floor(std::max(0.0, widthX) / 2.0));
    const int radiusY = static_cast<int>(std::floor(std::max(0.0, widthY) / 2.0));
    passes = std::max(1, std::min(15, passes));
    if (radiusX == 0 && radiusY == 0)
        return;

    std::vector<uint32_t> scratch(static_cast<size_t>(width) * height);

    for (int pass = 0; pass < passes; ++pass)
    {
        if (radiusX > 0)
        {
            for (int y = 0; y < height; ++y)
                blurLine(pixels + static_cast<size_t>(y) * width, scratch.data() + static_cast<size_t>(y) * width, width, 1, radiusX);
            std::copy(scratch.begin(), scratch.end(), pixels);
        }
        if (radiusY > 0)
        {
            for (int x = 0; x < width; ++x)
                blurLine(pixels + x, scratch.data() + x, height, width, radiusY);
            std::copy(scratch.begin(), scratch.end(), pixels);
        }
    }
}

} // namespace fla
