#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/symbol_instance.h"
#include "../src/edit/three_d.h"

#include <cmath>
#include <array>

namespace {

bool near(const fla::Point& p, double x, double y, double tolerance = 1.0e-6)
{
    return std::fabs(p.x - x) < tolerance && std::fabs(p.y - y) < tolerance;
}

} // namespace

TEST(three_d_projection_turns_and_pushes_clips_in_perspective)
{
    const fla::Point center(100.0, 100.0);
    const fla::Point vanishing(200.0, 150.0);
    const double focal = fla::threed::focalLength(55.0, 400.0);
    CHECK(std::fabs(focal - 200.0 / std::tan(27.5 * 3.14159265358979323846 / 180.0)) < 1.0e-9);

    // Flat, the clip is where it was.
    const std::array<double, 9> flat = fla::threed::projection(fla::threed::Pose(), center, vanishing, focal);
    CHECK(near(fla::threed::project(flat, fla::Point(37.0, -12.0)), 37.0, -12.0));

    // Pushed in, everything draws toward the vanishing point.
    fla::threed::Pose away;
    away.z = focal;
    const std::array<double, 9> far = fla::threed::projection(away, center, vanishing, focal);
    CHECK(near(fla::threed::project(far, center), 150.0, 125.0));

    // Turned a quarter about the vertical axis, it is edge on: every point
    // of a vertical line through the center stays, and the sides fold in.
    fla::threed::Pose turned;
    turned.rotationY = 90.0;
    const std::array<double, 9> edge = fla::threed::projection(turned, center, vanishing, focal);
    CHECK(near(fla::threed::project(edge, fla::Point(100.0, 60.0)), 100.0, 60.0));
    const fla::Point side = fla::threed::project(edge, fla::Point(150.0, 100.0));
    // The right side swings toward the viewer (z' = -50), on the line from
    // the vanishing point through the center.
    const double scale = focal / (focal - 50.0);
    CHECK(near(side, 200.0 + (100.0 - 200.0) * scale, 150.0 + (100.0 - 150.0) * scale, 1.0e-6));

    // Tilted back about the horizontal axis (negative, as in Flash: y down,
    // z into the stage), the top recedes and narrows.
    fla::threed::Pose tilted;
    tilted.rotationX = -30.0;
    const std::array<double, 9> tilt = fla::threed::projection(tilted, center, vanishing, focal);
    const fla::Point topLeft = fla::threed::project(tilt, fla::Point(50.0, 50.0));
    const fla::Point topRight = fla::threed::project(tilt, fla::Point(150.0, 50.0));
    const fla::Point bottomLeft = fla::threed::project(tilt, fla::Point(50.0, 150.0));
    const fla::Point bottomRight = fla::threed::project(tilt, fla::Point(150.0, 150.0));
    CHECK(topRight.x - topLeft.x < bottomRight.x - bottomLeft.x);
}

TEST(three_d_poses_are_kept_on_the_instance_and_tween)
{
    fla::SymbolInstance instance(nullptr);
    instance.extraAttributes.emplace_back("referenceID", "A");
    CHECK(fla::threed::of(instance).flat());
    fla::threed::Pose pose;
    pose.rotationX = 20.0;
    pose.z = -35.5;
    instance.extraAttributes = fla::threed::withPose(instance, pose);
    CHECK(instance.extraAttributes.size() == 3 && instance.extraAttributes[0].first == "referenceID");
    const fla::threed::Pose read = fla::threed::of(instance);
    CHECK(read.rotationX == 20.0 && read.rotationY == 0.0 && read.z == -35.5);
    instance.extraAttributes = fla::threed::withPose(instance, fla::threed::Pose());
    CHECK(instance.extraAttributes.size() == 1);

    fla::threed::Pose later;
    later.rotationX = 40.0;
    const fla::threed::Pose half = fla::threed::blend(pose, later, 0.5);
    CHECK(half.rotationX == 30.0 && half.z == -17.75);

    fla::Document document(nullptr);
    CHECK(fla::threed::perspectiveAngle(document) == 55.0);
    document.extraAttributes.emplace_back("phoenixPerspectiveAngle", "90");
    CHECK(fla::threed::perspectiveAngle(document) == 90.0);
}
