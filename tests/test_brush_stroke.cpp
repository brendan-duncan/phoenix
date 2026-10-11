#include "test_util.h"

#include "../src/edit/brush_stroke.h"

#include <algorithm>
#include <cmath>
#include <vector>

using fla::BrushDab;
using fla::BrushSample;
using fla::BrushSettings;

namespace {

/// A straight drag along y = 0 from x = 0 to \a length, a sample every
/// \a spacing, 16 ms apart, at full pressure.
std::vector<BrushSample> straightDrag(double length, double spacing)
{
    std::vector<BrushSample> samples;
    for (double x = 0.0; x <= length + 1.0e-9; x += spacing)
    {
        BrushSample sample;
        sample.x = x;
        sample.time = samples.size() * 16.0;
        samples.push_back(sample);
    }
    return samples;
}

BrushSettings plain()
{
    BrushSettings settings;
    settings.size = 10.0;
    settings.smoothing = 0;
    settings.minSize = 20.0;
    return settings;
}

double widest(const std::vector<BrushDab>& dabs, size_t from, size_t to)
{
    double most = 0.0;
    for (size_t i = from; i < to; ++i)
        most = std::max(most, dabs[i].width);
    return most;
}

double narrowest(const std::vector<BrushDab>& dabs, size_t from, size_t to)
{
    double least = 1.0e9;
    for (size_t i = from; i < to; ++i)
        least = std::min(least, dabs[i].width);
    return least;
}

double signedArea(const std::vector<fla::Point>& polygon)
{
    double area = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i)
    {
        const fla::Point& a = polygon[i];
        const fla::Point& b = polygon[(i + 1) % polygon.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area / 2.0;
}

} // namespace

TEST(a_mouse_stroke_paints_at_full_size_end_to_end)
{
    // A mouse reports full pressure throughout, so pressure plays no part,
    // and the stroke starts and ends where the drag did.
    const std::vector<BrushDab> dabs = fla::BrushStroke::dabs(straightDrag(100.0, 5.0), plain(), 1.0, true);
    CHECK(dabs.size() > 10);
    CHECK(std::fabs(dabs.front().x) < 1.0e-6 && std::fabs(dabs.back().x - 100.0) < 1.0e-6);
    CHECK(std::fabs(narrowest(dabs, 0, dabs.size()) - 10.0) < 1.0e-9);
    CHECK(std::fabs(widest(dabs, 0, dabs.size()) - 10.0) < 1.0e-9);
    // Close enough together that the sweep between them follows the line.
    for (size_t i = 1; i < dabs.size(); ++i)
        CHECK(dabs[i].x - dabs[i - 1].x <= 4.0 + 1.0e-9);
}

TEST(flickering_pressure_does_not_make_a_lumpy_edge)
{
    // A pen's readings flicker from one to the next. Unfiltered, a reading
    // of 0.4 then 0.8 swings the width by 40% every few pixels; eased, the
    // width settles near the middle.
    std::vector<BrushSample> samples = straightDrag(200.0, 3.0);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i].pressure = i % 2 == 0 ? 0.4 : 0.8;
    const std::vector<BrushDab> dabs = fla::BrushStroke::dabs(samples, plain(), 1.0, true);
    const size_t quarter = dabs.size() / 4;
    const double most = widest(dabs, quarter, dabs.size() - quarter);
    const double least = narrowest(dabs, quarter, dabs.size() - quarter);
    CHECK(most - least < 0.5);
    // The middle pressure, 0.6, at a 20% minimum: 10 * (0.2 + 0.8 * 0.6).
    CHECK(std::fabs((most + least) / 2.0 - 6.8) < 0.3);
}

TEST(pressure_sets_the_width_between_the_minimum_and_the_size)
{
    std::vector<BrushSample> samples = straightDrag(100.0, 2.0);
    for (BrushSample& sample : samples)
        sample.pressure = 0.0;
    std::vector<BrushDab> dabs = fla::BrushStroke::dabs(samples, plain(), 1.0, true);
    CHECK(std::fabs(dabs[dabs.size() / 2].width - 2.0) < 1.0e-6);

    // With Use Pressure off it paints at full size whatever the pen says.
    BrushSettings off = plain();
    off.usePressure = false;
    dabs = fla::BrushStroke::dabs(samples, off, 1.0, true);
    CHECK(std::fabs(dabs[dabs.size() / 2].width - 10.0) < 1.0e-6);

    // A firm response needs a harder press for the same width.
    for (BrushSample& sample : samples)
        sample.pressure = 0.5;
    BrushSettings firm = plain();
    firm.pressureCurve = 1.7;
    BrushSettings soft = plain();
    soft.pressureCurve = 0.6;
    const double firmWidth = fla::BrushStroke::dabs(samples, firm, 1.0, true)[20].width;
    const double softWidth = fla::BrushStroke::dabs(samples, soft, 1.0, true)[20].width;
    const double linearWidth = fla::BrushStroke::dabs(samples, plain(), 1.0, true)[20].width;
    CHECK(firmWidth < linearWidth && linearWidth < softWidth);
}

