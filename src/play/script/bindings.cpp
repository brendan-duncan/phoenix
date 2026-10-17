#include "native.h"

#include "../../data/bitmap.h"
#include "../../data/bitmap_decoder.h"
#include "../../data/symbol.h"
#include "../display/stage.h"

#include "abc/abc_domain.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace fla {
namespace play {
namespace script {

namespace {

using as3::Args;
using as3::Val;
using as3::VM;

std::string attributeOf(const std::vector<std::pair<std::string, std::string>>& attributes, const std::string& key)
{
    for (const auto& [name, value] : attributes)
        if (name == key)
            return value;
    return std::string();
}

/// Holds an engine event's script object while the event is about; as the
/// event goes, the object keeps the copy made of it.
struct EventHold
{
    Val value;
    std::weak_ptr<EventHost> host;
    ~EventHold()
    {
        if (std::shared_ptr<EventHost> held = host.lock())
            held->live = nullptr;
    }
};

bool isHttp(const std::string& url)
{
    const auto starts = [&url](const char* prefix) {
        const size_t n = std::strlen(prefix);
        if (url.size() < n)
            return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(static_cast<unsigned char>(url[i])) != prefix[i])
                return false;
        return true;
    };
    return starts("http://") || starts("https://");
}

/// %xx in a file URL's path, as the characters.
std::string unescapePath(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2])))
        {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else
            out += text[i];
    }
    return out;
}

} // namespace

// The host state of display objects.

DisplayHost::DisplayHost(Bindings& bindings, std::shared_ptr<display::DisplayObject> object, std::shared_ptr<Clip*> life)
    : bindings(bindings)
    , life(std::move(life))
{
    dispatcher = std::move(object);
}

as3::ValuePtr DisplayHost::get(const std::string& name)
{
    Clip* at = clip();
    // ActionScript 2's names, on a clip.
    if (at && !name.empty() && name[0] == '_')
        return bindings.runtime.property(*at, name);
    // The timeline's instances by name, as the class it makes declares
    // them: on the display list, or the clip even when a script took it off.
    display::DisplayObject* self = object();
    if (self && self->isContainer())
        if (std::shared_ptr<display::DisplayObject> child =
                static_cast<display::DisplayObjectContainer*>(self)->getChildByName(name))
            return bindings.wrap(child.get());
    if (at)
    {
        if (Clip* named = at->named(name))
            return bindings.clipObject(*named);
        // ActionScript 2's methods (hitTest, attachMovie...), which scripts
        // written for both still call.
        return bindings.runtime.property(*at, name);
    }
    return nullptr;
}

bool DisplayHost::set(const std::string& name, as3::ValuePtr value)
{
    Clip* at = clip();
    if (at && !name.empty() && name[0] == '_')
        return bindings.runtime.setProperty(*at, name, value);
    return false;
}

DisplayHost& displayHost(VM& vm, const Val& self)
{
    DisplayHost* host = self.host<DisplayHost>();
    if (!host || !host->dispatcher)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: DisplayObject expected.", 1034);
    return *host;
}

void throwDisplayError(VM& vm, display::DisplayError error)
{
    switch (error)
    {
    case display::DisplayError::None:
        return;
    case display::DisplayError::IndexOutOfRange:
        vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
    case display::DisplayError::NullChild:
        vm.throwError("TypeError", "Error #2007: Parameter child must be non-null.", 2007);
    case display::DisplayError::AddSelf:
        vm.throwError("ArgumentError", "Error #2024: An object cannot be added as a child of itself.", 2024);
    case display::DisplayError::NotAChild:
        vm.throwError("ArgumentError", "Error #2025: The supplied DisplayObject must be a child of the caller.", 2025);
    case display::DisplayError::AddAncestor:
        vm.throwError("ArgumentError",
            "Error #2150: An object cannot be added as a child to one of it's children (or children's children, etc.).",
            2150);
    }
}

// Bindings

Bindings::Bindings(MovieRuntime& runtime, VM& vm)
    : runtime(runtime)
    , vm(vm)
    , _alive(std::make_shared<bool>(true))
{
    _frameStart = std::chrono::steady_clock::now();
}

Bindings::~Bindings()
{
    *_alive = false;
    releaseAll();
}

