#include "test_util.h"

#include "../src/data/symbol_instance.h"
#include "../src/edit/behaviors.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/jsfl/behavior_engine.h"
#include "../src/jsfl/behavior_frames.h"
#include "../src/edit/guide_path.h"
#include "../src/edit/shape_operators.h"
#include "../src/edit/text_animators.h"
#include "../src/edit/adjustments.h"

#include <cmath>
#include <string>
#include <vector>

using namespace fla::behaviors;

namespace {

bool near(double a, double b, double tolerance = 1e-9)
{
    return std::fabs(a - b) <= tolerance;
}

/// A moment on frame \a frame with nothing keyed moving.
Moment still(int frame, int start = 0, int end = 100)
{
    Moment moment;
    moment.frame = frame;
    moment.frameRate = 24.0;
    moment.start = start;
    moment.end = end;
    return moment;
}

/// The first copy behaviors make.
Delta one(Engine& engine, const std::vector<Applied>& list, const Moment& moment, std::string* error = nullptr)
{
    const Copies copies = engine.evaluate(list, moment, error);
    return copies.empty() ? Delta() : copies.front();
}

} // namespace

TEST(behaviors_are_kept_on_the_instance)
{
    Applied wiggle;
    wiggle.type = "wiggle";
    wiggle.params["position"] = "12.5";
    wiggle.params["odd|name"] = "a=b&c?d%";
    Applied off;
    off.type = "spin";
    off.enabled = false;
    const std::vector<Applied> list = {wiggle, off};
    const std::vector<Applied> back = decode(encode(list));
    CHECK(back.size() == 2 && back[0] == wiggle && back[1] == off);

    fla::SymbolInstance instance(nullptr);
    instance.extraAttributes.emplace_back("other", "kept");
    instance.extraAttributes = withBehaviors(instance, list);
    CHECK(of(instance).size() == 2 && any(instance));
    CHECK(instance.extraAttributes.front().first == "other");
    instance.extraAttributes = withBehaviors(instance, {off});
    CHECK(!any(instance));
    instance.extraAttributes = withBehaviors(instance, {});
    CHECK(instance.extraAttributes.size() == 1);
}

TEST(behaviors_turn_and_scale_about_the_transformation_point)
{
    // An instance at (100, 50), its transformation point 10 right of its
    // origin: a quarter turn keeps that point where it is.
    const fla::Transform keyed(1, 0, 0, 1, 100, 50);
    const fla::Point pivot(10, 0);
    Delta delta;
    delta.rotation = 90;
    delta.scaleX = 2;
    delta.x = 5;
    const fla::Transform out = apply(keyed, pivot, delta);
    const double px = out.m11 * pivot.x + out.m21 * pivot.y + out.tx;
    const double py = out.m12 * pivot.x + out.m22 * pivot.y + out.ty;
    CHECK(near(px, 115.0) && near(py, 50.0));
    const Keyed now = keyedOf(out, pivot, fla::ColorTransform());
    CHECK(near(now.rotation, 90.0) && near(now.scaleX, 2.0) && near(now.scaleY, 1.0));
}

TEST(behaviors_built_in_ones_run_the_same_every_time)
{
    Engine engine;
    engine.addBuiltIns();
    CHECK(engine.find("wiggle") && engine.find("oscillate") && engine.find("follow") && engine.find("fade"));
    const Definition* wiggle = engine.find("wiggle");
    CHECK(wiggle && wiggle->params.size() == 5 && wiggle->params[0].name == "frequency");

    const Applied applied = engine.defaults("wiggle", 7);
    CHECK(applied.params.at("seed") == "7" && applied.params.at("position") == "10");
    const Delta a = one(engine, {applied}, still(30));
    const Delta b = one(engine, {applied}, still(30));
    const Delta c = one(engine, {applied}, still(31));
    CHECK(a.x == b.x && a.y == b.y && (a.x != c.x || a.y != c.y));
    CHECK(std::fabs(a.x) <= 10.0 && std::fabs(a.y) <= 10.0);
    // Another seed wiggles differently.
    const Delta other = one(engine, {engine.defaults("wiggle", 8)}, still(30));
    CHECK(other.x != a.x);

    // Oscillate a quarter of the way through a one-second swing: the top.
    Applied swing = engine.defaults("oscillate", 1);
    swing.params["amount"] = "20";
    CHECK(near(one(engine, {swing}, still(6)).y, 20.0, 1e-6));
    // Fade in over 12 frames from the start of its keyframes.
    const Delta fading = one(engine, {engine.defaults("fade", 1)}, still(15, 10, 100));
    CHECK(near(fading.alpha, 6.0 / 12.0));
    // Turned off, it does nothing.
    Applied off = swing;
    off.enabled = false;
    CHECK(one(engine, {off}, still(6)).none());
}

