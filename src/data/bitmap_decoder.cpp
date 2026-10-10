#include "bitmap_decoder.h"

#include <cstdint>
#include <zlib.h>

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

} // namespace fla
