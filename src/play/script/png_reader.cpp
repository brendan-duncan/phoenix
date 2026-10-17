// A PNG reader for the images scripts load (Loader, BitmapData classes):
// every color type and depth, interlaced or not, with tRNS transparency.

#include "bindings.h"

#include <zlib.h>

#include <cstring>

namespace fla {
namespace play {
namespace script {

namespace {

uint32_t bigEndian(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

int paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/// Undoes the filters of a pass's rows in place: \a stride bytes a row
/// (the filter byte not counted), \a bpp bytes a pixel (at least 1).
bool unfilter(uint8_t* data, size_t rows, size_t stride, size_t bpp, std::vector<uint8_t>& out)
{
    out.assign(rows * stride, 0);
    const uint8_t* in = data;
    for (size_t y = 0; y < rows; ++y)
    {
        const uint8_t filter = *in++;
        uint8_t* row = out.data() + y * stride;
        const uint8_t* prior = y > 0 ? row - stride : nullptr;
        for (size_t x = 0; x < stride; ++x)
        {
            const int a = x >= bpp ? row[x - bpp] : 0;
            const int b = prior ? prior[x] : 0;
            const int c = prior && x >= bpp ? prior[x - bpp] : 0;
            int value = in[x];
            switch (filter)
            {
            case 0:
                break;
            case 1:
                value += a;
                break;
            case 2:
                value += b;
                break;
            case 3:
                value += (a + b) / 2;
                break;
            case 4:
                value += paeth(a, b, c);
                break;
            default:
                return false;
            }
            row[x] = static_cast<uint8_t>(value);
        }
        in += stride;
    }
    return true;
}

} // namespace

bool decodePng(const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint32_t>& argb)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() < 33 || std::memcmp(bytes.data(), signature, 8) != 0)
        return false;
    int depth = 0, colorType = 0, interlace = 0;
    std::vector<uint8_t> compressed;
    std::vector<uint32_t> palette;
    bool hasKey = false;
    uint16_t keyGray = 0, keyR = 0, keyG = 0, keyB = 0;
    size_t at = 8;
    while (at + 12 <= bytes.size())
    {
        const uint32_t length = bigEndian(&bytes[at]);
        if (length > bytes.size() - at - 12)
            return false;
        const char* type = reinterpret_cast<const char*>(&bytes[at + 4]);
        const uint8_t* data = &bytes[at + 8];
        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13)
        {
            width = static_cast<int>(bigEndian(data));
            height = static_cast<int>(bigEndian(data + 4));
            depth = data[8];
            colorType = data[9];
            interlace = data[12];
        }
        else if (std::memcmp(type, "PLTE", 4) == 0)
        {
            for (uint32_t i = 0; i + 2 < length; i += 3)
                palette.push_back(0xff000000u | (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2]);
        }
        else if (std::memcmp(type, "tRNS", 4) == 0)
        {
            if (colorType == 3)
            {
                for (uint32_t i = 0; i < length && i < palette.size(); ++i)
                    palette[i] = (palette[i] & 0xffffff) | (uint32_t(data[i]) << 24);
            }
            else if (colorType == 0 && length >= 2)
            {
                hasKey = true;
                keyGray = static_cast<uint16_t>((data[0] << 8) | data[1]);
            }
            else if (colorType == 2 && length >= 6)
            {
                hasKey = true;
                keyR = static_cast<uint16_t>((data[0] << 8) | data[1]);
                keyG = static_cast<uint16_t>((data[2] << 8) | data[3]);
                keyB = static_cast<uint16_t>((data[4] << 8) | data[5]);
            }
        }
        else if (std::memcmp(type, "IDAT", 4) == 0)
            compressed.insert(compressed.end(), data, data + length);
        else if (std::memcmp(type, "IEND", 4) == 0)
            break;
        at += 12 + length;
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || compressed.empty())
        return false;
    int channels = 0;
    switch (colorType)
    {
    case 0:
    case 3:
        channels = 1;
        break;
    case 2:
        channels = 3;
        break;
    case 4:
        channels = 2;
        break;
    case 6:
        channels = 4;
        break;
    default:
        return false;
    }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)
        return false;
    const size_t bitsPerPixel = static_cast<size_t>(channels) * depth;
    const size_t bpp = std::max<size_t>(1, bitsPerPixel / 8);