TEST(behaviors_follow_lags_behind_the_keyed_motion)
{
    Engine engine;
    engine.addBuiltIns();
    // Keyed to move 10 pixels a frame to the right.
    Moment moment = still(20);
    moment.keyed.x = 200;
    moment.keyedAt = [](int frame, Keyed& keyed) {
        keyed = Keyed();
        keyed.x = frame * 10.0;
        return frame >= 0;
    };
    Applied follow = engine.defaults("follow", 1);
    follow.params["bounce"] = "0";
    const Delta delta = one(engine, {follow}, moment);
    CHECK(delta.x < -1.0 && delta.x > -200.0);
    // Standing still from the start, nothing to trail.
    Moment resting = still(20);
    resting.keyedAt = [](int, Keyed& keyed) {
        keyed = Keyed();
        return true;
    };
    CHECK(near(one(engine, {follow}, resting).x, 0.0, 1e-9));
}

TEST(behaviors_of_ones_own_report_errors_and_stop_when_too_slow)
{
    Engine engine;
    std::string error;
    CHECK(engine.add("drift", "behavior({ name: 'Drift', params: { speed: { value: 3 } }, "
                              "evaluate: function (ctx, p) { return { x: ctx.frame * p.speed }; } });",
        "drift.js", false, error));
    Applied drift = engine.defaults("drift", 1);
    CHECK(near(one(engine, {drift}, still(4)).x, 12.0));
    CHECK(!engine.add("broken", "behavior({ name: 'Broken' });", "broken.js", false, error) && !error.empty());
    CHECK(!engine.add("syntax", "behavior({", "syntax.js", false, error) && !error.empty());

    CHECK(engine.add("thrower", "behavior({ evaluate: function () { throw new Error('nope'); } });", "t.js", false, error));
    std::string why;
    const Delta thrown = one(engine, {drift, engine.defaults("thrower", 1)}, still(4), &why);
    CHECK(near(thrown.x, 12.0) && why.find("nope") != std::string::npos);

    CHECK(engine.add("forever", "behavior({ evaluate: function () { for (;;) {} } });", "f.js", false, error));
    why.clear();
    const Delta stuck = one(engine, {engine.defaults("forever", 1)}, still(1), &why);
    CHECK(stuck.none() && why.find("too long") != std::string::npos);
}

TEST(behaviors_settings_can_be_keyed)
{
    Applied applied;
    applied.type = "circle";
    applied.params["radius"] = "10";
    applied.params["align"] = "true";
    CHECK(!applied.keyed("radius") && applied.valueAt("radius", 5, "0") == "10");
    applied.setKey("radius", 24, "50");
    applied.setKey("radius", 0, "10");
    CHECK(applied.keyed("radius") && applied.keys("radius").size() == 2);
    // In a straight line between keys, held past them.
    CHECK(applied.valueAt("radius", 12, "0") == "30");
    CHECK(applied.valueAt("radius", -5, "0") == "10" && applied.valueAt("radius", 99, "0") == "50");
    // Text holds until the next key.
    applied.setKey("align", 0, "true");
    applied.setKey("align", 10, "false");
    CHECK(applied.valueAt("align", 9, "") == "true" && applied.valueAt("align", 10, "") == "false");
    // Kept through the attribute's text.
    CHECK(decode(encode({applied})).front() == applied);
    applied.removeKey("radius", 24, "10");
    applied.removeKey("radius", 0, "10");
    CHECK(!applied.keyed("radius") && applied.params["radius"] == "10");
}

