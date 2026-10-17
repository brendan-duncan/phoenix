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
namespace abc {
class Domain;
}
}

#include "script/class_path.h"

namespace fla {

class Bitmap;
class DOMElement;
class Element;
class Document;
class Frame;
class Layer;
class StaticText;
class Symbol;
class SymbolInstance;
class Timeline;

namespace display {
class DisplayObject;
class Stage;
class TextField;
} // namespace display

/// A movie played as Flash Player plays it, scripts and all: Control > Test
/// Movie. Free of Qt.
namespace play {

namespace script {
class Bindings;
struct DisplayHost;
}
namespace canvas {
class Scripts;
struct Native;
}

/// The root timeline, or a movie clip or button on it as it plays: which
/// frame it is on, whether it plays on, and what scripts have set on it.
/// What a script has not set comes from the timeline as drawn.
struct Clip
{
    Clip() = default;
    ~Clip();
    Clip(const Clip&) = delete;
    Clip& operator=(const Clip&) = delete;

    /// Points at it while it lives, null once it is freed: what its script
    /// object and the functions bound to it hold, so a script keeping them
    /// after it is gone finds nothing rather than freed memory.
    std::shared_ptr<Clip*> life = std::make_shared<Clip*>(this);
    /// Taken off the stage for good (forgotten): its scripts no longer run,
    /// though it may not be freed until no script is running.
    bool gone = false;

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

    /// It on the display list: a display::MovieClip, or a
    /// display::SimpleButton for a button. What scripts set (_visible, _x,
    /// _xscale, alpha...) is kept there, over what the timeline places.
    std::shared_ptr<display::DisplayObject> display;
    /// What the timeline has placed in it on the frame it is on, in depth
    /// order: its movie clips' and buttons' display objects, its text
    /// fields and the rest of its art (as display::TimelineArt). A script
    /// may have taken some off the display list.
    std::vector<std::shared_ptr<display::DisplayObject>> timelineObjects;
    /// The keyframes timelineObjects were made for, to tell when they need
    /// making again.
    std::vector<const Frame*> timelineFrames;

    std::vector<std::unique_ptr<Clip>> children;
    /// JavaScript (HTML5 Canvas): named instances of its timeline off the
    /// stage for now, as CreateJS keeps each from the start, made before
    /// their frame or kept from an earlier one, the same object (its
    /// listeners and all) when a frame places them again.
    std::vector<std::unique_ptr<Clip>> parked;
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
    /// Whether its ActionScript 3 class (its symbol's, or the document
    /// class for the root) has been run on it.
    bool classed = false;
    /// What addFrameScript gave its frames (from 0), run in place of the
    /// frame's own script.
    std::map<int, as3::ValuePtr> frameScripts;

    /// Whether ActionScript 3 listens on it for \a type.
    bool listensFor(const std::string& type) const;
    /// Whether the pointer finds it: a button, or a clip listening for the
    /// mouse (on its display object, in ActionScript 3).
    bool wantsMouse() const;

    /// The child with this key, or null.
    Clip* child(const std::string& key) const;
    /// The child with this instance name, or null.
    Clip* named(const std::string& name) const;

    /// Whether it shows: _visible unless a script hid it.
    bool shown() const;
    /// Its transform in its parent: the timeline's, with what scripts set.
    Transform transform(const Transform& placedTransform) const;
    /// Its opacity from 0 to 1, as scripts set it; 1 when they have not.
    double opacity() const;
    /// What a script set _x and _y to; none when it has not.
    std::optional<double> scriptedX() const;
    std::optional<double> scriptedY() const;
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

    /// The display list the movie's scripts and the pointer work with: the
    /// stage, the root clip's display object its first child.
    display::Stage& stage() { return *_stage; }
    /// How many scripts and handlers have run: one that ran may have
    /// changed what a text field showing a variable shows.
    uint64_t scriptRuns() const { return _scriptRuns; }
    /// After the pointer or a key went to the stage's display objects
    /// (their listeners may have sent clips to other frames): goes on with
    /// what they did.
    void afterEvents();

