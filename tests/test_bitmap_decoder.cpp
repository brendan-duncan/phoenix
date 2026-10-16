#include "test_util.h"

#include "../src/data/bitmap_decoder.h"
#include "../src/util/gif_writer.h"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
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

/// A .dat as FLA files store one: the header seen in real files, then the
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

TEST(dat_with_a_huge_size_and_a_tiny_stream_is_refused)
{
    // 65535 by 65535 claimed from a few bytes of stream: refused, not given
    // gigabytes.
    std::vector<uint8_t> dat = losslessDat(4, 1, 4, std::vector<uint8_t>(16, 0x40));
    dat[2] = dat[3] = 0xff; // bytes per row
    dat[4] = dat[5] = 0xff; // width
    dat[6] = dat[7] = 0xff; // height
    fla::DecodedBitmap decoded;
    CHECK(!fla::decodeLosslessBitmap(dat, decoded));
    CHECK(decoded.pixels.empty());

    // Rows far wider than four bytes a pixel are damage too.
    dat = losslessDat(4, 1, 4, std::vector<uint8_t>(16, 0x40));
    dat[2] = dat[3] = 0xff;
    CHECK(!fla::decodeLosslessBitmap(dat, decoded));
    // Padded to four bytes is still read.
    dat = losslessDat(1, 2, 4, std::vector<uint8_t>(14, 0x40));
    dat[2] = 7;
    CHECK(fla::decodeLosslessBitmap(dat, decoded));
}

TEST(lossless_bitmaps_encode_as_animate_reads_them)
{
    const std::vector<uint32_t> pixels = {0xff112233u, 0x80402010u, 0x00000000u, 0xffffffffu, 0xff000000u, 0x7f7f7f7fu};
    const std::vector<uint8_t> data = fla::encodeLosslessBitmap(3, 2, pixels);
    CHECK(fla::isLosslessBitmapData(data));
    // The header as FLA files store it: size in pixels, frame in twips.
    CHECK(data[4] == 3 && data[6] == 2 && data[12] == 60 && data[20] == 40 && data[25] == 1);
    fla::DecodedBitmap decoded;
    CHECK(fla::decodeLosslessBitmap(data, decoded));
    CHECK(decoded.width == 3 && decoded.height == 2);
    CHECK(decoded.pixels == pixels);
}

TEST(gif_writer_writes_a_gif_with_its_colors_exact)
{
    // 200 colors in a busy pattern: the palette holds them exactly, and the
    // LZW table fills and starts over several times.
    const int w = 300, h = 300;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int c = (x * 7 + y * 13 + (x * y) % 5) % 200;
            uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = static_cast<uint8_t>(c);
            p[1] = static_cast<uint8_t>(255 - c);
            p[2] = static_cast<uint8_t>((c * 3) % 256);
            p[3] = (x < 10 && y < 10) ? 0 : 255;
        }
    }
    const std::vector<uint8_t> file = gif::encode(w, h, rgba, true);
    CHECK(file.size() > 13 && file[0] == 'G' && file.back() == 0x3B);
    // Left for tools/check_gif.py to decode with an independent reader.
    const char* out = std::getenv("PHOENIX_GIF_OUT");
    if (out)
    {
        std::ofstream stream(out, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
}

TEST(gif_animation_writer_writes_frames_with_their_timing)
{
    // Three 16 x 16 frames of one color each, at 24 frames a second.
    gif::AnimationWriter writer(16, 16, true);
    const uint8_t colors[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    for (int i = 0; i < 3; ++i)
    {
        std::vector<uint8_t> rgba(16 * 16 * 4);
        for (size_t p = 0; p < 16 * 16; ++p)
        {
            rgba[p * 4] = colors[i][0];
            rgba[p * 4 + 1] = colors[i][1];
            rgba[p * 4 + 2] = colors[i][2];
            rgba[p * 4 + 3] = 255;
        }
        CHECK(writer.addFrame(rgba, gif::AnimationWriter::delayOf(i, 24.0), false));
    }
    const std::vector<uint8_t> file = writer.finish();
    CHECK(file.size() > 13 && file.back() == 0x3B);
    // A second's worth of frames adds up to a second.
    int total = 0;
    for (int i = 0; i < 24; ++i)
        total += gif::AnimationWriter::delayOf(i, 24.0);
    CHECK(total == 100);
    const char* out = std::getenv("PHOENIX_GIF_ANIM_OUT");
    if (out)
    {
        std::ofstream stream(out, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
}

TEST(gif_palettes_keep_to_their_color_count_and_dither)
{
    // A grey ramp, 64 wide: cut to 4 colors, with and without dither.
    const int width = 64, height = 4;
    std::vector<uint8_t> rgba(static_cast<size_t>(width * height * 4));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t* p = &rgba[static_cast<size_t>((y * width + x) * 4)];
            p[0] = p[1] = p[2] = static_cast<uint8_t>(x * 4);
            p[3] = 255;
        }
    gif::Options four;
    four.colors = 4;
    const gif::Reduced plain = gif::reduce(width, height, rgba, false, four);
    CHECK(plain.palette.size() == 4 * 3 && plain.indices.size() == static_cast<size_t>(width * height));
    four.dither = true;
    const gif::Reduced dithered = gif::reduce(width, height, rgba, false, four);
    // Without dither each row is the same runs; with it the colors mix, so
    // neighbors in a row change more often.
    const auto changes = [&](const gif::Reduced& r) {
        int count = 0;
        for (int x = 1; x < width; ++x)
            count += r.indices[static_cast<size_t>(x)] != r.indices[static_cast<size_t>(x - 1)];
        return count;
    };
    CHECK(changes(plain) <= 3);
    CHECK(changes(dithered) > changes(plain));
    // Transparency keeps a slot of its own.
    rgba[3] = 0;
    const gif::Reduced clear = gif::reduce(width, height, rgba, true, four);
    CHECK(clear.clearIndex >= 0 && clear.indices[0] == clear.clearIndex);
}
