#include "test_util.h"

#include "../src/edit/motion_object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <cstdlib>

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

#include "../src/edit/guide_path.h"

TEST(guide_path_joins_strokes_and_measures_along_them)
{
    // Two strokes meeting end to end, the second drawn backward.
    fla::GuidePath path;
    path.build({{fla::Point(0.0, 0.0), fla::Point(100.0, 0.0)}, {fla::Point(100.0, 50.0), fla::Point(100.0, 0.0)}});
    CHECK(!path.empty());
    CHECK(std::fabs(path.length() - 150.0) < 1.0e-9);
    CHECK(std::fabs(path.nearest(fla::Point(40.0, -10.0)) - 40.0) < 1.0e-9);
    CHECK(std::fabs(path.nearest(fla::Point(120.0, 30.0)) - 130.0) < 1.0e-9);
    const fla::Point p = path.at(125.0);
    CHECK(std::fabs(p.x - 100.0) < 1.0e-9 && std::fabs(p.y - 25.0) < 1.0e-9);
    CHECK(std::fabs(path.angleAt(10.0)) < 1.0e-9);
    CHECK(std::fabs(path.angleAt(140.0) - 90.0) < 1.0e-9);
    // Past its ends it holds to them.
    CHECK(std::fabs(path.at(500.0).y - 50.0) < 1.0e-9);
}

TEST(motion_path_bends_through_a_dragged_point)
{
    // A straight slide of 100 px over 10 frames at 10 fps.
    std::string xml = fla::motion::create(fla::MotionState(), 10, 10.0);
    fla::MotionState end;
    end.x = 100.0;
    CHECK(fla::motion::setKeys(xml, 30000.0, end));
    fla::MotionObject motion;
    CHECK(motion.parse(xml));
    CHECK(std::fabs(motion.at(5.0, 10.0).y) < 1.0e-9);

    // Pulled 40 px down halfway, it passes through there and keeps its ends.
    CHECK(fla::motion::bend(xml, 15000.0, 30000.0, 50.0, 40.0));
    CHECK(motion.parse(xml));
    const fla::MotionState middle = motion.at(5.0, 10.0);
    CHECK(std::fabs(middle.x - 50.0) < 1.0e-6 && std::fabs(middle.y - 40.0) < 1.0e-6);
    CHECK(std::fabs(motion.at(10.0, 10.0).y) < 1.0e-9 && std::fabs(motion.at(10.0, 10.0).x - 100.0) < 1.0e-9);
    CHECK(motion.at(2.0, 10.0).y > 0.0);
}

TEST(a_keyframe_added_on_a_curve_keeps_its_shape)
{
    std::string xml = fla::motion::create(fla::MotionState(), 10, 10.0);
    fla::MotionState end;
    end.x = 100.0;
    CHECK(fla::motion::setKeys(xml, 30000.0, end));
    CHECK(fla::motion::bend(xml, 15000.0, 30000.0, 50.0, 40.0));
    fla::MotionObject before;
    CHECK(before.parse(xml));
    CHECK(fla::motion::addKeys(xml, 9000.0));
    fla::MotionObject after;
    CHECK(after.parse(xml));
    for (double frame : {1.0, 2.0, 3.0, 4.5, 7.0, 9.0})
    {
        CHECK(std::fabs(before.at(frame, 10.0).x - after.at(frame, 10.0).x) < 1.0e-4);
        CHECK(std::fabs(before.at(frame, 10.0).y - after.at(frame, 10.0).y) < 1.0e-4);
    }
}