    /// The scene playing goes to \a frame (from 0), as a script's
    /// gotoAndPlay or gotoAndStop sends it, keeping whether it plays: a
    /// time slider's seek.
    void seek(int frame);

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
    /// The stage's display objects hear them too (keyDown and keyUp to
    /// the focus), with the modifier keys held.
    void keyDown(int code, int character, bool shift = false, bool ctrl = false, bool alt = false);
    void keyUp(int code, int character, bool shift = false, bool ctrl = false, bool alt = false);
    /// The pointer is at (\a x, \a y) on the stage: a clip being dragged
    /// follows it, and _xmouse and mouseX read it.
    void pointerAt(double x, double y);

    /// Library symbols and sounds scripts reach by their linkage: movie
    /// clips for attachMovie and new, sounds for Sound.attachSound and new.
    /// Set before start().
    void setLibrary(const std::vector<const Symbol*>& symbols, const std::vector<std::string>& soundIdentifiers,
        const std::vector<std::string>& soundClasses);
    /// ActionScript 3 class files, by class name, and the document class:
    /// the classes are compiled as the movie starts, the document class run
    /// on the main timeline before its first frame's scripts, and a
    /// symbol's class (its linkage) on each clip of it. Set before start().
    void setClasses(const std::map<std::string, std::string>& sources, const std::string& documentClass);
    /// The ActionScript version the movie's publish settings give (see
    /// actionScriptVersion); 0, unknown, has the scripts tell. Version 3
    /// plays the scripts with the flash.* classes over the display list;
    /// 1 and 2 with the timeline's own objects. Set before start().
    void setActionScriptVersion(int version) { _version = version; }
    /// Whether the movie plays as ActionScript 3 (once started).
    bool actionScript3() const { return _as3; }
    /// The movie's scripts are JavaScript, as an HTML5 Canvas document's
    /// are: played with CreateJS's API (see canvas::Scripts) rather than as
    /// ActionScript. Set before start().
    void setJavaScript(bool javaScript) { _javaScript = javaScript; }
    bool javaScript() const { return _javaScript; }
    /// The library bitmaps scripts reach by their linkage (BitmapData
    /// classes). Set before start().
    void setLibraryBitmaps(const std::vector<const Bitmap*>& bitmaps) { _bitmaps = bitmaps; }
    /// ActionScript 3 as a published movie has it compiled (each DoABC
    /// tag's abcFile): its classes are found before any class file, the
    /// document class and the symbols' classes among them. Set before
    /// start().
    void setCompiledScripts(std::vector<std::vector<uint8_t>> abcFiles) { _abcFiles = std::move(abcFiles); }
    /// Finds an ActionScript 3 class's source by its qualified name: the
    /// movie's class path (see ClassPath). The classes setClasses gave are
    /// looked in first.
    std::function<std::optional<ClassFile>(const std::string& qualifiedName)> findClass;
    /// The folder the movie's file is in (UTF-8), what the paths scripts
    /// load (URLLoader, Loader) start from.
    std::string folder;
    /// The movie's address, as loaderInfo.url gives it (file:///...swf).
    std::string url;
    /// Where SharedObject.getLocal keeps what it saves; nothing is saved
    /// when empty.
    std::string sharedObjectFolder;

    /// What scripts ask of the web (URLLoader, Loader, sendToURL): \a done
    /// is called with what came, later, as the host's network answers.
    /// Without it, only files load.
    struct Request
    {
        std::string url;
        std::string method = "GET";
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<uint8_t> body;
        std::string contentType;
    };
    struct Response
    {
        bool ok = false;
        /// The HTTP status, 0 for a file or none.
        int status = 0;
        std::vector<uint8_t> bytes;
        std::string error;
    };
    std::function<void(const Request& request, std::function<void(const Response&)> done)> fetch;
    /// An image file's pixels (premultiplied ARGB, a row after another):
    /// false when it cannot be read. PNG is read without it.
    std::function<bool(const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint32_t>& pixels)>
        decodeImage;
    /// What the movie did that the person playing it should know of (a
    /// form sent to the web, say): shown with what it traces.
    std::function<void(const std::string&)> notice;
    /// The pointer, as Mouse.hide and Mouse.cursor leave it: whether it is
    /// hidden, and the cursor ("auto", "arrow", "button", "hand", "ibeam").
    bool mouseHidden() const;
    std::string mouseCursor() const;
    /// The pointer comes to the display list from the stage (Stage's
    /// pointerMove and the rest, as the window sends them): pointerOver,
    /// pointerDown and pointerUp then work only buttons' ActionScript 2
    /// handlers. Without it they send the display list's mouse events too,
    /// to the clip they name.
    void setStagePointer(bool fromStage) { _stagePointer = fromStage; }

