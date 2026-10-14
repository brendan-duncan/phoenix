#include "test_util.h"

#include "../src/util/swf_writer.h"

#include "zlib.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>

namespace {

/// Reads bits most significant first, as SWF packs them.
struct BitReader
{
    const std::vector<uint8_t>& data;
    size_t at = 0;
    int bit = 0;

    uint32_t get(int count)
    {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i)
        {
            const int one = (data[at] >> (7 - bit)) & 1;
            value = (value << 1) | static_cast<uint32_t>(one);
            if (++bit == 8)
            {
                bit = 0;
                ++at;
            }
        }
        return value;
    }
    int32_t getSigned(int count)
    {
        const uint32_t value = get(count);
        if (count > 0 && (value & (1u << (count - 1))))
            return static_cast<int32_t>(value | ~((1u << count) - 1u));
        return static_cast<int32_t>(value);
    }
    void align()
    {
        if (bit)
        {
            bit = 0;
            ++at;
        }
    }
    int u8()
    {
        align();
        return data[at++];
    }
    int u16()
    {
        const int low = u8();
        return low | (u8() << 8);
    }
};

struct Tag
{
    int code;
    std::vector<uint8_t> body;
};

/// The movie's frame size in twips, frame count and tags.
std::vector<Tag> readMovie(const std::vector<uint8_t>& file, int& width, int& height, int& frames)
{
    std::vector<uint8_t> body(file.begin() + 8, file.end());
    if (file[0] == 'C')
    {
        uLongf size = file[4] | (file[5] << 8) | (file[6] << 16) | (static_cast<uLongf>(file[7]) << 24);
        std::vector<uint8_t> out(size);
        uncompress(out.data(), &size, body.data(), static_cast<uLong>(body.size()));
        out.resize(size);
        body = out;
    }
    BitReader in{body};
    const int bits = static_cast<int>(in.get(5));
    const int left = in.getSigned(bits);
    width = in.getSigned(bits) - left;
    const int top = in.getSigned(bits);
    height = in.getSigned(bits) - top;
    in.align();
    in.u16(); // frame rate
    frames = in.u16();
    std::vector<Tag> tags;
    while (in.at + 2 <= body.size())
    {
        const int header = in.u16();
        const int code = header >> 6;
        size_t length = static_cast<size_t>(header & 0x3F);
        if (length == 0x3F)
        {
            length = static_cast<size_t>(in.u16());
            length |= static_cast<size_t>(in.u16()) << 16;
        }
        if (in.at + length > body.size())
        {
            std::printf("    tag %d of %zu bytes runs past the end (%zu left)\n", code, length, body.size() - in.at);
            break;
        }
        tags.push_back({code, std::vector<uint8_t>(body.begin() + static_cast<long long>(in.at),
                                  body.begin() + static_cast<long long>(in.at + length))});
        in.at += length;
        if (code == 0)
            break;
    }
    return tags;
}

/// A DefineShape3's points, in twips: every move and edge end.
std::vector<std::pair<int, int>> shapePoints(const std::vector<uint8_t>& body, int& fillType)
{
    BitReader in{body};
    in.u16(); // id
    const int bits = static_cast<int>(in.get(5));
    for (int i = 0; i < 4; ++i)
        in.getSigned(bits);
    in.align();
    const int fills = in.u8();
    fillType = in.u8();
    if (fillType == 0x00)
        for (int i = 0; i < 4; ++i)
            in.u8();
    else
        return {}; // only solid fills are walked here
    (void)fills;
    in.u8(); // line styles
    const int fillBits = static_cast<int>(in.get(4));
    const int lineBits = static_cast<int>(in.get(4));
    std::vector<std::pair<int, int>> points;
    int x = 0, y = 0;
    for (;;)
    {
        if (in.get(1) == 0)
        {
            const uint32_t flags = in.get(5);
            if (flags == 0)
                break;
            if (flags & 1)
            {
                const int moveBits = static_cast<int>(in.get(5));
                x = in.getSigned(moveBits);
                y = in.getSigned(moveBits);
                points.emplace_back(x, y);
            }
            if (flags & 2)
                in.get(fillBits);
            if (flags & 4)
                in.get(fillBits);
            if (flags & 8)
                in.get(lineBits);
        }
        else if (in.get(1) == 1)
        {
            const int n = static_cast<int>(in.get(4)) + 2;
            if (in.get(1))
            {
                x += in.getSigned(n);
                y += in.getSigned(n);
            }
            else if (in.get(1))
                y += in.getSigned(n);
            else
                x += in.getSigned(n);
            points.emplace_back(x, y);
        }
        else
        {
            const int n = static_cast<int>(in.get(4)) + 2;
            x += in.getSigned(n);
            y += in.getSigned(n);
            x += in.getSigned(n);
            y += in.getSigned(n);
            points.emplace_back(x, y);
        }
    }
    return points;
}

} // namespace

TEST(swf_movies_hold_shapes_bitmaps_and_frames)
{
    swf::Writer writer(320, 240, 24.0, swf::Color{255, 255, 255, 255});
    swf::Fill red;
    red.color = swf::Color{255, 0, 0, 255};
    using K = swf::Segment::Kind;
    const std::vector<swf::Segment> square = {{K::Move, 10, 10}, {K::Line, 110, 10}, {K::Line, 110, 60},
        {K::Quad, 10, 60, 60, 90}, {K::Line, 10, 10}};
    const int shape = writer.defineShape(red, square);
    swf::Fill gradient;
    gradient.type = swf::Fill::Type::Radial;
    gradient.stops = {{0.0, {255, 255, 255, 255}}, {1.0, {0, 0, 255, 128}}};
    gradient.focal = 0.5;
    const int glow = writer.defineShape(gradient, square);
    const int bitmap = writer.defineBitmap(2, 2, std::vector<uint8_t>(16, 255));
    swf::Fill picture;
    picture.type = swf::Fill::Type::Bitmap;
    picture.bitmap = bitmap;
    const int framed = writer.defineShape(picture, square);
    writer.place(shape, 1);
    writer.showFrame();
    writer.remove(1);
    writer.place(glow, 1, 0.5);
    writer.place(framed, 2);
    writer.showFrame();
    CHECK(shape == 1 && glow == 2 && bitmap == 3 && framed == 4);

    for (bool compressed : {false, true})
    {
        const std::vector<uint8_t> file = swf::Writer(writer).finish(compressed);
        CHECK(file[0] == (compressed ? 'C' : 'F') && file[1] == 'W' && file[2] == 'S');
        int width = 0, height = 0, frames = 0;
        const std::vector<Tag> tags = readMovie(file, width, height, frames);
        CHECK(width == 6400 && height == 4800 && frames == 2);
        std::vector<int> codes;
        for (const Tag& tag : tags)
            codes.push_back(tag.code);
        // FileAttributes, background, the shapes and the bitmap, then the
        // frames, ended.
        const std::vector<int> expected = {69, 9, 32, 83, 36, 32, 26, 1, 28, 26, 26, 1, 0};
        CHECK(codes == expected);
        if (codes != expected)
            for (int code : codes)
                std::printf("    code %d\n", code);
        if (codes != expected)
            continue;
        int fillType = -1;
        const std::vector<std::pair<int, int>> points = shapePoints(tags[2].body, fillType);
        CHECK(fillType == 0);
        // The square's corners and the curve's end, back in twips.
        const std::vector<std::pair<int, int>> want = {{200, 200}, {2200, 200}, {2200, 1200}, {200, 1200}, {200, 200}};
        CHECK(points == want);
    }
}
