#pragma once

#include "../display/bitmap_data.h"
#include "../display/bitmap_filter.h"
#include "../display/display_object.h"
#include "../display/events.h"
#include "../display/geom.h"
#include "../display/text_field.h"
#include "../movie_runtime.h"

#include "bytecode/vm.h"
#include "runtime/value.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fla {
namespace play {

/// The flash.* classes ActionScript 3 movies use, as native classes of the
/// script VM over the display engine (display::): display objects, events,
/// geometry, bitmaps, filters, text, bytes, timers, loading and the rest.
/// Scripts make them with new, extend them and dispatch their events; the
/// movie runtime makes those its timelines place. One set per VM.
namespace script {

class Bindings;

/// What every flash.events.EventDispatcher keeps: the engine's dispatcher,
/// whose listeners the script's are.
struct DispatcherHost : as3::HostObject
{
    std::shared_ptr<display::EventDispatcher> dispatcher;
};

/// A display object's: the engine's object and, for a movie clip or button
/// a timeline placed or the library made, its clip (through its life, so a
/// script holding the object past the clip finds none).
struct DisplayHost : DispatcherHost
{
    DisplayHost(Bindings& bindings, std::shared_ptr<display::DisplayObject> object, std::shared_ptr<Clip*> life);

    display::DisplayObject* object() const { return static_cast<display::DisplayObject*>(dispatcher.get()); }
    std::shared_ptr<display::DisplayObject> shared() const
    {
        return std::static_pointer_cast<display::DisplayObject>(dispatcher);
    }
    Clip* clip() const { return life ? *life : nullptr; }

    // ActionScript 2's names (_x, _alpha...) on a clip, and a container's
    // children by name, as the timeline declares them.
    as3::ValuePtr get(const std::string& name) override;
    bool set(const std::string& name, as3::ValuePtr value) override;

    Bindings& bindings;
    std::shared_ptr<Clip*> life;
    /// graphics, the same object each time it is read.
    as3::Val graphics;
};

/// A flash.events.Event's: the engine's event. One the engine sends is
/// live while it is on its way (\a live); one a script made is its own.
struct EventHost : as3::HostObject
{
    std::shared_ptr<display::Event> owned;
    display::Event* live = nullptr;
    display::Event& event() const { return live ? *live : *owned; }
    /// target and currentTarget as the last listener saw them.
    as3::Val target;
    as3::Val currentTarget;
    // What the events the engine does not have carry.
    double bytesLoaded = 0.0;
    double bytesTotal = 0.0;
    std::string text;
    int errorID = 0;
    int status = 0;
};

/// The script objects of the geometry classes.
struct PointHost : as3::HostObject
{
    double x = 0.0;
    double y = 0.0;
};
struct RectangleHost : as3::HostObject
{
    display::Rect rect;
};
struct MatrixHost : as3::HostObject
{
    display::Matrix matrix;
};
struct ColorTransformHost : as3::HostObject
{
    display::ColorTransform transform;
};
/// flash.geom.Transform: a display object's, read and set through it.
struct TransformHost : as3::HostObject
{
    std::shared_ptr<display::DisplayObject> object;
};
/// flash.display.Graphics: a Shape's or Sprite's drawing, held through its
/// owner (drawing into one that is gone does nothing).
struct GraphicsHost : as3::HostObject
{
    std::weak_ptr<display::DisplayObject> owner;
    display::Graphics* graphics = nullptr;
    display::Graphics* get() const { return owner.expired() ? nullptr : graphics; }
};
/// flash.display.LoaderInfo: what is known of a file loaded, or the movie.
struct LoaderInfoHost : DispatcherHost
{
    std::string url;
    std::string contentType;
    double bytesLoaded = 0.0;
    double bytesTotal = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool complete = false;
    as3::Val content;
    as3::Val loader;
    as3::Val parameters;
};
/// flash.display.Loader: its display object holds what it loaded.
struct LoaderHost : DisplayHost
{
    using DisplayHost::DisplayHost;
    as3::Val info;
    as3::Val content;
    /// Bumped by each load and unload: what an older one loads is dropped.
    int generation = 0;
};
struct BitmapDataHost : as3::HostObject
{
    std::shared_ptr<display::BitmapData> data;
};
struct FilterHost : as3::HostObject
{
    display::BitmapFilter filter;
};
struct TextFormatHost : as3::HostObject
{
    display::TextFormat format;
};

/// flash.utils.ByteArray: its bytes, where reading and writing are, and
/// which way round numbers go. obj[i] reads and writes bytes.
struct ByteArrayHost : as3::HostObject
{
    std::vector<uint8_t> bytes;
    size_t position = 0;
    bool littleEndian = false;
    uint32_t objectEncoding = 3;

