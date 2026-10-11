#include "test_util.h"

#include "../src/edit/motion_object.h"

#include <cmath>
#include <string>

namespace {

const double kPi = 3.14159265358979323846;

/// Layer_14's tween in template_robot.fla: 11 frames at 30 fps, sliding
/// 1110.35 px left over the first 6, at 377% scale throughout.
const char* kSlide =
    "<motionObjectXML><AnimationCore TimeScale=\"30000\" Version=\"1\" duration=\"11000\">"
    "<TimeMap strength=\"0\" type=\"Quadratic\"/>"
    "<PropertyContainer id=\"headContainer\"><PropertyContainer id=\"Basic_Motion\">"
    "<Property enabled=\"1\" id=\"Motion_X\"><Keyframe anchor=\"0,0\" next=\"0,0\" previous=\"0,0\" timevalue=\"0\"/>"
    "<Keyframe anchor=\"0,-1110.35\" next=\"0,-1110.35\" previous=\"0,-1110.35\" timevalue=\"6000\"/></Property>"
    "<Property enabled=\"1\" id=\"Rotation_Z\"><Keyframe anchor=\"0,12.1955\" next=\"0,12.1955\" previous=\"0,12.1955\" timevalue=\"0\"/>"
    "<Keyframe anchor=\"0,17.0397\" next=\"0,17.0397\" previous=\"0,17.0397\" timevalue=\"9000\"/></Property>"
    "</PropertyContainer><PropertyContainer id=\"Transformation\">"
    "<Property enabled=\"1\" id=\"Scale_X\"><Keyframe anchor=\"0,377.142\" next=\"0,377.142\" previous=\"0,377.142\" timevalue=\"0\"/></Property>"
    "</PropertyContainer><PropertyContainer id=\"Colors\"/></PropertyContainer></AnimationCore></motionObjectXML>";

} // namespace

TEST(motion_object_curves_are_read_over_time)
{
    fla::MotionObject motion;
    CHECK(motion.parse(kSlide));
    CHECK(std::fabs(motion.frames(30.0) - 11.0) < 1.0e-9);
    // Handles at the keyframes make straight lines: half way in time is
    // half way in value.
    fla::MotionState state = motion.at(3.0, 30.0);
    CHECK(std::fabs(state.x + 555.175) < 1.0e-6);
    CHECK(std::fabs(state.rotation - (12.1955 + (17.0397 - 12.1955) / 3.0)) < 1.0e-6);
    // Past the last keyframe a value holds; a property with one keyframe is
    // that value throughout; a missing one is its default.
    state = motion.at(8.0, 30.0);
    CHECK(std::fabs(state.x + 1110.35) < 1.0e-6);
    CHECK(std::fabs(state.scaleX - 377.142) < 1.0e-9);
    CHECK(std::fabs(state.scaleY - 100.0) < 1.0e-9);
    CHECK(std::fabs(state.y) < 1.0e-9);

    CHECK(!motion.parse("<betweenFrameMatrixList/>"));
}

TEST(motion_object_handles_bend_the_curve)
{
    // A handle pulled out flattens the start: a quarter of the way in time
    // the value is under a quarter of the way.
    fla::MotionObject motion;
    CHECK(motion.parse(
        "<AnimationCore TimeScale=\"24000\" duration=\"4000\"><PropertyContainer id=\"headContainer\">"
        "<Property id=\"Motion_Y\"><Keyframe anchor=\"0,0\" next=\"2000,0\" previous=\"0,0\" timevalue=\"0\"/>"
        "<Keyframe anchor=\"0,100\" next=\"0,100\" previous=\"0,100\" timevalue=\"4000\"/></Property>"
        "</PropertyContainer></AnimationCore>"));
    const double quarter = motion.at(1.0, 24.0).y;
    CHECK(quarter > 0.0 && quarter < 25.0);
    CHECK(std::fabs(motion.at(4.0, 24.0).y - 100.0) < 1.0e-6);
}

TEST(motion_is_applied_about_the_pivot)
{
    // Stored turned 10 degrees with its pivot (0, 0) at (100, 50); at 40
    // degrees and 30 px further right it is turned 40 degrees about the
    // same pivot, moved.
    const double r = 10.0 * kPi / 180.0;
    const fla::Transform stored(std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 100.0, 50.0);
    fla::MotionState start;
    start.rotation = 10.0;
    fla::MotionState now = start;
    now.rotation = 40.0;
    now.x = 30.0;
    const fla::Transform moved = fla::applyMotion(stored, fla::Point(0.0, 0.0), start, now);
    const double r2 = 40.0 * kPi / 180.0;
    CHECK(std::fabs(moved.m11 - std::cos(r2)) < 1.0e-9 && std::fabs(moved.m12 - std::sin(r2)) < 1.0e-9);
    CHECK(std::fabs(moved.tx - 130.0) < 1.0e-9 && std::fabs(moved.ty - 50.0) < 1.0e-9);

    // About a pivot away from the origin, the pivot stays put.
    const fla::Point pivot(20.0, 0.0);
    const fla::Transform turned = fla::applyMotion(fla::Transform(), pivot, fla::MotionState(), [] {
        fla::MotionState s;
        s.rotation = 90.0;
        return s;
    }());
    const double px = turned.m11 * 20.0 + turned.tx;
    const double py = turned.m12 * 20.0 + turned.ty;
    CHECK(std::fabs(px - 20.0) < 1.0e-9 && std::fabs(py) < 1.0e-9);
}
