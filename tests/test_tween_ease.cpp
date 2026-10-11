#include "test_util.h"

#include "../src/edit/image_blur.h"
#include "../src/edit/tween_ease.h"

#include <cmath>
#include <vector>

namespace {

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) < tolerance;
}

fla::Point at(double x, double y)
{
    fla::Point p;
    p.x = x;
    p.y = y;
    return p;
}

} // namespace

TEST(no_ease_is_straight_progress)
{
    CHECK(near(fla::easeProgress(0.25, 0, {}), 0.25));
    CHECK(near(fla::easeProgress(0.0, 0, {}), 0.0));
    CHECK(near(fla::easeProgress(1.0, 0, {}), 1.0));
}

TEST(ease_in_starts_slowly_and_ease_out_finishes_slowly)
{
    // Fully eased in, half way through the frames is a quarter of the way
    // there; fully eased out, three quarters.
    CHECK(near(fla::easeProgress(0.5, -100, {}), 0.25));
    CHECK(near(fla::easeProgress(0.5, 100, {}), 0.75));
    // Half strength leans half way.
    CHECK(near(fla::easeProgress(0.5, -50, {}), 0.375));
    // The ends are still the ends.
    CHECK(near(fla::easeProgress(0.0, -100, {}), 0.0));
    CHECK(near(fla::easeProgress(1.0, 100, {}), 1.0));
}

TEST(ease_presets_follow_their_curves)
{
    // quadInOut is a quarter of the way at a quarter of the time squared
    // and doubled; its middle is the middle.
    CHECK(near(fla::easeProgress(0.25, 0, {}, "quadInOut"), 0.125));
    CHECK(near(fla::easeProgress(0.5, 0, {}, "quadInOut"), 0.5));
    CHECK(near(fla::easeProgress(0.5, 0, {}, "cubicIn"), 0.125));
    CHECK(near(fla::easeProgress(0.5, 0, {}, "cubicOut"), 0.875));
    CHECK(near(fla::easeProgress(0.5, 0, {}, "sineInOut"), 0.5));
    // Every preset starts and ends where the tween does.
    for (const char* method : {"quadIn", "quartOut", "quintInOut", "sineIn", "circOut", "backInOut", "bounceOut",
             "elasticIn", "elasticInOut"})
    {
        CHECK(near(fla::easeProgress(0.0, 0, {}, method), 0.0));
        CHECK(near(fla::easeProgress(1.0, 0, {}, method), 1.0));
    }
    // Back pulls back before it goes; bounce never goes past the end.
    CHECK(fla::easeProgress(0.2, 0, {}, "backIn") < 0.0);
    CHECK(fla::easeProgress(0.6, 0, {}, "bounceOut") <= 1.0);
    // The preset takes the place of the Ease value; none leaves it to it.
    CHECK(near(fla::easeProgress(0.5, 100, {}, "quadIn"), 0.25));
    CHECK(near(fla::easeProgress(0.5, 100, {}, "none"), 0.75));
    CHECK(near(fla::easeProgress(0.5, 0, {}, "noSuchEase"), 0.5));
}

TEST(custom_ease_follows_its_curve)
{
    // The curve from a real file: flat out of the start, steep through the
    // middle, flat into the end.
    const std::vector<fla::Point> curve = {at(0.0, 0.0), at(0.3889, 0.0), at(0.5488, 1.0), at(1.0, 1.0)};
    CHECK(near(fla::easeProgress(0.0, 0, curve), 0.0, 1.0e-4));
    CHECK(near(fla::easeProgress(1.0, 0, curve), 1.0, 1.0e-4));
    // Early on, well behind straight progress; late, well ahead.
    CHECK(fla::easeProgress(0.2, 0, curve) < 0.1);
    CHECK(fla::easeProgress(0.8, 0, curve) > 0.9);
    // And never going backwards.
    double previous = 0.0;
    for (int i = 1; i <= 20; ++i)
    {
        const double y = fla::easeProgress(i / 20.0, 0, curve);
        CHECK(y >= previous - 1.0e-9);
        previous = y;
    }
}

TEST(box_blur_spreads_a_pixel_and_keeps_its_coverage)
{
    const int size = 21;
    std::vector<uint32_t> pixels(size * size, 0);
    pixels[10 * size + 10] = 0xffffffffu;

    // A box five wide on each axis, once: the pixel becomes a 5x5 patch
    // whose coverage adds up to what the one pixel had.
    fla::boxBlur(pixels.data(), size, size, 5.0, 5.0, 1);

    long total = 0;
    for (const uint32_t pixel : pixels)
        total += static_cast<long>(pixel >> 24);
    CHECK(std::labs(total - 255) <= 25);
    CHECK((pixels[10 * size + 10] >> 24) >= 9 && (pixels[10 * size + 10] >> 24) <= 11);
    CHECK((pixels[10 * size + 12] >> 24) >= 9);
    CHECK((pixels[10 * size + 13] >> 24) == 0);
    CHECK((pixels[13 * size + 10] >> 24) == 0);
}

TEST(box_blur_on_one_axis_leaves_the_other_alone)
{
    const int size = 11;
    std::vector<uint32_t> pixels(size * size, 0);
    pixels[5 * size + 5] = 0xffffffffu;
    fla::boxBlur(pixels.data(), size, size, 3.0, 0.0, 1);
    CHECK((pixels[5 * size + 4] >> 24) > 0);
    CHECK((pixels[4 * size + 5] >> 24) == 0);
}
