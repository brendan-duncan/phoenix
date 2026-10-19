#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/edit/paper_cleanup.h"
#include "../src/edit/region_split.h"
#include "../src/geom/shape_geometry.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace paper = fla::paper;

namespace {

std::string dataDir()
{
#ifdef PHOENIX_TEST_DATA
    return PHOENIX_TEST_DATA;
#else
    std::string file = __FILE__;
    const size_t slash = file.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : file.substr(0, slash)) + "/data";
#endif
}

/// One of the synthetic scans tools/make_paper_scans.py draws, a binary
/// PPM, as straight RGBA.
bool loadScan(int number, std::vector<uint8_t>& rgba, int& width, int& height)
{
    std::ifstream in(dataDir() + "/paper/scan_" + std::to_string(number) + ".ppm", std::ios::binary);
    std::string magic;
    int maximum = 0;
    in >> magic >> width >> height >> maximum;
    in.get();
    if (!in || magic != "P6" || width <= 0 || height <= 0 || maximum != 255)
        return false;
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    in.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!in)
        return false;
    rgba.assign(static_cast<size_t>(width) * height * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
        std::copy(rgb.begin() + static_cast<std::ptrdiff_t>(i * 3), rgb.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3),
            rgba.begin() + static_cast<std::ptrdiff_t>(i * 4));
    return true;
}

/// How many separate pieces of line there are, joined through corners.
int pieces(const std::vector<uint8_t>& cleaned, int width, int height)
{
    std::vector<uint8_t> seen(static_cast<size_t>(width) * height, 0);
    int count = 0;
    for (int start = 0; start < width * height; ++start)
    {
        if (seen[static_cast<size_t>(start)] || cleaned[static_cast<size_t>(start) * 4 + 3] < 128)
            continue;
        ++count;
        std::vector<int> stack(1, start);
        seen[static_cast<size_t>(start)] = 1;
        while (!stack.empty())
        {
            const int at = stack.back();
            stack.pop_back();
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int x = at % width + dx;
                    const int y = at / width + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height)
                        continue;
                    const int next = y * width + x;
                    if (!seen[static_cast<size_t>(next)] && cleaned[static_cast<size_t>(next) * 4 + 3] >= 128)
                    {
                        seen[static_cast<size_t>(next)] = 1;
                        stack.push_back(next);
                    }
                }
        }
    }
    return count;
}

} // namespace

TEST(paper_cleanup_makes_uneven_paper_white)
{
    // A blank page, lit from one side: 240 on the left fading to 150 on the
    // right, warm-tinted.
    const int width = 120;
    const int height = 80;
    std::vector<uint8_t> page(static_cast<size_t>(width) * height * 4, 255);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int light = 240 - 90 * x / (width - 1);
            uint8_t* p = &page[(static_cast<size_t>(y) * width + x) * 4];
            p[0] = static_cast<uint8_t>(light);
            p[1] = static_cast<uint8_t>(light - 8);
            p[2] = static_cast<uint8_t>(light - 30);
        }

    paper::Settings settings;
    int lines = -1;
    paper::cleanup(page.data(), width, height, settings, &lines);
    CHECK(lines == 0);
    const std::vector<uint8_t> leveled =
        paper::level(paper::lightness(page.data(), width, height), width, height, true, false);
    CHECK(leveled[static_cast<size_t>(40) * width + 2] > 240);
    CHECK(leveled[static_cast<size_t>(40) * width + width - 3] > 240);

    // Without evening it out, the dim side reads as a smudge of line.
    settings.evenPaper = false;
    settings.autoLevel = false;
    paper::cleanup(page.data(), width, height, settings, &lines);
    CHECK(lines > width * height / 4);
}

