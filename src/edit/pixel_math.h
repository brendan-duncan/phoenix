#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#include <emmintrin.h>
#define PHOENIX_PIXEL_SSE2 1
#endif

namespace fla {

/// The four channels of 0xAARRGGBB pixels summed with weights, as doubles:
/// two channels at a time on processors with SSE2, one at a time on the
/// rest. The same sums either way, added in the same order, so a picture
/// comes out the same on every machine.
struct ChannelSum
{
#ifdef PHOENIX_PIXEL_SSE2
    __m128d low = _mm_setzero_pd();  // blue, green
    __m128d high = _mm_setzero_pd(); // red, alpha

    void add(uint32_t pixel, double weight)
    {
        const __m128i zero = _mm_setzero_si128();
        const __m128i words = _mm_unpacklo_epi8(_mm_cvtsi32_si128(static_cast<int>(pixel)), zero);
        const __m128i ints = _mm_unpacklo_epi16(words, zero);
        const __m128d w = _mm_set1_pd(weight);
        low = _mm_add_pd(low, _mm_mul_pd(_mm_cvtepi32_pd(ints), w));
        high = _mm_add_pd(high, _mm_mul_pd(_mm_cvtepi32_pd(_mm_shuffle_epi32(ints, 0x0E)), w));
    }
    void get(double out[4]) const
    {
        _mm_storeu_pd(out, low);
        _mm_storeu_pd(out + 2, high);
    }
#else
    double sum[4] = {0, 0, 0, 0};

    void add(uint32_t pixel, double weight)
    {
        for (int c = 0; c < 4; ++c)
            sum[c] += ((pixel >> (c * 8)) & 0xFF) * weight;
    }
    void get(double out[4]) const
    {
        for (int c = 0; c < 4; ++c)
            out[c] = sum[c];
    }
#endif

    /// The sums, each divided by \a divisor (1 for none), rounded and
    /// clamped to 0-255, as a pixel again.
    uint32_t pixel(double divisor = 1.0) const
    {
        double sums[4];
        get(sums);
        uint32_t out = 0;
        for (int c = 0; c < 4; ++c)
        {
            const double v = divisor == 1.0 ? sums[c] : sums[c] / divisor;
            out |= static_cast<uint32_t>(std::lround(std::clamp(v, 0.0, 255.0))) << (c * 8);
        }
        return out;
    }
};

/// The premultiplied pixel at a place between pixels, its four neighbors
/// mixed; transparent outside the image.
inline uint32_t samplePixel(const uint32_t* pixels, int width, int height, double x, double y)
{
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    ChannelSum sum;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
        {
            const int px = x0 + dx, py = y0 + dy;
            if (px < 0 || py < 0 || px >= width || py >= height)
                continue;
            sum.add(pixels[static_cast<size_t>(py) * width + px], (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy));
        }
    return sum.pixel();
}

} // namespace fla
