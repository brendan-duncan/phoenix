#pragma once

#include "../data/transform.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace as3 {
class BytecodeFunction;
class Value;
class VM;
using ValuePtr = std::shared_ptr<Value>;
}

namespace fla {

class Frame;
class Layer;
class Symbol;
class SymbolInstance;
class Timeline;

/// A movie played as Flash Player plays it, scripts and all: Control > Test
/// Movie. Free of Qt.
namespace play {

/// The root timeline, or a movie clip or button on it as it plays: which
/// frame it is on, whether it plays on, and what scripts have set on it.
/// What a script has not set comes from the timeline as drawn.
struct Clip
{
    const Symbol* symbol = nullptr;
    const Timeline* timeline = nullptr;
    Clip* parent = nullptr;
    /// Where it is placed in its parent: the layer, the instance as the
    /// parent's current frame has it (a keyframe holds its own copy), and
    /// the instance name scripts address it by.
    const Layer* layer = nullptr;
    const SymbolInstance* placed = nullptr;
    std::string name;
    /// What tells it apart among its parent's: its name, or its layer and
    /// place when it has none.
    std::string key;
    bool button = false;

    /// From 0.
    int frame = 0;
    bool playing = true;
    int length = 1;

    /// What scripts have set: _visible, _x and _y (in the parent's
    /// pixels), _xscale, _yscale (percent), _rotation (degrees) and _alpha
    /// (0 to 100).
    std::optional<bool> visible;
    std::optional<double> x, y, xscale, yscale, rotation, alpha;

    std::vector<std::unique_ptr<Clip>> children;
    /// What scripts see it as.
    as3::ValuePtr object;
    /// Its onClipEvent handlers, by event.
    std::map<std::string, as3::ValuePtr> clipEvents;

    /// The child with this key, or null.
    Clip* child(const std::string& key) const;
    /// The child with this instance name, or null.
    Clip* named(const std::string& name) const;

    /// Whether it shows: _visible unless a script hid it.
    bool shown() const { return visible.value_or(true); }
    /// Its transform in its parent: the timeline's, with what scripts set.
    Transform transform(const Transform& placedTransform) const;
    /// Its opacity from 0 to 1, as scripts set it; 1 when they have not.
    double opacity() const { return alpha ? std::max(0.0, std::min(1.0, *alpha / 100.0)) : 1.0; }
};

class MovieRuntime
{
public:
    /// Plays \a root, the scene, at \a frameRate frames a second.
    MovieRuntime(const Timeline& root, double frameRate);
    ~MovieRuntime();

    MovieRuntime(const MovieRuntime&) = delete;
    MovieRuntime& operator=(const MovieRuntime&) = delete;

    /// Starts again from the first frame: its scripts run.
    void start();
    /// One frame on: every clip that plays moves on (round to its first
    /// frame after its last), and the scripts of the frames entered run.
    void step();

    Clip& root() { return *_root; }
    double frameRate() const { return _frameRate; }

    /// The clip \a instance (on \a layer, on \a parent's current frame) is,
    /// or null when it is not a movie clip or button the runtime knows.
    Clip* clipOf(const Clip& parent, const Layer& layer, const SymbolInstance& instance) const;

    /// The pointer is over \a button (null: over no button), down or up:
    /// buttons show their over and down frames and their handlers run
    /// (rollOver, rollOut, press, release, releaseOutside, dragOver,
    /// dragOut).
    void pointerOver(Clip* button);
    void pointerDown();
    void pointerUp();
    Clip* hovered() const { return _hovered; }
    /// The frame a button shows: down, over or up.
    int buttonFrame(const Clip& button) const;

    /// What the scripts trace, and the errors they stop on.
    std::function<void(const std::string&)> trace;
    std::function<void(const std::string&)> error;
    /// getURL and fscommand: what the movie asks of the player around it.
    std::function<void(const std::string& url, const std::string& window)> getURL;

    /// The movie changed: what shows needs drawing again.
    std::function<void()> changed;

private:
    friend class ClipObject;

    void setupGlobals();
    /// Brings \a clip's children up to its current frame: new ones made
    /// (their first frame entered), gone ones dropped.
    void syncChildren(Clip& clip, std::vector<Clip*>& entered);
    /// Runs the scripts of the frame \a clip is on.
    void runFrameScripts(Clip& clip);
    /// Goes on with the frames clips were sent to while scripts ran.
    void settle();
    void advance(Clip& clip, std::vector<Clip*>& entered);
    void enter(std::vector<Clip*>& entered);
    void runClipEvent(Clip& clip, const std::string& event);
    void runButtonEvent(Clip& button, const std::string& event);

    /// Runs code compiled from \a source (cached by \a key), on \a scope.
    void run(const void* key, const std::string& source, Clip& scope, const std::string& where);
    std::shared_ptr<as3::BytecodeFunction> compiled(const void* key, const std::string& source,
        const std::string& where);
    void report(const std::string& where);

    /// A frame by number or label, on \a clip's timeline; -1 when none.
    int frameOf(const Clip& clip, const as3::ValuePtr& target) const;
    void goTo(Clip& clip, int frame, bool play);
    /// The clip a target names ("hat", "/hat", "_root.hat", "../hat",
    /// "_parent.hat", "_level0.hat", "this"), from \a from; null when none.
    Clip* resolve(Clip& from, const std::string& target);
    Clip* resolve(Clip& from, const as3::ValuePtr& target);

    as3::ValuePtr property(Clip& clip, const std::string& name);
    bool setProperty(Clip& clip, const std::string& name, const as3::ValuePtr& value);
    as3::ValuePtr objectFor(Clip& clip);
    void forget(Clip& clip);

    const Timeline& _rootTimeline;
    double _frameRate;
    std::unique_ptr<as3::VM> _vm;
    std::unique_ptr<Clip> _root;
    /// The clip whose script runs: what a bare stop() or gotoAndPlay()
    /// moves (tellTarget changes it).
    Clip* _current = nullptr;
    /// Clips sent to another frame by a script, whose new frame's scripts
    /// run once the script is done.
    std::vector<Clip*> _sent;
    std::map<const void*, std::shared_ptr<as3::BytecodeFunction>> _compiled;
    std::map<const void*, std::map<std::string, as3::ValuePtr>> _buttonHandlers;
    Clip* _hovered = nullptr;
    Clip* _pressed = nullptr;
};

/// The script kept on an instance (a button's on handlers, a movie clip's
/// onClipEvent handlers), or empty.
std::string instanceScript(const SymbolInstance& instance);

} // namespace play

} // namespace fla