void Bindings::install()
{
    vm.setClock([this]() { return now(); });
    vm.setClassResolver([this](const std::string& name) { return findSource(name); });
    vm.setIncludeResolver([this](const std::string& path, const std::string&) -> std::optional<std::string> {
        namespace fs = std::filesystem;
        std::ifstream in(fs::u8path(runtime.folder) / fs::u8path(path), std::ios::binary);
        if (!in)
            return std::nullopt;
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    });
    defineGeom();
    defineEvents();
    defineDisplay();
    defineBitmaps();
    defineFilters();
    defineText();
    defineUtils();
    defineNet();
    defineSystem();
}

std::shared_ptr<as3::ClassInfo> Bindings::classNamed(const std::string& name)
{
    const auto found = _classes.find(name);
    if (found != _classes.end())
        return found->second;
    std::shared_ptr<as3::ClassInfo> cls = vm.findClass(name);
    if (cls)
        _classes[name] = cls;
    return cls;
}

Val Bindings::instance(const std::string& cls, std::shared_ptr<as3::HostObject> host)
{
    Val made = vm.newInstance(classNamed(cls));
    made->host = std::move(host);
    return made;
}

Val Bindings::wrap(display::DisplayObject* object)
{
    if (!object)
        return Val::null();
    if (std::shared_ptr<void> script = object->script.lock())
        return Val(std::static_pointer_cast<as3::Value>(script));
    if (object->clip &&
        (object->kind() == display::ObjectKind::MovieClip || object->kind() == display::ObjectKind::SimpleButton))
        return clipObject(*object->clip);
    const char* cls = "flash.display.Shape";
    switch (object->kind())
    {
    case display::ObjectKind::Shape:
    case display::ObjectKind::TimelineArt:
        cls = "flash.display.Shape";
        break;
    case display::ObjectKind::Sprite:
        cls = "flash.display.Sprite";
        break;
    case display::ObjectKind::MovieClip:
        cls = "flash.display.MovieClip";
        break;
    case display::ObjectKind::SimpleButton:
        cls = "flash.display.SimpleButton";
        break;
    case display::ObjectKind::Bitmap:
        cls = "flash.display.Bitmap";
        break;
    case display::ObjectKind::TextField:
        cls = "flash.text.TextField";
        break;
    case display::ObjectKind::Stage:
        cls = "flash.display.Stage";
        break;
    }
    std::shared_ptr<display::DisplayObject> shared = object->shared();
    if (!shared)
        return Val::null();
    Val made = instance(cls, std::make_shared<DisplayHost>(*this, shared, nullptr));
    object->script = made.ref;
    keep(made, shared);
    return made;
}

Val Bindings::wrap(display::EventDispatcher* dispatcher)
{
    if (!dispatcher)
        return Val::null();
    if (auto* object = dynamic_cast<display::DisplayObject*>(dispatcher))
        return wrap(object);
    if (std::shared_ptr<void> script = dispatcher->script.lock())
        return Val(std::static_pointer_cast<as3::Value>(script));
    return Val::null();
}

Val Bindings::wrapEvent(display::Event& event)
{
    if (event.script)
        return static_cast<EventHold*>(event.script.get())->value;
    const char* cls = "flash.events.Event";
    switch (event.eventClass())
    {
    case display::Event::Class::Event:
        break;
    case display::Event::Class::Mouse:
        cls = "flash.events.MouseEvent";
        break;
    case display::Event::Class::Keyboard:
        cls = "flash.events.KeyboardEvent";
        break;
    case display::Event::Class::Focus:
        cls = "flash.events.FocusEvent";
        break;
    case display::Event::Class::Text:
        cls = "flash.events.TextEvent";
        break;
    }
    auto host = std::make_shared<EventHost>();
    host->live = &event;
    host->owned = std::shared_ptr<display::Event>(event.clone().release());
    Val made = instance(cls, host);
    auto hold = std::make_shared<EventHold>();
    hold->value = made;
    hold->host = host;
    event.script = hold;
    return made;
}

display::DisplayObject* Bindings::displayOf(const Val& value) const
{
    const DisplayHost* host = value.host<DisplayHost>();
    return host ? host->object() : nullptr;
}

std::shared_ptr<display::DisplayObject> Bindings::sharedDisplayOf(const Val& value) const
{
    const DisplayHost* host = value.host<DisplayHost>();
    return host ? host->shared() : nullptr;
}

display::EventDispatcher* Bindings::dispatcherOf(const Val& value) const
{
    const DispatcherHost* host = value.host<DispatcherHost>();
    return host ? host->dispatcher.get() : nullptr;
}