    // Inflated whole: the passes' rows, each with its filter byte.
    std::vector<uint8_t> raw;
    {
        z_stream stream{};
        if (inflateInit(&stream) != Z_OK)
            return false;
        std::vector<uint8_t> chunk(1 << 16);
        stream.next_in = compressed.data();
        stream.avail_in = static_cast<uInt>(compressed.size());
        int result = Z_OK;
        while (result == Z_OK)
        {
            stream.next_out = chunk.data();
            stream.avail_out = static_cast<uInt>(chunk.size());
            result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END)
                break;
            raw.insert(raw.end(), chunk.data(), chunk.data() + (chunk.size() - stream.avail_out));
            if (result == Z_OK && stream.avail_in == 0 && stream.avail_out != 0)
                break;
        }
        inflateEnd(&stream);
        if (result != Z_STREAM_END && result != Z_OK)
            return false;
    }

    argb.assign(static_cast<size_t>(width) * height, 0);
    const auto sample = [depth](const uint8_t* row, size_t index) -> uint16_t {
        switch (depth)
        {
        case 16:
            return static_cast<uint16_t>((row[index * 2] << 8) | row[index * 2 + 1]);
        case 8:
            return row[index];
        default:
        {
            const size_t bit = index * depth;
            const int shift = 8 - depth - static_cast<int>(bit % 8);
            return static_cast<uint16_t>((row[bit / 8] >> shift) & ((1 << depth) - 1));
        }
        }
    };
    const auto to8 = [depth](uint16_t value) -> uint32_t {
        switch (depth)
        {
        case 16:
            return value >> 8;
        case 8:
            return value;
        case 4:
            return value * 17u;
        case 2:
            return value * 85u;
        default:
            return value * 255u;
        }
    };
    const auto pixelAt = [&](const uint8_t* row, size_t x) -> uint32_t {
        const size_t base = x * static_cast<size_t>(channels);
        switch (colorType)
        {
        case 0:
        {
            const uint16_t g = sample(row, base);
            const uint32_t v = to8(g);
            const uint32_t a = hasKey && g == keyGray ? 0 : 255;
            return (a << 24) | (v << 16) | (v << 8) | v;
        }
        case 2:
        {
            const uint16_t r = sample(row, base), g = sample(row, base + 1), b = sample(row, base + 2);
            const uint32_t a = hasKey && r == keyR && g == keyG && b == keyB ? 0 : 255;
            return (a << 24) | (to8(r) << 16) | (to8(g) << 8) | to8(b);
        }
        case 3:
        {
            const uint16_t i = sample(row, base);
            return i < palette.size() ? palette[i] : 0xff000000u;
        }
        case 4:
        {
            const uint32_t v = to8(sample(row, base));
            return (to8(sample(row, base + 1)) << 24) | (v << 16) | (v << 8) | v;
        }
        default:
            return (to8(sample(row, base + 3)) << 24) | (to8(sample(row, base)) << 16) | (to8(sample(row, base + 1)) << 8) |
                to8(sample(row, base + 2));
        }
    };

    // The passes: one for a plain image, Adam7's seven for an interlaced.
    struct Pass
    {
        int x0, y0, dx, dy;
    };
    const std::vector<Pass> passes = interlace
        ? std::vector<Pass>{{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}}
        : std::vector<Pass>{{0, 0, 1, 1}};
    size_t offset = 0;
    std::vector<uint8_t> rows;
    for (const Pass& pass : passes)
    {
        const size_t passWidth = width > pass.x0 ? static_cast<size_t>((width - pass.x0 + pass.dx - 1) / pass.dx) : 0;
        const size_t passHeight = height > pass.y0 ? static_cast<size_t>((height - pass.y0 + pass.dy - 1) / pass.dy) : 0;
        if (passWidth == 0 || passHeight == 0)
            continue;
        const size_t stride = (passWidth * bitsPerPixel + 7) / 8;
        if (offset + passHeight * (stride + 1) > raw.size())
            return false;
        if (!unfilter(raw.data() + offset, passHeight, stride, bpp, rows))
            return false;
        offset += passHeight * (stride + 1);
        for (size_t py = 0; py < passHeight; ++py)
        {
            const uint8_t* row = rows.data() + py * stride;
            const size_t y = pass.y0 + py * pass.dy;
            for (size_t px = 0; px < passWidth; ++px)
                argb[y * width + pass.x0 + px * pass.dx] = pixelAt(row, px);
        }
    }
    return true;
}

} // namespace script
} // namespace play
} // namespace fla