TEST(paper_cleanup_finds_the_pencil_lines_of_a_scan)
{
    std::vector<uint8_t> scan;
    int width = 0;
    int height = 0;
    CHECK(loadScan(1, scan, width, height));
    if (scan.empty())
        return;
    CHECK(width == 160 && height == 120);

    const std::vector<uint8_t> leveled =
        paper::level(paper::lightness(scan.data(), width, height), width, height, true, true);
    // Paper in both the bright and the shaded corner is white; the ground
    // line, 82% of the way down, is dark.
    CHECK(leveled[static_cast<size_t>(4) * width + 4] > 220);
    CHECK(leveled[static_cast<size_t>(height - 4) * width + width - 4] > 220);
    const int groundY = static_cast<int>(height * 0.82);
    int darkest = 255;
    for (int y = groundY - 2; y <= groundY + 2; ++y)
        darkest = std::min(darkest, static_cast<int>(leveled[static_cast<size_t>(y) * width + width / 2]));
    CHECK(darkest < 90);
    const int suggested = paper::suggestThreshold(leveled);
    CHECK(suggested >= 60 && suggested <= 230);

    // A higher threshold takes in more; none takes nothing.
    paper::Settings settings;
    int few = 0, more = 0, none = -1;
    settings.threshold = 120;
    paper::cleanup(scan.data(), width, height, settings, &few);
    settings.threshold = 200;
    paper::cleanup(scan.data(), width, height, settings, &more);
    settings.threshold = 0;
    paper::cleanup(scan.data(), width, height, settings, &none);
    CHECK(few > 0 && more > few && none == 0);

    // Cleaned: the ground line and the ball (its band joined to it), and no
    // dust. Without despeckling there are at least as many pieces.
    settings = paper::Settings();
    std::vector<uint8_t> cleaned = paper::cleanup(scan.data(), width, height, settings);
    CHECK(pieces(cleaned, width, height) == 2);
    settings.despeckle = 0;
    CHECK(pieces(paper::cleanup(scan.data(), width, height, settings), width, height) >= 2);
    // The lines come out in the line color, the paper clear.
    bool colored = true;
    for (size_t i = 0; i < cleaned.size(); i += 4)
        if (cleaned[i + 3] != 0 && (cleaned[i] != 0 || cleaned[i + 3] != 255))
            colored = false;
    CHECK(colored);
}

TEST(paper_cleanup_closes_gaps_and_vectorizes_in_place)
{
    // Two strokes 3 pixels apart on white paper: closing gaps joins them.
    const int width = 60;
    const int height = 40;
    std::vector<uint8_t> page(static_cast<size_t>(width) * height * 4, 255);
    for (int y = 18; y < 21; ++y)
        for (int x = 5; x < 55; ++x)
            if (x < 28 || x > 30)
                std::fill_n(&page[(static_cast<size_t>(y) * width + x) * 4], 3, static_cast<uint8_t>(40));
    paper::Settings settings;
    settings.smoothing = 0;
    CHECK(pieces(paper::cleanup(page.data(), width, height, settings), width, height) == 2);
    settings.closeGaps = 4;
    CHECK(pieces(paper::cleanup(page.data(), width, height, settings), width, height) == 1);

    // A scan traced: a shape of the line color where the lines were.
    std::vector<uint8_t> scan;
    int w = 0;
    int h = 0;
    CHECK(loadScan(2, scan, w, h));
    if (scan.empty())
        return;
    const std::vector<uint8_t> cleaned = paper::cleanup(scan.data(), w, h, paper::Settings());
    int left = 0, top = 0, right = 0, bottom = 0;
    CHECK(paper::lineBounds(cleaned, w, h, left, top, right, bottom));
    std::unique_ptr<fla::Shape> shape(paper::vectorize(cleaned, w, h, 1.0));
    CHECK(shape != nullptr);
    if (!shape)
        return;
    CHECK(!shape->edges.empty() && !shape->fills.empty());
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const fla::ShapeCurve& curve : fla::shapeCurves(*shape))
        for (const double t : {0.0, 0.5, 1.0})
        {
            const fla::Point p = curve.curve.pointAt(t);
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    // Where the lines are, a pixel or two either way.
    CHECK(std::abs(minX - left) <= 2.0 && std::abs(maxX - (right + 1)) <= 2.0);
    CHECK(std::abs(minY - top) <= 2.0 && std::abs(maxY - (bottom + 1)) <= 2.0);

    // The traced lines enclose regions the Paint Bucket can fill: the ball's
    // two halves are empty regions (no fill, which a trace writes as 0).
    {
        fla::RegionSplitter regions(*shape);
        const double ballX = (0.25 + 0.17) * w;
        const double ballY = (0.30 + 0.13) * h;
        const int top = regions.faceAt(fla::Point(ballX, ballY - 0.06 * w));
        const int under = regions.faceAt(fla::Point(ballX, ballY + 0.06 * w));
        CHECK(top >= 0 && under >= 0 && top != under);
        CHECK(regions.faceFill(top) <= 0 && regions.faceFill(under) <= 0);
    }

    // Nothing to trace on a blank page.
    std::vector<uint8_t> blank(static_cast<size_t>(w) * h * 4, 0);
    CHECK(paper::vectorize(blank, w, h, 1.0) == nullptr);
    CHECK(!paper::lineBounds(blank, w, h, left, top, right, bottom));
}
