#pragma once

#include "../data/point.h"

#include <cstdint>
#include <vector>

namespace fla {

/// The Magic Wand, for a bitmap broken apart into a fill: the area of
/// like color around a pixel, and its outline as loops to cut the fill by.
namespace MagicWand {

/// How the outline of the area is tidied, as the Smoothing setting
/// offers it.
enum class Smoothing
{
    /// Along the pixel edges, every step kept.
    Pixels,
    /// Straightened roughly: steps within a pixel and a half go.
    Rough,
    /// The steps cut across at their middles, then straightened a little.
    Normal,
    /// As Normal, then rounded off.
    Smooth
};

/// The pixels joined to (\a x, \a y), side to side, whose color is within
/// \a threshold of its own on each of red, green, blue and alpha (0 to 255
/// apart): a mask of width * height, 1 for in. \a pixels are 0xAARRGGBB,
/// row after row. Empty when the pixel is outside the image.
std::vector<uint8_t> region(const uint32_t* pixels, int width, int height, int x, int y, int threshold);

/// A mask with its strands and its gaps narrower than 2 * \a radius + 1
/// pixels taken out: closed (gaps fill), then opened (strands go).
/// Smoothing wants this; thin strands of a pixel make outlines whose sides
/// run so close that cutting a fill by them goes wrong. Pixels beyond the
/// image count as the nearest edge pixel.
std::vector<uint8_t> tidy(const std::vector<uint8_t>& mask, int width, int height, int radius);

/// The outline of a mask: a loop round each piece and each hole in it,
/// along the pixel edges, corners in pixel coordinates (a pixel's square
/// runs from x to x + 1). Straight runs are one side each.
std::vector<std::vector<Point>> outline(const std::vector<uint8_t>& mask, int width, int height);

/// Whether a loop crosses itself.
bool selfIntersects(const std::vector<Point>& loop);

/// A loop tidied as \a smoothing says, never crossing itself: where tidying
/// would make it cross, it is tidied less.
std::vector<Point> smooth(const std::vector<Point>& loop, Smoothing smoothing);

} // namespace MagicWand

} // namespace fla
