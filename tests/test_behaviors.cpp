#include "test_util.h"

#include "../src/data/symbol_instance.h"
#include "../src/edit/behaviors.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/jsfl/behavior_engine.h"
#include "../src/jsfl/behavior_frames.h"
#include "../src/edit/guide_path.h"

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