TEST(behaviors_repeat_into_copies_that_later_behaviors_change)
{
    Engine engine;
    engine.addBuiltIns();
    Applied circle = engine.defaults("circle", 1);
    circle.params["count"] = "4";
    circle.params["radius"] = "100";
    const Copies ring = engine.evaluate({circle}, still(0));
    CHECK(ring.size() == 4);
    if (ring.size() == 4)
    {
        CHECK(near(ring[0].x, 100.0, 1e-9) && near(ring[1].y, 100.0, 1e-9) && near(ring[2].x, -100.0, 1e-9));
        CHECK(near(ring[1].rotation, 90.0) && near(ring[3].rotation, 270.0));
    }
    // Stagger runs each copy's loop on further.
    Applied stagger = engine.defaults("stagger", 1);
    stagger.params["frames"] = "3";
    const Copies staggered = engine.evaluate({circle, stagger}, still(0));
    CHECK(staggered.size() == 4 && staggered[3].frame == 9 && near(staggered[3].y, -100.0, 1e-9));
    // A wiggle after the ring wiggles each copy its own way.
    const Copies wiggled = engine.evaluate({circle, engine.defaults("wiggle", 3)}, still(10));
    CHECK(wiggled.size() == 4 && wiggled[0].x - 100.0 != wiggled[2].x + 100.0);
    // A keyed radius grows the ring over time.
    circle.setKey("radius", 0, "0");
    circle.setKey("radius", 10, "100");
    CHECK(near(engine.evaluate({circle}, still(5)).front().x, 50.0, 1e-9));
    // A grid of 3 by 2.
    Applied grid = engine.defaults("grid", 1);
    grid.params["columns"] = "3";
    grid.params["rows"] = "2";
    const Copies cells = engine.evaluate({grid}, still(0));
    CHECK(cells.size() == 6 && near(cells[5].x, 120.0) && near(cells[5].y, 60.0));
}

TEST(behaviors_bake_into_keyframes_with_copies_of_their_own)
{
    Engine engine;
    engine.addBuiltIns();
    fla::Timeline timeline(nullptr);
    auto* layer = new fla::Layer(&timeline);
    timeline.layers.push_back(layer);
    auto* frame = new fla::Frame(layer);
    frame->index = 0;
    frame->duration = 3;
    layer->frames.push_back(frame);
    auto* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = "Dot";
    instance->transform = fla::Transform(1, 0, 0, 1, 50, 50);
    Applied circle = engine.defaults("circle", 1);
    circle.params["count"] = "2";
    circle.params["align"] = "false";
    circle.setKey("radius", 0, "10");
    circle.setKey("radius", 2, "30");
    instance->extraAttributes = withBehaviors(*instance, {circle});
    frame->elements.push_back(instance);
    // A plain instance beside it is left as it is.
    auto* plain = new fla::SymbolInstance(frame);
    plain->libraryItemName = "Other";
    frame->elements.push_back(plain);

    CHECK(bake(engine, *layer, 0, 2, 24.0) == 3);
    CHECK(layer->frames.size() == 3);
    if (layer->frames.size() != 3)
        return;
    for (int f = 0; f < 3; ++f)
    {
        const fla::Frame* keyframe = layer->frames[static_cast<size_t>(f)];
        CHECK(keyframe->index == f && keyframe->elements.size() == 3);
        if (keyframe->elements.size() != 3)
            continue;
        const auto* first = static_cast<const fla::SymbolInstance*>(keyframe->elements[0]);
        const auto* second = static_cast<const fla::SymbolInstance*>(keyframe->elements[1]);
        const double radius = 10.0 + 10.0 * f;
        CHECK(near(first->transform.tx, 50.0 + radius, 1e-9) && near(second->transform.tx, 50.0 - radius, 1e-9));
        CHECK(!any(*first) && !any(*second) && second->libraryItemName == "Dot");
        CHECK(static_cast<const fla::SymbolInstance*>(keyframe->elements[2])->libraryItemName == "Other");
    }
    // Nothing left to bake.
    CHECK(bake(engine, *layer, 0, 2, 24.0) == 0);
}

