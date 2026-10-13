#include "test_util.h"

#include "../src/edit/snapping.h"

#include <cmath>
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

TEST(guides_snap_and_hold_against_objects_no_closer)
{
    fla::Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setObjectSnapEnabled(true);
    snapper.addCandidateX(103.0);
    snapper.setGuides({103.0}, {});
    fla::SnapResult result = snapper.snapX({100.0});
    CHECK(result.kind == fla::SnapKind::Guide);
    CHECK_NEAR(result.adjustment, 3.0);
    // An object strictly closer still wins.
    snapper.addCandidateX(101.0);
    result = snapper.snapX({100.0});
    CHECK(result.kind == fla::SnapKind::Object);
    // Switched off, guides are ignored, and with nothing else on nothing snaps.
    snapper.setGuideSnapEnabled(false);
    snapper.setObjectSnapEnabled(false);
    CHECK(!snapper.isEnabled());
    CHECK(!snapper.snapX({100.0}).snapped());
}

TEST(snap_to_pixels_when_nothing_else_is_near)
{
    fla::Snapper snapper;
    snapper.setObjectSnapEnabled(false);
    CHECK(!snapper.isEnabled() || snapper.guideSnapEnabled());
    snapper.setPixelSnapEnabled(true);
    CHECK(snapper.isEnabled());
    const fla::SnapResult x = snapper.snapX({10.3, 20.0});
    CHECK(x.kind == fla::SnapKind::Pixel);
    CHECK(std::fabs(x.adjustment + 0.3) < 1.0e-9);

    // An object in reach still wins.
    snapper.setObjectSnapEnabled(true);
    snapper.addCandidateX(12.0);
    const fla::SnapResult y = snapper.snapX({10.3});
    CHECK(y.kind == fla::SnapKind::Object);
}

namespace {

/// A box from (0, 0) to (100, 50), its right edge handle dragged with the
/// left edge held: only the x scale follows the pointer.
fla::HandleDrag rightEdgeDrag(double scaleX)
{
    fla::HandleDrag drag;
    drag.anchorX = 0.0;
    drag.anchorY = 25.0;
    drag.axisXx = 100.0;
    drag.scaleX = scaleX;
    drag.scalesY = false;
    return drag;
}

/// The same box's bottom right corner dragged with the top left held.
fla::HandleDrag cornerDrag(double scaleX, double scaleY)
{
    fla::HandleDrag drag;
    drag.axisXx = 100.0;
    drag.axisYy = 50.0;
    drag.scaleX = scaleX;
    drag.scaleY = scaleY;
    return drag;
}

Snapper objectSnapper()
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setObjectSnapEnabled(true);
    return snapper;
}

} // namespace

TEST(handle_lands_exactly_on_a_vertical_line)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateX(150.0);

    // The handle would sit at x = 147; the scale is solved so it sits at 150.
    const fla::HandleSnapResult result = snapper.snapHandle(rightEdgeDrag(1.47));
    CHECK(result.snapped());
    CHECK(result.kind == SnapKind::Object);
    CHECK_NEAR(result.scaleX, 1.5);
    CHECK_NEAR(result.scaleY, 1.0);
    CHECK_NEAR(result.x, 150.0);
    CHECK_NEAR(result.y, 25.0);
}

TEST(handle_lands_exactly_on_a_horizontal_line)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateY(50.0);

    // A corner moves on both axes; the line fixes only the y scale.
    const fla::HandleSnapResult result = snapper.snapHandle(cornerDrag(1.48, 0.97));
    CHECK(result.snapped());
    CHECK(!result.onPoint);
    CHECK_NEAR(result.scaleX, 1.48);
    CHECK_NEAR(result.scaleY, 1.0);
    CHECK_NEAR(result.y, 50.0);
}

