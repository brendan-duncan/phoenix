#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/edit/trace_bitmap.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdint>

namespace {

/// The area a fill covers, from the edges with it on either side: each
/// edge's shoelace sum counts toward the fill on its right, against the
/// fill on its left (the page's y runs down).
double areaOf(const fla::Shape& shape, int fill)
{
    double area = 0.0;
    for (const fla::Edge* edge : shape.edges)
    {
        double sum = 0.0;
        for (const fla::Path* path : edge->paths)
            for (size_t i = 1; i < path->segments.size(); ++i)
            {
                const fla::Point& a = path->segments[i - 1]->points.back();
                const fla::Point& b = path->segments[i]->points.back();
                sum += a.x * b.y - b.x * a.y;
            }
        if (edge->fillStyle1 == fill)
            area += sum / 2.0;
        if (edge->fillStyle0 == fill)
            area -= sum / 2.0;
    }
    return area;
}

} // namespace

TEST(trace_bitmap_turns_areas_of_color_into_fills_that_meet)
{
    // 8 by 6: red on the left, blue on the right, one stray near-red pixel
    // in the blue, and a transparent hole at the top right.
    const int width = 8, height = 6;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4), 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t* p = &pixels[static_cast<size_t>((y * width + x) * 4)];
            if (x < 4)
            {
                p[0] = 250;
                p[1] = 10;
            }
            else
            {
                p[2] = 240;
            }
            p[3] = 255;
        }
    uint8_t* stray = &pixels[static_cast<size_t>((4 * width + 6) * 4)];
    stray[0] = 255;
    stray[2] = 0;
    pixels[static_cast<size_t>((0 * width + 7) * 4 + 3)] = 0;

    fla::trace::Options options;
    options.colorThreshold = 60;
    options.minimumArea = 4;
    // Tight enough to keep every pixel corner, so the areas are exact.
    options.curveFit = 0.25;
    int count = 0;
    const std::vector<int> labels = fla::trace::regions(pixels.data(), width, height, options, count);
    CHECK(count == 2);
    CHECK(labels[7] == -1 && labels[static_cast<size_t>(4 * width + 6)] == labels[6]);

    std::unique_ptr<fla::Shape> shape(fla::trace::traceBitmap(pixels.data(), width, height, options));
    CHECK(shape != nullptr);
    if (!shape)
        return;
    CHECK(shape->fills.size() == 2);
    const auto* red = static_cast<const fla::SolidColor*>(shape->fillsMap[1]);
    CHECK(red->color[0] == 250 && red->color[2] == 0);
    // Every pixel is covered once: 24 red, 23 blue (the hole is not).
    CHECK(std::fabs(areaOf(*shape, 1) - 24.0) < 1.0e-9);
    CHECK(std::fabs(areaOf(*shape, 2) - 23.0) < 1.0e-9);
    // The straight border is one edge of two points, red on its left.
    int borders = 0;
    for (const fla::Edge* edge : shape->edges)
        if (edge->fillStyle0 > 0 && edge->fillStyle1 > 0)
            ++borders;
    CHECK(borders == 1);
}

TEST(trace_bitmap_splits_a_soft_noisy_border_where_its_colors_cross)
{
    // 24 by 12: orange on the left, purple on the right, a column of the
    // two blended between them, and a little noise everywhere, as a
    // scanned or compressed picture has.
    const int width = 24, height = 12;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4), 255);
    const int left[3] = {230, 140, 40};
    const int right[3] = {120, 60, 180};
    unsigned seed = 7;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t* p = &pixels[static_cast<size_t>((y * width + x) * 4)];
            const double mix = x < 12 ? 0.0 : x == 12 ? 0.5 : 1.0;
            for (int c = 0; c < 3; ++c)
            {
                seed = seed * 1103515245u + 12345u;
                const int noise = static_cast<int>((seed >> 16) % 13) - 6;
                p[c] = static_cast<uint8_t>(std::clamp(static_cast<int>(left[c] * (1.0 - mix) + right[c] * mix) + noise, 0, 255));
            }
        }
    fla::trace::Options options;
    options.colorThreshold = 60;
    options.minimumArea = 8;
    options.curveFit = 1.0;
    int count = 0;
    fla::trace::regions(pixels.data(), width, height, options, count);
    CHECK(count == 2);
    std::unique_ptr<fla::Shape> shape(fla::trace::traceBitmap(pixels.data(), width, height, options));
    CHECK(shape != nullptr);
    if (!shape)
        return;
    // The border between them is one straight edge: two points, no steps
    // where noise or the blended column pulled pixels one way or the other.
    int borders = 0;
    for (const fla::Edge* edge : shape->edges)
        if (edge->fillStyle0 > 0 && edge->fillStyle1 > 0)
        {
            ++borders;
            CHECK(edge->paths.size() == 1 && edge->paths[0]->segments.size() == 2);
        }
    CHECK(borders == 1);
}

TEST(trace_bitmap_fits_curves_to_rounds_and_keeps_squares_square)
{
    // 64 by 64 white: a dark disc of radius 18 with soft edges on the left,
    // a dark 16 pixel square on the right.
    const int width = 64, height = 64;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4), 255);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t* p = &pixels[static_cast<size_t>((y * width + x) * 4)];
            // How much of the pixel the disc covers, sampled 4 by 4.
            int inside = 0;
            for (int s = 0; s < 16; ++s)
            {
                const double sx = x + (s % 4 + 0.5) / 4.0 - 20.0;
                const double sy = y + (s / 4 + 0.5) / 4.0 - 32.0;
                inside += sx * sx + sy * sy < 18.0 * 18.0 ? 1 : 0;
            }
            const bool square = x >= 44 && x < 60 && y >= 24 && y < 40;
            const int shade = square ? 30 : 255 - (255 - 30) * inside / 16;
            p[0] = p[1] = p[2] = static_cast<uint8_t>(shade);
        }
    fla::trace::Options options;
    options.colorThreshold = 100;
    options.minimumArea = 8;
    options.curveFit = 2.0;
    std::unique_ptr<fla::Shape> shape(fla::trace::traceBitmap(pixels.data(), width, height, options));
    CHECK(shape != nullptr);
    if (!shape)
        return;
    // White, the disc and the square.
    CHECK(shape->fills.size() == 3);
    int discCurves = 0, squareCurves = 0, squareLines = 0;
    for (const fla::Edge* edge : shape->edges)
        for (const fla::Path* path : edge->paths)
        {
            const bool square = path->segments.front()->points.back().x > 40.0;
            for (const fla::PathSegment* segment : path->segments)
            {
                if (segment->command == fla::PathSegment::Command::Quad)
                    ++(square ? squareCurves : discCurves);
                if (square && segment->command == fla::PathSegment::Command::Line)
                    ++squareLines;
            }
        }
    CHECK(discCurves >= 4);
    CHECK(squareCurves == 0 && squareLines == 4);
}