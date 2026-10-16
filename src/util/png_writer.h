#pragma once

#include <cstdint>
#include <vector>

/// Writing PNG images without Qt, for the parts of Phoenix free of it (a
/// script exporting a library bitmap from the command line).
namespace png {

/// A PNG of \a width by \a height pixels, straight (not premultiplied)
/// RGBA, top row first. Empty when the size is not one a PNG holds.
std::vector<uint8_t> encodeRgba(int width, int height, const std::vector<uint8_t>& rgba);

} // namespace png
