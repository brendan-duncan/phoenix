#include "test_util.h"

#include "../src/edit/magic_wand.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

double area(const std::vector<fla::Point>& loop)
{
    double sum = 0.0;
    for (size_t i = 0; i < loop.size(); ++i)
    {
        const fla::Point& a = loop[i];
        const fla::Point& b = loop[(i + 1) % loop.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return sum / 2.0;
}

std::vector<uint8_t> maskOf(const std::vector<const char*>& rows)
{
    std::vector<uint8_t> mask;
    for (const char* row : rows)
        for (const char* c = row; *c; ++c)
            mask.push_back(*c == '#' ? 1 : 0);
    return mask;
}

} // namespace

TEST(the_wand_takes_the_joined_pixels_of_like_color)
{
    const uint32_t red = 0xFFFF0000;
    const uint32_t nearRed = 0xFFF80404;
    const uint32_t blue = 0xFF0000FF;
    // A red block with a slightly different red beside it, and a red pixel
    // across the blue that is not joined to them.
    const std::vector<uint32_t> pixels = {
        red,  red,     blue, red,
        red,  nearRed, blue, blue,
        blue, blue,    blue, blue,
    };
    std::vector<uint8_t> mask = fla::MagicWand::region(pixels.data(), 4, 3, 0, 0, 10);
    CHECK(mask == std::vector<uint8_t>({1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0}));
    // A tight threshold leaves the near red out.
    mask = fla::MagicWand::region(pixels.data(), 4, 3, 0, 0, 2);
    CHECK(mask == std::vector<uint8_t>({1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(fla::MagicWand::region(pixels.data(), 4, 3, 9, 0, 10).empty());
}

TEST(the_outline_follows_pixel_edges_with_a_loop_per_piece_and_hole)
{
    // A block: one loop, its four corners.
    std::vector<std::vector<fla::Point>> loops = fla::MagicWand::outline(maskOf({"##.", "##.", "..."}), 3, 3);
    CHECK(loops.size() == 1);
    if (loops.size() == 1)
    {
        CHECK(loops[0].size() == 4);
        CHECK(std::fabs(std::fabs(area(loops[0])) - 4.0) < 1.0e-9);
    }

    // A ring: the outside and the hole, wound opposite ways.
    loops = fla::MagicWand::outline(maskOf({"###", "#.#", "###"}), 3, 3);
    CHECK(loops.size() == 2);
    if (loops.size() == 2)
    {
        CHECK(std::fabs(std::fabs(area(loops[0])) + std::fabs(area(loops[1])) - 10.0) < 1.0e-9);
        CHECK(area(loops[0]) * area(loops[1]) < 0.0);
    }

    // Pixels touching only at a corner are two pieces, as the wand's region
    // joins only side to side.
    loops = fla::MagicWand::outline(maskOf({"#.", ".#"}), 2, 2);
    CHECK(loops.size() == 2);
    for (const auto& loop : loops)
        CHECK(std::fabs(std::fabs(area(loop)) - 1.0) < 1.0e-9);
}

TEST(smoothing_straightens_the_pixel_steps)
{
    // A staircase diagonal: a triangle of pixels.
    const std::vector<uint8_t> mask = maskOf({
        "#.......", "##......", "###.....", "####....",
        "#####...", "######..", "#######.", "########"});
    const std::vector<std::vector<fla::Point>> loops = fla::MagicWand::outline(mask, 8, 8);
    CHECK(loops.size() == 1);
    if (loops.empty())
        return;
    const std::vector<fla::Point>& steps = loops[0];
    CHECK(steps.size() == 18);
    const std::vector<fla::Point> rough = fla::MagicWand::smooth(steps, fla::MagicWand::Smoothing::Rough);
    const std::vector<fla::Point> normal = fla::MagicWand::smooth(steps, fla::MagicWand::Smoothing::Normal);
    const std::vector<fla::Point> smooth = fla::MagicWand::smooth(steps, fla::MagicWand::Smoothing::Smooth);
    CHECK(fla::MagicWand::smooth(steps, fla::MagicWand::Smoothing::Pixels).size() == steps.size());
    CHECK(rough.size() >= 3 && rough.size() < steps.size());
    CHECK(normal.size() >= 3 && normal.size() < steps.size());
    CHECK(smooth.size() >= 3);
    // Still about the same area: the triangle's 36 pixels.
    CHECK(std::fabs(std::fabs(area(normal)) - 36.0) < 3.0);
    CHECK(std::fabs(std::fabs(area(smooth)) - 36.0) < 5.0);
}

TEST(smoothed_outlines_never_cross_themselves)
{
    // Masks full of strands a pixel wide and pixels touching at corners,
    // where straightening is most tempted to cut across.
    unsigned seed = 12345;
    const auto next = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7FFF;
    };
    for (int trial = 0; trial < 20; ++trial)
    {
        const int width = 40;
        const int height = 30;
        std::vector<uint8_t> mask(width * height, 0);
        for (int y = 2; y < height - 2; ++y)
            for (int x = 2; x < width - 2; ++x)
                mask[y * width + x] = (next() % 100) < 55 ? 1 : 0;
        for (const auto& loop : fla::MagicWand::outline(mask, width, height))
        {
            CHECK(!fla::MagicWand::selfIntersects(loop));
            for (auto how : {fla::MagicWand::Smoothing::Rough, fla::MagicWand::Smoothing::Normal,
                     fla::MagicWand::Smoothing::Smooth})
                CHECK(!fla::MagicWand::selfIntersects(fla::MagicWand::smooth(loop, how)));
        }
    }
    // A figure eight crosses; a square does not.
    CHECK(fla::MagicWand::selfIntersects({fla::Point(0, 0), fla::Point(2, 2), fla::Point(2, 0), fla::Point(0, 2)}));
    CHECK(!fla::MagicWand::selfIntersects({fla::Point(0, 0), fla::Point(2, 0), fla::Point(2, 2), fla::Point(0, 2)}));
}

TEST(tidying_takes_out_thin_strands_and_gaps)
{
    // A block with a strand a pixel wide off it and a gap a pixel wide in it.
    std::vector<uint8_t> grid = maskOf({
        "..........",
        ".#####....",
        ".#####....",
        ".##.##....",
        ".#####....",
        ".#########",
        ".#####....",
        ".........."});
    const std::vector<uint8_t> tidied = fla::MagicWand::tidy(grid, 10, 8, 1);
    // The strand is gone and the gap filled.
    CHECK(tidied[5 * 10 + 8] == 0);
    CHECK(tidied[3 * 10 + 3] == 1);
    // The block itself stays.
    CHECK(tidied[2 * 10 + 2] == 1 && tidied[4 * 10 + 4] == 1);
    // Radius 0 changes nothing.
    CHECK(fla::MagicWand::tidy(grid, 10, 8, 0) == grid);
}
