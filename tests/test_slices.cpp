#include "test_util.h"

#include "../src/data/symbol.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/slices.h"
#include "../src/geom/nine_slice.h"

#include <cmath>

using fla::Point;
using fla::Rect;
namespace nineslice = fla::nineslice;

// A 200 wide panel with a tab from 80 to 120 in the middle of its top edge:
// guides at 20, 80, 120 and 180 keep the corners and the tab, and the edge
// either side of the tab stretches.
TEST(slices_take_turns_keeping_their_size_and_stretching)
{
    const std::vector<nineslice::Cell> cells = nineslice::cells(Rect(Point(0.0, 0.0), Point(200.0, 100.0)),
        {20.0, 80.0, 120.0, 180.0}, {30.0, 70.0}, nineslice::AxisScale{2.0, 1.0});
    // Five strips across, three down.
    CHECK(cells.size() == 15);
    // Across: in the symbol's space a kept strip is its width over the
    // stretch, so the instance's matrix brings it back; the two stretching
    // strips share what is left as they share the art (60 each).
    // Kept strips: 20, 40 and 20 wide, so 10, 20 and 10 here; the stretching
    // two take 200 - 40 = 160 between them, 80 each.
    const double edges[6] = {0.0, 10.0, 90.0, 110.0, 190.0, 200.0};
    for (int column = 0; column < 5; ++column)
    {
        const nineslice::Cell& cell = cells[static_cast<size_t>(column)];
        CHECK_NEAR(cell.target.topLeft.x, edges[column]);
        CHECK_NEAR(cell.target.bottomRight.x, edges[column + 1]);
    }
    // Two guides each way are 9-slice scaling, as before.
    const std::vector<nineslice::Cell> nine = nineslice::cells(Rect(Point(0.0, 0.0), Point(100.0, 60.0)), 20.0, 20.0,
        80.0, 40.0, nineslice::AxisScale{2.0, 2.0});
    CHECK(nine.size() == 9);
    CHECK_NEAR(nine[1].target.topLeft.x, 10.0);
    CHECK_NEAR(nine[1].target.bottomRight.x, 90.0);
}

TEST(slices_shrink_together_without_room)
{
    // Shrunk to a third, the kept strips (40 wide in all) would need 120:
    // they shrink to fit the 100 there is, and nothing stretches.
    const std::vector<nineslice::Cell> cells = nineslice::cells(
        Rect(Point(0.0, 0.0), Point(100.0, 10.0)), {20.0, 80.0}, {0.0, 10.0}, nineslice::AxisScale{1.0 / 3.0, 1.0});
    CHECK(cells.size() == 2);
    CHECK_NEAR(cells[0].target.bottomRight.x, 50.0);
    CHECK_NEAR(cells[1].target.topLeft.x, 50.0);
    CHECK_NEAR(cells[1].target.bottomRight.x, 100.0);
}

TEST(a_symbol_keeps_its_slice_guides_and_its_9_slice_guides_follow)
{
    fla::Symbol symbol(nullptr);
    symbol.scale9 = true;
    symbol.scaleGridLeft = 10.0;
    symbol.scaleGridRight = 90.0;
    symbol.scaleGridTop = 5.0;
    symbol.scaleGridBottom = 55.0;
    CHECK(fla::slices::across(symbol) == std::vector<double>({10.0, 90.0}));
    CHECK(!fla::slices::more(symbol));

    fla::CommandStack stack;
    stack.push(fla::slices::set(symbol, {90.0, 20.0, 40.0, 60.0}, {5.0, 55.0}, "Slice Guides"));
    CHECK(fla::slices::across(symbol) == std::vector<double>({20.0, 40.0, 60.0, 90.0}));
    CHECK(fla::slices::more(symbol));
    // The first and last are the 9-slice guides other apps scale by.
    CHECK_NEAR(symbol.scaleGridLeft, 20.0);
    CHECK_NEAR(symbol.scaleGridRight, 90.0);
    // Down has only two, so no list of its own.
    for (const auto& [key, value] : symbol.extraAttributes)
        CHECK(key != fla::slices::kDown);

    // Another app moving a 9-slice guide leaves 9-slice scaling only.
    symbol.scaleGridLeft = 25.0;
    CHECK(fla::slices::across(symbol) == std::vector<double>({25.0, 90.0}));
    symbol.scaleGridLeft = 20.0;

    stack.undo();
    CHECK_NEAR(symbol.scaleGridLeft, 10.0);
    CHECK(!fla::slices::more(symbol));
    CHECK(symbol.extraAttributes.empty());

    const std::vector<double> spread = fla::slices::spread(0.0, 100.0, 4);
    CHECK(spread == std::vector<double>({20.0, 40.0, 60.0, 80.0}));
}
