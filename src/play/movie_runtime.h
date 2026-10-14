#pragma once

#include "../data/transform.h"

#include <algorithm>
#include <chrono>
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

class DOMElement;
class Frame;
class Layer;
class StaticText;
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
    /// What ActionScript 3 added with addEventListener, by event type: the
    /// function, and the clip whose script added it (where the function's
    /// names are looked up).
    struct Listener
    {
        as3::ValuePtr function;
        Clip* scope = nullptr;
        /// The script it was written in: a keyframe, or an instance.
        const DOMElement* source = nullptr;
    };
    std::map<std::string, std::vector<Listener>> listeners;

    /// Where it was last drawn, as whatever draws the movie tells it: from
    /// its parent's space to the stage's, and the box its art covers on the
    /// stage. Unknown until it has been drawn.
    struct Placement
    {
        Transform parentToStage;
        double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
        bool known = false;
    };
    Placement placement;

    /// What scripts put in its text fields, by the field's instance name,
    /// shown in place of the text as drawn.
    std::map<std::string, std::string> texts;

    /// A clip a script made (attachMovie, duplicateMovieClip, or new and
    /// addChild in ActionScript 3), not on any frame: the instance it shows
    /// as, its own. Drawn over what its parent's timeline has.
    std::shared_ptr<SymbolInstance> made;

    /// Whether ActionScript 3 listens on it for \a type.
    bool listensFor(const std::string& type) const;
    /// Whether the pointer finds it: a button, or a clip listening for the
    /// mouse.
    bool wantsMouse() const;

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
    /// Plays \a scenes one after another, from the first, as Flash Player
    /// plays a movie's scenes; after the last, the first again.
    MovieRuntime(const std::vector<const Timeline*>& scenes, double frameRate);
    ~MovieRuntime();

    MovieRuntime(const MovieRuntime&) = delete;
    MovieRuntime& operator=(const MovieRuntime&) = delete;

    /// Starts again from the first frame: its scripts run.
    void start();
    /// One frame on: every clip that plays moves on (round to its first
    /// frame after its last), and the scripts of the frames entered run.
    void step();

    Clip& root() { return *_root; }
    /// The scene playing, from 0, and how many there are.
    int scene() const { return _scene; }
    int sceneCount() const { return static_cast<int>(_scenes.size()); }
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
    /// A key went down or up: \a code is the key's code as Flash numbers
    /// them (Key.LEFT 37, "A" 65...), \a character what it types, 0 for
    /// none. AS2 keyPress handlers, onClipEvent(keyDown) and Key listeners
    /// hear it, and ActionScript 3's keyDown and keyUp listeners.
    void keyDown(int code, int character);
    void keyUp(int code, int character);
    /// The pointer is at (\a x, \a y) on the stage: a clip being dragged
    /// follows it, and _xmouse and mouseX read it.
    void pointerAt(double x, double y);

    /// Library symbols and sounds scripts reach by their linkage: movie
    /// clips for attachMovie and new, sounds for Sound.attachSound and new.
    /// Set before start().
    void setLibrary(const std::vector<const Symbol*>& symbols, const std::vector<std::string>& soundIdentifiers,
        const std::vector<std::string>& soundClasses);

    /// Whether \a clip's art is at (\a x, \a y) on the stage, as drawn; set
    /// by whatever draws the movie. Without it, its box counts.
    std::function<bool(const Clip& clip, double x, double y)> hitShape;
    /// Plays the library sound \a linkage names from \a offset seconds in,
    /// \a loops times, at \a volume (0 to 1); the channel, or -1.
    std::function<int(const std::string& linkage, double offset, int loops, double volume)> playSound;
    std::function<void(int channel)> stopSound;
    std::function<void()> stopAllSounds;

    /// The text \a field (on \a clip's timeline) shows now: what a script
    /// put in it, or the value of the variable it shows; nothing when it
    /// shows as drawn.
    std::optional<std::string> textOf(Clip& clip, const StaticText& field);
    /// The clips scripts made on \a clip, in the order they draw.
    std::vector<Clip*> madeChildren(const Clip& clip) const;
    /// The clip a made instance (Clip::made) is, or null.
    Clip* clipOfMade(const SymbolInstance& instance) const;
    /// Whether \a clip is on the stage now: a pointer kept since the movie
    /// was last drawn may be to one a script or a frame took off.
    bool alive(const Clip* clip) const;
    /// Whether a clip is being dragged.
    bool dragging() const { return _drag.clip != nullptr; }
    Clip* hovered() const { return _hovered; }
    /// The frame a button shows: down, over or up.
    int buttonFrame(const Clip& button) const;

    /// What the scripts trace, and the errors they stop on.
    std::function<void(const std::string&)> trace;
    std::function<void(const std::string&)> error;
    /// An error a script stopped on, and the script it is in: the keyframe
    /// (a Frame) or the button or movie clip instance (a SymbolInstance)
    /// whose script it is, null when not known, and the line, 0 when not
    /// known. Given as well as error.
    struct ScriptError
    {
        std::string message;
        const DOMElement* source = nullptr;
        int line = 0;
    };
    std::function<void(const ScriptError&)> scriptError;
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
    /// A button's on() handlers, by event, gathered the first time.
    std::map<std::string, as3::ValuePtr>& buttonHandlers(Clip& button);
    /// Sends an ActionScript 3 event of \a type to \a target's listeners,
    /// and on up its parents' when it \a bubbles; \a fill adds what the
    /// event carries.
    void dispatch(Clip& target, const std::string& type, bool bubbles,
        const std::function<void(const as3::ValuePtr&)>& fill = {});
    /// Every clip on the stage, the root first.
    void eachClip(const std::function<void(Clip&)>& visit);
    void runFunction(const as3::ValuePtr& function, const std::vector<as3::ValuePtr>& args, Clip& scope,
        const std::string& where, const DOMElement* source = nullptr);

    /// From \a clip's own space to the stage's, as last drawn.
    Transform toStage(const Clip& clip) const;
    /// (\a x, \a y) on the stage in \a clip's own space.
    void stageToClip(const Clip& clip, double& x, double& y) const;
    /// Whether \a clip is at (\a x, \a y) on the stage: its art with
    /// \a shape, else its box.
    bool hits(const Clip& clip, double x, double y, bool shape) const;
    bool overlaps(const Clip& a, const Clip& b) const;
    /// The top clip under the pointer other than \a except and what is in
    /// it (_droptarget), or null.
    Clip* clipUnderPointer(const Clip* except);
    /// A path to \a clip from the root, as Flash 4 wrote them: "/hat/bow".
    std::string slashPath(const Clip& clip) const;
    void startDrag(Clip& clip, bool lockCenter, const std::vector<double>& bounds);
    void stopDrag();

    /// The symbol a linkage names, or null.
    const Symbol* linked(const std::string& linkage) const;
    /// A new clip of \a symbol named \a name on \a parent (null: not on the
    /// stage yet, as ActionScript 3's new makes it), its first frame
    /// entered once the script making it is done.
    Clip* makeClip(Clip* parent, const Symbol& symbol, const std::string& name);
    /// Takes a made clip off its parent: forgotten, or kept off the stage
    /// for addChild when \a keep.
    void takeOff(Clip& clip, bool keep);
    /// The clip a script value is (a clip's object), or null.
    Clip* clipOfValue(const as3::ValuePtr& value);
    as3::ValuePtr textFieldObject(Clip& clip, const std::string& name);
    as3::ValuePtr soundObject(const std::string& linkage, bool actionScript3);
    /// Tells of an error: \a message, in the script \a source, at \a line.
    void tell(const std::string& message, const DOMElement* source, int line);

    /// Runs code compiled from \a source (cached by \a key), on \a scope.
    void run(const DOMElement* key, const std::string& source, Clip& scope, const std::string& where);
    std::shared_ptr<as3::BytecodeFunction> compiled(const DOMElement* key, const std::string& source,
        const std::string& where);
    void report(const std::string& where);

    /// A frame by number or label, on \a clip's timeline; -1 when none.
    int frameOf(const Clip& clip, const as3::ValuePtr& target) const;
    int frameIn(const Timeline* timeline, const as3::ValuePtr& target) const;
    void goTo(Clip& clip, int frame, bool play);
    /// The root moves to frame \a frame of scene \a scene: what was on the
    /// stage goes, and the scene's own is placed when the move settles.
    void goToScene(int scene, int frame, bool play);
    /// Makes the move now: only once no script is running.
    void applyScene(int scene, int frame, bool play);
    /// The scene \a name names, or -1.
    int sceneNamed(const std::string& name) const;
    /// The clip a target names ("hat", "/hat", "_root.hat", "../hat",
    /// "_parent.hat", "_level0.hat", "this"), from \a from; null when none.
    Clip* resolve(Clip& from, const std::string& target);
    Clip* resolve(Clip& from, const as3::ValuePtr& target);

    as3::ValuePtr property(Clip& clip, const std::string& name);
    bool setProperty(Clip& clip, const std::string& name, const as3::ValuePtr& value);
    as3::ValuePtr objectFor(Clip& clip);
    void forget(Clip& clip);

    std::vector<const Timeline*> _scenes;
    int _scene = 0;
    /// A move to another scene a script asked for, made when it is done.
    struct SceneMove
    {
        int scene = -1;
        int frame = 0;
        bool play = true;
    };
    SceneMove _sceneMove;
    double _frameRate;
    std::unique_ptr<as3::VM> _vm;
    std::unique_ptr<Clip> _root;
    /// The clip whose script runs: what a bare stop() or gotoAndPlay()
    /// moves (tellTarget changes it).
    Clip* _current = nullptr;
    /// The script running, for errors.
    const DOMElement* _source = nullptr;
    /// Clips sent to another frame by a script, whose new frame's scripts
    /// run once the script is done.
    std::vector<Clip*> _sent;
    std::map<const void*, std::shared_ptr<as3::BytecodeFunction>> _compiled;
    std::map<const void*, std::map<std::string, as3::ValuePtr>> _buttonHandlers;
    Clip* _hovered = nullptr;
    Clip* _pressed = nullptr;
    /// The keys held down, and the last to go down (Key.getCode and
    /// Key.getAscii), and what Key.addListener added.
    std::vector<int> _keysDown;
    int _lastKey = 0;
    int _lastCharacter = 0;
    std::vector<as3::ValuePtr> _keyListeners;
    /// The pointer on the stage, and the clip being dragged.
    double _mouseX = 0.0;
    double _mouseY = 0.0;
    struct Drag
    {
        Clip* clip = nullptr;
        bool lock = false;
        double dx = 0.0, dy = 0.0;
        bool bounded = false;
        double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
    };
    Drag _drag;
    std::chrono::steady_clock::time_point _started;
    std::vector<const Symbol*> _library;
    std::vector<std::string> _soundIdentifiers;
    std::vector<std::string> _soundClasses;
    /// Clips made by scripts whose first frame is still to be entered, and
    /// those ActionScript 3 made that are not on the stage.
    std::vector<Clip*> _madeToEnter;
    std::vector<std::unique_ptr<Clip>> _offStage;
    int _madeCount = 0;
};

/// The script kept on an instance (a button's on handlers, a movie clip's
/// onClipEvent handlers), or empty.
std::string instanceScript(const SymbolInstance& instance);

/// Whether \a source parses as ActionScript; when it does not, the line
/// (from 1, or 0 when unknown) and the message of the first error.
bool checkSyntax(const std::string& source, int& line, std::string& message);

} // namespace play

} // namespace fla