void Bindings::keep(const Val& wrapper, const std::shared_ptr<display::DisplayObject>& object)
{
    _kept.push_back(Kept{wrapper, object});
}

void Bindings::sweep()
{
    // What is held only by the other (the object by its script object's
    // host, the script object by this list) is let go, as a collector
    // would: off the display list, and in no script's hands.
    _kept.erase(std::remove_if(_kept.begin(), _kept.end(),
                    [](const Kept& kept) {
                        const std::shared_ptr<display::DisplayObject> object = kept.object.lock();
                        if (!object)
                            return true;
                        return object.use_count() <= 2 && kept.wrapper.ref.use_count() <= 1;
                    }),
        _kept.end());
}

void Bindings::releaseAll()
{
    // What scripts kept in SharedObjects is saved as the movie ends, as
    // Flash Player saves it.
    if (_flushShared)
    {
        std::function<void()> flush;
        flush.swap(_flushShared);
        flush();
    }
    // The listeners hold script functions that hold the objects again:
    // taken off, the two let each other go.
    for (const Kept& kept : _kept)
        if (const std::shared_ptr<display::DisplayObject> object = kept.object.lock())
            object->removeAllEventListeners();
    _kept.clear();
    timers.clear();
    _posted.clear();
    _rootLoaderInfo = Val();
    _classes.clear();
}

void Bindings::callListener(const Val& listener, display::Event& event)
{
    Val object = wrapEvent(event);
    if (EventHost* host = object.host<EventHost>())
    {
        host->target = wrap(event.target);
        host->currentTarget = wrap(event.currentTarget);
    }
    // A function a frame script declared runs with its timeline as this.
    Val self = Val::null();
    if (listener.isObject() && listener->function && listener->function->scope.isObject())
        self = listener->function->scope;
    ++runtime._scriptRuns;
    callSafely(listener, self, {object}, "listener for " + event.type);
}

bool Bindings::callSafely(const Val& function, const Val& self, const std::vector<Val>& args, const std::string& where)
{
    try
    {
        vm.callFunction(function, self, args);
        return true;
    }
    catch (as3::ScriptException& exception)
    {
        report(exception, where);
    }
    catch (const std::exception& failed)
    {
        runtime.tell(where + ": " + failed.what(), runtime._source, 0);
    }
    return false;
}

void Bindings::report(const as3::ScriptException& exception, const std::string& where)
{
    std::string message;
    std::string stack;
    try
    {
        message = vm.toString(exception.value);
        if (exception.value.isObject() && exception.value->cls &&
            exception.value->cls->isSubclassOf(vm.builtins().error.get()))
            stack = vm.getProperty(exception.value, "$stack").toString();
    }
    catch (...)
    {
        message = exception.value.toString();
    }
    if (stack == "undefined")
        stack.clear();
    // Where in the movie's compiled scripts it came from.
    if (runtime._abcDomain)
    {
        std::string compiled = runtime._abcDomain->takeErrorStack();
        if (stack.empty())
            stack = std::move(compiled);
    }
    std::string text = where;
    if (exception.line >= 0)
        text += ", line " + std::to_string(exception.line);
    text += ": " + message;
    if (!stack.empty())
        text += "\n" + stack;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    runtime.tell(text, runtime._source, std::max(0, exception.line));
}

bool Bindings::dispatchScriptEvent(display::EventDispatcher& dispatcher, Val event)
{
    EventHost* host = event.host<EventHost>();
    if (!host)
        return false;
    // One sent before goes again as its clone, as Flash Player sends it:
    // the script's clone, which a subclass of Event may have.
    if (host->event().target || !host->target.isNullish())
    {
        event = vm.callMethod(event, "clone");
        host = event.host<EventHost>();
        if (!host)
            vm.throwError("TypeError", "Error #1034: Type Coercion failed: clone() did not make an Event.", 1034);
    }
    std::shared_ptr<display::EventDispatcher> keep = dispatcher.weak_from_this().lock();
    display::Event& sent = host->event();
    auto hold = std::make_shared<EventHold>();
    hold->value = event;
    hold->host = std::static_pointer_cast<EventHost>(event->host);
    display::Event* wasLive = host->live;
    sent.script = hold;
    const bool result = dispatcher.dispatchEvent(sent);
    sent.script.reset();
    host->live = wasLive;
    host->target = wrap(&dispatcher);
    host->currentTarget = Val::null();
    return result;
}

