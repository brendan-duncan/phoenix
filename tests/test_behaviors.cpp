#include "test_util.h"

#include "../src/data/symbol_instance.h"
#include "../src/edit/behaviors.h"
#include "../src/jsfl/behavior_engine.h"

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
    const Delta a = engine.evaluate({applied}, still(30));
    const Delta b = engine.evaluate({applied}, still(30));
    const Delta c = engine.evaluate({applied}, still(31));
    CHECK(a.x == b.x && a.y == b.y && (a.x != c.x || a.y != c.y));
    CHECK(std::fabs(a.x) <= 10.0 && std::fabs(a.y) <= 10.0);
    // Another seed wiggles differently.
    const Delta other = engine.evaluate({engine.defaults("wiggle", 8)}, still(30));
    CHECK(other.x != a.x);

    // Oscillate a quarter of the way through a one-second swing: the top.
    Applied swing = engine.defaults("oscillate", 1);
    swing.params["amount"] = "20";
    CHECK(near(engine.evaluate({swing}, still(6)).y, 20.0, 1e-6));
    // Fade in over 12 frames from the start of its keyframes.
    const Delta fading = engine.evaluate({engine.defaults("fade", 1)}, still(15, 10, 100));
    CHECK(near(fading.alpha, 6.0 / 12.0));
    // Turned off, it does nothing.
    Applied off = swing;
    off.enabled = false;
    CHECK(engine.evaluate({off}, still(6)).none());
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
    const Delta delta = engine.evaluate({follow}, moment);
    CHECK(delta.x < -1.0 && delta.x > -200.0);
    // Standing still from the start, nothing to trail.
    Moment resting = still(20);
    resting.keyedAt = [](int, Keyed& keyed) {
        keyed = Keyed();
        return true;
    };
    CHECK(near(engine.evaluate({follow}, resting).x, 0.0, 1e-9));
}

TEST(behaviors_of_ones_own_report_errors_and_stop_when_too_slow)
{
    Engine engine;
    std::string error;
    CHECK(engine.add("drift", "behavior({ name: 'Drift', params: { speed: { value: 3 } }, "
                              "evaluate: function (ctx, p) { return { x: ctx.frame * p.speed }; } });",
        "drift.js", false, error));
    Applied drift = engine.defaults("drift", 1);
    CHECK(near(engine.evaluate({drift}, still(4)).x, 12.0));
    CHECK(!engine.add("broken", "behavior({ name: 'Broken' });", "broken.js", false, error) && !error.empty());
    CHECK(!engine.add("syntax", "behavior({", "syntax.js", false, error) && !error.empty());

    CHECK(engine.add("thrower", "behavior({ evaluate: function () { throw new Error('nope'); } });", "t.js", false, error));
    std::string why;
    const Delta thrown = engine.evaluate({drift, engine.defaults("thrower", 1)}, still(4), &why);
    CHECK(near(thrown.x, 12.0) && why.find("nope") != std::string::npos);

    CHECK(engine.add("forever", "behavior({ evaluate: function () { for (;;) {} } });", "f.js", false, error));
    why.clear();
    const Delta stuck = engine.evaluate({engine.defaults("forever", 1)}, still(1), &why);
    CHECK(stuck.none() && why.find("too long") != std::string::npos);
}