TEST(behaviors_repeat_and_follow_along_a_motion_guide)
{
    Engine engine;
    engine.addBuiltIns();
    fla::GuidePath path;
    path.build(std::vector<std::vector<fla::Point>>{{fla::Point(0, 0), fla::Point(100, 0), fla::Point(100, 100)}});
    Moment moment = still(0);
    moment.keyed.x = 10;
    moment.keyed.y = 10;
    moment.path = &path;

    // Three copies from end to end of a 200-pixel path, not wrapped.
    Applied repeat = engine.defaults("pathrepeat", 1);
    repeat.params["count"] = "3";
    repeat.params["wrap"] = "false";
    const Copies along = engine.evaluate({repeat}, moment);
    CHECK(along.size() == 3);
    if (along.size() == 3)
    {
        CHECK(near(along[0].x, -10.0) && near(along[0].y, -10.0) && near(along[0].rotation, 0.0));
        CHECK(near(along[1].x, 90.0) && near(along[1].y, -10.0));
        CHECK(near(along[2].x, 90.0) && near(along[2].y, 90.0) && near(along[2].rotation, 90.0));
    }
    // Slid along off the end, unwrapped: those copies go.
    repeat.params["offset"] = "60";
    CHECK(engine.evaluate({repeat}, moment).size() == 1);
    // Wrapped, they come round to the start.
    repeat.params["wrap"] = "true";
    CHECK(engine.evaluate({repeat}, moment).size() == 3);

    // Follow Path, keyed from start to end over 10 frames.
    Applied follow = engine.defaults("followpath", 1);
    follow.setKey("progress", 0, "0");
    follow.setKey("progress", 10, "100");
    Moment halfway = moment;
    halfway.frame = 5;
    const Delta middle = engine.evaluate({follow}, halfway).front();
    CHECK(near(middle.x, 90.0) && near(middle.y, -10.0));

    // Without a guide, it says so and changes nothing.
    std::string why;
    const Copies none = engine.evaluate({follow}, still(5), &why);
    CHECK(none.size() == 1 && none.front().none() && why.find("motion guide") != std::string::npos);
}

TEST(behaviors_follow_sound_by_its_loudness)
{
    Engine engine;
    engine.addBuiltIns();
    // A beat on frame 10, silence elsewhere.
    const auto beat = [](int frame) { return frame == 10 ? 1.0 : 0.0; };
    Applied sound = engine.defaults("sound", 1);
    sound.params["quiet"] = "0";
    sound.params["smoothing"] = "2";
    Moment moment = still(10);
    moment.loudness = beat;
    CHECK(near(engine.evaluate({sound}, moment).front().scaleX, 1.3, 1e-9));
    // It falls away after the beat, and is still before it.
    moment = still(11);
    moment.loudness = beat;
    const double after = engine.evaluate({sound}, moment).front().scaleX;
    CHECK(after > 1.0 && after < 1.3);
    moment = still(9);
    moment.loudness = beat;
    CHECK(near(engine.evaluate({sound}, moment).front().scaleX, 1.0, 1e-9));
    // As a jump, delayed a frame; with nothing to hear, nothing moves.
    sound.params["change"] = "Move Up";
    sound.params["delay"] = "1";
    moment = still(11);
    moment.loudness = beat;
    CHECK(near(engine.evaluate({sound}, moment).front().y, -30.0, 1e-9));
    CHECK(engine.evaluate({sound}, still(11)).front().none());
}

