#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Writing GIF images, which Qt reads but does not write: the colors cut
/// down to a palette of at most 256 (median cut, weighted by how much of
/// the picture each color covers), the pixels LZW-compressed. Free of Qt.
namespace gif {

/// An image of \a width by \a height pixels, \a rgba holding four bytes a
/// pixel (not premultiplied). Pixels whose alpha is under half become the
/// transparent color when \a transparent; otherwise alpha is ignored.
/// Returns the GIF file's bytes, empty when the size is out of range.
std::vector<uint8_t> encode(int width, int height, const std::vector<uint8_t>& rgba, bool transparent);

} // namespace gif