TEST(motion_property_keys_are_read_written_and_eased)
{
    std::string xml = fla::motion::create(fla::MotionState(), 10, 10.0);
    std::vector<fla::motion::PropertyKey> keys = fla::motion::keysOf(xml, "Rotation_Z");
    CHECK(keys.size() == 1 && keys[0].time == 0.0 && keys[0].value == 0.0);
    keys.push_back(fla::motion::PropertyKey::at(27000.0, 90.0));
    // Ease in: slow to start, so a third of the way in it has turned less.
    fla::motion::easeSegment(keys, 0, 0.42, 0.0, 1.0, 1.0);
    CHECK(fla::motion::setKeysOf(xml, "Rotation_Z", keys));
    fla::MotionObject motion;
    CHECK(motion.parse(xml));
    CHECK(motion.at(3.0, 10.0).rotation < 30.0 * 0.5);
    CHECK(std::fabs(motion.at(9.0, 10.0).rotation - 90.0) < 1.0e-6);
    CHECK(fla::motion::keysOf(xml, "Rotation_Z").size() == 2);

    // A property the tween did not have is added.
    CHECK(fla::motion::setKeysOf(xml, "Skew_Y", {fla::motion::PropertyKey::at(0.0, 5.0)}));
    CHECK(motion.parse(xml) && std::fabs(motion.at(4.0, 10.0).skewY - 5.0) < 1.0e-9);

    CHECK(fla::motion::spanEase(xml) == 0.0);
    CHECK(fla::motion::setSpanEase(xml, -50.0) && fla::motion::spanEase(xml) == -50.0);
}

TEST(motion_color_and_filter_curves_play)
{
    std::string xml = fla::motion::create(fla::MotionState(), 10, 10.0);
    // Alpha from 100% to 0% over the span; a drop shadow's blur from 4 to 20.
    CHECK(fla::motion::setKeysOf(xml, "Alpha_Amount",
        {fla::motion::PropertyKey::at(0.0, 100.0), fla::motion::PropertyKey::at(27000.0, 0.0)}));
    fla::Filter shadow;
    shadow.type = fla::Filter::Type::DropShadow;
    shadow.blurX = 4.0;
    const std::string blur = fla::motion::filterPropertyId(shadow, 0, "blurX");
    CHECK(blur == "DropShadowFilter_0_blurX");
    CHECK(fla::motion::setKeysOf(xml, blur,
        {fla::motion::PropertyKey::at(0.0, 4.0), fla::motion::PropertyKey::at(27000.0, 20.0)}));
    CHECK(xml.find("<PropertyContainer id=\"Colors\"><Property enabled=\"1\" id=\"Alpha_Amount\"") != std::string::npos);

    fla::MotionObject motion;
    CHECK(motion.parse(xml));
    CHECK(motion.animatesColor() && motion.animatesFilters());
    fla::ColorTransform base;
    base.brightness = 0.25;
    const fla::ColorTransform half = motion.colorAt(4.5, 10.0, base);
    CHECK(std::fabs(half.alphaMultiplier - 0.5) < 1.0e-6);
    CHECK(half.brightness == 0.25);
    const std::vector<fla::Filter> filters = motion.filtersAt(9.0, 10.0, {shadow});
    CHECK(filters.size() == 1 && std::fabs(filters[0].blurX - 20.0) < 1.0e-6 && filters[0].blurY == 5.0);
    CHECK(fla::motion::colorValue(half, "Alpha_Amount") > 49.0);
}

#include "../src/edit/brush_texture.h"
#include "../src/data/shape.h"