TEST(behaviors_look_at_and_link_to_other_instances_and_controllers)
{
    Engine engine;
    engine.addBuiltIns();
    Moment moment = still(10, 0, 20);
    moment.keyed.x = 0;
    moment.keyed.y = 0;
    // "ball" starts at (100, 0) and is at (0, 100) on frame 10.
    moment.other = [](const std::string& name, int frame, Keyed& keyed) {
        if (name != "ball")
            return false;
        keyed = Keyed();
        keyed.x = frame == 0 ? 100 : 0;
        keyed.y = frame == 0 ? 0 : 100;
        keyed.rotation = frame == 0 ? 0 : 45;
        return true;
    };
    moment.controller = [](const std::string& name, double& value) {
        value = 0.25;
        return name == "Smile";
    };
    Applied look = engine.defaults("lookat", 1);
    look.params["target"] = "ball";
    CHECK(near(engine.evaluate({look}, moment).front().rotation, 90.0, 1e-9));
    look.params["target"] = "nobody";
    CHECK(engine.evaluate({look}, moment).front().none());

    Applied link = engine.defaults("linkobject", 1);
    link.params["target"] = "ball";
    const Delta moved = engine.evaluate({link}, moment).front();
    CHECK(near(moved.x, -100.0) && near(moved.y, 100.0));
    link.params["what"] = "Turn";
    link.params["amount"] = "50";
    CHECK(near(engine.evaluate({link}, moment).front().rotation, 22.5));

    Applied slider = engine.defaults("linkcontroller", 1);
    slider.params["controller"] = "Smile";
    slider.params["what"] = "Turn";
    slider.params["from"] = "0";
    slider.params["to"] = "40";
    CHECK(near(engine.evaluate({slider}, moment).front().rotation, 10.0));

    // An effector grows the copies near its point and leaves the far ones.
    Applied line = engine.defaults("grid", 1);
    line.params["columns"] = "5";
    line.params["across"] = "100";
    Applied effector = engine.defaults("effector", 1);
    effector.params["size"] = "150";
    effector.params["grow"] = "100";
    effector.params["moveY"] = "0";
    const Copies grown = engine.evaluate({line, effector}, still(0));
    CHECK(grown.size() == 5 && near(grown[0].scaleX, 2.0) && near(grown[4].scaleX, 1.0) && grown[1].scaleX > 1.0 &&
          grown[1].scaleX < 2.0);
}

TEST(behaviors_find_other_instances_on_the_timeline_by_name)
{
    Engine engine;
    engine.addBuiltIns();
    fla::Timeline timeline(nullptr);
    const auto place = [&timeline](const char* name, double x, double y) {
        auto* layer = new fla::Layer(&timeline);
        timeline.layers.push_back(layer);
        auto* frame = new fla::Frame(layer);
        frame->duration = 10;
        layer->frames.push_back(frame);
        auto* instance = new fla::SymbolInstance(frame);
        instance->libraryItemName = "Thing";
        instance->name = name;
        instance->transform = fla::Transform(1, 0, 0, 1, x, y);
        frame->elements.push_back(instance);
        return instance;
    };
    fla::SymbolInstance* eye = place("eye", 0, 0);
    fla::SymbolInstance* ball = place("ball", 0, 50);
    Applied look = engine.defaults("lookat", 1);
    look.params["target"] = "ball";
    eye->extraAttributes = withBehaviors(*eye, {look});
    CHECK(near(evaluateOn(engine, *eye, eye->transform, 3, 24.0).front().rotation, 90.0, 1e-9));
    // The ball's own behavior moves where the eye looks.
    Applied drift = engine.defaults("oscillate", 1);
    drift.params["motion"] = "Side to side";
    drift.params["amount"] = "50";
    ball->extraAttributes = withBehaviors(*ball, {drift});
    CHECK(near(evaluateOn(engine, *eye, eye->transform, 6, 24.0).front().rotation, 45.0, 1e-6));
    // Two that look at each other do not go round for ever.
    Applied back = engine.defaults("lookat", 1);
    back.params["target"] = "eye";
    ball->extraAttributes = withBehaviors(*ball, {back});
    const Copies settled = evaluateOn(engine, *eye, eye->transform, 3, 24.0);
    CHECK(settled.size() == 1);
}

TEST(behaviors_particles_are_born_live_and_die_by_the_clock)
{
    Engine engine;
    engine.addBuiltIns();
    Applied particles = engine.defaults("particles", 5);
    particles.params["rate"] = "10";
    particles.params["life"] = "1";
    // Three seconds in, ten a second living a second: ten alive.
    const Copies alive = engine.evaluate({particles}, still(72));
    CHECK(alive.size() == 10);
    // The same frame, the same particles.
    const Copies again = engine.evaluate({particles}, still(72));
    CHECK(again.size() == alive.size() && again.back().x == alive.back().x && again.back().y == alive.back().y);
    // The newest is just born where it is; the oldest has fallen and faded.
    CHECK(near(alive.back().x, 0.0, 1e-9) && alive.front().alpha < alive.back().alpha);
    // Stopped after half a second: none left at three seconds.
    particles.params["emitFor"] = "0.5";
    CHECK(engine.evaluate({particles}, still(72)).empty());
    CHECK(!engine.evaluate({particles}, still(12)).empty());
}