bool Bindings::send(const Val& target, const std::string& cls, const std::string& type)
{
    display::EventDispatcher* dispatcher = dispatcherOf(target);
    if (!dispatcher)
        return false;
    return dispatchScriptEvent(*dispatcher, makeEvent(cls, type));
}

std::shared_ptr<DisplayHost> Bindings::attach(const Val& self, std::shared_ptr<display::DisplayObject> object)
{
    auto host = std::make_shared<DisplayHost>(*this, object, nullptr);
    self->host = host;
    object->script = self.ref;
    keep(self, object);
    return host;
}

void Bindings::attachSymbol(const Val& self, const Symbol& symbol)
{
    Clip* made = runtime.makeClip(nullptr, symbol, std::string());
    keepFirstFrame(*made, self->cls.get());
    made->object = self;
    made->classed = true;
    self->host = std::make_shared<DisplayHost>(*this, made->display, made->life);
    made->display->script = self.ref;
    // What its first frame places is there before its constructor goes on
    // (a component's avatar); those clips' first frames are entered after.
    std::vector<Clip*> entered;
    runtime.syncChildren(*made, entered);
    for (Clip* child : entered)
        runtime._madeToEnter.push_back(child);
}

void Bindings::keepFirstFrame(Clip& clip, const as3::ClassInfo* cls)
{
    // A symbol whose class is a Sprite and not a MovieClip (a component's)
    // has no timeline to play: its first frame is all it shows.
    const std::shared_ptr<as3::ClassInfo> movieClip = classNamed("flash.display.MovieClip");
    if (clip.button || !cls || !movieClip || cls->isSubclassOf(movieClip.get()))
        return;
    clip.playing = false;
    clip.length = 1;
}

void Bindings::goToNow(Clip& clip, int frame, bool play)
{
    const int was = clip.frame;
    runtime.goTo(clip, frame, play);
    if (clip.frame == was)
        return;
    // What the frame places, there now; the new clips' first frames and
    // this frame's scripts once the script running is done.
    std::vector<Clip*> entered;
    runtime.syncChildren(clip, entered);
    for (Clip* made : entered)
    {
        if (made->gone)
            continue;
        clipObject(*made);
        runtime._madeToEnter.push_back(made);
    }
    declareInstances(clip);
}

display::EventDispatcher::Callback Bindings::listenerCallback(const Val& listener)
{
    const std::shared_ptr<bool> alive = _alive;
    Val function = listener;
    return [this, alive, function](display::Event& event) {
        if (*alive)
            callListener(function, event);
    };
}

// Time

double Bindings::now()
{
    const double within =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _frameStart).count();
    _lastNow = std::max(_lastNow, _frameTime + within);
    return _lastNow;
}

void Bindings::frameStarted()
{
    // A frame's worth of movie time on, however long the last one took:
    // a movie played without a window times as it would at its rate.
    const double period = 1000.0 / std::max(1.0, runtime._frameRate);
    _frameTime = std::max(_frameTime + period, _lastNow);
    _frameStart = std::chrono::steady_clock::now();
    _lastNow = _frameTime;

    std::vector<std::function<void()>> tasks;
    tasks.swap(_posted);
    for (const std::function<void()>& task : tasks)
        task();

    // setTimeout and setInterval.
    vm.clearError();
    vm.runTimers(timerTime());
    if (vm.hasError())
    {
        runtime.report("timer");
        vm.clearError();
    }
    runTimers();

    if (++_frames % 30 == 0)
        sweep();
}

double Bindings::timerTime()
{
    // What is due by the middle of the frame starting is due now: timers
    // fire at frame boundaries, as Flash Player fires them.
    return now() + 500.0 / std::max(1.0, runtime._frameRate);
}

void Bindings::post(std::function<void()> task)
{
    _posted.push_back(std::move(task));
}

// Loading

