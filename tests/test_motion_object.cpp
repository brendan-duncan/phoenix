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

namespace {

bool near(const fla::Transform& a, const fla::Transform& b, double tolerance = 1.0e-6)
{
    return std::fabs(a.m11 - b.m11) < tolerance && std::fabs(a.m12 - b.m12) < tolerance &&
           std::fabs(a.m21 - b.m21) < tolerance && std::fabs(a.m22 - b.m22) < tolerance &&
           std::fabs(a.tx - b.tx) < tolerance && std::fabs(a.ty - b.ty) < tolerance;
}

} // namespace

TEST(motion_tween_is_created_holding_its_object)
{
    // Turned 30 degrees and scaled 2 by 1.5, as the matrix has it.
    const double r = 30.0 * kPi / 180.0;
    const fla::Transform stored(2.0 * std::cos(r), 2.0 * std::sin(r), -1.5 * std::sin(r), 1.5 * std::cos(r), 40.0, 60.0);
    const fla::MotionState start = fla::motion::stateOf(stored);
    CHECK(std::fabs(start.rotation - 30.0) < 1.0e-9 && std::fabs(start.skewX) < 1.0e-9);
    CHECK(std::fabs(start.scaleX - 200.0) < 1.0e-9 && std::fabs(start.scaleY - 150.0) < 1.0e-9);

    const std::string xml = fla::motion::create(start, 24, 24.0, 0.25, 0.75);
    CHECK(xml.compare(0, 16, "<motionObjectXML") == 0);
    CHECK(xml.find("xformPtXOffsetPct=\"0.25\"") != std::string::npos);
    fla::MotionObject motion;
    CHECK(motion.parse(xml));
    CHECK(std::fabs(motion.frames(24.0) - 24.0) < 1.0e-9);
    const fla::MotionState read = motion.at(10.0, 24.0);
    CHECK(std::fabs(read.rotation - 30.0) < 1.0e-9 && std::fabs(read.scaleX - 200.0) < 1.0e-9);
    CHECK(near(fla::applyMotion(stored, fla::Point(5.0, 5.0), motion.at(0.0, 24.0), read), stored));
}

TEST(motion_keys_record_what_an_edit_shows)
{
    const fla::Transform stored(1.0, 0.0, 0.0, 1.0, 100.0, 50.0);
    const fla::Point pivot(10.0, 20.0);
    std::string xml = fla::motion::create(fla::motion::stateOf(stored), 20, 20.0);
    fla::MotionObject motion;
    CHECK(motion.parse(xml));
    const fla::MotionState start = motion.at(0.0, 20.0);

    // Halfway, the object is turned, scaled and moved on the stage.
    const double r = 45.0 * kPi / 180.0;
    const fla::Transform wanted(1.5 * std::cos(r), 1.5 * std::sin(r), -std::sin(r), std::cos(r), 300.0, 10.0);
    const fla::MotionState state = fla::motion::solve(stored, pivot, start, wanted, motion.at(10.0, 20.0));
    CHECK(near(fla::applyMotion(stored, pivot, start, state), wanted));
    CHECK(fla::motion::setKeys(xml, 15000.0, state));
    CHECK(motion.parse(xml));
    CHECK(near(fla::applyMotion(stored, pivot, start, motion.at(10.0, 20.0)), wanted));
    // The start is as it was, and the way there is in between.
    CHECK(near(fla::applyMotion(stored, pivot, start, motion.at(0.0, 20.0)), stored));
    CHECK(std::fabs(motion.at(5.0, 20.0).rotation - 22.5) < 1.0e-6);

    // Keying it again where it is moves that keyframe.
    fla::MotionState again = state;
    again.x += 10.0;
    CHECK(fla::motion::setKeys(xml, 15000.0, again));
    CHECK(motion.parse(xml));
    CHECK(motion.keyTimes().size() == 2);
    CHECK(std::fabs(motion.at(10.0, 20.0).x - again.x) < 1.0e-6);

    // The start moved, the later positions stay.
    CHECK(fla::motion::moveStart(xml, 5.0, 0.0));
    CHECK(motion.parse(xml));
    CHECK(std::fabs(motion.at(10.0, 20.0).x - (again.x - 5.0)) < 1.0e-6);
    CHECK(std::fabs(motion.at(0.0, 20.0).x) < 1.0e-9);
}

TEST(motion_unapplied_is_the_stored_transform)
{
    const fla::Transform stored(1.2, 0.3, -0.2, 0.9, 15.0, -7.0);
    const fla::Point pivot(3.0, 4.0);
    fla::MotionState start;
    start.rotation = 10.0;
    start.scaleX = 120.0;
    fla::MotionState now;
    now.rotation = 70.0;
    now.skewX = 5.0;
    now.scaleY = 80.0;
    now.x = 12.0;
    now.y = -30.0;
    const fla::Transform shown = fla::applyMotion(stored, pivot, start, now);
    CHECK(near(fla::unapplyMotion(shown, pivot, start, now), stored));
}