TEST(shape_operators_trim_wiggle_and_zigzag_lines)
{
    using fla::shapeops::Line;
    // A line 100 pixels across, and a square outline.
    Line straight;
    straight.points = {fla::Point(0, 0), fla::Point(100, 0)};
    Line square;
    square.closed = true;
    square.points = {fla::Point(0, 0), fla::Point(10, 0), fla::Point(10, 10), fla::Point(0, 10)};

    Applied trim = fla::shapeops::defaults("trim", 1);
    trim.setKey("end", 0, "0");
    trim.setKey("end", 10, "100");
    // Half drawn on at frame 5.
    std::vector<Line> half = fla::shapeops::apply({straight}, true, {trim}, 5, 24.0);
    CHECK(half.size() == 1 && near(half[0].points.back().x, 50.0, 1e-9));
    // Nothing drawn at frame 0, all at frame 10.
    CHECK(fla::shapeops::apply({straight}, true, {trim}, 0, 24.0).empty());
    CHECK(fla::shapeops::apply({straight}, true, {trim}, 10, 24.0).front().points.back().x == 100.0);
    // Fills are not trimmed.
    CHECK(fla::shapeops::apply({square}, false, {trim}, 5, 24.0).size() == 1);
    // A stretch slid round past the end comes back at the start.
    Applied window = fla::shapeops::defaults("trim", 1);
    window.params["start"] = "0";
    window.params["end"] = "20";
    window.params["offset"] = "90";
    const std::vector<Line> round = fla::shapeops::apply({straight}, true, {window}, 0, 24.0);
    CHECK(round.size() == 2 && near(round[0].points.front().x, 90.0, 1e-9) && near(round[1].points.back().x, 10.0, 1e-9));

    // Wiggle moves points, the same each time, differently on another frame.
    Applied wiggle = fla::shapeops::defaults("wigglepath", 3);
    wiggle.params["amount"] = "5";
    const std::vector<Line> a = fla::shapeops::apply({straight}, true, {wiggle}, 3, 24.0);
    const std::vector<Line> b = fla::shapeops::apply({straight}, true, {wiggle}, 3, 24.0);
    const std::vector<Line> c = fla::shapeops::apply({straight}, true, {wiggle}, 9, 24.0);
    CHECK(a.size() == 1 && a[0].points.size() > 10 && a[0].points[5].y == b[0].points[5].y);
    CHECK(a[0].points[5].y != c[0].points[5].y && std::fabs(a[0].points[5].y) <= 5.0);
    // Zig-zag teeth alternate either side.
    Applied zigzag = fla::shapeops::defaults("zigzag", 1);
    const std::vector<Line> teeth = fla::shapeops::apply({straight}, true, {zigzag}, 0, 24.0);
    CHECK(teeth.size() == 1 && teeth[0].points.size() > 4 && teeth[0].points[0].y * teeth[0].points[1].y < 0);
}

