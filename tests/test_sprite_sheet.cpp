#include "test_util.h"

#include "../src/util/sprite_sheet.h"

#include <cstdint>

namespace {

/// A clear frame with a filled rectangle in it.
sprites::Frame frameWith(int width, int height, int x, int y, int w, int h, uint8_t shade)
{
    sprites::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    for (int row = y; row < y + h; ++row)
    {
        for (int column = x; column < x + w; ++column)
        {
            uint8_t* p = &frame.rgba[(static_cast<size_t>(row) * width + column) * 4];
            p[0] = shade;
            p[3] = 255;
        }
    }
    return frame;
}

} // namespace

TEST(sprite_sheet_grids_frames_and_shares_repeats)
{
    std::vector<sprites::Frame> frames;
    for (int i = 0; i < 5; ++i)
        frames.push_back(frameWith(10, 8, i, 0, 2, 2, static_cast<uint8_t>(10 + i)));
    // The sixth repeats the first.
    frames.push_back(frames[0]);
    sprites::Options options;
    options.padding = 2;
    options.border = 1;
    const sprites::Sheet sheet = sprites::layout(frames, options);
    CHECK(sheet.frames.size() == 6);
    // Five different frames: three columns, two rows.
    CHECK(sheet.width == 1 * 2 + 3 * 10 + 2 * 2);
    CHECK(sheet.height == 1 * 2 + 2 * 8 + 1 * 2);
    CHECK(sheet.frames[1].x == 1 + 12);
    CHECK(sheet.frames[3].y == 1 + 10);
    CHECK(sheet.frames[5].sameAs == 0);
    CHECK(sheet.frames[5].x == sheet.frames[0].x && sheet.frames[5].y == sheet.frames[0].y);

    options.dedupe = false;
    CHECK(sprites::layout(frames, options).frames[5].sameAs == -1);
}

TEST(sprite_sheet_trims_frames_and_keeps_their_offsets)
{
    std::vector<sprites::Frame> frames{frameWith(20, 20, 5, 6, 4, 3, 50), frameWith(20, 20, 0, 0, 20, 20, 60)};
    int x = 0, y = 0, w = 0, h = 0;
    sprites::trimBounds(frames[0], x, y, w, h);
    CHECK(x == 5 && y == 6 && w == 4 && h == 3);

    sprites::Options options;
    options.trim = true;
    options.layout = sprites::Layout::Strip;
    options.padding = 1;
    const sprites::Sheet sheet = sprites::layout(frames, options);
    CHECK(sheet.frames[0].width == 4 && sheet.frames[0].height == 3);
    CHECK(sheet.frames[0].offsetX == 5 && sheet.frames[0].offsetY == 6);
    CHECK(sheet.frames[0].sourceWidth == 20);
    CHECK(sheet.frames[1].x == 5);
    CHECK(sheet.width == 4 + 1 + 20 && sheet.height == 20);

    const std::string json = sprites::describe(sheet, {"run 0001", "run 0002"}, "run.png", sprites::DataFormat::JsonHash, 24);
    CHECK(json.find("\"run 0001\": {") != std::string::npos);
    CHECK(json.find("\"frame\": {\"x\": 0, \"y\": 0, \"w\": 4, \"h\": 3}") != std::string::npos);
    CHECK(json.find("\"spriteSourceSize\": {\"x\": 5, \"y\": 6, \"w\": 4, \"h\": 3}") != std::string::npos);
    CHECK(json.find("\"trimmed\": true") != std::string::npos);
    CHECK(json.find("\"image\": \"run.png\"") != std::string::npos);
    const std::string list = sprites::describe(sheet, {"a", "b"}, "run.png", sprites::DataFormat::JsonArray, 24);
    CHECK(list.find("\"filename\": \"b\"") != std::string::npos);
}

TEST(sprite_sheet_packs_rows_within_the_width)
{
    std::vector<sprites::Frame> frames{frameWith(30, 10, 0, 0, 30, 10, 1), frameWith(30, 20, 0, 0, 30, 20, 2),
        frameWith(30, 5, 0, 0, 30, 5, 3)};
    sprites::Options options;
    options.layout = sprites::Layout::Packed;
    options.padding = 0;
    options.maxWidth = 64;
    options.powerOfTwo = true;
    const sprites::Sheet sheet = sprites::layout(frames, options);
    // Tallest first: 20 and 10 on the first shelf, 5 on the next.
    CHECK(sheet.frames[1].x == 0 && sheet.frames[1].y == 0);
    CHECK(sheet.frames[0].x == 30 && sheet.frames[0].y == 0);
    CHECK(sheet.frames[2].x == 0 && sheet.frames[2].y == 20);
    CHECK(sheet.width == 64 && sheet.height == 32);
}