TEST(brush_textures_are_made_alike_and_kept_on_shapes)
{
    for (const std::string& name : fla::texture::builtIn())
    {
        const fla::texture::Tile tile = fla::texture::builtInTile(name);
        CHECK(tile.size == 256 && tile.coverage.size() == 256u * 256u);
        // A grain, not a flat tone.
        const auto range = std::minmax_element(tile.coverage.begin(), tile.coverage.end());
        CHECK(*range.second - *range.first > 100);
        CHECK(fla::texture::builtInTile(name).coverage == tile.coverage);
        // Seamless: the columns (and rows) either side of the edge where the
        // tile repeats differ no more than any neighboring pair inside it.
        const auto at = [&tile](int x, int y) { return static_cast<int>(tile.coverage[y * 256 + x]); };
        const auto columns = [&at](int a, int b) {
            double sum = 0.0;
            for (int i = 0; i < 256; ++i)
                sum += std::abs(at(a, i) - at(b, i));
            return sum / 256.0;
        };
        const auto rows = [&at](int a, int b) {
            double sum = 0.0;
            for (int i = 0; i < 256; ++i)
                sum += std::abs(at(i, a) - at(i, b));
            return sum / 256.0;
        };
        double mostX = 0.0;
        double mostY = 0.0;
        for (int j = 0; j < 255; ++j)
        {
            mostX = std::max(mostX, columns(j, j + 1));
            mostY = std::max(mostY, rows(j, j + 1));
        }
        const double wrapX = columns(255, 0);
        const double wrapY = rows(255, 0);
        if (wrapX > mostX + 1.0 || wrapY > mostY + 1.0)
            std::printf("    seam in %s: %.1f/%.1f across, %.1f/%.1f down\n", name.c_str(), wrapX, mostX, wrapY, mostY);
        CHECK(wrapX <= mostX + 1.0);
        CHECK(wrapY <= mostY + 1.0);
    }
    CHECK(fla::texture::builtIn().size() >= 19);
    CHECK(fla::texture::builtInTile("Nothing").size == 0);

    fla::Shape shape(nullptr);
    fla::texture::Spec spec;
    spec.name = "Chalk";
    spec.scale = 2.0;
    spec.strength = 0.5;
    fla::texture::set(shape.extraAttributes, spec);
    const fla::texture::Spec read = fla::texture::of(shape);
    CHECK(read.name == "Chalk" && read.scale == 2.0 && read.strength == 0.5);
    fla::texture::set(shape.extraAttributes, fla::texture::Spec());
    CHECK(!fla::texture::of(shape).valid() && shape.extraAttributes.empty());
}

TEST(motion_object_honors_ignore_time_map_and_orient_to_path)
{
    // Eased all the way out, but X ignores the ease; Y goes down then
    // right, a quarter turn of the path.
    const std::string xml =
        "<AnimationCore TimeScale=\"10\" Version=\"1\" duration=\"20\">"
        "<TimeMap strength=\"100\" type=\"Quadratic\"/>"
        "<metadata><Settings orientToPath=\"1\"/></metadata>"
        "<PropertyContainer id=\"headContainer\"><PropertyContainer id=\"Basic_Motion\">"
        "<Property enabled=\"1\" id=\"Motion_X\" ignoreTimeMap=\"1\">"
        "<Keyframe anchor=\"0,0\" timevalue=\"0\"/><Keyframe anchor=\"0,0\" timevalue=\"10\"/>"
        "<Keyframe anchor=\"0,100\" timevalue=\"20\"/></Property>"
        "<Property enabled=\"1\" id=\"Motion_Y\" ignoreTimeMap=\"1\">"
        "<Keyframe anchor=\"0,0\" timevalue=\"0\"/><Keyframe anchor=\"0,100\" timevalue=\"10\"/></Property>"
        "<Property enabled=\"1\" id=\"Scale_X\">"
        "<Keyframe anchor=\"0,100\" timevalue=\"0\"/><Keyframe anchor=\"0,200\" timevalue=\"20\"/></Property>"
        "</PropertyContainer></PropertyContainer></AnimationCore>";
    fla::MotionObject motion;
    CHECK(motion.parse(xml));
    // Half way: X and Y linear in time; the eased scale is past half way.
    const fla::MotionState middle = motion.at(1.0, 1.0);
    CHECK(std::fabs(middle.y - 100.0) < 1.0e-6);
    CHECK(middle.scaleX > 150.0 + 1.0);
    // Heading down at the start, then right: turned a quarter turn back.
    const fla::MotionState start = motion.at(0.0, 1.0);
    const fla::MotionState later = motion.at(1.5, 1.0);
    CHECK(std::fabs(start.rotation) < 1.0e-6);
    CHECK(std::fabs(later.rotation + 90.0) < 1.0e-3);
}
