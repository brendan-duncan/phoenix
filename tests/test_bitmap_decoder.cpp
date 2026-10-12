#include "test_util.h"

#include "../src/data/bitmap_decoder.h"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

void putU16(std::vector<uint8_t>& out, unsigned value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void putU32(std::vector<uint8_t>& out, unsigned value)
{
    putU16(out, value & 0xffff);
    putU16(out, (value >> 16) & 0xffff);
}

/// A .dat as Animate writes one: the header seen in real files, then the
/// rows deflated.
std::vector<uint8_t> losslessDat(int width, int height, int bytesPerPixel,
    const std::vector<uint8_t>& rows)
{
    std::vector<uint8_t> out;
    putU16(out, 0x0503);
    putU16(out, static_cast<unsigned>(width * bytesPerPixel));
    putU16(out, static_cast<unsigned>(width));
    putU16(out, static_cast<unsigned>(height));
    putU32(out, 0);
    putU32(out, static_cast<unsigned>(width * 20));
    putU32(out, 0);
    putU32(out, static_cast<unsigned>(height * 20));
    out.push_back(0);
    out.push_back(1);

    uLongf size = compressBound(static_cast<uLong>(rows.size()));
    std::vector<uint8_t> packed(size);
    compress(packed.data(), &size, rows.data(), static_cast<uLong>(rows.size()));
    packed.resize(size);

    // The stream goes in pieces of a few bytes, as the real files cut it
    // into pieces of 2048, ended by an empty one.
    for (size_t at = 0; at < packed.size(); at += 5)
    {
        const size_t length = std::min<size_t>(5, packed.size() - at);
        putU16(out, static_cast<unsigned>(length));
        out.insert(out.end(), packed.begin() + at, packed.begin() + at + length);
    }
    putU16(out, 0);
    return out;
}

} // namespace

TEST(lossless_dat_decodes_to_argb_pixels)
{
    // Two by two: opaque red, half-transparent green (premultiplied), clear,
    // opaque blue.
    const std::vector<uint8_t> rows = {
        0xff, 0xff, 0x00, 0x00,  0x80, 0x00, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00,  0xff, 0x00, 0x00, 0xff,
    };
    const std::vector<uint8_t> dat = losslessDat(2, 2, 4, rows);
    CHECK(fla::isLosslessBitmapData(dat));
    CHECK(!fla::isJpegData(dat));

    fla::DecodedBitmap decoded;
    CHECK(fla::decodeLosslessBitmap(dat, decoded));
    CHECK(decoded.width == 2);
    CHECK(decoded.height == 2);
    CHECK(decoded.pixels.size() == 4);
    CHECK(decoded.pixels[0] == 0xffff0000u);
    CHECK(decoded.pixels[1] == 0x80008000u);
    CHECK(decoded.pixels[2] == 0x00000000u);
    CHECK(decoded.pixels[3] == 0xff0000ffu);
}

TEST(lossless_dat_without_alpha_reads_three_bytes_a_pixel)
{
    const std::vector<uint8_t> rows = {
        0x10, 0x20, 0x30,  0x40, 0x50, 0x60,
    };
    const std::vector<uint8_t> dat = losslessDat(2, 1, 3, rows);
    fla::DecodedBitmap decoded;
    CHECK(fla::decodeLosslessBitmap(dat, decoded));
    CHECK(decoded.pixels.size() == 2);
    CHECK(decoded.pixels[0] == 0xff102030u);
    CHECK(decoded.pixels[1] == 0xff405060u);
}

TEST(damaged_dat_is_refused)
{
    std::vector<uint8_t> junk(40, 0x11);
    fla::DecodedBitmap decoded;
    CHECK(!fla::decodeLosslessBitmap(junk, decoded));
    CHECK(!fla::isLosslessBitmapData(std::vector<uint8_t>{0x03, 0x05}));
    CHECK(fla::isJpegData(std::vector<uint8_t>{0xff, 0xd8, 0xff, 0xe0}));
}

TEST(lossless_bitmaps_encode_as_animate_reads_them)
{
    const std::vector<uint32_t> pixels = {0xff112233u, 0x80402010u, 0x00000000u, 0xffffffffu, 0xff000000u, 0x7f7f7f7fu};
    const std::vector<uint8_t> data = fla::encodeLosslessBitmap(3, 2, pixels);
    CHECK(fla::isLosslessBitmapData(data));
    // The header as Animate writes it: size in pixels, frame in twips.
    CHECK(data[4] == 3 && data[6] == 2 && data[12] == 60 && data[20] == 40 && data[25] == 1);
    fla::DecodedBitmap decoded;
    CHECK(fla::decodeLosslessBitmap(data, decoded));
    CHECK(decoded.width == 3 && decoded.height == 2);
    CHECK(decoded.pixels == pixels);
}
