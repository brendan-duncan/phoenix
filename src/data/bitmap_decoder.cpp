#include "bitmap_decoder.h"

#include <cstdint>
#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace fla {

namespace {

uint16_t readU16(const std::vector<uint8_t>& data, size_t at)
{
    return static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
}

/// The zlib stream, which is stored in pieces: from byte 26 on, a little
/// endian u16 length and that many bytes, over and over, until a length of
/// zero. Joined, the pieces are one stream.
bool gatherStream(const std::vector<uint8_t>& data, std::vector<uint8_t>& stream)
{
    size_t at = 26;
    while (at + 2 <= data.size())
    {
        const size_t length = readU16(data, at);
        at += 2;
        if (length == 0)
            return !stream.empty();
        if (at + length > data.size())
        {
            // Cut short: keep what there is.
            stream.insert(stream.end(), data.begin() + at, data.end());
            return !stream.empty();
        }
        stream.insert(stream.end(), data.begin() + at, data.begin() + at + length);
        at += length;
    }
    return !stream.empty();
}

} // namespace

bool isJpegData(const std::vector<uint8_t>& data)
{
    return data.size() > 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
}

bool isLosslessBitmapData(const std::vector<uint8_t>& data)
{
    return data.size() > 28 && readU16(data, 0) == 0x0503;
}

bool decodeLosslessBitmap(const std::vector<uint8_t>& data, DecodedBitmap& out)
{
    if (!isLosslessBitmapData(data))
        return false;

    const size_t rowBytes = readU16(data, 2);
    const int width = readU16(data, 4);
    const int height = readU16(data, 6);
    if (width <= 0 || height <= 0 || rowBytes < static_cast<size_t>(width) * 3)
        return false;

    std::vector<uint8_t> packed;
    if (!gatherStream(data, packed))
        return false;

    // The header's size is not trusted with memory: past what the stream
    // could inflate to (deflate shrinks by at most about 1032 to 1), or past
    // a quarter billion pixels, the file is damaged.
    const uint64_t claimed = static_cast<uint64_t>(rowBytes) * static_cast<uint64_t>(height);
    if (static_cast<uint64_t>(width) * height > (uint64_t(1) << 28) ||
        claimed > static_cast<uint64_t>(packed.size()) * 1032 + 1024)
        return false;

    std::vector<uint8_t> rows(rowBytes * static_cast<size_t>(height));

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (inflateInit(&stream) != Z_OK)
        return false;
    stream.next_in = packed.data();
    stream.avail_in = static_cast<uInt>(packed.size());
    stream.next_out = rows.data();
    stream.avail_out = static_cast<uInt>(rows.size());
    const int result = inflate(&stream, Z_FINISH);
    const size_t produced = rows.size() - stream.avail_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END && result != Z_OK && result != Z_BUF_ERROR)
        return false;
    if (produced < rows.size())
    {
        // A short stream: keep what came, the rest stays clear.
        std::memset(rows.data() + produced, 0, rows.size() - produced);
    }

    const bool hasAlpha = rowBytes >= static_cast<size_t>(width) * 4;
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* row = rows.data() + rowBytes * static_cast<size_t>(y);
        uint32_t* line = out.pixels.data() + static_cast<size_t>(width) * y;
        for (int x = 0; x < width; ++x)
        {
            if (hasAlpha)
            {
                const uint8_t* p = row + x * 4;
                line[x] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                    (static_cast<uint32_t>(p[2]) << 8) | p[3];
            }
            else
            {
                const uint8_t* p = row + x * 3;
                line[x] = 0xff000000u | (static_cast<uint32_t>(p[0]) << 16) |
                    (static_cast<uint32_t>(p[1]) << 8) | p[2];
            }
        }
    }
    return true;
}

std::vector<uint8_t> encodeLosslessBitmap(int width, int height, const std::vector<uint32_t>& pixels)
{
    std::vector<uint8_t> out;
    if (width <= 0 || height <= 0 || width > 0xffff / 4 || height > 0xffff ||
        pixels.size() < static_cast<size_t>(width) * height)
        return out;

    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> rows(rowBytes * height);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
    {
        const uint32_t p = pixels[i];
        rows[i * 4] = static_cast<uint8_t>(p >> 24);
        rows[i * 4 + 1] = static_cast<uint8_t>(p >> 16);
        rows[i * 4 + 2] = static_cast<uint8_t>(p >> 8);
        rows[i * 4 + 3] = static_cast<uint8_t>(p);
    }
    uLongf packedSize = compressBound(static_cast<uLong>(rows.size()));
    std::vector<uint8_t> packed(packedSize);
    if (compress2(packed.data(), &packedSize, rows.data(), static_cast<uLong>(rows.size()), Z_BEST_COMPRESSION) != Z_OK)
        return out;
    packed.resize(packedSize);

    const auto u16 = [&out](uint32_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    };
    const auto u32 = [&u16](uint32_t v) {
        u16(v & 0xffff);
        u16(v >> 16);
    };
    // Magic, bytes per row, size in pixels, then the frame in twips (left,
    // right, top, bottom) and the alpha flag.
    u16(0x0503);
    u16(static_cast<uint32_t>(rowBytes));
    u16(static_cast<uint32_t>(width));
    u16(static_cast<uint32_t>(height));
    u32(0);
    u32(static_cast<uint32_t>(width) * 20);
    u32(0);
    u32(static_cast<uint32_t>(height) * 20);
    out.push_back(0);
    out.push_back(1);
    for (size_t at = 0; at < packed.size(); at += 2048)
    {
        const size_t length = std::min<size_t>(2048, packed.size() - at);
        u16(static_cast<uint32_t>(length));
        out.insert(out.end(), packed.begin() + at, packed.begin() + at + length);
    }
    u16(0);
    return out;
}

} // namespace fla
