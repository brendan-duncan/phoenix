#pragma once

#include <cstdint>
#include <vector>

namespace fla {

/// Pixels of a decoded library bitmap, top row first, each 0xAARRGGBB with
/// the color already multiplied by the alpha.
struct DecodedBitmap
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
};

/// Whether the bytes are a JPEG stream, which the .dat of a photo-compressed
/// bitmap is and which any image library reads.
bool isJpegData(const std::vector<uint8_t>& data);

/// Whether the bytes are the FLA's lossless bitmap encoding: a short header
/// (magic 0x0503, bytes per row, width, height, the size in twips) and a
/// zlib stream of the rows, stored in length-prefixed pieces. Each pixel is
/// alpha, red, green, blue.
bool isLosslessBitmapData(const std::vector<uint8_t>& data);

/// Decodes a lossless .dat into pixels. False when the bytes are not one or
/// the stream is damaged.
bool decodeLosslessBitmap(const std::vector<uint8_t>& data, DecodedBitmap& out);

/// Encodes pixels (0xAARRGGBB, premultiplied, top row first) as the FLA's
/// lossless .dat: the header, then the rows deflated and stored in pieces
/// of 2048 bytes, as FLA files store them.
std::vector<uint8_t> encodeLosslessBitmap(int width, int height, const std::vector<uint32_t>& pixels);

} // namespace fla