TEST(smoothing_takes_the_wobble_out_of_the_line)
{
    // A line along y = 0 with a pixel of zigzag in it.
    std::vector<BrushSample> samples = straightDrag(200.0, 2.0);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i].y = i % 2 == 0 ? 1.0 : -1.0;
    const auto deviation = [&](int smoothing) {
        BrushSettings settings = plain();
        settings.smoothing = smoothing;
        const std::vector<BrushDab> dabs = fla::BrushStroke::dabs(samples, settings, 1.0, true);
        double most = 0.0;
        for (size_t i = dabs.size() / 4; i < dabs.size() * 3 / 4; ++i)
            most = std::max(most, std::fabs(dabs[i].y));
        return most;
    };
    CHECK(deviation(0) > 0.5);
    CHECK(deviation(50) < 0.15);
    // Smoothing works in screen pixels: zoomed in 4x, the same wobble on
    // screen is a quarter of the size in the document and is still taken out.
    std::vector<BrushSample> zoomed = samples;
    for (BrushSample& sample : zoomed)
    {
        sample.x /= 4.0;
        sample.y /= 4.0;
    }
    BrushSettings settings = plain();
    settings.size = 2.5;
    settings.smoothing = 50;
    const std::vector<BrushDab> dabs = fla::BrushStroke::dabs(zoomed, settings, 4.0, true);
    double most = 0.0;
    for (size_t i = dabs.size() / 4; i < dabs.size() * 3 / 4; ++i)
        most = std::max(most, std::fabs(dabs[i].y));
    CHECK(most < 0.15 / 4.0);
}

TEST(the_stabilizer_holds_the_line_until_the_pen_pulls_it)
{
    // A shaky hand circling within a few pixels, then moving off to the right.
    std::vector<BrushSample> samples;
    for (int i = 0; i < 40; ++i)
    {
        BrushSample sample;
        sample.x = 3.0 * std::cos(i * 1.3);
        sample.y = 3.0 * std::sin(i * 1.3);
        samples.push_back(sample);
    }
    for (int i = 1; i <= 30; ++i)
    {
        BrushSample sample;
        sample.x = 3.0 + i * 3.0;
        samples.push_back(sample);
    }
    BrushSettings settings = plain();
    settings.stabilizer = 10.0;
    std::vector<BrushDab> dabs = fla::BrushStroke::dabs(samples, settings, 1.0, false);
    // The circling never pulled the string taut: nothing went up or down
    // more than the first reading did.
    double highest = 0.0;
    for (const BrushDab& dab : dabs)
        highest = std::max(highest, std::fabs(dab.y));
    CHECK(highest <= 3.0 + 1.0e-6);
    // While drawing it trails the pen by the string; lifted, it catches up.
    CHECK(dabs.back().x < 93.0 - 9.0);
    dabs = fla::BrushStroke::dabs(samples, settings, 1.0, true);
    CHECK(std::fabs(dabs.back().x - 93.0) < 1.0e-6);
}

TEST(tapers_bring_the_ends_to_a_point)
{
    BrushSettings settings = plain();
    settings.taperStart = 20.0;
    settings.taperEnd = 20.0;
    const std::vector<BrushDab> dabs = fla::BrushStroke::dabs(straightDrag(200.0, 2.0), settings, 1.0, true);
    CHECK(dabs.front().width < 1.0);
    CHECK(dabs.back().width < 1.0);
    CHECK(std::fabs(dabs[dabs.size() / 2].width - 10.0) < 1.0e-6);
    // A quarter of the way into the start taper it is already well open.
    double quarter = 0.0;
    for (const BrushDab& dab : dabs)
    {
        if (dab.x >= 10.0)
        {
            quarter = dab.width;
            break;
        }
    }
    CHECK(quarter > 3.0 && quarter < 10.0);
}