void Bindings::load(const MovieRuntime::Request& request, std::function<void(const MovieRuntime::Response&)> done)
{
    const std::shared_ptr<bool> alive = _alive;
    if (isHttp(request.url))
    {
        if (!runtime.fetch)
        {
            MovieRuntime::Response failed;
            failed.error = "Error #2032: Stream Error. URL: " + request.url;
            post([done, failed]() { done(failed); });
            return;
        }
        runtime.fetch(request, [this, alive, done](const MovieRuntime::Response& response) {
            if (*alive)
                post([done, response]() { done(response); });
        });
        return;
    }
    namespace fs = std::filesystem;
    std::string path = request.url;
    if (path.compare(0, 8, "file:///") == 0)
        path = unescapePath(path.substr(8));
    else if (path.compare(0, 7, "file://") == 0)
        path = unescapePath(path.substr(7));
    // A query has no meaning for a file.
    const size_t query = path.find('?');
    if (query != std::string::npos)
        path = path.substr(0, query);
    std::replace(path.begin(), path.end(), '\\', '/');
    fs::path file = fs::u8path(path);
    if (!file.is_absolute())
        file = fs::u8path(runtime.folder) / file;
    MovieRuntime::Response response;
    std::ifstream in(file, std::ios::binary);
    if (in)
    {
        response.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        response.ok = true;
    }
    else
        response.error = "Error #2032: Stream Error. URL: " + request.url;
    post([done, response]() { done(response); });
}

std::shared_ptr<display::BitmapData> Bindings::decodeImage(const std::vector<uint8_t>& bytes)
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    if (decodePng(bytes, width, height, pixels))
    {
        auto data = std::make_shared<display::BitmapData>(width, height, true, 0);
        if (data->disposed())
            return nullptr;
        uint32_t* bits = data->bits();
        for (size_t i = 0; i < pixels.size(); ++i)
            bits[i] = display::premultiplied(pixels[i]);
        data->changed();
        return data;
    }
    if (runtime.decodeImage && runtime.decodeImage(bytes, width, height, pixels) && width > 0 && height > 0)
    {
        auto data = std::make_shared<display::BitmapData>(width, height, true, 0);
        if (data->disposed() || pixels.size() < static_cast<size_t>(width) * height)
            return nullptr;
        std::copy(pixels.begin(), pixels.begin() + static_cast<size_t>(width) * height, data->bits());
        data->changed();
        return data;
    }
    return nullptr;
}

std::shared_ptr<display::BitmapData> libraryBitmap(const Bitmap& bitmap,
    const std::function<std::shared_ptr<display::BitmapData>(const std::vector<uint8_t>&)>& decode)
{
    DecodedBitmap decoded;
    const std::vector<uint8_t>& dat = bitmap.datData.get();
    if (!dat.empty() && isLosslessBitmapData(dat) && decodeLosslessBitmap(dat, decoded) && decoded.width > 0)
    {
        auto data = std::make_shared<display::BitmapData>(decoded.width, decoded.height, true, 0);
        if (!data->disposed() && decoded.pixels.size() >= static_cast<size_t>(decoded.width) * decoded.height)
        {
            std::copy(decoded.pixels.begin(), decoded.pixels.begin() + static_cast<size_t>(decoded.width) * decoded.height,
                data->bits());
            data->changed();
            return data;
        }
    }
    if (decode)
    {
        const std::vector<uint8_t>& image = bitmap.imageData.get();
        if (!image.empty())
            if (std::shared_ptr<display::BitmapData> data = decode(image))
                return data;
        if (!dat.empty() && isJpegData(dat))
            return decode(dat);
    }
    return nullptr;
}

// Library symbols as classes

const Symbol* Bindings::symbolOf(const as3::ClassInfo* cls) const
{
    for (const as3::ClassInfo* at = cls; at; at = at->base.get())
    {
        if (at->isNative)
            break;
        for (const Symbol* symbol : runtime._library)
            if (symbol && attributeOf(symbol->extraAttributes, "linkageClassName") == at->name)
                return symbol;
    }
    return nullptr;
}

std::string Bindings::soundOf(const as3::ClassInfo* cls) const
{
    for (const as3::ClassInfo* at = cls; at; at = at->base.get())
    {
        if (at->isNative)
            break;
        for (const std::string& linked : runtime._soundClasses)
            if (linked == at->name)
                return linked;
    }
    return std::string();
}

const Bitmap* Bindings::bitmapOf(const as3::ClassInfo* cls) const
{
    for (const as3::ClassInfo* at = cls; at; at = at->base.get())
    {
        if (at->isNative)
            break;
        for (const Bitmap* bitmap : runtime._bitmaps)
            if (bitmap && attributeOf(bitmap->extraAttributes, "linkageClassName") == at->name)
                return bitmap;
    }
    return nullptr;
}