TEST(handle_corner_takes_a_line_on_each_axis_at_once)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateX(150.0);
    snapper.addCandidateY(50.0);

    const fla::HandleSnapResult result = snapper.snapHandle(cornerDrag(1.48, 0.97));
    CHECK(result.onPoint);
    CHECK_NEAR(result.x, 150.0);
    CHECK_NEAR(result.y, 50.0);
    CHECK_NEAR(result.scaleX, 1.5);
    CHECK_NEAR(result.scaleY, 1.0);
}

TEST(handle_snap_refuses_to_flip_or_collapse_the_box)
{
    Snapper snapper = objectSnapper();

    // The handle is 2 units from the anchor; a line just past the anchor
    // would turn the box over.
    snapper.addCandidateX(-1.0);
    CHECK(!snapper.snapHandle(rightEdgeDrag(0.02)).snapped());

    // And one on the anchor would squash it flat.
    snapper.clearCandidates();
    snapper.addCandidateX(0.0);
    CHECK(!snapper.snapHandle(rightEdgeDrag(0.02)).snapped());

    // A box already flipped by the drag may still snap on its flipped side.
    snapper.clearCandidates();
    snapper.addCandidateX(-50.0);
    const fla::HandleSnapResult flipped = snapper.snapHandle(rightEdgeDrag(-0.48));
    CHECK(flipped.snapped());
    CHECK_NEAR(flipped.scaleX, -0.5);
}

TEST(handle_snap_refuses_to_move_the_handle_far)
{
    Snapper snapper = objectSnapper();

    // The handle moves along a shallow slope, so a horizontal line 4 units
    // above is only met 80 units along: too far to jump.
    fla::HandleDrag drag;
    drag.axisXx = 100.0;
    drag.axisXy = 5.0;
    drag.scaleX = 1.0;
    drag.scalesY = false;
    snapper.addCandidateY(9.0);
    CHECK(!snapper.snapHandle(drag).snapped());

    // A steeper path meets it close by.
    drag.axisXy = 50.0;
    drag.scaleX = 0.12;
    snapper.clearCandidates();
    snapper.addCandidateY(8.0);
    const fla::HandleSnapResult result = snapper.snapHandle(drag);
    CHECK(result.snapped());
    CHECK_NEAR(result.y, 8.0);
    CHECK_NEAR(result.scaleX, 0.16);
}

TEST(handle_snap_prefers_points_over_lines)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateX(149.0);
    snapper.addCandidatePoint(151.0, 51.0);

    // The line is nearer, but the vertex wins.
    const fla::HandleSnapResult result = snapper.snapHandle(cornerDrag(1.48, 0.97));
    CHECK(result.onPoint);
    CHECK_NEAR(result.x, 151.0);
    CHECK_NEAR(result.y, 51.0);
    CHECK_NEAR(result.scaleX, 1.51);
    CHECK_NEAR(result.scaleY, 1.02);

    // Out of reach, the vertex gives way to the line.
    snapper.clearCandidates();
    snapper.addCandidateX(149.0);
    snapper.addCandidatePoint(160.0, 60.0);
    const fla::HandleSnapResult line = snapper.snapHandle(cornerDrag(1.48, 0.97));
    CHECK(!line.onPoint);
    CHECK_NEAR(line.x, 149.0);
}

TEST(handle_snap_with_shift_solves_one_scale)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateX(150.0);

    fla::HandleDrag drag = cornerDrag(1.47, 1.47);
    drag.uniform = true;
    const fla::HandleSnapResult result = snapper.snapHandle(drag);
    CHECK(result.snapped());
    CHECK_NEAR(result.scaleX, 1.5);
    CHECK_NEAR(result.scaleY, 1.5);
    CHECK_NEAR(result.x, 150.0);
    CHECK_NEAR(result.y, 75.0);
}

TEST(handle_snap_with_alt_scales_about_the_center)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateX(150.0);

    // Held at the center (50, 25), the corner is (50, 25) away from it.
    fla::HandleDrag drag;
    drag.anchorX = 50.0;
    drag.anchorY = 25.0;
    drag.axisXx = 50.0;
    drag.axisYy = 25.0;
    drag.scaleX = 1.9;
    drag.scaleY = 1.3;
    const fla::HandleSnapResult result = snapper.snapHandle(drag);
    CHECK(result.snapped());
    CHECK_NEAR(result.scaleX, 2.0);
    CHECK_NEAR(result.scaleY, 1.3);
    CHECK_NEAR(result.x, 150.0);
}