    bool getIndex(const as3::Val& key, as3::Val& out) override;
    bool setIndex(const as3::Val& key, const as3::Val& value) override;
    bool hasIndex(const as3::Val& key) override;
};

/// A ByteArray's bytes, or null when \a value is not one.
ByteArrayHost* byteArrayOf(const as3::Val& value);

/// Everything the classes share: the VM, the movie, the classes by role,
/// the script objects of the engine's objects, and what waits for the next
/// frame (timers, loads).
class Bindings
{
public:
    Bindings(MovieRuntime& runtime, as3::VM& vm);
    ~Bindings();
    Bindings(const Bindings&) = delete;
    Bindings& operator=(const Bindings&) = delete;

    /// Defines every class and function.
    void install();

    MovieRuntime& runtime;
    as3::VM& vm;

    /// A class by qualified name, as defined (null when it is not).
    std::shared_ptr<as3::ClassInfo> classNamed(const std::string& name);
    /// A new instance of \a cls with \a host attached, its constructor not
    /// run (the host made it: the native classes need none).
    as3::Val instance(const std::string& cls, std::shared_ptr<as3::HostObject> host);

    // The script objects of the engine's.

    /// The object scripts see for \a object: the one it has, else one made
    /// of the class its kind is (a clip's of its symbol's class). Null for
    /// none.
    as3::Val wrap(display::DisplayObject* object);
    as3::Val wrap(const std::shared_ptr<display::DisplayObject>& object) { return wrap(object.get()); }
    as3::Val wrap(display::EventDispatcher* dispatcher);
    /// The event object for \a event: the one it has, else one made of the
    /// class its kind is.
    as3::Val wrapEvent(display::Event& event);
    /// The display object a script value is, or null.
    display::DisplayObject* displayOf(const as3::Val& value) const;
    std::shared_ptr<display::DisplayObject> sharedDisplayOf(const as3::Val& value) const;
    display::EventDispatcher* dispatcherOf(const as3::Val& value) const;
    /// Keeps \a wrapper while its object is about: on the display list, or
    /// held by a script.
    void keep(const as3::Val& wrapper, const std::shared_ptr<display::DisplayObject>& object);
    /// Lets go of the objects only each other hold.
    void sweep();
    /// Lets go of every script object kept: the VM is going.
    void releaseAll();

    // Calling scripts.

    /// Calls a script listener with the event, reporting what it throws.
    void callListener(const as3::Val& listener, display::Event& event);
    /// Calls \a function, reporting what it throws; false when it threw.
    bool callSafely(const as3::Val& function, const as3::Val& self, const std::vector<as3::Val>& args,
        const std::string& where);
    /// Tells of an error a script threw, with its stack.
    void report(const as3::ScriptException& exception, const std::string& where);
    /// The listener addEventListener adds for \a listener.
    display::EventDispatcher::Callback listenerCallback(const as3::Val& listener);
    /// Sends a script's event object from \a dispatcher (a clone of it when
    /// it was sent before); false when a listener prevented its default.
    bool dispatchScriptEvent(display::EventDispatcher& dispatcher, as3::Val event);
    /// A new event object of class \a cls (an Event or one of the classes
    /// that keep their own fields: TimerEvent, ProgressEvent, the error
    /// events...).
    as3::Val makeEvent(const std::string& cls, const std::string& type, bool bubbles = false, bool cancelable = false);
    /// Makes and sends an event from the object \a target; false when there
    /// is no such object or a listener prevented its default.
    bool send(const as3::Val& target, const std::string& cls, const std::string& type);

    /// Gives a new script object \a object to show: its host, and kept.
    std::shared_ptr<DisplayHost> attach(const as3::Val& self, std::shared_ptr<display::DisplayObject> object);
    /// Gives a new script object of a library symbol's class a clip of the
    /// symbol, off the stage until it is added.
    void attachSymbol(const as3::Val& self, const Symbol& symbol);
    /// A clip of a symbol whose class is not a MovieClip: held on its first
    /// frame, as it has no timeline in ActionScript 3.
    void keepFirstFrame(Clip& clip, const as3::ClassInfo* cls);
    /// A display object's graphics object (Shape, Sprite).
    as3::Val graphicsOf(const as3::Val& self);
    /// Moves \a clip to \a frame now, as gotoAndStop does in ActionScript 3:
    /// what the frame places is there at once, its scripts run after.
    void goToNow(Clip& clip, int frame, bool play);

    // Values of the geometry classes.

    as3::Val newPoint(double x, double y);
    as3::Val newRectangle(const display::Rect& rect);
    as3::Val newMatrix(const display::Matrix& matrix);
    as3::Val newColorTransform(const display::ColorTransform& transform);
    as3::Val newBitmapData(std::shared_ptr<display::BitmapData> data);
    /// What a Point, Rectangle, Matrix or ColorTransform holds; a plain
    /// object's x, y, width... when it is not one.
    display::Point pointOf(const as3::Val& value);
    display::Rect rectOf(const as3::Val& value);
    display::Matrix matrixOf(const as3::Val& value);
    display::ColorTransform colorTransformOf(const as3::Val& value);
    std::shared_ptr<display::BitmapData> bitmapDataOf(const as3::Val& value) const;
    /// A filter object's filter; false when \a value is not one.
    bool filterOf(const as3::Val& value, display::BitmapFilter& filter);
    /// The script object of a copy of \a filter.
    as3::Val newFilter(const display::BitmapFilter& filter);
    as3::Val newTextFormat(const display::TextFormat& format);
    as3::Val newByteArray(std::vector<uint8_t> bytes);