std::optional<as3::ClassSource> Bindings::findSource(const std::string& qualifiedName)
{
    const size_t dot = qualifiedName.find_last_of('.');
    const std::string shortName = dot == std::string::npos ? qualifiedName : qualifiedName.substr(dot + 1);
    const std::string package = dot == std::string::npos ? std::string() : qualifiedName.substr(0, dot);
    // The classes the runtime was given, by name.
    for (const std::string& key : {qualifiedName, shortName})
    {
        const auto given = runtime._classSources.find(key);
        if (given != runtime._classSources.end())
        {
            // A short name matches only a class of that package.
            if (key == shortName && key != qualifiedName &&
                given->second.find("package " + package) == std::string::npos)
                continue;
            return as3::ClassSource{given->second, shortName + ".as"};
        }
    }
    if (runtime.findClass)
        if (std::optional<ClassFile> file = runtime.findClass(qualifiedName))
            return as3::ClassSource{file->source, file->fileName};
    // A library symbol with a class and no file for it: the class the
    // authoring tool makes, of the symbol's base class.
    std::string base;
    for (const Symbol* symbol : runtime._library)
    {
        if (!symbol || attributeOf(symbol->extraAttributes, "linkageClassName") != qualifiedName)
            continue;
        // A component's class comes with the component.
        if (symbol->itemElement == "DOMComponentItem")
            return std::nullopt;
        base = attributeOf(symbol->extraAttributes, "linkageBaseClass");
        if (base.empty())
            base = symbol->symbolType == SymbolType::Button ? "flash.display.SimpleButton" : "flash.display.MovieClip";
        break;
    }
    if (base.empty())
        for (const std::string& linked : runtime._soundClasses)
            if (linked == qualifiedName)
                base = "flash.media.Sound";
    if (base.empty())
        for (const Bitmap* bitmap : runtime._bitmaps)
            if (bitmap && attributeOf(bitmap->extraAttributes, "linkageClassName") == qualifiedName)
            {
                base = attributeOf(bitmap->extraAttributes, "linkageBaseClass");
                if (base.empty())
                    base = "flash.display.BitmapData";
            }
    if (base.empty())
        return std::nullopt;
    std::string source = "package " + package + " {\n";
    if (base == "flash.display.BitmapData")
        source += "public dynamic class " + shortName + " extends " + base + " {\n" + "public function " + shortName +
            "(width:int = 0, height:int = 0) { super(width, height); }\n}\n";
    else
        source += "public dynamic class " + shortName + " extends " + base + " {}\n";
    source += "}\n";
    return as3::ClassSource{source, shortName + ".as"};
}

Val Bindings::clipObject(Clip& clip)
{
    if (clip.object)
        return Val(clip.object);
    const bool isRoot = &clip == runtime._root.get();
    std::string name;
    if (isRoot)
        name = runtime._documentClass;
    else if (clip.symbol)
        name = attributeOf(clip.symbol->extraAttributes, "linkageClassName");
    const std::string fallback = clip.button ? "flash.display.SimpleButton" : "flash.display.MovieClip";
    std::shared_ptr<as3::ClassInfo> cls;
    if (!name.empty())
    {
        try
        {
            cls = vm.findClass(name);
            if (!cls)
                runtime.tell((isRoot ? "The document class " : "The class ") + name + " was not found.", nullptr, 0);
        }
        catch (as3::ScriptException& exception)
        {
            report(exception, name);
        }
    }
    // Of a kind its display object can be.
    const std::shared_ptr<as3::ClassInfo> wanted = classNamed(clip.button ? fallback : "flash.display.Sprite");
    if (!cls || !cls->isSubclassOf(wanted.get()))
        cls = classNamed(fallback);
    Val made = vm.newInstance(cls);
    keepFirstFrame(clip, cls.get());
    made->host = std::make_shared<DisplayHost>(*this, clip.display, clip.life);
    clip.object = made;
    clip.classed = true;
    clip.display->script = made.ref;
    declareInstances(clip);
    if (cls->constructor.isObject() || !cls->isNative)
    {
        const DOMElement* wasSource = runtime._source;
        Clip* was = runtime._current;
        runtime._current = &clip;
        try
        {
            vm.runConstructor(made, Args());
        }
        catch (as3::ScriptException& exception)
        {
            report(exception, cls->name + " constructor");
        }
        catch (const std::exception& failed)
        {
            runtime.tell(cls->name + " constructor: " + failed.what(), nullptr, 0);
        }
        runtime._current = was;
        runtime._source = wasSource;
        declareInstances(clip);
    }
    applyParameters(clip);
    return made;
}

