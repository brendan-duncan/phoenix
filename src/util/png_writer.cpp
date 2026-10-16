#include "png_writer.h"

#include "zlib.h"

#include <cstring>

namespace png {

namespace {

void put32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

/// A chunk: its length, type, data and the CRC of type and data.
void chunk(std::vector<uint8_t>& out, const char type[5], const uint8_t* data, size_t size)
{
    put32(out, static_cast<uint32_t>(size));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (size)
        out.insert(out.end(), data, data + size);
    const uLong crc = crc32(crc32(0L, Z_NULL, 0), out.data() + start, static_cast<uInt>(4 + size));
    put32(out, static_cast<uint32_t>(crc));
}

} // namespace

std::vector<uint8_t> encodeRgba(int width, int height, const std::vector<uint8_t>& rgba)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return {};
    // Each row with filter type 0 (none) before it.
    const size_t stride = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> raw((stride + 1) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        uint8_t* row = raw.data() + (stride + 1) * static_cast<size_t>(y);
        row[0] = 0;
        std::memcpy(row + 1, rgba.data() + stride * static_cast<size_t>(y), stride);
    }
    uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> packed(packedSize);
    if (compress2(packed.data(), &packedSize, raw.data(), static_cast<uLong>(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK)
        return {};

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> header;
    put32(header, static_cast<uint32_t>(width));
    put32(header, static_cast<uint32_t>(height));
    // 8 bits a channel, RGBA, deflate, adaptive filtering, no interlace.
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", header.data(), header.size());
    chunk(out, "IDAT", packed.data(), packedSize);
    chunk(out, "IEND", nullptr, 0);
    return out;
}

} // namespace png