TEST(handle_snap_on_a_turned_box_lands_on_the_line)
{
    Snapper snapper = objectSnapper();

    // A box turned 30 degrees: its sides are no longer the page's axes, yet
    // the handle still lands exactly on the vertical line.
    const double c = std::cos(0.5235987755982988);
    const double s = std::sin(0.5235987755982988);
    fla::HandleDrag drag;
    drag.axisXx = 100.0 * c;
    drag.axisXy = 100.0 * s;
    drag.axisYx = -50.0 * s;
    drag.axisYy = 50.0 * c;
    drag.scaleX = 1.2;
    drag.scaleY = 0.9;
    const double hx = 1.2 * 100.0 * c - 0.9 * 50.0 * s;
    const double hy = 1.2 * 100.0 * s + 0.9 * 50.0 * c;
    snapper.addCandidateX(hx + 3.0);
    const fla::HandleSnapResult result = snapper.snapHandle(drag);
    CHECK(result.snapped());
    CHECK_NEAR(result.x, hx + 3.0);
    CHECK_NEAR(result.y, hy);
    CHECK_NEAR(drag.anchorX + result.scaleX * drag.axisXx + result.scaleY * drag.axisYx, hx + 3.0);
    CHECK_NEAR(drag.anchorY + result.scaleX * drag.axisXy + result.scaleY * drag.axisYy, hy);
}

TEST(handle_snaps_to_an_outline_segment)
{
    Snapper snapper = objectSnapper();

    // A slanted piece of outline crossing the edge handle's path at x = 152.
    snapper.addCandidateSegment(148.0, 21.0, 156.0, 29.0);
    const fla::HandleSnapResult result = snapper.snapHandle(rightEdgeDrag(1.5));
    CHECK(result.snapped());
    CHECK(result.kind == SnapKind::Object);
    CHECK_NEAR(result.x, 152.0);
    CHECK_NEAR(result.scaleX, 1.52);

    // A corner lands on the nearest spot of the segment.
    snapper.clearCandidates();
    snapper.addCandidateSegment(140.0, 60.0, 160.0, 40.0);
    const fla::HandleSnapResult corner = snapper.snapHandle(cornerDrag(1.48, 0.97));
    CHECK(corner.snapped());
    CHECK_NEAR(corner.x + corner.y, 200.0);
}

TEST(handle_snaps_to_the_grid_and_guides)
{
    Snapper snapper;
    snapper.setTolerance(5.0);
    snapper.setGridEnabled(true);
    snapper.setGridSpacing(20.0, 20.0);

    const fla::HandleSnapResult grid = snapper.snapHandle(rightEdgeDrag(1.43));
    CHECK(grid.kind == SnapKind::Grid);
    CHECK_NEAR(grid.x, 140.0);

    // A corner near a grid crossing takes the crossing.
    const fla::HandleSnapResult crossing = snapper.snapHandle(cornerDrag(1.43, 1.17));
    CHECK(crossing.onPoint);
    CHECK_NEAR(crossing.x, 140.0);
    CHECK_NEAR(crossing.y, 60.0);

    // A guide as near as the grid line wins the tie.
    snapper.setGuides({140.0}, {});
    CHECK(snapper.snapHandle(rightEdgeDrag(1.43)).kind == SnapKind::Guide);
}

TEST(handle_drag_left_alone_with_nothing_in_reach)
{
    Snapper snapper = objectSnapper();
    snapper.addCandidateX(200.0);
    const fla::HandleSnapResult result = snapper.snapHandle(cornerDrag(1.48, 0.97));
    CHECK(!result.snapped());
    CHECK_NEAR(result.scaleX, 1.48);
    CHECK_NEAR(result.scaleY, 0.97);
}