    /// The frame (from 0) \a clip is held on rather than playing on by
    /// itself, as a Time Remap behavior on its instance says for its
    /// parent's frame; -1 when it plays as it would. Set by whatever draws
    /// the movie. A held clip goes to that frame as the movie steps, its
    /// scripts there run when it gets there.
    std::function<int(const Clip& clip)> heldFrame;
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
    /// The display text field for \a field on \a clip's frame, or null.
    display::TextField* displayField(const Clip& clip, const StaticText& field) const;
    /// The display object \a element (of \a clip's frame now) is placed
    /// as, or null.
    display::DisplayObject* timelineObject(const Clip& clip, const Element* element) const;
    /// The instance an element of a frame places: itself, or for a
    /// component kept as its XML, an instance of its symbol read from it;
    /// null for one that is not an instance.
    const SymbolInstance* instanceOf(const Element* element) const;
    /// The parameters (its parametersAsXML) of a component instance
    /// instanceOf read, or empty.
    std::string componentParameters(const SymbolInstance* instance) const
    {
        const auto found = _componentParameters.find(instance);
        return found == _componentParameters.end() ? std::string() : found->second;
    }
    /// The clip a made instance (Clip::made) is, or null.
    Clip* clipOfMade(const SymbolInstance& instance) const;
    /// Whether \a clip is on the stage now: a pointer kept since the movie
    /// was last drawn may be to one a script or a frame took off.
    bool alive(const Clip* clip) const;
    /// Whether a clip is being dragged.
    bool dragging() const { return _drag.clip != nullptr; }

    /// The input text field typing goes into: \a field on \a clip's
    /// timeline; null for none. A click on one focuses it, as a script can.
    void focusText(Clip* clip, const StaticText* field);
    /// Whether \a field on \a clip has the focus.
    bool focused(const Clip& clip, const StaticText& field) const
    {
        return _focus.clip == &clip && _focus.field == &field;
    }

    /// A keyframe was entered that has a sound on it (soundName...): the
    /// player plays it as the frame's sound settings say.
    std::function<void(const Frame& frame)> frameSound;
    Clip* hovered() const { return _hovered; }
    /// The frame a button shows: down, over or up.
    int buttonFrame(const Clip& button) const;

    /// The movie's frame rate was set by a script (stage.frameRate).
    std::function<void(double)> frameRateChanged;

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
    friend class script::Bindings;
    friend struct script::DisplayHost;
    friend class canvas::Scripts;
    friend struct canvas::Native;

    void setupGlobals();
    /// ActionScript 3's own globals (the rest are the flash.* classes).
    void setupActionScript3();
    /// Brings \a clip's children up to its current frame: new ones made
    /// (their first frame entered), gone ones dropped.
    void syncChildren(Clip& clip, std::vector<Clip*>& entered);
    /// Brings \a clip's display list up to its current frame: what the
    /// timeline places put in by depth, what it no longer places taken off.
    void syncDisplay(Clip& clip);
    /// Gives \a clip its display object.
    void attachDisplay(Clip& clip);
    /// Runs the scripts of the frame \a clip is on.
    void runFrameScripts(Clip& clip);
    /// Plays the sounds on button state frame \a state (0 Up to 3 Hit).
    void buttonSound(const Clip& button, int state);
    /// Goes on with the frames clips were sent to while scripts ran.
    void settle();
    /// Moves \a clip and what is in it on a frame; the clips whose new
    /// frames' scripts are to run are added to \a scripts.
    void advance(Clip& clip, std::vector<Clip*>& entered, std::vector<Clip*>& scripts);
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
    /// Compiles the class files, keeping the classes they define.
    void loadClasses();
    /// Runs \a clip's class on it: its methods and fields put on it, its
    /// constructor called with it as this. Once.
    void runClass(Clip& clip);
    /// Typing into the focused field: \a character added, Backspace
    /// (\a code 8) taking the last one off, Enter a new line where the field
    /// has more than one.
    void type(int code, int character);
    /// Sets the text \a field shows: its instance name's text, or the
    /// variable it shows.
    void setFieldText(Clip& clip, const StaticText& field, const std::string& text);
    /// The dynamic or input text field \a name names on \a clip's frame
    /// now, or null; and the text it shows (what a script put in it, else
    /// its own).
    const StaticText* textField(const Clip& clip, const std::string& name) const;
    std::string fieldText(Clip& clip, const StaticText& field);
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
    /// Marks  clip and what is in it gone, and lets go of every pointer
    /// to them the runtime keeps.
    void forget(Clip& clip);
    void letGo(std::vector<Clip*> going);
    /// Frees the clips taken off (_gone), once no script is running.
    void freeGone();

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
    std::shared_ptr<display::Stage> _stage;
    uint64_t _scriptRuns = 0;
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
    std::map<std::string, std::string> _classSources;
    std::string _documentClass;
    std::map<std::string, as3::ValuePtr> _classes;
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
    struct TextFocus
    {
        Clip* clip = nullptr;
        const StaticText* field = nullptr;
    };
    TextFocus _focus;
    std::chrono::steady_clock::time_point _started;
    std::vector<const Symbol*> _library;
    std::vector<std::string> _soundIdentifiers;
    std::vector<std::string> _soundClasses;
    /// Clips made by scripts whose first frame is still to be entered, and
    /// those ActionScript 3 made that are not on the stage.
    std::vector<Clip*> _madeToEnter;
    std::vector<std::unique_ptr<Clip>> _offStage;
    /// Clips the frames took off, freed once no script is running and the
    /// frames have settled, so nothing still at work on them is left
    /// holding freed memory.
    std::vector<std::unique_ptr<Clip>> _gone;
    int _madeCount = 0;

