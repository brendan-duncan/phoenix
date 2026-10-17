#include "scripts.h"

#include "../../data/frame.h"
#include "../../data/layer.h"
#include "../../data/symbol.h"
#include "../../data/action_script.h"
#include "../../data/timeline.h"
#include "../display/display_object.h"
#include "../display/events.h"
#include "../display/stage.h"
#include "../movie_runtime.h"

#include <quickjs.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <sstream>
#include <vector>

namespace fla::play::canvas {

namespace {

using Clock = std::chrono::steady_clock;

/// How long the scripts may run at a time before they are stopped (a loop
/// that never ends, say).
constexpr auto kTimeLimit = std::chrono::seconds(5);

/// What the stage's number is to the scripts, and its parent's.
constexpr int kStage = -1;
constexpr int kNone = -2;

std::string text(JSContext* ctx, JSValueConst value)
{
    size_t length = 0;
    const char* chars = JS_ToCStringLen(ctx, &length, value);
    if (!chars)
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return std::string();
    }
    std::string out(chars, length);
    JS_FreeCString(ctx, chars);
    return out;
}

double number(JSContext* ctx, JSValueConst value, double otherwise = 0.0)
{
    double out = otherwise;
    if (JS_ToFloat64(ctx, &out, value) < 0)
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return otherwise;
    }
    return out;
}

int integer(JSContext* ctx, JSValueConst value)
{
    const double n = number(ctx, value, -1.0);
    return std::isfinite(n) ? static_cast<int>(n) : -1;
}

JSValueConst arg(int argc, JSValueConst* argv, int i)
{
    return i < argc ? argv[i] : JS_UNDEFINED;
}

/// The display event types CreateJS's mouse and key events are, on an
/// object or the stage; empty for one the scripts make themselves (tick,
/// pressmove, their own).
const char* displayType(const std::string& type, bool stage)
{
    static const std::pair<const char*, const char*> types[] = {{"click", display::events::kClick},
        {"dblclick", display::events::kDoubleClick}, {"mousedown", display::events::kMouseDown},
        {"mouseup", display::events::kMouseUp}, {"mouseover", display::events::kMouseOver},
        {"mouseout", display::events::kMouseOut}, {"rollover", display::events::kRollOver},
        {"rollout", display::events::kRollOut}};
    for (const auto& [js, shown] : types)
        if (type == js)
            return shown;
    if (stage && type == "keydown")
        return display::events::kKeyDown;
    if (stage && type == "keyup")
        return display::events::kKeyUp;
    return nullptr;
}

/// The keyframe of \a layer showing at \a index, or null.
const Frame* keyframeAt(const Layer& layer, int index)
{
    for (const Frame* frame : layer.frames)
        if (frame && frame->index <= index && index < frame->index + std::max(1, frame->duration))
            return frame;
    return nullptr;
}

/// The labels of a timeline, by frame (from 0).
std::vector<std::pair<std::string, int>> labelsOf(const Timeline* timeline)
{
    std::vector<std::pair<std::string, int>> labels;
    for (const Layer* layer : timeline ? timeline->layers : std::vector<Layer*>())
        for (const Frame* frame : layer ? layer->frames : std::vector<Frame*>())
            if (frame && !frame->name.empty() && frame->labelType != "comment" && frame->labelType != "anchor")
                labels.emplace_back(frame->name, frame->index);
    std::stable_sort(labels.begin(), labels.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return labels;
}

/// Where an error was, from its stack: the first frame in a script of the
/// movie (not the CreateJS classes), as "file:line:column".
void locate(const std::string& stack, const std::map<std::string, const DOMElement*>& sources, std::string& file,
    int& line)
{
    std::istringstream lines(stack);
    std::string entry;
    while (std::getline(lines, entry))
    {
        const size_t at = entry.find("at ");
        if (at == std::string::npos)
            continue;
        std::string place = entry.substr(at + 3);
        const size_t open = place.find('(');
        if (open != std::string::npos && !place.empty() && place.back() == ')')
            place = place.substr(open + 1, place.size() - open - 2);
        const size_t column = place.rfind(':');
        if (column == std::string::npos || column == 0)
            continue;
        const size_t lineAt = place.rfind(':', column - 1);
        if (lineAt == std::string::npos)
            continue;
        const std::string name = place.substr(0, lineAt);
        if (!sources.count(name))
            continue;
        file = name;
        line = std::atoi(place.c_str() + lineAt + 1);
        return;
    }
}

} // namespace

