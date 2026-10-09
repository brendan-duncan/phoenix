#include "test_util.h"

#include "../src/edit/editable_path.h"
#include "../src/edit/shape_recognition.h"

#include <cmath>
#include <vector>

using fla::Anchor;
using fla::EditablePath;
using fla::Point;

namespace {

EditablePath polyline(const std::vector<Point>& points)
{
    EditablePath path;
    for (const Point& p : points)
        path.anchors.push_back(Anchor(p));
    return path;
}

} // namespace

TEST(a_stroke_whose_ends_nearly_meet_is_closed)
{
    EditablePath path = polyline({Point(0, 0), Point(100, 0), Point(100, 100), Point(0, 100), Point(0, 6)});
    const std::vector<Point> raw = {Point(0, 0), Point(100, 0), Point(100, 100), Point(0, 100), Point(0, 6)};

    fla::StrokeTidyOptions options;
    options.recognizeShapes = false;
    fla::tidyStraightenedStroke(path, raw, options);

    CHECK(path.closed);
    CHECK(path.anchors.size() == 4);
}

TEST(a_stroke_whose_ends_are_far_apart_stays_open)
{
    EditablePath path = polyline({Point(0, 0), Point(100, 0), Point(100, 100), Point(0, 100), Point(0, 40)});
    const std::vector<Point> raw = {Point(0, 0), Point(100, 0), Point(100, 100), Point(0, 100), Point(0, 40)};

    fla::tidyStraightenedStroke(path, raw, fla::StrokeTidyOptions());

    CHECK(!path.closed);
    CHECK(path.anchors.size() == 5);
}

TEST(a_nearly_square_closed_stroke_becomes_a_rectangle)
{
    EditablePath path = polyline({Point(2, 0), Point(101, 3), Point(98, 100), Point(0, 97), Point(1, 5)});
    const std::vector<Point> raw = {Point(2, 0), Point(101, 3), Point(98, 100), Point(0, 97), Point(1, 5)};

    fla::tidyStraightenedStroke(path, raw, fla::StrokeTidyOptions());

    CHECK(path.closed);
    CHECK(path.anchors.size() == 4);
    if (path.anchors.size() == 4)
    {
        CHECK_NEAR(path.anchors[0].position.x, 0.0);
        CHECK_NEAR(path.anchors[0].position.y, 0.0);
        CHECK_NEAR(path.anchors[2].position.x, 101.0);
        CHECK_NEAR(path.anchors[2].position.y, 100.0);
        CHECK(!path.anchors[0].hasOutCurve());
    }
}

TEST(a_roundish_closed_stroke_becomes_an_oval)
{
    // Raw points round a circle of radius 50, and the straightened stroke as
    // a dozen short sides through them.
    std::vector<Point> raw;
    std::vector<Point> corners;
    for (int i = 0; i < 48; ++i)
    {
        const double a = 2.0 * 3.14159265358979323846 * i / 48.0;
        const double wobble = 1.0 + 0.03 * std::sin(5.0 * a);
        raw.push_back(Point(100.0 + 50.0 * wobble * std::cos(a), 80.0 + 50.0 * wobble * std::sin(a)));
        if (i % 4 == 0)
            corners.push_back(raw.back());
    }
    corners.push_back(raw.front());
    EditablePath path = polyline(corners);

    fla::tidyStraightenedStroke(path, raw, fla::StrokeTidyOptions());

    CHECK(path.closed);
    CHECK(path.anchors.size() == 4);
    if (path.anchors.size() == 4)
    {
        CHECK(path.anchors[0].smooth);
        CHECK(path.anchors[0].hasOutCurve());
        // The oval spans the wobbly stroke's extent, so its middle is near 80.
        CHECK(std::fabs(path.anchors[1].position.y - 80.0) < 3.0);
    }
}

TEST(a_stroke_the_fitter_already_closed_loses_its_seam_anchor)
{
    EditablePath path = polyline({Point(2, 0), Point(101, 3), Point(98, 100), Point(0, 97), Point(1, 5)});
    path.closed = true;
    const std::vector<Point> raw = {Point(2, 0), Point(101, 3), Point(98, 100), Point(0, 97), Point(1, 5)};

    fla::tidyStraightenedStroke(path, raw, fla::StrokeTidyOptions());

    CHECK(path.closed);
    CHECK(path.anchors.size() == 4);
    if (path.anchors.size() == 4)
        CHECK_NEAR(path.anchors[0].position.x, 0.0);
}

#include "../src/edit/curve_fit.h"

/// The rough rectangle the on-screen harness draws, through the fitter and
/// the tidier exactly as the pencil runs them: it comes out square.
TEST(a_rough_rectangle_drawn_with_the_pencil_comes_out_square)
{
    const double corners[5][2] = {{400, 650}, {610, 655}, {600, 790}, {405, 785}, {402, 660}};
    std::vector<Point> raw;
    for (int k = 0; k < 4; ++k)
    {
        for (int i = 0; i <= 8; ++i)
        {
            const double t = i / 8.0;
            raw.push_back(Point((corners[k][0] + (corners[k + 1][0] - corners[k][0]) * t) / 0.914,
                (corners[k][1] + (corners[k + 1][1] - corners[k][1]) * t) / 0.914));
        }
    }
    fla::CurveFit::Options options;
    options.straighten = true;
    options.tolerance = 2.625 / 0.914;
    options.minimumSpacing = 0.5 / 0.914;
    EditablePath path = fla::CurveFit::fit(raw, options);

    fla::StrokeTidyOptions tidy;
    tidy.closeTolerance = 15.0 / 0.914;
    fla::tidyStraightenedStroke(path, raw, tidy);

    CHECK(path.closed);
    CHECK(path.anchors.size() == 4);
    if (path.anchors.size() == 4)
    {
        CHECK_NEAR(path.anchors[0].position.y, path.anchors[1].position.y);
        CHECK_NEAR(path.anchors[1].position.x, path.anchors[2].position.x);
    }
}