    // ActionScript 3.
    int _version = 0;
    bool _as3 = false;
    bool _stagePointer = false;
    std::vector<const Bitmap*> _bitmaps;
    mutable std::map<const Element*, std::unique_ptr<SymbolInstance>> _components;
    mutable std::map<const SymbolInstance*, std::string> _componentParameters;
    std::unique_ptr<script::Bindings> _bindings;
    // JavaScript.
    bool _javaScript = false;
    std::unique_ptr<canvas::Scripts> _canvas;
    /// The compiled scripts, and what runs them on the VM.
    std::vector<std::vector<uint8_t>> _abcFiles;
    std::unique_ptr<as3::abc::Domain> _abcDomain;
    /// The clip the pointer was last sent over (pointerOver) and pressed
    /// on, for the display list's events when the stage does not send them.
    std::weak_ptr<display::DisplayObject> _pointerOver;
    std::weak_ptr<display::DisplayObject> _pointerPressed;
    /// Whether the scripts look like ActionScript 3 (for a movie whose
    /// version is not known).
    bool scriptsLookLikeActionScript3() const;
    /// Whether they look like ActionScript 1 or 2 (scripts on instances...).
    bool scriptsLookLikeActionScript2() const;
    /// JavaScript: the instance named \a name on \a clip's timeline,
    /// wherever its frame is: on the stage, or kept off it (made now when
    /// its frame is still to come). Null when the timeline has none.
    Clip* canvasInstance(Clip& clip, const std::string& name);
    /// A kept instance placed again: from its first frame, as CreateJS
    /// resets a clip a timeline adds back.
    void revive(Clip& clip, std::vector<Clip*>& entered);
    /// Moves the clips scripts made to where the display list has them:
    /// into the clip whose display object holds theirs, or off the stage.
    void placeMadeClips();
    /// The display list's mouse event \a type to \a target, for
    /// pointerOver, pointerDown and pointerUp when the stage does not send
    /// them.
    void sendPointer(const char* type, display::DisplayObject* target, bool bubbles,
        display::DisplayObject* related = nullptr);
};

/// The ActionScript 3 class files a document uses, by class name: its
/// document class and the classes its symbols are linked to, found in
/// \a folder (the document's) or under the source paths its publish
/// settings give, by package ("com.studio.Hat" in com/studio/Hat.as).
/// \a documentClass is set to the document class's name, empty when none.
std::map<std::string, std::string> classFiles(const Document& document, const std::string& folder,
    std::string& documentClass);

/// The script kept on an instance (a button's on handlers, a movie clip's
/// onClipEvent handlers), or empty.
std::string instanceScript(const SymbolInstance& instance);

/// Whether \a source parses as ActionScript; when it does not, the line
/// (from 1, or 0 when unknown) and the message of the first error.
bool checkSyntax(const std::string& source, int& line, std::string& message);

} // namespace play

} // namespace fla