TEST(compositing_mattes_and_adjustments)
{
    namespace comp = fla::compositing;
    fla::Layer layer(nullptr);
    layer.extraAttributes = comp::withMatte(layer, comp::Matte::LumaInverted);
    CHECK(comp::matteOf(layer) == comp::Matte::LumaInverted);
    layer.extraAttributes = comp::withMatte(layer, comp::Matte::None);
    CHECK(comp::matteOf(layer) == comp::Matte::None && layer.extraAttributes.empty());
    // A new adjustment layer starts with a blur; turned off, its effects stay.
    layer.extraAttributes = comp::withAdjustment(layer, true);
    CHECK(comp::isAdjustment(layer) && comp::effectsOf(layer).size() == 1 && comp::effectsOf(layer)[0].type == "blur");
    layer.extraAttributes = comp::withAdjustment(layer, false);
    CHECK(!comp::isAdjustment(layer) && comp::effectsOf(layer).size() == 1);

    // Matte strength: alpha, inverted, and luma of premultiplied white at half.
    const uint32_t matte[3] = {0xFF000000u, 0x00000000u, 0x80808080u};
    std::vector<uint8_t> alpha = comp::matteStrength(matte, 3, 1, comp::Matte::Alpha);
    std::vector<uint8_t> inverted = comp::matteStrength(matte, 3, 1, comp::Matte::AlphaInverted);
    std::vector<uint8_t> luma = comp::matteStrength(matte, 3, 1, comp::Matte::Luma);
    CHECK(alpha[0] == 255 && alpha[1] == 0 && inverted[1] == 255 && luma[0] == 0 && luma[2] == 128);

    // Color: full saturation down to gray; tint keeps the alpha.
    Applied gray = comp::defaults("color", 1);
    gray.params["saturation"] = "-100";
    uint32_t red = 0xFFFF0000u;
    comp::apply(&red, 1, 1, {gray}, 0, 1.0);
    const unsigned r = (red >> 16) & 0xFF, g = (red >> 8) & 0xFF, b = red & 0xFF;
    CHECK(r == g && g == b && (red >> 24) == 0xFF);
    uint32_t half = 0x80808080u;
    comp::apply(&half, 1, 1, {comp::defaults("tint", 1)}, 0, 1.0);
    CHECK((half >> 24) == 0x80);
    // Blur spreads a dot; with no coverage there, nothing changes.
    std::vector<uint32_t> dot(15 * 15, 0);
    dot[7 * 15 + 7] = 0xFFFFFFFFu;
    std::vector<uint32_t> blurred = dot;
    comp::apply(blurred.data(), 15, 15, {comp::defaults("blur", 1)}, 0, 1.0);
    CHECK(blurred[7 * 15 + 8] != 0 && blurred[7 * 15 + 7] != 0xFFFFFFFFu);
    std::vector<uint8_t> none(15 * 15, 0);
    std::vector<uint32_t> covered = dot;
    comp::apply(covered.data(), 15, 15, {comp::defaults("blur", 1)}, 0, 1.0, none.data());
    CHECK(covered == dot);
}

TEST(text_animators_change_letters_one_by_one)
{
    using fla::textanim::Change;
    using fla::textanim::Letter;
    // "ab cd": five characters, two words.
    std::vector<Letter> letters;
    const std::string text = "ab cd";
    for (int i = 0; i < 5; ++i)
    {
        Letter letter;
        letter.index = i;
        letter.word = i < 3 ? 0 : 1;
        letter.count = 5;
        letter.words = 2;
        letter.character = static_cast<unsigned char>(text[static_cast<size_t>(i)]);
        letters.push_back(letter);
    }
    // Typewriter at 24 letters a second: the first at once, then one a
    // frame, so three shown two frames on.
    Applied type = fla::textanim::defaults("typewriter", 1);
    type.params["rate"] = "24";
    std::vector<Change> typed = fla::textanim::apply({type}, letters, 12, 10, 24.0);
    CHECK(typed[0].alpha == 1.0 && typed[2].alpha == 1.0 && typed[3].alpha == 0.0);
    // Fade by word: the second word later than the first.
    std::vector<Change> words = fla::textanim::apply({fla::textanim::defaults("fadeword", 1)}, letters, 6, 0, 24.0);
    CHECK(words[0].alpha > 0.0 && words[4].alpha == 0.0);
    // A range over the first half lifts those letters only.
    Applied range = fla::textanim::defaults("range", 1);
    range.params["end"] = "50";
    std::vector<Change> lifted = fla::textanim::apply({range}, letters, 0, 0, 24.0);
    CHECK(near(lifted[0].y, -20.0) && near(lifted[1].y, -20.0) && near(lifted[4].y, 0.0));
    // Scramble swaps letters not yet settled, never spaces, the same each time.
    std::vector<Change> scrambled = fla::textanim::apply({fla::textanim::defaults("scramble", 2)}, letters, 0, 0, 24.0);
    std::vector<Change> again = fla::textanim::apply({fla::textanim::defaults("scramble", 2)}, letters, 0, 0, 24.0);
    CHECK(scrambled[1].character != 0 && scrambled[2].character == 0 && scrambled[1].character == again[1].character);
    // Bounce: nothing yet before its time, full size long after.
    std::vector<Change> early = fla::textanim::apply({fla::textanim::defaults("bounce", 1)}, letters, 0, 0, 24.0);
    std::vector<Change> late = fla::textanim::apply({fla::textanim::defaults("bounce", 1)}, letters, 100, 0, 24.0);
    CHECK(early[4].scale == 0.0 && near(late[4].scale, 1.0, 1e-6));
}