    /// Numbers of an array or vector.
    std::vector<double> numbers(const as3::Val& array);
    /// An array of numbers.
    as3::Val numberArray(const std::vector<double>& values);

    // Time and what waits for it.

    /// Milliseconds of movie time: a frame's worth more each frame, and
    /// the time spent within the frame now.
    double now();
    /// A frame starts: the clock moves on, timers due fire, loads finished
    /// are told of.
    void frameStarted();
    /// The time timers are run to as a frame starts.
    double timerTime();
    /// Runs \a task at the start of the next frame.
    void post(std::function<void()> task);
    /// The flash.utils.Timer objects running: they fire as the movie clock
    /// passes them, each kept while it runs.
    std::vector<as3::Val> timers;
    void runTimers();

    // Loading: files beside the movie, and the web through the host.

    /// Reads \a url (a path from the movie's folder, file:, or http(s)
    /// through MovieRuntime::fetch), and gives what came to \a done at the
    /// start of a frame after.
    void load(const MovieRuntime::Request& request, std::function<void(const MovieRuntime::Response&)> done);
    /// An image's pixels from its file's bytes (PNG built in; the rest
    /// through MovieRuntime::decodeImage); null when it cannot be read.
    std::shared_ptr<display::BitmapData> decodeImage(const std::vector<uint8_t>& bytes);

    // Library symbols as classes.

    /// The library symbol whose class is \a cls or one it extends, or null.
    const Symbol* symbolOf(const as3::ClassInfo* cls) const;
    /// The library sound or bitmap linked to \a cls or a class it extends.
    std::string soundOf(const as3::ClassInfo* cls) const;
    const Bitmap* bitmapOf(const as3::ClassInfo* cls) const;

    /// The main timeline's loaderInfo.
    as3::Val rootLoaderInfo();
    /// A new LoaderInfo: the movie's (\a root) or a Loader's.
    as3::Val makeLoaderInfo(bool root);
    /// A clip's script object made for it (a placed instance's), of its
    /// symbol's class, its constructor run.
    as3::Val clipObject(Clip& clip);
    /// Sets the variables a clip's class declares for the instances its
    /// timeline names to them (the frame now places them).
    void declareInstances(Clip& clip);
    /// Sets a component instance's parameters (its parametersAsXML) on its
    /// object, as its inspector gave them.
    void applyParameters(Clip& clip);

    /// The source of a class the movie needs, by qualified name: what the
    /// runtime's class path finds, or a class made for a library symbol
    /// that has no file.
    std::optional<as3::ClassSource> findSource(const std::string& qualifiedName);

    // Each package's classes (bind_*.cpp).
    void defineGeom();
    void defineEvents();
    void defineDisplay();
    void defineBitmaps();
    void defineFilters();
    void defineText();
    void defineUtils();
    void defineNet();
    void defineSystem();

    /// The current Mouse cursor wanted, and whether it is hidden.
    std::string mouseCursor = "auto";
    bool mouseHidden = false;

private:
    struct Kept
    {
        as3::Val wrapper;
        std::weak_ptr<display::DisplayObject> object;
    };
    std::vector<Kept> _kept;
    std::map<std::string, std::shared_ptr<as3::ClassInfo>> _classes;
    std::vector<std::function<void()>> _posted;
    /// False once the bindings are gone, for what the engine may call
    /// back after.
    std::shared_ptr<bool> _alive;
    /// Saves the SharedObjects scripts opened (as the movie ends).
    std::function<void()> _flushShared;
    double _frameTime = 0.0;
    double _lastNow = 0.0;
    uint64_t _frames = 0;
    std::chrono::steady_clock::time_point _frameStart;
    as3::Val _rootLoaderInfo;
};

/// The host state of \a self as \a T, or a TypeError naming \a what.
template <class T>
T& hostOf(as3::VM& vm, const as3::Val& self, const char* what)
{
    T* host = self.host<T>();
    if (!host)
        vm.throwError("TypeError", std::string("Error #1034: Type Coercion failed: ") + what + " expected.", 1034);
    return *host;
}

/// A display object's host state, or a TypeError.
DisplayHost& displayHost(as3::VM& vm, const as3::Val& self);

/// The error the display list methods throw for \a error.
void throwDisplayError(as3::VM& vm, display::DisplayError error);

/// A PNG's pixels: false when \a bytes are not a PNG it can read.
bool decodePng(const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint32_t>& argb);

/// The pixels of a library bitmap (premultiplied ARGB): its .dat, or the
/// image file through \a decode.
std::shared_ptr<display::BitmapData> libraryBitmap(const Bitmap& bitmap,
    const std::function<std::shared_ptr<display::BitmapData>(const std::vector<uint8_t>&)>& decode);

} // namespace script
} // namespace play
} // namespace fla