/// The native functions the CreateJS classes are made over, as __phoenix's
/// methods: a clip's properties and frames, its children, listening for
/// the display list's events, the stage, sounds and the output.
struct Native
{
    static Scripts& self(JSContext* ctx) { return *static_cast<Scripts*>(JS_GetContextOpaque(ctx)); }
    static Clip* clip(JSContext* ctx, JSValueConst id) { return self(ctx).clipOf(integer(ctx, id)); }

    static int interrupt(JSRuntime*, void* opaque)
    {
        return Clock::now() > static_cast<Scripts*>(opaque)->_deadline ? 1 : 0;
    }

    static JSValue time(JSContext* ctx, JSValueConst, int, JSValueConst*)
    {
        return JS_NewFloat64(ctx, self(ctx)._time);
    }

    static JSValue frameRate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (argc > 0 && !JS_IsUndefined(argv[0]))
        {
            const double rate = number(ctx, argv[0], runtime._frameRate);
            if (rate > 0.0 && rate <= 1000.0 && rate != runtime._frameRate)
            {
                runtime._frameRate = rate;
                runtime._stage->setFrameRate(rate);
                if (runtime.frameRateChanged)
                    runtime.frameRateChanged(rate);
            }
        }
        return JS_NewFloat64(ctx, runtime._frameRate);
    }

    static JSValue root(JSContext* ctx, JSValueConst, int, JSValueConst*)
    {
        Scripts& s = self(ctx);
        return JS_NewInt32(ctx, s.idOf(*s._runtime._root));
    }

    static JSValue alive(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        return JS_NewBool(ctx, clip(ctx, arg(argc, argv, 0)) != nullptr);
    }

    static JSValue get(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        const std::string name = text(ctx, arg(argc, argv, 1));
        if (!c || !c->display)
            return JS_UNDEFINED;
        display::DisplayObject& d = *c->display;
        if (name == "x")
            return JS_NewFloat64(ctx, d.x());
        if (name == "y")
            return JS_NewFloat64(ctx, d.y());
        if (name == "rotation")
            return JS_NewFloat64(ctx, d.rotation());
        if (name == "scaleX")
            return JS_NewFloat64(ctx, d.scaleX());
        if (name == "scaleY")
            return JS_NewFloat64(ctx, d.scaleY());
        if (name == "alpha")
            return JS_NewFloat64(ctx, d.alpha());
        if (name == "visible")
            return JS_NewBool(ctx, d.visible());
        if (name == "currentFrame")
            return JS_NewInt32(ctx, c->frame);
        if (name == "totalFrames")
            return JS_NewInt32(ctx, c->length);
        if (name == "paused")
            return JS_NewBool(ctx, !c->playing);
        if (name == "name")
        {
            // The main timeline has none, as exportRoot has none.
            const bool root = c == self(ctx)._runtime._root.get();
            return c->name.empty() || root ? JS_NULL : JS_NewString(ctx, c->name.c_str());
        }
        if (name == "mouseX")
            return JS_NewFloat64(ctx, d.mouseX());
        if (name == "mouseY")
            return JS_NewFloat64(ctx, d.mouseY());
        if (name == "mouseEnabled")
            return JS_NewBool(ctx, !d.isInteractive() || static_cast<display::InteractiveObject&>(d).mouseEnabled);
        if (name == "mouseChildren")
            return JS_NewBool(ctx, !d.isContainer() || static_cast<display::DisplayObjectContainer&>(d).mouseChildren);
        if (name == "numChildren")
            return JS_NewInt32(ctx, d.isContainer() ? static_cast<display::DisplayObjectContainer&>(d).numChildren() : 0);
        if (name == "cursor")
        {
            const bool hand = d.kind() == display::ObjectKind::MovieClip && static_cast<display::Sprite&>(d).buttonMode;
            return hand ? JS_NewString(ctx, "pointer") : JS_NULL;
        }
        if (name == "currentLabel")
        {
            std::string label;
            for (const auto& [named, frame] : labelsOf(c->timeline))
                if (frame <= c->frame)
                    label = named;
            return label.empty() ? JS_NULL : JS_NewString(ctx, label.c_str());
        }
        if (name == "labels")
        {
            JSValue out = JS_NewArray(ctx);
            uint32_t i = 0;
            for (const auto& [named, frame] : labelsOf(c->timeline))
            {
                JSValue label = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, label, "label", JS_NewString(ctx, named.c_str()));
                JS_SetPropertyStr(ctx, label, "position", JS_NewInt32(ctx, frame));
                JS_SetPropertyUint32(ctx, out, i++, label);
            }
            return out;
        }
        return JS_UNDEFINED;
    }

    static JSValue set(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        const std::string name = text(ctx, arg(argc, argv, 1));
        JSValueConst value = arg(argc, argv, 2);
        if (!c || !c->display)
            return JS_FALSE;
        display::DisplayObject& d = *c->display;
        const auto finite = [&](void (display::DisplayObject::*setter)(double)) {
            const double n = number(ctx, value, NAN);
            if (std::isfinite(n))
                (d.*setter)(n);
        };
        if (name == "x")
            finite(&display::DisplayObject::setX);
        else if (name == "y")
            finite(&display::DisplayObject::setY);
        else if (name == "rotation")
            finite(&display::DisplayObject::setRotation);
        else if (name == "scaleX")
            finite(&display::DisplayObject::setScaleX);
        else if (name == "scaleY")
            finite(&display::DisplayObject::setScaleY);
        else if (name == "alpha")
            finite(&display::DisplayObject::setAlpha);
        else if (name == "visible")
            d.setVisible(JS_ToBool(ctx, value) > 0);
        else if (name == "paused")
            c->playing = JS_ToBool(ctx, value) <= 0;
        else if (name == "name")
        {
            c->name = JS_IsNull(value) || JS_IsUndefined(value) ? std::string() : text(ctx, value);
            d.setName(c->name);
        }
        else if (name == "mouseEnabled" && d.isInteractive())
            static_cast<display::InteractiveObject&>(d).mouseEnabled = JS_ToBool(ctx, value) > 0;
        else if (name == "mouseChildren" && d.isContainer())
            static_cast<display::DisplayObjectContainer&>(d).mouseChildren = JS_ToBool(ctx, value) > 0;
        else if (name == "cursor" && d.kind() == display::ObjectKind::MovieClip)
            static_cast<display::Sprite&>(d).buttonMode = JS_IsString(value) && text(ctx, value) == "pointer";
        else
            return JS_FALSE;
        if (self(ctx)._runtime.changed)
            self(ctx)._runtime.changed();
        return JS_TRUE;
    }

    /// go(id, frame or label, play): false when there is no such frame.
    static JSValue go(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        JSValueConst target = arg(argc, argv, 1);
        const bool play = JS_ToBool(ctx, arg(argc, argv, 2)) > 0;
        if (!c)
            return JS_FALSE;
        int frame = -1;
        if (JS_IsNumber(target))
            frame = integer(ctx, target);
        else if (JS_IsString(target))
        {
            const std::string label = text(ctx, target);
            for (const auto& [named, at] : labelsOf(c->timeline))
                if (named == label)
                {
                    frame = at;
                    break;
                }
        }
        if (frame < 0)
            return JS_FALSE;
        frame = std::min(frame, std::max(0, c->length - 1));
        // As CreateJS moves: what the frame places is there at once, its
        // script runs once the script running is done.
        MovieRuntime& runtime = self(ctx)._runtime;
        const int was = c->frame;
        runtime.goTo(*c, frame, play);
        if (c->frame != was)
        {
            std::vector<Clip*> entered;
            runtime.syncChildren(*c, entered);
            for (Clip* made : entered)
                if (!made->gone)
                    runtime._madeToEnter.push_back(made);
        }
        if (runtime.changed)
            runtime.changed();
        return JS_TRUE;
    }

    static JSValue play(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        if (Clip* c = clip(ctx, arg(argc, argv, 0)))
            c->playing = JS_ToBool(ctx, arg(argc, argv, 1)) > 0;
        return JS_UNDEFINED;
    }

    /// child(id, name): the clip of that instance name in it; -3 for a text
    /// field; kNone for neither.
    static JSValue child(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        const std::string name = text(ctx, arg(argc, argv, 1));
        if (!c || name.empty())
            return JS_NewInt32(ctx, kNone);
        if (Clip* named = c->named(name); named && !named->gone)
            return JS_NewInt32(ctx, self(ctx).idOf(*named));
        if (self(ctx)._runtime.textField(*c, name))
            return JS_NewInt32(ctx, -3);
        return JS_NewInt32(ctx, kNone);
    }

    /// text(id, name[, value]): a text field's text, set when given.
    static JSValue fieldText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        const std::string name = text(ctx, arg(argc, argv, 1));
        const StaticText* field = c ? self(ctx)._runtime.textField(*c, name) : nullptr;
        if (!field)
            return JS_UNDEFINED;
        if (argc > 2)
            self(ctx)._runtime.setFieldText(*c, *field, text(ctx, argv[2]));
        return JS_NewString(ctx, self(ctx)._runtime.fieldText(*c, *field).c_str());
    }

    /// parent(id): its parent's number, kStage for the main timeline's,
    /// kNone for one off the stage.
    static JSValue parent(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        Scripts& s = self(ctx);
        if (!c)
            return JS_NewInt32(ctx, kNone);
        if (c == s._runtime._root.get())
            return JS_NewInt32(ctx, kStage);
        if (c->parent && c->display && c->display->parent())
            return JS_NewInt32(ctx, s.idOf(*c->parent));
        return JS_NewInt32(ctx, kNone);
    }

    /// The clips in it, bottom first.
    static JSValue children(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        JSValue out = JS_NewArray(ctx);
        if (!c || !c->display || !c->display->isContainer())
            return out;
        uint32_t i = 0;
        for (const auto& object : static_cast<display::DisplayObjectContainer&>(*c->display).children())
            if (object && object->clip && !object->clip->gone)
                JS_SetPropertyUint32(ctx, out, i++, JS_NewInt32(ctx, self(ctx).idOf(*object->clip)));
        return out;
    }

    static JSValue listen(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        self(ctx).listen(integer(ctx, arg(argc, argv, 0)), text(ctx, arg(argc, argv, 1)),
            JS_ToBool(ctx, arg(argc, argv, 2)) > 0);
        return JS_UNDEFINED;
    }

    /// make(name): a new clip of the library symbol \a name links to (or
    /// is named, as lib names them), off the stage; -1 for none.
    static JSValue make(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Scripts& s = self(ctx);
        const std::string name = text(ctx, arg(argc, argv, 0));
        const Symbol* symbol = s._runtime.linked(name);
        for (const Symbol* each : s._runtime._library)
        {
            if (symbol || !each)
                break;
            // lib's name for a symbol: its name without its folders, and
            // with only what a JavaScript name can have.
            std::string base = each->name.substr(each->name.find_last_of('/') + 1);
            base.erase(std::remove_if(base.begin(), base.end(),
                           [](char ch) {
                               return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$');
                           }),
                base.end());
            if (base == name)
                symbol = each;
        }
        if (!symbol || symbol->symbolType == SymbolType::Graphic)
            return JS_NewInt32(ctx, kNone);
        Clip* made = s._runtime.makeClip(nullptr, *symbol, std::string());
        return JS_NewInt32(ctx, s.idOf(*made));
    }

    /// add(parent, child, index): the child put in the parent's display
    /// list, on top or at index.
    static JSValue add(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* to = clip(ctx, arg(argc, argv, 0));
        Clip* c = clip(ctx, arg(argc, argv, 1));
        if (!to || !c || !to->display || !to->display->isContainer() || !c->display || to == c)
            return JS_FALSE;
        for (const Clip* at = to; at; at = at->parent)
            if (at == c)
                return JS_FALSE;
        auto& container = static_cast<display::DisplayObjectContainer&>(*to->display);
        const int index = argc > 2 && !JS_IsUndefined(argv[2]) ? integer(ctx, argv[2]) : -1;
        if (index >= 0 && index <= container.numChildren())
            container.addChildAt(c->display, index);
        else
            container.addChild(c->display);
        self(ctx)._runtime.placeMadeClips();
        if (self(ctx)._runtime.changed)
            self(ctx)._runtime.changed();
        return JS_TRUE;
    }

    static JSValue remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* from = clip(ctx, arg(argc, argv, 0));
        Clip* c = clip(ctx, arg(argc, argv, 1));
        if (!from || !c || !from->display || !c->display || c->display->parent() != from->display.get())
            return JS_FALSE;
        static_cast<display::DisplayObjectContainer&>(*from->display).removeChild(c->display.get());
        self(ctx)._runtime.placeMadeClips();
        if (self(ctx)._runtime.changed)
            self(ctx)._runtime.changed();
        return JS_TRUE;
    }

    /// local(id, x, y, toGlobal): a point from its space to the stage's, or
    /// back.
    static JSValue local(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        display::DisplayObject* d = self(ctx).displayOf(integer(ctx, arg(argc, argv, 0)));
        display::Point point{number(ctx, arg(argc, argv, 1)), number(ctx, arg(argc, argv, 2))};
        if (d)
            point = JS_ToBool(ctx, arg(argc, argv, 3)) > 0 ? d->localToGlobal(point) : d->globalToLocal(point);
        JSValue out = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, out, 0, JS_NewFloat64(ctx, point.x));
        JS_SetPropertyUint32(ctx, out, 1, JS_NewFloat64(ctx, point.y));
        return out;
    }

    /// bounds(id): its box in its own space, [x, y, width, height]; null
    /// for one with nothing in it.
    static JSValue bounds(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        if (!c || !c->display)
            return JS_NULL;
        const display::Rect box = c->display->getBounds(c->display.get());
        if (box.width <= 0.0 && box.height <= 0.0)
            return JS_NULL;
        JSValue out = JS_NewArray(ctx);
        const double values[] = {box.x, box.y, box.width, box.height};
        for (uint32_t i = 0; i < 4; ++i)
            JS_SetPropertyUint32(ctx, out, i, JS_NewFloat64(ctx, values[i]));
        return out;
    }

    /// hit(id, x, y): whether its art is at the point, in its own space.
    static JSValue hit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Clip* c = clip(ctx, arg(argc, argv, 0));
        return JS_NewBool(ctx, c && c->display &&
                c->display->hitsLocal(number(ctx, arg(argc, argv, 1)), number(ctx, arg(argc, argv, 2))));
    }

    /// stage(name): the stage's size and the pointer on it.
    static JSValue stage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        const display::Stage& s = *self(ctx)._runtime._stage;
        const std::string name = text(ctx, arg(argc, argv, 0));
        if (name == "mouseX")
            return JS_NewFloat64(ctx, s.pointerX());
        if (name == "mouseY")
            return JS_NewFloat64(ctx, s.pointerY());
        if (name == "width")
            return JS_NewFloat64(ctx, s.stageWidth());
        if (name == "height")
            return JS_NewFloat64(ctx, s.stageHeight());
        if (name == "color")
            return JS_NewFloat64(ctx, s.color);
        return JS_UNDEFINED;
    }

    /// print(text): to the output, as trace.
    static JSValue print(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (runtime.trace)
            runtime.trace(text(ctx, arg(argc, argv, 0)));
        return JS_UNDEFINED;
    }

    /// sound(linkage, loops, volume, offset): a library sound played; its
    /// channel or -1.
    static JSValue sound(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (!runtime.playSound)
            return JS_NewInt32(ctx, -1);
        const int loops = std::max(1, integer(ctx, arg(argc, argv, 1)));
        const double volume = argc > 2 && !JS_IsUndefined(argv[2]) ? number(ctx, argv[2], 1.0) : 1.0;
        const double offset = argc > 3 && !JS_IsUndefined(argv[3]) ? number(ctx, argv[3], 0.0) / 1000.0 : 0.0;
        return JS_NewInt32(ctx, runtime.playSound(text(ctx, arg(argc, argv, 0)), offset, loops, volume));
    }

    static JSValue stopSound(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        const int channel = argc > 0 ? integer(ctx, argv[0]) : -1;
        if (channel >= 0 && runtime.stopSound)
            runtime.stopSound(channel);
        else if (channel < 0 && runtime.stopAllSounds)
            runtime.stopAllSounds();
        return JS_UNDEFINED;
    }

    /// open(url, window): what window.open and location ask of the player.
    static JSValue open(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        MovieRuntime& runtime = self(ctx)._runtime;
        if (runtime.getURL)
            runtime.getURL(text(ctx, arg(argc, argv, 0)), argc > 1 ? text(ctx, argv[1]) : std::string("_blank"));
        return JS_UNDEFINED;
    }

    /// Calls the prelude's global function \a name with \a args (freed
    /// after); false when it threw, which is told as in \a where. Whether
    /// it returned true goes in \a returned.
    static bool call(Scripts& s, const char* name, std::initializer_list<JSValue> args, const std::string& where,
        bool* returned = nullptr)
    {
        std::vector<JSValue> values(args);
        s.begin();
        JSValue global = JS_GetGlobalObject(s._ctx);
        JSValue function = JS_GetPropertyStr(s._ctx, global, name);
        JSValue result = JS_Call(s._ctx, function, JS_UNDEFINED, static_cast<int>(values.size()), values.data());
        const bool ok = !JS_IsException(result);
        if (!ok)
            s.report(where);
        else if (returned)
            *returned = JS_ToBool(s._ctx, result) > 0;
        JS_FreeValue(s._ctx, result);
        JS_FreeValue(s._ctx, function);
        JS_FreeValue(s._ctx, global);
        for (JSValue& value : values)
            JS_FreeValue(s._ctx, value);
        s.end();
        return ok;
    }
};

