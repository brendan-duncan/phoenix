#include "test_util.h"

#include "../src/edit/image_blur.h"
#include "../src/util/parallel.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

/// The box blur as it was first written, a channel and a pixel at a time,
/// to check the faster one against.
void referenceBlur(std::vector<uint32_t>& pixels, int width, int height, double widthX, double widthY, int passes)
{
    const int radiusX = static_cast<int>(std::floor(std::max(0.0, widthX) / 2.0));
    const int radiusY = static_cast<int>(std::floor(std::max(0.0, widthY) / 2.0));
    const auto line = [](const uint32_t* in, uint32_t* out, int count, int stride, int radius) {
        const int window = 2 * radius + 1;
        for (int i = 0; i < count; ++i)
        {
            uint32_t pixel = 0;
            for (int c = 0; c < 4; ++c)
            {
                int sum = 0;
                for (int k = i - radius; k <= i + radius; ++k)
                    sum += (in[std::clamp(k, 0, count - 1) * stride] >> (8 * c)) & 0xff;
                pixel |= static_cast<uint32_t>((sum + window / 2) / window) << (8 * c);
            }
            out[i * stride] = pixel;
        }
    };
    std::vector<uint32_t> scratch(pixels.size());
    for (int pass = 0; pass < passes; ++pass)
    {
        if (radiusX > 0)
        {
            for (int y = 0; y < height; ++y)
                line(pixels.data() + y * width, scratch.data() + y * width, width, 1, radiusX);
            pixels = scratch;
        }
        if (radiusY > 0)
        {
            for (int x = 0; x < width; ++x)
                line(pixels.data() + x, scratch.data() + x, height, width, radiusY);
            pixels = scratch;
        }
    }
}

} // namespace

TEST(parallel_for_covers_every_index_once)
{
    for (size_t count : {size_t(0), size_t(1), size_t(7), size_t(1000), size_t(100003)})
        for (size_t grain : {size_t(1), size_t(64), size_t(5000)})
        {
            std::vector<std::atomic<int>> seen(count);
            fla::parallelFor(count, grain, [&](size_t from, size_t to) {
                CHECK(from < to && to <= count);
                for (size_t i = from; i < to; ++i)
                    ++seen[i];
            });
            CHECK(std::all_of(seen.begin(), seen.end(), [](const std::atomic<int>& n) { return n == 1; }));
        }
    // Asked for again from inside a range: done there, every index still once.
    std::vector<std::atomic<int>> seen(64 * 64);
    fla::parallelRows(64, 64, [&](int y) {
        fla::parallelPixels(64, [&](size_t from, size_t to) {
            for (size_t x = from; x < to; ++x)
                ++seen[static_cast<size_t>(y) * 64 + x];
        });
    });
    CHECK(std::all_of(seen.begin(), seen.end(), [](const std::atomic<int>& n) { return n == 1; }));
    CHECK(fla::parallelThreads() >= 1);
}

TEST(box_blur_matches_a_pixel_at_a_time)
{
    std::mt19937 random(7);
    for (const auto& [width, height, bx, by, passes] :
        std::vector<std::tuple<int, int, double, double, int>>{{1, 1, 5, 5, 1}, {37, 23, 9, 3, 3}, {300, 17, 40, 0, 2},
            {5, 211, 0, 31, 3}, {129, 77, 6, 12, 1}, {64, 64, 200, 200, 3}})
    {
        std::vector<uint32_t> image(static_cast<size_t>(width) * height);
        for (uint32_t& pixel : image)
        {
            // Premultiplied: no channel above the alpha.
            const uint32_t a = random() & 0xFF;
            pixel = a << 24;
            for (int c = 0; c < 3; ++c)
                pixel |= (a ? random() % (a + 1) : 0u) << (8 * c);
        }
        std::vector<uint32_t> fast = image;
        fla::boxBlur(fast.data(), width, height, bx, by, passes);
        referenceBlur(image, width, height, bx, by, passes);
        CHECK(fast == image);
    }
}

TEST(parallel_for_passes_an_exception_back_after_every_range)
{
    // One range throws: every other range still runs, the caller gets the
    // exception, and the pool works on afterwards.
    std::vector<std::atomic<int>> seen(10000);
    bool caught = false;
    try
    {
        fla::parallelFor(seen.size(), 100, [&](size_t from, size_t to) {
            for (size_t i = from; i < to; ++i)
                ++seen[i];
            if (from == 0)
                throw std::runtime_error("out of room");
        });
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    CHECK(caught);
    CHECK(std::all_of(seen.begin(), seen.end(), [](const std::atomic<int>& n) { return n == 1; }));
    std::atomic<int> again{0};
    fla::parallelFor(1000, 10, [&](size_t from, size_t to) { again += static_cast<int>(to - from); });
    CHECK(again == 1000);
}