TEST(velocity_thickens_or_thins_the_fast_part)
{
    // Slow for the first 100 pixels (2 px every 16 ms), fast for the next
    // 100 (40 px every 16 ms).
    std::vector<BrushSample> samples;
    double time = 0.0;
    for (double x = 0.0; x < 100.0; x += 2.0, time += 16.0)
    {
        BrushSample sample;
        sample.x = x;
        sample.time = time;
        samples.push_back(sample);
    }
    for (double x = 100.0; x <= 300.0; x += 40.0, time += 16.0)
    {
        BrushSample sample;
        sample.x = x;
        sample.time = time;
        samples.push_back(sample);
    }
    const auto widthAt = [](const std::vector<BrushDab>& dabs, double x) {
        for (const BrushDab& dab : dabs)
        {
            if (dab.x >= x)
                return dab.width;
        }
        return dabs.back().width;
    };
    BrushSettings settings = plain();
    settings.velocity = BrushSettings::Velocity::ThinToThick;
    std::vector<BrushDab> dabs = fla::BrushStroke::dabs(samples, settings, 1.0, true);
    CHECK(widthAt(dabs, 220.0) > widthAt(dabs, 40.0) * 1.5);
    settings.velocity = BrushSettings::Velocity::ThickToThin;
    dabs = fla::BrushStroke::dabs(samples, settings, 1.0, true);
    CHECK(widthAt(dabs, 220.0) * 1.5 < widthAt(dabs, 40.0));
}

TEST(tilt_turns_the_classic_tip_and_broadens_the_ink_line)
{
    std::vector<BrushSample> samples = straightDrag(100.0, 2.0);
    for (BrushSample& sample : samples)
    {
        sample.tiltX = 0.0;
        sample.tiltY = 40.0;
    }
    // Leaning straight down the tablet turns the tip a quarter turn.
    BrushSettings classic = BrushSettings::classicDefaults();
    classic.useTilt = true;
    std::vector<BrushDab> dabs = fla::BrushStroke::dabs(samples, classic, 1.0, true);
    CHECK(std::fabs(dabs[dabs.size() / 2].angle - 3.14159265358979323846 / 2.0) < 1.0e-6);
    // Without Use Tilt it stays as set.
    classic.useTilt = false;
    dabs = fla::BrushStroke::dabs(samples, classic, 1.0, true);
    CHECK(std::fabs(dabs[dabs.size() / 2].angle) < 1.0e-9);

    // The Ink Brush broadens instead: two thirds of the way over, twice as wide.
    BrushSettings ink = BrushSettings::inkDefaults();
    ink.stabilizer = 0.0;
    dabs = fla::BrushStroke::dabs(samples, ink, 1.0, true);
    CHECK(std::fabs(dabs[dabs.size() / 2].width - ink.size * 2.0) < 1.0e-6);
}

TEST(the_sweep_joins_each_pair_of_dabs_with_their_hull)
{
    // A tap is one dab, round.
    std::vector<BrushSample> tap(1);
    std::vector<BrushDab> dabs = fla::BrushStroke::dabs(tap, plain(), 1.0, true);
    CHECK(dabs.size() == 1);
    std::vector<std::vector<fla::Point>> polygons = fla::BrushStroke::sweep(dabs, 1.0);
    CHECK(polygons.size() == 1 && polygons.front().size() >= 8);

    dabs = fla::BrushStroke::dabs(straightDrag(50.0, 5.0), plain(), 1.0, true);
    polygons = fla::BrushStroke::sweep(dabs, 1.0);
    CHECK(polygons.size() == dabs.size() - 1);
    // Every hull winds the same way, so together they fill as one.
    for (const std::vector<fla::Point>& polygon : polygons)
        CHECK(signedArea(polygon) > 0.0);

    // The hull of a square's corners and its center is the square.
    const std::vector<fla::Point> hull = fla::BrushStroke::convexHull(
        {fla::Point(0, 0), fla::Point(2, 0), fla::Point(1, 1), fla::Point(2, 2), fla::Point(0, 2)});
    CHECK(hull.size() == 4);
    CHECK(std::fabs(signedArea(hull) - 4.0) < 1.0e-9);

    // A rectangular tip turned 45 degrees has its corners on the axes.
    BrushDab square;
    square.width = 2.0;
    square.height = 2.0;
    square.angle = 3.14159265358979323846 / 4.0;
    square.rectangular = true;
    const std::vector<fla::Point> corners = fla::BrushStroke::dabOutline(square, 1.0);
    CHECK(corners.size() == 4);
    for (const fla::Point& corner : corners)
        CHECK(std::fabs(std::hypot(corner.x, corner.y) - std::sqrt(2.0)) < 1.0e-9);
}
