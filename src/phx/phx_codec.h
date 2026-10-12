#pragma once

#include <cmath>
#include <cstdint>

/// Conversions shared by the .phx writer and reader.
namespace phx {

constexpr uint32_t kOpaqueBlack = 0xFF000000;
constexpr uint32_t kOpaqueWhite = 0xFFFFFFFF;

/// RGBA bytes as the format's fixed32, red in the low byte.
inline uint32_t packColor(const uint8_t color[4])
{
    return static_cast<uint32_t>(color[0]) | (static_cast<uint32_t>(color[1]) << 8) |
        (static_cast<uint32_t>(color[2]) << 16) | (static_cast<uint32_t>(color[3]) << 24);
}

inline void unpackColor(uint32_t packed, uint8_t color[4])
{
    for (int i = 0; i < 4; ++i)
        color[i] = static_cast<uint8_t>(packed >> (8 * i));
}

/// Geometry is stored on Animate's grid, 1/256 of a twip: 5120 to a pixel.
constexpr double kUnitsPerPixel = 5120.0;

inline int64_t toUnits(double pixels)
{
    return static_cast<int64_t>(std::llround(pixels * kUnitsPerPixel));
}

inline double fromUnits(int64_t units)
{
    return static_cast<double>(units) / kUnitsPerPixel;
}

} // namespace phx
