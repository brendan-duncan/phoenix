#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/edit/trace_bitmap.h"

#include <cmath>
#include <memory>

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
