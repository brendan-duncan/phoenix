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
#include "../src/edit/templates.h"
#include "../src/edit/motion_blur.h"
#include "../src/data/document.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/bitmap_instance.h"

#include <cmath>
#include <map>
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

TEST(behaviors_bake_keeps_a_keyframe_only_where_something_changes)
{
    Engine engine;
    engine.addBuiltIns();
    fla::Timeline timeline(nullptr);
    auto* layer = new fla::Layer(&timeline);
    timeline.layers.push_back(layer);
    auto* frame = new fla::Frame(layer);
    frame->duration = 10;
    layer->frames.push_back(frame);
    auto* instance = new fla::SymbolInstance(frame);
    instance->libraryItemName = "Dot";
    instance->symbolType = fla::SymbolType::MovieClip;
    // Fades in over two frames, then holds.
    Applied fade = engine.defaults("fade", 1);
    fade.params["fadeIn"] = "2";
    fade.params["fadeOut"] = "0";
    instance->extraAttributes = withBehaviors(*instance, {fade});
    frame->elements.push_back(instance);

    CHECK(bake(engine, *layer, 0, 9, 24.0) == 10);
    CHECK(layer->frames.size() == 2);
    if (layer->frames.size() != 2)
        return;
    CHECK(layer->frames[0]->index == 0 && layer->frames[0]->duration == 1);
    CHECK(layer->frames[1]->index == 1 && layer->frames[1]->duration == 9);
    const auto* half = static_cast<const fla::SymbolInstance*>(layer->frames[0]->elements[0]);
    const auto* full = static_cast<const fla::SymbolInstance*>(layer->frames[1]->elements[0]);
    CHECK(near(half->colorTransform.alphaMultiplier, 0.5, 1e-9) && near(full->colorTransform.alphaMultiplier, 1.0, 1e-9));

    // A graphic that plays on is held only where its loop would be anyway.
    fla::Timeline cycle(nullptr);
    auto* steps = new fla::Layer(&cycle);
    cycle.layers.push_back(steps);
    auto* key = new fla::Frame(steps);
    key->duration = 4;
    steps->frames.push_back(key);
    auto* graphic = new fla::SymbolInstance(key);
    graphic->libraryItemName = "Walk";
    graphic->symbolType = fla::SymbolType::Graphic;
    graphic->loopType = fla::LoopType::Loop;
    Applied still = engine.defaults("fade", 1);
    still.params["fadeIn"] = "0";
    still.params["fadeOut"] = "0";
    graphic->extraAttributes = withBehaviors(*graphic, {still});
    key->elements.push_back(graphic);
    CHECK(bake(engine, *steps, 0, 3, 24.0) == 4);
    CHECK(steps->frames.size() == 1 && steps->frames[0]->duration == 4);
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
    // Its moving down turns this one, plus ten degrees, kept within limits.
    link.params["what"] = "Move down";
    link.params["into"] = "Turn";
    link.params["amount"] = "100";
    link.params["offset"] = "10";
    CHECK(near(engine.evaluate({link}, moment).front().rotation, 110.0));
    link.params["limit"] = "true";
    link.params["low"] = "-30";
    link.params["high"] = "30";
    CHECK(near(engine.evaluate({link}, moment).front().rotation, 30.0));
    // Lagging back to where it started: no change but the offset.
    link.params["limit"] = "false";
    link.params["lag"] = "10";
    CHECK(near(engine.evaluate({link}, moment).front().rotation, 10.0));

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

TEST(shape_operators_offset_round_bloat_and_repeat)
{
    using fla::shapeops::Line;
    Line square;
    square.closed = true;
    square.points = {fla::Point(0, 0), fla::Point(100, 0), fla::Point(100, 100), fla::Point(0, 100)};
    const auto bounds = [](const std::vector<Line>& lines, double& minX, double& maxX) {
        minX = 1e9;
        maxX = -1e9;
        for (const Line& line : lines)
            for (const fla::Point& point : line.points)
            {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
            }
    };
    double minX = 0, maxX = 0;
    // Offset grows it on every side, either way round it runs.
    Applied grow = fla::shapeops::defaults("offset", 1);
    grow.params["amount"] = "10";
    bounds(fla::shapeops::apply({square}, false, {grow}, 0, 24.0), minX, maxX);
    CHECK(near(minX, -10.0, 1e-6) && near(maxX, 110.0, 1e-6));
    Line reversed = square;
    std::reverse(reversed.points.begin(), reversed.points.end());
    bounds(fla::shapeops::apply({reversed}, false, {grow}, 0, 24.0), minX, maxX);
    CHECK(near(minX, -10.0, 1e-6) && near(maxX, 110.0, 1e-6));
    // A hole inside it shrinks as the outline grows.
    Line hole;
    hole.closed = true;
    hole.points = {fla::Point(40, 40), fla::Point(60, 40), fla::Point(60, 60), fla::Point(40, 60)};
    const std::vector<Line> both = fla::shapeops::apply({square, hole}, false, {grow}, 0, 24.0);
    bounds({both[1]}, minX, maxX);
    CHECK(near(minX, 50.0, 1e-6) && near(maxX, 50.0, 1e-6));

    // Round corners: no point left at a corner, the sides kept straight.
    Applied round = fla::shapeops::defaults("round", 1);
    round.params["radius"] = "20";
    const std::vector<Line> rounded = fla::shapeops::apply({square}, false, {round}, 0, 24.0);
    bool cornerLeft = false;
    for (const fla::Point& point : rounded[0].points)
        cornerLeft = cornerLeft || (near(point.x, 0.0, 1e-9) && near(point.y, 0.0, 1e-9));
    CHECK(!cornerLeft && rounded[0].points.size() == 36);

    // Bloat pushes the middle of each side out; pucker draws it in.
    Applied bloat = fla::shapeops::defaults("puckerbloat", 1);
    bloat.params["amount"] = "20";
    bounds(fla::shapeops::apply({square}, false, {bloat}, 0, 24.0), minX, maxX);
    CHECK(near(maxX, 110.0, 0.5) && near(minX, -10.0, 0.5));
    bloat.params["amount"] = "-20";
    bounds(fla::shapeops::apply({square}, false, {bloat}, 0, 24.0), minX, maxX);
    CHECK(near(maxX, 100.0, 1e-6));

    // Repeater: the shape and three copies, each 20 pixels on.
    const std::vector<Line> repeated = fla::shapeops::apply({square}, false, {fla::shapeops::defaults("repeater", 1)}, 0, 24.0);
    bounds(repeated, minX, maxX);
    CHECK(repeated.size() == 4 && near(maxX, 160.0, 1e-6));
}

TEST(behaviors_loop_scatter_and_jiggle)
{
    Engine engine;
    engine.addBuiltIns();
    // Keyed: x goes 0, 10, 20... a frame.
    const auto keyedAt = [](int frame, Keyed& keyed) {
        keyed = Keyed();
        keyed.x = frame * 10.0;
        return true;
    };
    Applied loop = engine.defaults("loop", 1);
    loop.params["length"] = "4";
    Moment moment = still(6);
    moment.keyed.x = 60.0;
    moment.keyedAt = keyedAt;
    // A cycle of four frames: frame 6 shows frame 2.
    CHECK(near(one(engine, {loop}, moment).x, -40.0, 1e-9));
    loop.params["mode"] = "Back and Forth";
    // 0 1 2 3 2 1 0: frame 6 shows frame 0.
    CHECK(near(one(engine, {loop}, moment).x, -60.0, 1e-9));
    loop.params["mode"] = "Continue";
    // On at ten a frame: where it is keyed anyway.
    CHECK(near(one(engine, {loop}, moment).x, 0.0, 1e-9));
    moment = still(2);
    moment.keyedAt = keyedAt;
    CHECK(one(engine, {loop}, moment).none());

    // Scatter: every copy inside the square the guide draws.
    fla::GuidePath square;
    square.build(std::vector<std::vector<fla::Point>>{
        {fla::Point(0, 0), fla::Point(100, 0), fla::Point(100, 100), fla::Point(0, 100), fla::Point(0, 0)}});
    Applied scatter = engine.defaults("scatter", 1);
    scatter.params["count"] = "25";
    scatter.params["spacing"] = "5";
    moment = still(0);
    moment.path = &square;
    const Copies scattered = engine.evaluate({scatter}, moment);
    bool allInside = scattered.size() == 25;
    for (const Delta& copy : scattered)
        allInside = allInside && copy.x >= 0.0 && copy.x <= 100.0 && copy.y >= 0.0 && copy.y <= 100.0;
    CHECK(allInside);

    // Jiggle: still when nothing moves; swings when the parent carries it.
    Applied jiggle = engine.defaults("jiggle", 1);
    moment = still(12);
    moment.keyedAt = [](int, Keyed& keyed) {
        keyed = Keyed();
        return true;
    };
    CHECK(near(one(engine, {jiggle}, moment).rotation, 0.0, 1e-9));
    moment.parentAt = [](int frame, double m[6]) {
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 1;
        m[4] = frame < 10 ? 0.0 : 200.0;
        m[5] = 0;
        return true;
    };
    // Carried to the right, it trails to the left: it leans left.
    CHECK(one(engine, {jiggle}, moment).rotation < -1.0);
}

TEST(text_animators_space_color_and_blur_letters)
{
    using fla::textanim::Letter;
    std::vector<Letter> letters(3);
    for (int i = 0; i < 3; ++i)
    {
        letters[static_cast<size_t>(i)].index = i;
        letters[static_cast<size_t>(i)].count = 3;
        letters[static_cast<size_t>(i)].words = 1;
        letters[static_cast<size_t>(i)].character = 'a';
    }
    Applied range = fla::textanim::defaults("range", 1);
    range.params["moveY"] = "0";
    range.params["tracking"] = "5";
    range.params["colorAmount"] = "50";
    range.params["color"] = "#FF0000";
    range.params["blur"] = "2";
    const std::vector<fla::textanim::Change> changes = fla::textanim::apply({range}, letters, 0, 0, 24.0);
    CHECK(near(changes[0].x, 0.0) && near(changes[1].x, 5.0) && near(changes[2].x, 10.0));
    CHECK(near(changes[1].tint, 0.5) && changes[1].tintColor == 0xFF0000u && near(changes[2].blur, 2.0));
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

TEST(templates_find_fields_and_read_csv_rows)
{
    namespace tpl = fla::templates;
    // Values survive the trip through the attribute, odd characters too.
    tpl::Values values{{"title", "Ada & Grace = 100%"}, {"bar", "#00C000"}};
    CHECK(tpl::decode(tpl::encode(values)) == values);
    fla::SymbolInstance instance(nullptr);
    instance.extraAttributes = tpl::withValues(instance, values);
    CHECK(tpl::any(instance) && tpl::valuesOf(instance) == values);
    instance.extraAttributes = tpl::withValues(instance, {});
    CHECK(!tpl::any(instance) && instance.extraAttributes.empty());

    unsigned char rgb[3];
    CHECK(tpl::colorOf("#00C0FF", rgb) && rgb[0] == 0 && rgb[1] == 0xC0 && rgb[2] == 0xFF);
    CHECK(tpl::colorOf("ff0000", rgb) && rgb[0] == 0xFF);
    CHECK(!tpl::colorOf("", rgb) && !tpl::colorOf("red", rgb));

    // A symbol with a named dynamic text, a static text and a named clip.
    fla::Symbol symbol(nullptr);
    auto* timeline = new fla::Timeline(&symbol);
    symbol.timelines.push_back(timeline);
    auto* layer = new fla::Layer(timeline);
    timeline->layers.push_back(layer);
    auto* frame = new fla::Frame(layer);
    layer->frames.push_back(frame);
    auto* name = new fla::StaticText(frame);
    name->textType = fla::StaticText::TextType::Dynamic;
    name->instanceName = "title";
    fla::TextRun run;
    run.text = "Name Here";
    name->runs.push_back(run);
    auto* plain = new fla::StaticText(frame);
    plain->runs.push_back(run);
    auto* bar = new fla::SymbolInstance(frame);
    bar->symbolType = fla::SymbolType::MovieClip;
    bar->name = "bar";
    frame->elements = {name, plain, bar};
    const std::vector<tpl::Field> fields = tpl::fieldsOf(symbol);
    CHECK(fields.size() == 2);
    CHECK(fields[0].name == "title" && fields[0].kind == tpl::Field::Kind::Text && fields[0].value == "Name Here");
    CHECK(fields[1].name == "bar" && fields[1].kind == tpl::Field::Kind::Color);

    // A clip of several frames is a slider; one holding just a bitmap is an
    // image.
    const auto clipOf = [](fla::Symbol& clip, int frames, fla::Element* (*make)(fla::Frame*)) {
        auto* inner = new fla::Timeline(&clip);
        clip.timelines.push_back(inner);
        auto* innerLayer = new fla::Layer(inner);
        inner->layers.push_back(innerLayer);
        auto* innerFrame = new fla::Frame(innerLayer);
        innerFrame->duration = frames;
        innerLayer->frames.push_back(innerFrame);
        if (make)
            innerFrame->elements.push_back(make(innerFrame));
    };
    fla::Symbol stars(nullptr);
    clipOf(stars, 5, nullptr);
    int frames = 0;
    CHECK(tpl::clipKind(stars, &frames) == tpl::Field::Kind::Frame && frames == 5);
    fla::Symbol photo(nullptr);
    clipOf(photo, 1, [](fla::Frame* at) -> fla::Element* {
        auto* bitmap = new fla::BitmapInstance(at);
        bitmap->libraryItemName = "face.png";
        return bitmap;
    });
    std::string drawn;
    CHECK(tpl::clipKind(photo, nullptr, &drawn) == tpl::Field::Kind::Image && drawn == "face.png");
    bar->symbol = &photo;
    CHECK(tpl::fieldsOf(symbol)[1].kind == tpl::Field::Kind::Image && tpl::fieldsOf(symbol)[1].value == "face.png");
    bar->symbol = nullptr;
    int held = 0;
    CHECK(tpl::frameOf("3", held) && held == 3);
    CHECK(!tpl::frameOf("0", held) && !tpl::frameOf("2.5", held) && !tpl::frameOf("#00FF00", held));

    // CSV: quotes, commas and line breaks in them, a byte order mark, an
    // empty cell left out.
    const std::vector<tpl::Values> rows =
        tpl::rowsOfCsv("\xEF\xBB\xBF" "file,title,bar\r\none,\"Hello, \"\"world\"\"\",#0000FF\r\ntwo,\"two\nlines\",\r\n\r\n");
    CHECK(rows.size() == 2);
    CHECK(rows[0].at("file") == "one" && rows[0].at("title") == "Hello, \"world\"" && rows[0].at("bar") == "#0000FF");
    CHECK(rows[1].at("title") == "two\nlines" && rows[1].count("bar") == 0);
}

TEST(compositing_displacement_moves_pixels)
{
    namespace comp = fla::compositing;
    // A white column on the left of a 40 by 20 image.
    std::vector<uint32_t> image(40 * 20, 0xFF000000u);
    for (int y = 0; y < 20; ++y)
        image[static_cast<size_t>(y) * 40 + 5] = 0xFFFFFFFFu;
    // A wave running down the image pushes the column sideways, more in some
    // rows than others, and nothing with no height.
    Applied wave = comp::defaults("wave", 1);
    wave.params["direction"] = "90";
    wave.params["wavelength"] = "20";
    wave.params["amount"] = "3";
    std::vector<uint32_t> waved = image;
    comp::apply(waved.data(), 40, 20, {wave}, 0, 1.0, nullptr, 24.0);
    int moved = 0;
    for (int y = 0; y < 20; ++y)
        moved += waved[static_cast<size_t>(y) * 40 + 5] != 0xFFFFFFFFu;
    CHECK(moved > 0 && moved < 20);
    wave.params["amount"] = "0";
    std::vector<uint32_t> still = image;
    comp::apply(still.data(), 40, 20, {wave}, 0, 1.0, nullptr, 24.0);
    CHECK(still == image);
    // Turbulence: the same frame twice gives the same picture; another
    // frame, another.
    Applied turbulent = comp::defaults("turbulent", 1);
    turbulent.params["size"] = "8";
    std::vector<uint32_t> one = image, again = image, later = image;
    comp::apply(one.data(), 40, 20, {turbulent}, 3, 1.0, nullptr, 24.0);
    comp::apply(again.data(), 40, 20, {turbulent}, 3, 1.0, nullptr, 24.0);
    comp::apply(later.data(), 40, 20, {turbulent}, 30, 1.0, nullptr, 24.0);
    CHECK(one == again && one != image && one != later);
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

TEST(motion_blur_settings_and_sample_times)
{
    namespace mb = fla::motionblur;
    // Centered on the frame: a half-frame shutter takes pictures from a
    // quarter frame before to a quarter after.
    const std::vector<double> times = mb::sampleTimes(180.0, 4);
    CHECK(times.size() == 4 && near(times.front(), -0.1875, 1e-9) && near(times.back(), 0.1875, 1e-9));
    CHECK(near(times[0] + times[3], 0.0, 1e-9));
    CHECK(mb::sampleTimes(0.0, 3) == std::vector<double>({0.0, 0.0, 0.0}));

    fla::Layer layer(nullptr);
    CHECK(!mb::on(layer));
    layer.extraAttributes = mb::withOn(layer, true);
    CHECK(mb::on(layer));
    layer.extraAttributes = mb::withOn(layer, false);
    CHECK(!mb::on(layer) && layer.extraAttributes.empty());

    fla::Document document(nullptr);
    CHECK(mb::shutter(document) == 180.0 && mb::samples(document) == 16);
    document.extraAttributes = mb::withSettings(document, 270.0, 32);
    CHECK(mb::shutter(document) == 270.0 && mb::samples(document) == 32);
    // Back to the defaults, nothing is kept.
    document.extraAttributes = mb::withSettings(document, 180.0, 16);
    CHECK(document.extraAttributes.empty());
}

TEST(time_remap_holds_the_frame_keyed)
{
    Engine engine;
    engine.addBuiltIns();
    Applied remap = engine.defaults("timeremap", 1);
    // Frames 1 to 11 of the graphic over frames 0 to 20: half speed.
    remap.params["frame"] = "@0=1;20=11";
    CHECK(engine.evaluate({remap}, still(0)).front().hold == 0);
    CHECK(engine.evaluate({remap}, still(10)).front().hold == 5);
    CHECK(engine.evaluate({remap}, still(20)).front().hold == 10);
    // Held, run backwards from 11 to 1.
    remap.params["frame"] = "@0=11;10=1";
    CHECK(engine.evaluate({remap}, still(5)).front().hold == 5);
    // With copies, every copy is held; a later behavior's loop shift does
    // not undo it.
    Applied ring = engine.defaults("circle", 1);
    remap.params["frame"] = "4";
    const Copies copies = engine.evaluate({ring, remap}, still(3));
    CHECK(copies.size() > 1 && copies.front().hold == 3 && copies.back().hold == 3);
    fla::behaviors::Delta delta;
    delta.add(copies.front());
    fla::behaviors::Delta shift;
    shift.frame = 2;
    delta.add(shift);
    CHECK(delta.hold == 3 && !delta.none());
}

TEST(recorded_motion_moves_by_the_keys_recorded)
{
    Engine engine;
    engine.addBuiltIns();
    // As Motion Sketch writes it: how far from the press, a key a frame.
    Applied recorded = engine.defaults("recorded", 1);
    recorded.params["dx"] = "@0=0.00;1=10.00;2=30.00";
    recorded.params["dy"] = "@0=0.00;1=-5.00;2=-20.00";
    const Copies at0 = engine.evaluate({recorded}, still(0));
    CHECK(near(at0.front().x, 0.0) && near(at0.front().y, 0.0));
    const Copies at2 = engine.evaluate({recorded}, still(2));
    CHECK(near(at2.front().x, 30.0) && near(at2.front().y, -20.0));
}

TEST(shape_operators_dash_and_taper_strokes)
{
    using fla::shapeops::Line;
    Line line;
    line.points = {fla::Point(0, 0), fla::Point(100, 0)};
    // Dashes of 20 with gaps of 10: four whole dashes and a short one.
    Applied dashes = fla::shapeops::defaults("dashes", 1);
    std::vector<Line> out = fla::shapeops::apply({line}, true, {dashes}, 0, 24.0);
    CHECK(out.size() == 4);
    CHECK(near(out[0].points.front().x, 0.0) && near(out[0].points.back().x, 20.0));
    CHECK(near(out[1].points.front().x, 30.0));
    // Sent 5 along: the first dash starts at 5.
    dashes.params["offset"] = "5";
    out = fla::shapeops::apply({line}, true, {dashes}, 0, 24.0);
    CHECK(near(out[0].points.front().x, 5.0) && near(out[0].points.back().x, 25.0));
    // A fill's outline is left whole.
    CHECK(fla::shapeops::apply({line}, false, {dashes}, 0, 24.0).size() == 1);

    // Taper: full width at the start, none at the end.
    Applied taper = fla::shapeops::defaults("taper", 1);
    const fla::shapeops::Taper look = fla::shapeops::taperOf({taper}, 0);
    CHECK(look.on && look.start == 100.0 && look.end == 0.0);
    const std::vector<fla::Point> outline = fla::shapeops::taperedOutline(line, 10.0, look, 0.0);
    CHECK(!outline.empty());
    double startWidth = 0.0;
    double endWidth = 0.0;
    for (const fla::Point& point : outline)
    {
        if (near(point.x, 0.0, 1e-6))
            startWidth = std::max(startWidth, std::fabs(point.y) * 2.0);
        if (near(point.x, 100.0, 1e-6))
            endWidth = std::max(endWidth, std::fabs(point.y) * 2.0);
    }
    CHECK(near(startWidth, 10.0, 1e-6) && near(endWidth, 0.0, 1e-6));
    CHECK(!fla::shapeops::taperOf({}, 0).on);
}

TEST(text_on_a_path_and_wiggly_letters)
{
    namespace ta = fla::textanim;
    // Three letters 10 apart along x, middles on y = 5.
    std::vector<ta::Letter> letters(3);
    for (int i = 0; i < 3; ++i)
    {
        letters[i].index = i;
        letters[i].count = 3;
        letters[i].middleX = 5.0 + 10.0 * i;
        letters[i].middleY = 5.0;
    }
    // A line going straight down from (100, 0), 50 long.
    ta::PathInText path;
    path.length = 50.0;
    path.at = [](double d) { return fla::Point(100.0, d); };
    path.angleAt = [](double) { return 90.0; };
    Applied onPath = ta::defaults("path", 1);
    onPath.params["margin"] = "10";
    std::vector<ta::Change> changes = ta::apply({onPath}, letters, 0, 0, 24.0, &path);
    // The first letter's middle at 10 along, the next at 20, turned down.
    CHECK(near(letters[0].middleX + changes[0].x, 100.0) && near(letters[0].middleY + changes[0].y, 10.0));
    CHECK(near(letters[1].middleY + changes[1].y, 20.0) && near(changes[1].rotation, 90.0));
    // Past the end, not shown; with Wrap, round to the start.
    onPath.params["margin"] = "40";
    changes = ta::apply({onPath}, letters, 0, 0, 24.0, &path);
    CHECK(changes[2].alpha == 0.0 && changes[0].alpha == 1.0);
    onPath.params["wrap"] = "true";
    changes = ta::apply({onPath}, letters, 0, 0, 24.0, &path);
    CHECK(changes[2].alpha == 1.0 && near(letters[2].middleY + changes[2].y, 10.0));
    // No guide: the letters stay put.
    CHECK(ta::apply({onPath}, letters, 0, 0, 24.0, nullptr)[1].none());

    // Wiggly: each letter its own way, the same on the same frame, and all
    // alike when wholly together.
    Applied wiggly = ta::defaults("wiggly", 3);
    changes = ta::apply({wiggly}, letters, 7, 0, 24.0);
    CHECK(!changes[0].none() && (changes[0].x != changes[1].x || changes[0].y != changes[1].y));
    CHECK(ta::apply({wiggly}, letters, 7, 0, 24.0)[1].x == changes[1].x);
    wiggly.params["together"] = "100";
    changes = ta::apply({wiggly}, letters, 7, 0, 24.0);
    CHECK(near(changes[0].x, changes[2].x) && near(changes[0].rotation, changes[1].rotation));
}

TEST(adjustment_effects_color_generate_distort_and_wipe)
{
    namespace comp = fla::compositing;
    // A 20 by 10 picture, a mid-gray left half and a clear right half.
    std::vector<uint32_t> image(200, 0u);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            image[static_cast<size_t>(y) * 20 + x] = 0xFF808080u;
    const auto run = [&](const char* id, std::map<std::string, std::string> params) {
        Applied effect = comp::defaults(id, 1);
        for (const auto& [name, value] : params)
            effect.params[name] = value;
        std::vector<uint32_t> out = image;
        comp::apply(out.data(), 20, 10, {effect}, 0, 1.0, nullptr, 24.0);
        return out;
    };
    // Every effect is listed, with its settings.
    CHECK(comp::find("glow") && comp::find("dissolve") && comp::find("fractalnoise") && comp::find("levels"));
    // Fill: the gray goes red, the clear stays clear.
    std::vector<uint32_t> out = run("fill", {{"color", "#FF0000"}});
    CHECK(out[0] == 0xFFFF0000u && out[15] == 0u);
    // Levels: white pulled down to the gray makes it white.
    out = run("levels", {{"inWhite", "128"}});
    CHECK(((out[0] >> 16) & 0xFF) == 255);
    // A ramp, replacing: black at the left edge, white at the right.
    out = run("ramp", {});
    CHECK((out[0] & 0xFF) < 20 && (out[19] & 0xFF) > 235 && (out[19] >> 24) == 255);
    // Wipes: nothing left at 100, all of it at 0.
    CHECK(run("linearwipe", {{"completion", "100"}})[0] == 0u);
    CHECK(run("linearwipe", {{"completion", "0"}})[0] == image[0]);
    CHECK(run("dissolve", {{"completion", "100"}})[5] == 0u);
    // Fractal noise the same twice, and changing with the seed.
    CHECK(run("fractalnoise", {}) == run("fractalnoise", {}));
    CHECK(run("fractalnoise", {{"seed", "2"}}) != run("fractalnoise", {}));
    // Displacement Map with no map: nothing moves.
    CHECK(run("displace", {{"layer", "Nowhere"}}) == image);
    CHECK(comp::effects::layersNeeded({comp::defaults("displace", 1)}, 0).empty());
}

TEST(mask_edges_and_layer_modes)
{
    namespace comp = fla::compositing;
    fla::Layer layer(nullptr);
    CHECK(comp::maskEdgeOf(layer).plain() && comp::layerModeOf(layer) == comp::LayerMode::Normal);
    comp::MaskEdge edge;
    edge.feather = 12.0;
    edge.expand = -3.0;
    edge.invert = true;
    layer.extraAttributes = comp::withMaskEdge(layer, edge);
    const comp::MaskEdge read = comp::maskEdgeOf(layer);
    CHECK(read.feather == 12.0 && read.expand == -3.0 && read.invert);
    layer.extraAttributes = comp::withMaskEdge(layer, comp::MaskEdge());
    CHECK(layer.extraAttributes.empty());
    layer.extraAttributes = comp::withLayerMode(layer, comp::LayerMode::Silhouette);
    CHECK(comp::layerModeOf(layer) == comp::LayerMode::Silhouette);

    // A 21 by 21 square mask, 7 wide in the middle.
    std::vector<uint8_t> mask(21 * 21, 0);
    for (int y = 7; y < 14; ++y)
        for (int x = 7; x < 14; ++x)
            mask[static_cast<size_t>(y) * 21 + x] = 255;
    // Grown by 2: covers from 5 to 15 along the middle row.
    std::vector<uint8_t> grown = mask;
    comp::MaskEdge grow;
    grow.expand = 2.0;
    comp::shapeMask(grown, 21, 21, grow, 1.0);
    CHECK(grown[10 * 21 + 5] == 255 && grown[10 * 21 + 4] == 0 && grown[10 * 21 + 15] == 255);
    // Shrunk by 2: from 9 to 11.
    std::vector<uint8_t> shrunk = mask;
    comp::MaskEdge shrink;
    shrink.expand = -2.0;
    comp::shapeMask(shrunk, 21, 21, shrink, 1.0);
    CHECK(shrunk[10 * 21 + 9] == 255 && shrunk[10 * 21 + 8] == 0);
    // Feathered: the edge between in and out; inverted, the outside in.
    std::vector<uint8_t> soft = mask;
    comp::MaskEdge feather;
    feather.feather = 4.0;
    comp::shapeMask(soft, 21, 21, feather, 1.0);
    CHECK(soft[10 * 21 + 7] > 0 && soft[10 * 21 + 7] < 255 && soft[10 * 21 + 10] > soft[10 * 21 + 7] &&
        soft[10 * 21 + 5] < soft[10 * 21 + 7]);
    std::vector<uint8_t> inside = mask;
    comp::MaskEdge flip;
    flip.invert = true;
    comp::shapeMask(inside, 21, 21, flip, 1.0);
    CHECK(inside[0] == 255 && inside[10 * 21 + 10] == 0);
}

TEST(shape_operators_twist_and_merge)
{
    using fla::shapeops::Line;
    Line square;
    square.closed = true;
    square.points = {fla::Point(0, 0), fla::Point(100, 0), fla::Point(100, 100), fla::Point(0, 100)};
    Applied twist = fla::shapeops::defaults("twist", 1);
    const std::vector<Line> out = fla::shapeops::apply({square}, false, {twist}, 0, 24.0);
    CHECK(out.size() == 1 && out.front().points.size() > 4);
    // The corners, at the far edge, stay put; points nearer the middle turn.
    bool corner = false;
    for (const fla::Point& point : out.front().points)
        corner = corner || (near(point.x, 100.0, 1e-6) && near(point.y, 100.0, 1e-6));
    CHECK(corner);
    CHECK(fla::shapeops::mergeOf({}, 0).empty());
    Applied merge = fla::shapeops::defaults("merge", 1);
    CHECK(fla::shapeops::mergeOf({merge}, 0) == "Add");
    merge.params["mode"] = "Exclude";
    CHECK(fla::shapeops::mergeOf({merge}, 0) == "Exclude");
    // Geometry alone, merging leaves the lines as they are.
    CHECK(fla::shapeops::apply({square}, false, {merge}, 0, 24.0).front().points.size() == 4);
}

TEST(letter_flips_source_text_and_presets)
{
    namespace ta = fla::textanim;
    std::vector<ta::Letter> letters(3);
    for (int i = 0; i < 3; ++i)
    {
        letters[i].index = i;
        letters[i].count = 3;
    }
    // Flip In: at the start the letters are turned edge-on; once in, flat.
    Applied flip = ta::defaults("flipin", 1);
    CHECK(near(ta::apply({flip}, letters, 0, 0, 24.0)[0].turnX, 90.0));
    CHECK(near(ta::apply({flip}, letters, 48, 0, 24.0)[2].turnX, 0.0));
    flip.params["axis"] = "Swing";
    CHECK(near(ta::apply({flip}, letters, 0, 0, 24.0)[0].turnY, 90.0));
    // Animate Letters turns them in depth too.
    Applied range = ta::defaults("range", 1);
    range.params["turnX"] = "30";
    range.params["moveY"] = "0";
    CHECK(near(ta::apply({range}, letters, 0, 0, 24.0)[1].turnX, 30.0));
    // Source Text: the words keyed for each frame, held between keys.
    Applied source = ta::defaults("sourcetext", 1);
    source.params["text"] = "@0=Three;24=Two;48=One";
    std::string words;
    CHECK(ta::sourceTextOf({source}, 30, words) && words == "Two");
    CHECK(ta::sourceTextOf({source}, 60, words) && words == "One");
    CHECK(!ta::sourceTextOf({range}, 0, words));
    // The presets are made of animators there are.
    CHECK(ta::presets().size() >= 10);
    for (const ta::Preset& preset : ta::presets())
        for (const Applied& animator : preset.animators)
            CHECK(ta::find(animator.type) != nullptr);
}

TEST(layer_echo_and_posterize_time)
{
    namespace mb = fla::motionblur;
    fla::Layer layer(nullptr);
    CHECK(!mb::layerTimeOf(layer).any());
    mb::LayerTime time;
    time.echoes = 4;
    time.interval = 3;
    time.decay = 0.5;
    time.posterize = 2;
    layer.extraAttributes = mb::withLayerTime(layer, time);
    const mb::LayerTime read = mb::layerTimeOf(layer);
    CHECK(read.echoes == 4 && read.interval == 3 && near(read.decay, 0.5) && read.posterize == 2);
    // On twos: frames 0 and 1 show 0, 2 and 3 show 2.
    CHECK(mb::posterized(read, 1) == 0 && mb::posterized(read, 3) == 2 && mb::posterized(read, 4) == 4);
    layer.extraAttributes = mb::withLayerTime(layer, mb::LayerTime());
    CHECK(layer.extraAttributes.empty());
}
