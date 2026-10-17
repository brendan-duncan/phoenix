#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

struct JSContext;
struct JSRuntime;

namespace fla {

class DOMElement;
class Frame;

namespace display {
class DisplayObject;
class Event;
} // namespace display

namespace play {

class MovieRuntime;
struct Clip;

/// HTML5 Canvas documents' JavaScript, played as CreateJS plays it: frame
/// scripts run with this as the timeline's MovieClip (stop, gotoAndPlay
/// from frame 0, the instances by name...), event listeners on clips and
/// the stage, createjs.Ticker, Tween and Ease, setTimeout, console.log and
/// alert, all over the movie runtime's clips and display list. QuickJS
/// runs the scripts; the CreateJS classes are JavaScript (createjs.cpp)
/// over a few native functions. One set per movie start. Free of Qt.
namespace canvas {

class Scripts
{
public:
    explicit Scripts(MovieRuntime& runtime);
    ~Scripts();
    Scripts(const Scripts&) = delete;
    Scripts& operator=(const Scripts&) = delete;

    /// Runs the script of \a frame (a keyframe of \a clip's timeline):
    /// compiled once, as a function called with the clip as this.
    void runFrame(Clip& clip, const Frame& frame, const std::string& where);
    /// The frame has played: timers due fire, tweens move on, and tick goes
    /// to the clips and the stage listening and to Ticker's listeners.
    void tick();
    /// The pointer's events have all gone to the display list: a release's
    /// pressup follows its click.
    void afterEvents();

private:
    friend struct Native;

    /// The number scripts know \a clip by, made the first time.
    int idOf(Clip& clip);
    /// The clip numbered \a id, or null once it is gone.
    Clip* clipOf(int id) const;
    /// The display object \a id's events come from: the clip's, or the
    /// stage's for -1.
    display::DisplayObject* displayOf(int id) const;
    /// Starts or stops telling the scripts of the display list's events of
    /// CreateJS's \a type ("click", "mousedown"...) on \a id's object.
    void listen(int id, const std::string& type, bool on);
    /// A display list event, \a type to CreateJS, at \a id's object.
    void deliver(int id, const std::string& type, display::Event& event);
    /// What the stage hears of the pointer, for stagemousedown and the rest
    /// and for pressmove and pressup.
    void stagePointer(const std::string& type, display::Event& event);
    /// The scripts start to run (and not from within themselves): their
    /// time limit starts.
    void begin();
    void end() { --_depth; }
    /// Tells of the exception the scripts threw, running in \a where.
    void report(const std::string& where);

    MovieRuntime& _runtime;
    JSRuntime* _js = nullptr;
    JSContext* _ctx = nullptr;
    /// False once these are gone, for display listeners called after.
    std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
    std::chrono::steady_clock::time_point _deadline;
    int _depth = 0;

    int _nextId = 0;
    /// The clips scripts have seen, by number, through their lives; and the
    /// numbers, by life.
    std::map<int, std::shared_ptr<Clip*>> _clips;
    std::map<const void*, int> _ids;
    /// The listeners put on the display list, by number and type.
    struct Listening
    {
        std::weak_ptr<display::DisplayObject> object;
        uint64_t id = 0;
    };
    std::map<std::pair<int, std::string>, Listening> _listening;
    /// The frame scripts compiled, by frame: their place in the scripts'
    /// list, -1 for one that does not compile. And the frames the scripts
    /// are named for, to tell where an error was from its stack.
    std::map<const Frame*, int> _compiled;
    std::map<std::string, const DOMElement*> _sources;
    /// Numbers each display event, so the scripts see one event as it
    /// passes from object to object.
    uint64_t _eventSerial = 0;
    /// Milliseconds of movie time: a frame's worth a tick.
    double _time = 0.0;
};

/// Whether \a source parses as JavaScript (a frame script, as a function's
/// body); when it does not, the line (from 1, 0 when unknown) and the
/// message of the error.
bool checkSyntax(const std::string& source, int& line, std::string& message);

/// The CreateJS classes the scripts run over (createjs.cpp).
extern const char* const kPrelude;

} // namespace canvas
} // namespace play
} // namespace fla
