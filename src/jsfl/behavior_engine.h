#pragma once

#include "../edit/behaviors.h"
#include "../edit/guide_path.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fla::behaviors {

/// What a behavior is told about the frame it works out.
struct Moment
{
    /// The frame on the instance's own timeline, from 0, and its rate.
    int frame = 0;
    double frameRate = 24.0;
    /// The first and last frames of the run of keyframes the instance is
    /// on (its layer's keyframes holding it, one after another).
    int start = 0;
    int end = 0;
    /// The keyed values now, and at another frame of the run (false when
    /// it is not there).
    Keyed keyed;
    std::function<bool(int frame, Keyed& keyed)> keyedAt;
    /// The line of the motion guide the instance's layer is under, on this
    /// frame, in the same space as the instance's place; null without one.
    const GuidePath* path = nullptr;
    /// Another instance on the same timeline, by its instance name, as it
    /// is on \a frame (its keys and its own behaviors' first copy); false
    /// when there is none of that name.
    std::function<bool(const std::string& name, int frame, Keyed& keyed)> other;
    /// A master controller's value, from 0 to 1; false when there is none
    /// of that name.
    std::function<bool(const std::string& name, double& value)> controller;
    /// How loud the scene's sound is on a frame, from 0 (silent) to 1 (its
    /// loudest); null when nothing can hear it.
    std::function<double(int frame)> loudness;
};

/// Runs behaviors: JavaScript files of the form
///
///     behavior({
///         name: "Wiggle",
///         description: "Shakes it about at random, smoothly.",
///         params: {
///             frequency: { label: "Wiggles per second", value: 2, min: 0, max: 30, step: 0.1 },
///             position: { label: "Move (pixels)", value: 10, min: 0, max: 500 },
///             seed: { type: "seed", value: 1 }
///         },
///         evaluate: function (ctx, p) {
///             var t = ctx.time * p.frequency;
///             return { x: ctx.noise(t, 1) * p.position, y: ctx.noise(t, 2) * p.position };
///         }
///     });
///
/// evaluate gets ctx (frame, time, fps, start, end, keyed, keyedAt(frame),
/// random(n) and noise(t, n), both seeded by the "seed" setting) and the
/// settings, and returns what to add: x and y (pixels), rotation
/// (degrees), scale, scaleX and scaleY (factors) and alpha (a factor).
/// Every call has a time limit; nothing reaches files, the clock or the
/// document, so a frame always comes out the same. The built-in
/// behaviors are written the same way.
class Engine
{
public:
    Engine();
    ~Engine();

    /// Reads a behavior file's \a source as behavior \a id. False with \a
    /// error when it does not define one.
    bool add(const std::string& id, const std::string& source, const std::string& file, bool builtIn,
        std::string& error);
    /// The behaviors Phoenix comes with.
    void addBuiltIns();

    const std::vector<Definition>& definitions() const;
    const Definition* find(const std::string& id) const;

    /// A behavior of \a id with its settings at their defaults; its seed,
    /// when it has one, is \a seed.
    Applied defaults(const std::string& id, int seed) const;

    /// What \a list makes of the instance on \a moment: the behaviors that
    /// are on, in order, each working on every copy the ones before it
    /// made (one, unless one repeats it). One that fails (an error, or too
    /// slow) adds nothing, and \a error says why.
    Copies evaluate(const std::vector<Applied>& list, const Moment& moment, std::string* error = nullptr);

    /// The sources of the built-in behaviors, by id.
    static const std::vector<std::pair<std::string, std::string>>& builtInSources();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace fla::behaviors
