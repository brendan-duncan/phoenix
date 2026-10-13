#include "test_util.h"

#include "../src/geom/nine_slice.h"

#include <cmath>

using fla::Point;
using fla::Rect;
using fla::Transform;
namespace nineslice = fla::nineslice;

// The symbol is 100 x 60 with guides 20 in from each side.
static std::vector<nineslice::Cell> boxCells(const Transform& matrix)
{
    return nineslice::cells(Rect(Point(0.0, 0.0), Point(100.0, 60.0)), 20.0, 20.0, 80.0, 40.0,
        nineslice::axisScale(matrix));
}

TEST(nine_slice_axis_scale_plain)
{
    const nineslice::AxisScale scale = nineslice::axisScale(Transform(2.5, 0.0, 0.0, 1.6, 10.0, 20.0));
    CHECK_NEAR(scale.x, 2.5);
    CHECK_NEAR(scale.y, 1.6);
}

TEST(nine_slice_axis_scale_rotated_and_mirrored)
{
    // A 2.5 x 1.6 stretch turned 30 degrees keeps its stretch.
    const double angle = 30.0 * 3.14159265358979323846 / 180.0;
    const Transform turned(2.5 * std::cos(angle), 2.5 * std::sin(angle), -1.6 * std::sin(angle),
        1.6 * std::cos(angle), 0.0, 0.0);
    nineslice::AxisScale scale = nineslice::axisScale(turned);
    CHECK_NEAR(scale.x, 2.5);
    CHECK_NEAR(scale.y, 1.6);

    // A mirror is part of what turns it, not of the stretch.
    scale = nineslice::axisScale(Transform(-2.0, 0.0, 0.0, 3.0, 0.0, 0.0));
    CHECK_NEAR(scale.x, 2.0);
    CHECK_NEAR(scale.y, 3.0);
}

TEST(nine_slice_stretches)
{
    CHECK(!nineslice::stretches(Transform()));
    CHECK(!nineslice::stretches(Transform(1.0, 0.0, 0.0, 1.0, 50.0, 50.0)));
    // Turned but not stretched: drawn as it is.
    CHECK(!nineslice::stretches(Transform(0.0, 1.0, -1.0, 0.0, 0.0, 0.0)));
    CHECK(nineslice::stretches(Transform(2.0, 0.0, 0.0, 1.0, 0.0, 0.0)));
    CHECK(nineslice::stretches(Transform(0.0, 2.0, -1.0, 0.0, 0.0, 0.0)));
    CHECK(!nineslice::stretches(Transform(0.0, 0.0, 0.0, 1.0, 0.0, 0.0)));
}

TEST(nine_slice_corners_keep_their_size)
{
    const std::vector<nineslice::Cell> cells = boxCells(Transform(2.5, 0.0, 0.0, 2.0, 0.0, 0.0));
    CHECK(cells.size() == 9);
    // The top left corner is cut 20 / 2.5 wide and 20 / 2 tall, so the
    // instance brings it back to 20 x 20.
    CHECK_NEAR(cells[0].target.topLeft.x, 0.0);
    CHECK_NEAR(cells[0].target.bottomRight.x, 8.0);
    CHECK_NEAR(cells[0].target.bottomRight.y, 10.0);
    CHECK_NEAR(cells[0].source.bottomRight.x, 20.0);
    // The bottom right corner too, against the far sides.
    CHECK_NEAR(cells[8].target.topLeft.x, 92.0);
    CHECK_NEAR(cells[8].target.topLeft.y, 50.0);
    CHECK_NEAR(cells[8].target.bottomRight.x, 100.0);
    // The middle takes what is left.
    CHECK_NEAR(cells[4].target.width(), 84.0);
    CHECK_NEAR(cells[4].target.height(), 40.0);
}

TEST(nine_slice_turned_cut_as_its_stretch)
{
    // Turned, the cells are those of the same stretch unturned.
    const double angle = 30.0 * 3.14159265358979323846 / 180.0;
    const Transform turned(2.5 * std::cos(angle), 2.5 * std::sin(angle), -2.0 * std::sin(angle),
        2.0 * std::cos(angle), 0.0, 0.0);
    const std::vector<nineslice::Cell> a = boxCells(turned);
    const std::vector<nineslice::Cell> b = boxCells(Transform(2.5, 0.0, 0.0, 2.0, 0.0, 0.0));
    CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        CHECK_NEAR(a[i].target.topLeft.x, b[i].target.topLeft.x);
        CHECK_NEAR(a[i].target.bottomRight.y, b[i].target.bottomRight.y);
    }
}

TEST(nine_slice_squeezed_drops_middle)
{
    // Shrunk below the corners' size there is no room for the middle.
    const std::vector<nineslice::Cell> cells = boxCells(Transform(0.25, 0.0, 0.0, 1.0, 0.0, 0.0));
    for (const nineslice::Cell& cell : cells)
        CHECK(cell.target.width() > 0.0 && cell.target.height() > 0.0);
    CHECK(cells.size() == 6);
}