Scripts::Scripts(MovieRuntime& runtime)
    : _runtime(runtime)
{
    _js = JS_NewRuntime();
    JS_SetMemoryLimit(_js, size_t(256) * 1024 * 1024);
    JS_SetMaxStackSize(_js, size_t(1) * 1024 * 1024);
    JS_SetInterruptHandler(_js, &Native::interrupt, this);
    _ctx = JS_NewContext(_js);
    JS_SetContextOpaque(_ctx, this);
    JSValue global = JS_GetGlobalObject(_ctx);
    JSValue native = JS_NewObject(_ctx);
    const struct
    {
        const char* name;
        JSCFunction* function;
        int length;
    } functions[] = {{"time", &Native::time, 0}, {"frameRate", &Native::frameRate, 1}, {"root", &Native::root, 0},
        {"alive", &Native::alive, 1}, {"get", &Native::get, 2}, {"set", &Native::set, 3}, {"go", &Native::go, 3},
        {"play", &Native::play, 2}, {"child", &Native::child, 2}, {"text", &Native::fieldText, 3},
        {"parent", &Native::parent, 1}, {"children", &Native::children, 1}, {"listen", &Native::listen, 3},
        {"make", &Native::make, 1}, {"add", &Native::add, 3}, {"remove", &Native::remove, 2},
        {"local", &Native::local, 4}, {"bounds", &Native::bounds, 1}, {"hit", &Native::hit, 3},
        {"stage", &Native::stage, 1}, {"print", &Native::print, 1}, {"sound", &Native::sound, 4},
        {"stopSound", &Native::stopSound, 1}, {"open", &Native::open, 2}};
    for (const auto& f : functions)
        JS_SetPropertyStr(_ctx, native, f.name, JS_NewCFunction(_ctx, f.function, f.name, f.length));
    JS_SetPropertyStr(_ctx, global, "__phoenix", native);
    JS_SetPropertyStr(_ctx, global, "__phoenixFrames", JS_NewArray(_ctx));
    JS_FreeValue(_ctx, global);
    begin();
    JSValue ran = JS_Eval(_ctx, kPrelude, std::strlen(kPrelude), "<createjs>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ran))
        report("CreateJS");
    JS_FreeValue(_ctx, ran);
    end();

    // The pointer on the stage, for stagemousedown and the rest: as it
    // passes down to what it is on (capture), or at the stage itself.
    const std::shared_ptr<bool> alive = _alive;
    for (const char* type : {display::events::kMouseDown, display::events::kMouseMove, display::events::kMouseUp})
        for (const bool capture : {true, false})
            _runtime._stage->addEventListener(
                type,
                [this, alive, capture, type](display::Event& event) {
                    const bool atStage = event.target == _runtime._stage.get();
                    if (*alive && capture != atStage)
                        stagePointer(type, event);
                },
                capture);
}

Scripts::~Scripts()
{
    *_alive = false;
    for (const auto& [key, listening] : _listening)
        if (const auto object = listening.object.lock())
            object->removeEventListener(listening.id);
    JS_FreeContext(_ctx);
    JS_FreeRuntime(_js);
}

void Scripts::begin()
{
    if (_depth++ == 0)
        _deadline = Clock::now() + kTimeLimit;
}

int Scripts::idOf(Clip& clip)
{
    const auto found = _ids.find(clip.life.get());
    if (found != _ids.end())
        return found->second;
    const int id = _nextId++;
    _ids[clip.life.get()] = id;
    _clips[id] = clip.life;
    return id;
}

Clip* Scripts::clipOf(int id) const
{
    const auto found = _clips.find(id);
    if (found == _clips.end() || !*found->second || (*found->second)->gone)
        return nullptr;
    return *found->second;
}

display::DisplayObject* Scripts::displayOf(int id) const
{
    if (id == kStage)
        return _runtime._stage.get();
    Clip* clip = clipOf(id);
    return clip ? clip->display.get() : nullptr;
}

void Scripts::listen(int id, const std::string& type, bool on)
{
    const auto key = std::make_pair(id, type);
    const auto found = _listening.find(key);
    if (!on)
    {
        if (found == _listening.end())
            return;
        if (const auto object = found->second.object.lock())
            object->removeEventListener(found->second.id);
        _listening.erase(found);
        return;
    }
    const char* shown = displayType(type, id == kStage);
    display::DisplayObject* object = displayOf(id);
    if (!shown || !object || found != _listening.end())
        return;
    if (type == "dblclick" && object->isInteractive())
        static_cast<display::InteractiveObject*>(object)->doubleClickEnabled = true;
    const std::shared_ptr<bool> alive = _alive;
    Listening listening;
    listening.object = object->shared();
    listening.id = object->addEventListener(shown, [this, alive, id, type](display::Event& event) {
        if (*alive)
            deliver(id, type, event);
    });
    _listening[key] = listening;
}

namespace {

/// The clip an event is at (the clip whose display object it is, or
/// holds it), by its number; the stage's for one at the stage.
int targetOf(const display::Event& event, const std::function<int(Clip&)>& idOf)
{
    auto* object = dynamic_cast<display::DisplayObject*>(event.target);
    for (; object; object = object->parent())
        if (object->clip && !object->clip->gone)
            return idOf(*object->clip);
    return kStage;
}

} // namespace

void Scripts::deliver(int id, const std::string& type, display::Event& event)
{
    if (!event.script)
        event.script = std::make_shared<uint64_t>(++_eventSerial);
    const double serial = static_cast<double>(*static_cast<uint64_t*>(event.script.get()));
    const int target = targetOf(event, [this](Clip& clip) { return idOf(clip); });
    // A mouse event's place on the stage, or a key's code, character and
    // modifiers.
    double a = 0.0, b = 0.0;
    int modifiers = 0;
    if (event.eventClass() == display::Event::Class::Mouse)
    {
        const auto& mouse = static_cast<const display::MouseEvent&>(event);
        a = mouse.stageX;
        b = mouse.stageY;
    }
    else if (event.eventClass() == display::Event::Class::Keyboard)
    {
        const auto& key = static_cast<const display::KeyboardEvent&>(event);
        a = key.keyCode;
        b = key.charCode;
        modifiers = (key.shiftKey ? 1 : 0) | (key.ctrlKey ? 2 : 0) | (key.altKey ? 4 : 0);
    }
    Clip* clip = clipOf(id);
    const std::string where = (clip ? (clip->name.empty() ? clip->key : clip->name) : std::string("stage")) + ", " + type;
    ++_runtime._scriptRuns;
    bool stopped = false;
    Native::call(*this, "__phoenixEvent",
        {JS_NewInt32(_ctx, id), JS_NewString(_ctx, type.c_str()), JS_NewInt32(_ctx, target),
            JS_NewFloat64(_ctx, serial), JS_NewInt32(_ctx, static_cast<int>(event.eventPhase)), JS_NewFloat64(_ctx, a),
            JS_NewFloat64(_ctx, b), JS_NewInt32(_ctx, modifiers)},
        where, &stopped);
    if (stopped)
        event.stopPropagation();
}

void Scripts::stagePointer(const std::string& type, display::Event& event)
{
    if (event.eventClass() != display::Event::Class::Mouse)
        return;
    const auto& mouse = static_cast<const display::MouseEvent&>(event);
    const int target = targetOf(event, [this](Clip& clip) { return idOf(clip); });
    Native::call(*this, "__phoenixStage",
        {JS_NewString(_ctx, type.c_str()), JS_NewFloat64(_ctx, mouse.stageX), JS_NewFloat64(_ctx, mouse.stageY),
            JS_NewInt32(_ctx, target)},
        "stage, " + type);
}

void Scripts::afterEvents()
{
    Native::call(*this, "__phoenixReleased", {}, "stage, pressup");
}

void Scripts::runFrame(Clip& clip, const Frame& frame, const std::string& where)
{
    if (!frame.actionScript || frame.actionScript->code.empty())
        return;
    auto found = _compiled.find(&frame);
    if (found == _compiled.end())
    {
        // As a function of its own, as a published movie has it: its var
        // declarations its own, this the clip. On the first line, so the
        // lines errors give are the script's.
        _sources.emplace(where, &frame);
        const std::string code = "(function () {" + frame.actionScript->code + "\n})";
        int index = -1;
        begin();
        JSValue function = JS_Eval(_ctx, code.c_str(), code.size(), where.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(function))
            report(where);
        else
        {
            JSValue global = JS_GetGlobalObject(_ctx);
            JSValue frames = JS_GetPropertyStr(_ctx, global, "__phoenixFrames");
            index = static_cast<int>(_compiled.size());
            JS_SetPropertyUint32(_ctx, frames, static_cast<uint32_t>(index), JS_DupValue(_ctx, function));
            JS_FreeValue(_ctx, frames);
            JS_FreeValue(_ctx, global);
        }
        JS_FreeValue(_ctx, function);
        end();
        found = _compiled.emplace(&frame, index).first;
    }
    if (found->second < 0)
        return;
    ++_runtime._scriptRuns;
    Native::call(*this, "__phoenixRun", {JS_NewInt32(_ctx, found->second), JS_NewInt32(_ctx, idOf(clip))}, where);
}

void Scripts::tick()
{
    const double delta = 1000.0 / std::max(1.0, _runtime._frameRate);
    _time += delta;
    // The clips gone since, let go.
    std::vector<int> gone;
    for (const auto& [id, life] : _clips)
        if (!*life || (*life)->gone)
            gone.push_back(id);
    for (const int id : gone)
    {
        _ids.erase(_clips[id].get());
        _clips.erase(id);
        for (auto it = _listening.begin(); it != _listening.end();)
            it = it->first.first == id ? _listening.erase(it) : std::next(it);
        Native::call(*this, "__phoenixForget", {JS_NewInt32(_ctx, id)}, "tick");
    }
    ++_runtime._scriptRuns;
    Native::call(*this, "__phoenixTick", {JS_NewFloat64(_ctx, delta), JS_NewFloat64(_ctx, _time)}, "tick");
}

void Scripts::report(const std::string& where)
{
    JSValue exception = JS_GetException(_ctx);
    std::string message = text(_ctx, exception);
    std::string stack;
    if (JS_IsError(exception))
    {
        JSValue value = JS_GetPropertyStr(_ctx, exception, "stack");
        if (JS_IsString(value))
            stack = text(_ctx, value);
        JS_FreeValue(_ctx, value);
    }
    JS_FreeValue(_ctx, exception);
    if (Clock::now() > _deadline)
        message = "the script ran for longer than " + std::to_string(kTimeLimit.count()) +
            " seconds and was stopped (a loop that does not end?)";
    // The script the error is in: the frame script its stack runs through,
    // else the one that was run.
    std::string file;
    int line = 0;
    locate(stack, _sources, file, line);
    if (file.empty() && message.rfind("SyntaxError", 0) == 0 && _sources.count(where))
    {
        // A script that does not compile: the line is at its end.
        const size_t at = message.find(where + ":");
        if (at != std::string::npos)
            line = std::atoi(message.c_str() + at + where.size() + 1);
        file = where;
    }
    std::string told = file.empty() ? where : file;
    if (line > 0)
        told += ", line " + std::to_string(line);
    told += ": " + message;
    if (!file.empty() && file != where)
        told += " (in " + where + ")";
    const auto source = _sources.find(file.empty() ? where : file);
    // The line itself, as QuickJS's messages do not always name what was
    // wrong ("not a function").
    const auto* frame = source == _sources.end() ? nullptr : dynamic_cast<const Frame*>(source->second);
    if (frame && frame->actionScript && line > 0)
    {
        std::istringstream lines(frame->actionScript->code);
        std::string shown;
        for (int i = 0; i < line; ++i)
            if (!std::getline(lines, shown))
            {
                shown.clear();
                break;
            }
        const size_t first = shown.find_first_not_of(" \t\r");
        if (first != std::string::npos)
        {
            shown = shown.substr(first);
            while (!shown.empty() && (shown.back() == '\r' || shown.back() == ' '))
                shown.pop_back();
            told += "\n    " + shown;
        }
    }
    _runtime.tell(told, source == _sources.end() ? nullptr : source->second, line);
}

bool checkSyntax(const std::string& source, int& line, std::string& message)
{
    line = 0;
    message.clear();
    JSRuntime* runtime = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(runtime);
    const std::string code = "(function () {" + source + "\n})";
    JSValue compiled =
        JS_Eval(ctx, code.c_str(), code.size(), "<script>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    const bool ok = !JS_IsException(compiled);
    if (!ok)
    {
        JSValue exception = JS_GetException(ctx);
        message = text(ctx, exception);
        std::string stack;
        if (JS_IsError(exception))
        {
            JSValue value = JS_GetPropertyStr(ctx, exception, "stack");
            if (JS_IsString(value))
                stack = text(ctx, value);
            JS_FreeValue(ctx, value);
        }
        const size_t at = stack.find("<script>:");
        if (at != std::string::npos)
            line = std::atoi(stack.c_str() + at + 9);
        JS_FreeValue(ctx, exception);
        // The line past the end the wrapper adds is the script's last.
        const int lines = static_cast<int>(std::count(source.begin(), source.end(), '\n')) + 1;
        line = std::min(line, lines);
    }
    JS_FreeValue(ctx, compiled);
    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    return ok;
}

} // namespace fla::play::canvas
