#pragma once

#include <cstdint>
#include <vector>

namespace fla {

class Shape;

/// Trace Bitmap: a picture turned into vector artwork. Neighboring pixels
/// of like color become one filled area, in the area's average color, and
/// every boundary between two areas becomes one edge with each area's fill
/// on its side, so the areas meet without gaps. Free of Qt.
namespace trace {

struct Options
{
    /// How far apart two colors may be (the sum of the red, green and blue
    /// differences) and still count as one area: 0 to 500.
    int colorThreshold = 100;
    /// Areas of fewer pixels are folded into the neighbor they share the
    /// most edge with.
    int minimumArea = 8;
    /// How far, in pixels, the traced outline may stray from the pixels'
    /// edges for fewer points: small for close outlines, larger for smooth.
    double curveFit = 1.0;
};

/// The settings Curve fit offers, from closest to smoothest, with their
/// names and the curveFit each gives.
struct CurveFit
{
    const char* label;
    const char* name;
    double tolerance;
};
extern const CurveFit kCurveFits[6];

/// Each pixel's area, or -1 for a (mostly) transparent one, row by row;
/// \a count is how many areas there are.
std::vector<int> regions(const uint8_t* rgba, int width, int height, const Options& options, int& count);

/// The traced artwork of \a rgba (\a width by \a height, straight RGBA, row
/// by row) as a merge shape, a pixel to a unit, its top-left at (0, 0).
/// Null for a picture with nothing opaque. The caller owns it.
Shape* traceBitmap(const uint8_t* rgba, int width, int height, const Options& options);

} // namespace trace

} // namespace fla