void Bindings::applyParameters(Clip& clip)
{
    const std::string parameters = runtime.componentParameters(clip.placed);
    if (parameters.empty() || !clip.object)
        return;
    tinyxml2::XMLDocument xml;
    const std::string wrapped = "<parameters>" + parameters + "</parameters>";
    if (xml.Parse(wrapped.c_str(), wrapped.size()) != tinyxml2::XML_SUCCESS)
        return;
    const Val self(clip.object);
    const auto set = [&](const std::string& name, const Val& value) {
        try
        {
            vm.setProperty(self, name, value);
        }
        catch (as3::ScriptException& exception)
        {
            report(exception, (clip.name.empty() ? clip.key : clip.name) + "." + name);
        }
    };
    // As the player sets a component's parameters from its inspector: each
    // in turn, by its type, between componentInspectorSetting true and false.
    set("componentInspectorSetting", boolean(true));
    for (const tinyxml2::XMLElement* property = xml.RootElement()->FirstChildElement("property"); property;
         property = property->NextSiblingElement("property"))
    {
        const tinyxml2::XMLElement* inspectable = property->FirstChildElement("Inspectable");
        if (!inspectable)
            continue;
        const char* variable = inspectable->Attribute("variable");
        const char* named = inspectable->Attribute("name");
        const std::string name = variable && *variable ? variable : named ? named : "";
        const std::string value = inspectable->Attribute("defaultValue") ? inspectable->Attribute("defaultValue") : "";
        const std::string type = inspectable->Attribute("type") ? inspectable->Attribute("type") : "String";
        if (name.empty())
            continue;
        if (type == "Boolean")
            set(name, boolean(value == "true"));
        else if (type == "Number")
            set(name, number(as3::stringToNumber(value)));
        else if (type == "Color")
            set(name, number(static_cast<double>(std::strtoul(value.c_str() + (value[0] == '#' ? 1 : 0), nullptr, 16))));
        else if (type == "Array")
        {
            std::vector<Val> items;
            std::stringstream list(value);
            std::string item;
            while (std::getline(list, item, ','))
                items.push_back(text(item));
            if (!value.empty())
                set(name, vm.newArray(std::move(items)));
        }
        else if (type == "Collection")
            continue;
        else
            set(name, text(value));
    }
    set("componentInspectorSetting", boolean(false));
}

void Bindings::declareInstances(Clip& clip)
{
    if (!clip.object || !clip.display || !clip.display->isContainer())
        return;
    const Val self(clip.object);
    const as3::ClassInfo* cls = self->cls.get();
    if (!cls || cls->isNative)
        return;
    for (const auto& child : static_cast<display::DisplayObjectContainer*>(clip.display.get())->children())
    {
        if (!child || !child->timeline || child->name().empty())
            continue;
        const as3::Trait* trait = cls->findTrait(child->name());
        if (!trait || trait->kind != as3::TraitKind::Slot || trait->slot >= self->slots.size())
            continue;
        if (!self->slots[trait->slot].isNullish())
            continue;
        try
        {
            vm.setProperty(self, child->name(), wrap(child.get()));
        }
        catch (as3::ScriptException& exception)
        {
            report(exception, cls->name + "." + child->name());
        }
    }
}

Val Bindings::rootLoaderInfo()
{
    if (_rootLoaderInfo.isUndefined())
        _rootLoaderInfo = makeLoaderInfo(true);
    return _rootLoaderInfo;
}

// Values

Val Bindings::newByteArray(std::vector<uint8_t> bytes)
{
    auto host = std::make_shared<ByteArrayHost>();
    host->bytes = std::move(bytes);
    return instance("flash.utils.ByteArray", host);
}

std::vector<double> Bindings::numbers(const Val& array)
{
    std::vector<double> out;
    if (!array.isObject())
        return out;
    for (const Val& element : array->elements)
        out.push_back(vm.toNumber(element));
    return out;
}

Val Bindings::numberArray(const std::vector<double>& values)
{
    std::vector<Val> elements;
    elements.reserve(values.size());
    for (const double value : values)
        elements.push_back(Val::number(value));
    return vm.newArray(std::move(elements));
}

} // namespace script
} // namespace play
} // namespace fla
