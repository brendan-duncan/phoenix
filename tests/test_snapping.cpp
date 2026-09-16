#include "test_util.h"

#include "../src/edit/snapping.h"

#include <vector>

using fla::SnapKind;
using fla::Snapper;
using fla::SnapResult;

namespace {

Snapper gridSnapper(double spacing, double tolerance)
{
    Snapper snapper;
    snapper.setTolerance(tolerance);
    snapper.setGridEnabled(true);
    snapper.setGridSpacing(spacing, spacing);
    return snapper;
}

} // namespace

TEST(snapper_does_nothing_when_disabled)
{
    Snapper snapper;
    CHECK(!snapper.isEnabled());

    const SnapResult result = snapper.snapX({10.3});
    CHECK(!result.snapped());
    CHECK(result.adjustment == 0.0);
}

TEST(snapper_snaps_to_the_grid)
{
    Snapper snapper = gridSnapper(18.0, 5.0);

    const SnapResult result = snapper.snapX({20.0});
    CHECK(result.snapped());
    CHECK(result.kind == SnapKind::Grid);
    CHECK_NEAR(result.target, 18.0);
    CHECK_NEAR(result.adjustment, -2.0);
}

TEST(snapper_leaves_values_beyond_tolerance_alone)
{
    Snapper snapper = gridSnapper(100.0, 5.0);

    // 40 is 40 away from 0 and 60 from 100, so nothing is within reach.
    const SnapResult result = snapper.snapX({40.0});
    CHECK(!result.snapped());
    CHECK(result.adjustment == 0.0);
}

TEST(snapper_picks_the_closest_of_several_moving_values)
{
    Snapper snapper = gridSnapper(100.0, 10.0);

    // A box whose left edge is at 3 and right edge at 55: the left edge is the
    // one close to a grid line, so the whole box moves by its offset.
    const SnapResult result = snapper.snapX({3.0, 29.0, 55.0});
    CHECK(result.snapped());
    CHECK_NEAR(result.target, 0.0);
    CHECK_NEAR(result.adjustment, -3.0);
}

TEST(snapper_uses_one_adjustment_for_the_whole_axis)
{
    Snapper snapper = gridSnapper(100.0, 10.0);

    // Two edges are both near grid lines. Only one adjustment comes back, so the
    // shape shifts rather than stretching to meet both -- and it is the closer
    // edge that decides: 102 is 2 from 100, while 197 is 3 from 200.
    const SnapResult result = snapper.snapX({102.0, 197.0});
    CHECK(result.snapped());
    CHECK_NEAR(result.adjustment, -2.0);
    CHECK_NEAR(result.target, 100.0);
}

TEST(snapper_snaps_to_objects)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setObjectSnapEnabled(true);
    snapper.addCandidateX(250.0);

    const SnapResult result = snapper.snapX({248.0});
    CHECK(result.snapped());
    CHECK(result.kind == SnapKind::Object);
    CHECK_NEAR(result.adjustment, 2.0);
}

TEST(snapper_prefers_objects_over_the_grid_at_equal_distance)
{
    Snapper snapper;
    snapper.setTolerance(10.0);
    snapper.setGridEnabled(true);
    snapper.setGridSpacing(100.0, 100.0);
    snapper.setObjectSnapEnabled(true);
    // Both are 5 away from 105.
    snapper.addCandidateX(110.0);

    const SnapResult result = snapper.snapX({105.0});
    CHECK(result.snapped());
    // Lining up with another object is the more useful of the two.
    CHECK(result.kind == SnapKind::Object);
    CHECK_NEAR(result.target, 110.0);
}

TEST(snapper_takes_the_grid_when_it_is_strictly_closer)
{
    Snapper snapper;
    snapper.setTolerance(10.0);
    snapper.setGridEnabled(true);
    snapper.setGridSpacing(100.0, 100.0);
    snapper.setObjectSnapEnabled(true);
    snapper.addCandidateX(108.0);

    const SnapResult result = snapper.snapX({102.0});
    CHECK(result.snapped());
    CHECK(result.kind == SnapKind::Grid);
    CHECK_NEAR(result.target, 100.0);
}

TEST(snapper_ignores_objects_when_object_snapping_is_off)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.addCandidateX(250.0);

    const SnapResult result = snapper.snapX({248.0});
    CHECK(!result.snapped());
}

TEST(snapper_axes_are_independent)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setObjectSnapEnabled(true);
    snapper.addCandidateX(100.0);
    snapper.addCandidateY(500.0);

    CHECK(snapper.snapX({102.0}).snapped());
    CHECK(!snapper.snapY({102.0}).snapped());
    CHECK(snapper.snapY({498.0}).snapped());
}

TEST(snapper_handles_a_non_square_grid)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setGridEnabled(true);
    snapper.setGridSpacing(10.0, 40.0);

    CHECK_NEAR(snapper.snapX({12.0}).adjustment, -2.0);
    CHECK_NEAR(snapper.snapY({42.0}).adjustment, -2.0);
}

TEST(snapper_ignores_a_zero_spacing_axis)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setGridEnabled(true);
    // A zero spacing would otherwise divide by nothing.
    snapper.setGridSpacing(0.0, 20.0);

    CHECK(!snapper.snapX({12.0}).snapped());
    CHECK(snapper.snapY({21.0}).snapped());
}

TEST(snapper_clear_candidates_forgets_objects)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setObjectSnapEnabled(true);
    snapper.addCandidateX(100.0);
    CHECK(snapper.snapX({102.0}).snapped());

    snapper.clearCandidates();
    CHECK(!snapper.snapX({102.0}).snapped());
}

TEST(snapper_handles_negative_coordinates)
{
    Snapper snapper = gridSnapper(20.0, 5.0);

    const SnapResult result = snapper.snapX({-38.0});
    CHECK(result.snapped());
    CHECK_NEAR(result.target, -40.0);
    CHECK_NEAR(result.adjustment, -2.0);
}
