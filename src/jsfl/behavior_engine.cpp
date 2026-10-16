#include "behavior_engine.h"

#include <quickjs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fla::behaviors {

namespace {

using Clock = std::chrono::steady_clock;

/// What every behavior file runs beside: behavior() to define one, and the
/// seeded randomness and noise ctx hands out.
const char* const kPrelude = R"JS(
var __behaviors = {};
var __pending = null;
function behavior(definition) { __pending = definition; }
function __hash(seed, n) {
    var h = (Math.imul((seed | 0) ^ 0x9e3779b9, 0x85ebca6b) ^ Math.imul(n | 0, 0xc2b2ae35)) | 0;
    h = Math.imul(h ^ (h >>> 16), 0x7feb352d);
    h = Math.imul(h ^ (h >>> 15), 0x846ca68b);
    h ^= h >>> 16;
    return (h >>> 0) / 4294967296;
}
function __noise(seed, t, n) {
    var i = Math.floor(t), f = t - i;
    var a = __hash(seed, i * 131 + n * 7919) * 2 - 1;
    var b = __hash(seed, (i + 1) * 131 + n * 7919) * 2 - 1;
    var s = f * f * (3 - 2 * f);
    return a + (b - a) * s;
}
function __describe(definition) {
    var out = [];
    var params = definition.params || {};
    for (var name in params) {
        var s = params[name] || {};
        var type = s.type || (s.choices ? "choice" : "number");
        var value = s.value;
        if (value === undefined)
            value = type === "choice" ? (s.choices || [""])[0] : type === "checkbox" ? false : (s.min || 0);
        var min = s.min !== undefined ? s.min : type === "angle" ? -360 : 0;
        var max = s.max !== undefined ? s.max : type === "angle" ? 360 : type === "seed" ? 9999 : 100;
        var step = s.step !== undefined ? s.step : 1;
        out.push([name, s.label || name, type, String(value), min, max, step, (s.choices || []).join("\u0001")].join("\u0002"));
    }
    return out.join("\u0003");
}
function __run(id, params, m) {
    var definition = __behaviors[id];
    // Each copy has randomness of its own.
    var seed = (typeof params.seed === "number" ? params.seed : 1) + m.copy * 1000003;
    var ctx = {
        frame: m.frame, fps: m.fps, time: m.frame / m.fps, start: m.start, end: m.end, keyed: m.keyed,
        copy: m.copy, copies: m.copies, current: m.current,
        other: function (name, frame) { return name ? __other(String(name), frame === undefined ? m.frame : frame) : null; },
        controller: function (name) { return name ? __controller(String(name)) : null; },
        control: function (holder, name, frame) {
            var r = holder && name ? __control(String(holder), String(name), frame === undefined ? m.frame : frame) : null;
            if (r === null || r === undefined)
                return null;
            var kind = r.substring(0, r.indexOf(":"));
            var value = r.substring(r.indexOf(":") + 1);
            if (kind === "point") {
                var xy = value.split(",");
                return { x: Number(xy[0]) || 0, y: Number(xy[1]) || 0 };
            }
            if (kind === "color")
                return value;
            if (kind === "checkbox")
                return value === "true" || value === "1";
            return Number(value) || 0;
        },
        loudness: function (frame) { return __loudness(frame === undefined ? m.frame : frame); },
        spectrum: function (frame) { return __spectrum(frame === undefined ? m.frame : frame, 0); },
        waveform: function (frame) { return __spectrum(frame === undefined ? m.frame : frame, 1); },
        // Where the layers it is parented to put a point of its own on a
        // frame: the point given, moved as the parent chain moves it.
        parentAt: function (frame, x, y) {
            var t = __parentAt(frame === undefined ? m.frame : frame);
            if (!t)
                return { x: x, y: y };
            return { x: t[0] * x + t[2] * y + t[4], y: t[1] * x + t[3] * y + t[5] };
        },
        // The motion guide's line: its length, and where along it a place
        // is (x, y and the line's direction in degrees).
        path: m.pathLength >= 0 ? {
            length: m.pathLength,
            at: function (distance) { var p = __pathAt(distance); return { x: p[0], y: p[1], angle: p[2] }; }
        } : null,
        keyedAt: function (frame) { return __keyedAt(frame); },
        random: function (n) { return __hash(seed, n === undefined ? 0 : n); },
        noise: function (t, n) { return __noise(seed, t, n === undefined ? 0 : n); }
    };
    var result = definition.evaluate(ctx, params);
    return result && typeof result === "object" ? result : {};
}
)JS";

std::vector<std::string> split(const std::string& text, char by)
{
    std::vector<std::string> parts;
    if (text.empty())
        return parts;
    size_t from = 0;
    for (;;)
    {
        const size_t at = text.find(by, from);
        parts.push_back(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos)
            break;
        from = at + 1;
    }
    return parts;
}

JSValue keyedObject(JSContext* ctx, const Keyed& keyed)
{
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, keyed.x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, keyed.y));
    JS_SetPropertyStr(ctx, object, "rotation", JS_NewFloat64(ctx, keyed.rotation));
    JS_SetPropertyStr(ctx, object, "scaleX", JS_NewFloat64(ctx, keyed.scaleX));
    JS_SetPropertyStr(ctx, object, "scaleY", JS_NewFloat64(ctx, keyed.scaleY));
    JS_SetPropertyStr(ctx, object, "alpha", JS_NewFloat64(ctx, keyed.alpha));
    return object;
}

} // namespace

struct Engine::Impl
{
    JSRuntime* runtime = nullptr;
    JSContext* ctx = nullptr;
    std::vector<Definition> definitions;
    Clock::time_point deadline;
    const Moment* moment = nullptr;

    Impl()
    {
        runtime = JS_NewRuntime();
        JS_SetMemoryLimit(runtime, size_t(64) * 1024 * 1024);
        JS_SetMaxStackSize(runtime, size_t(1) * 1024 * 1024);
        JS_SetInterruptHandler(runtime, &Impl::interrupt, this);
        ctx = JS_NewContext(runtime);
        JS_SetContextOpaque(ctx, this);
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "__keyedAt", JS_NewCFunction(ctx, &Impl::keyedAt, "__keyedAt", 1));
        JS_SetPropertyStr(ctx, global, "__pathAt", JS_NewCFunction(ctx, &Impl::pathAt, "__pathAt", 1));
        JS_SetPropertyStr(ctx, global, "__other", JS_NewCFunction(ctx, &Impl::other, "__other", 2));
        JS_SetPropertyStr(ctx, global, "__controller", JS_NewCFunction(ctx, &Impl::controller, "__controller", 1));
        JS_SetPropertyStr(ctx, global, "__loudness", JS_NewCFunction(ctx, &Impl::loudness, "__loudness", 1));
        JS_SetPropertyStr(ctx, global, "__control", JS_NewCFunction(ctx, &Impl::control, "__control", 3));
        JS_SetPropertyStr(ctx, global, "__spectrum", JS_NewCFunction(ctx, &Impl::spectrum, "__spectrum", 2));
        JS_SetPropertyStr(ctx, global, "__parentAt", JS_NewCFunction(ctx, &Impl::parentAt, "__parentAt", 1));
        JS_FreeValue(ctx, global);
        deadline = Clock::now() + std::chrono::seconds(5);
        JS_FreeValue(ctx, JS_Eval(ctx, kPrelude, std::strlen(kPrelude), "<behaviors>", JS_EVAL_TYPE_GLOBAL));
    }

    ~Impl()
    {
        JS_FreeContext(ctx);
        JS_FreeRuntime(runtime);
    }

    static int interrupt(JSRuntime*, void* opaque)
    {
        return Clock::now() > static_cast<Impl*>(opaque)->deadline ? 1 : 0;
    }

    static JSValue keyedAt(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        double frame = 0;
        if (argc < 1 || JS_ToFloat64(ctx, &frame, argv[0]) < 0 || !self->moment || !self->moment->keyedAt)
            return JS_NULL;
        Keyed keyed;
        const int f = static_cast<int>(std::lround(frame));
        if (f == self->moment->frame)
            return keyedObject(ctx, self->moment->keyed);
        if (!self->moment->keyedAt(f, keyed))
            return JS_NULL;
        return keyedObject(ctx, keyed);
    }

    static JSValue other(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        if (argc < 1 || !self->moment || !self->moment->other)
            return JS_NULL;
        const char* chars = JS_ToCString(ctx, argv[0]);
        const std::string name = chars ? chars : "";
        if (chars)
            JS_FreeCString(ctx, chars);
        double frame = self->moment->frame;
        if (argc > 1)
            JS_ToFloat64(ctx, &frame, argv[1]);
        // A behavior asking about another instance gives the moment it is
        // working out back after: the other's own behaviors run in between.
        const Moment* mine = self->moment;
        Keyed keyed;
        const bool found = mine->other(name, static_cast<int>(std::lround(frame)), keyed);
        self->moment = mine;
        return found ? keyedObject(ctx, keyed) : JS_NULL;
    }

    static JSValue spectrum(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        JSValue array = JS_NewArray(ctx);
        if (argc < 2 || !self->moment || !self->moment->spectrum)
            return array;
        double frame = 0.0;
        int which = 0;
        JS_ToFloat64(ctx, &frame, argv[0]);
        JS_ToInt32(ctx, &which, argv[1]);
        std::vector<double> bands;
        std::vector<double> wave;
        if (!self->moment->spectrum(static_cast<int>(std::lround(frame)), bands, wave))
            return array;
        const std::vector<double>& values = which == 1 ? wave : bands;
        for (size_t i = 0; i < values.size(); ++i)
            JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), JS_NewFloat64(ctx, values[i]));
        return array;
    }

    static JSValue control(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        if (argc < 2 || !self->moment || !self->moment->control)
            return JS_NULL;
        const auto text = [ctx](JSValueConst value) {
            const char* chars = JS_ToCString(ctx, value);
            std::string out = chars ? chars : "";
            if (chars)
                JS_FreeCString(ctx, chars);
            return out;
        };
        double frame = self->moment->frame;
        if (argc > 2)
            JS_ToFloat64(ctx, &frame, argv[2]);
        const Moment* mine = self->moment;
        std::string kind;
        std::string value;
        const bool found = mine->control(text(argv[0]), text(argv[1]), static_cast<int>(std::lround(frame)), kind, value);
        self->moment = mine;
        return found ? JS_NewString(ctx, (kind + ":" + value).c_str()) : JS_NULL;
    }

    std::string text(JSValueConst object, const char* name)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, name);
        std::string out;
        if (JS_IsString(value))
            if (const char* chars = JS_ToCString(ctx, value))
            {
                out = chars;
                JS_FreeCString(ctx, chars);
            }
        JS_FreeValue(ctx, value);
        return out;
    }

    static JSValue controller(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        if (argc < 1 || !self->moment || !self->moment->controller)
            return JS_NULL;
        const char* chars = JS_ToCString(ctx, argv[0]);
        const std::string name = chars ? chars : "";
        if (chars)
            JS_FreeCString(ctx, chars);
        double value = 0.0;
        return self->moment->controller(name, value) ? JS_NewFloat64(ctx, value) : JS_NULL;
    }

    static JSValue loudness(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        double frame = 0;
        if (argc < 1 || JS_ToFloat64(ctx, &frame, argv[0]) < 0 || !self->moment || !self->moment->loudness)
            return JS_NewFloat64(ctx, 0.0);
        return JS_NewFloat64(ctx, self->moment->loudness(static_cast<int>(std::lround(frame))));
    }

    static JSValue parentAt(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        double frame = 0;
        double m[6] = {1, 0, 0, 1, 0, 0};
        if (argc < 1 || JS_ToFloat64(ctx, &frame, argv[0]) < 0 || !self->moment || !self->moment->parentAt)
            return JS_NULL;
        // The parents' own behaviors may run in between, as for other().
        const Moment* mine = self->moment;
        const bool found = mine->parentAt(static_cast<int>(std::lround(frame)), m);
        self->moment = mine;
        if (!found)
            return JS_NULL;
        JSValue array = JS_NewArray(ctx);
        for (uint32_t i = 0; i < 6; ++i)
            JS_SetPropertyUint32(ctx, array, i, JS_NewFloat64(ctx, m[i]));
        return array;
    }

    static JSValue pathAt(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* self = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        double distance = 0;
        if (argc > 0)
            JS_ToFloat64(ctx, &distance, argv[0]);
        const GuidePath* path = self->moment ? self->moment->path : nullptr;
        JSValue out = JS_NewArray(ctx);
        const Point point = path ? path->at(distance) : Point();
        JS_SetPropertyUint32(ctx, out, 0, JS_NewFloat64(ctx, point.x));
        JS_SetPropertyUint32(ctx, out, 1, JS_NewFloat64(ctx, point.y));
        JS_SetPropertyUint32(ctx, out, 2, JS_NewFloat64(ctx, path ? path->angleAt(distance) : 0.0));
        return out;
    }

    std::string text(JSValueConst value)
    {
        const char* chars = JS_ToCString(ctx, value);
        std::string out = chars ? chars : "";
        if (chars)
            JS_FreeCString(ctx, chars);
        return out;
    }

    /// The pending exception, as a message.
    std::string error()
    {
        JSValue exception = JS_GetException(ctx);
        std::string message = text(exception);
        JSValue stack = JS_IsObject(exception) ? JS_GetPropertyStr(ctx, exception, "stack") : JS_UNDEFINED;
        if (JS_IsString(stack))
        {
            const std::string where = text(stack);
            if (!where.empty())
                message += "\n" + where;
        }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, exception);
        if (Clock::now() > deadline)
            message = "it took too long";
        return message;
    }

    double number(JSValueConst object, const char* name, double fallback)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, name);
        double out = fallback;
        if (JS_IsNumber(value))
            JS_ToFloat64(ctx, &out, value);
        JS_FreeValue(ctx, value);
        return std::isfinite(out) ? out : fallback;
    }
};

Engine::Engine()
    : _impl(std::make_unique<Impl>())
{
}

Engine::~Engine() = default;

bool Engine::add(const std::string& id, const std::string& source, const std::string& file, bool builtIn,
    std::string& error)
{
    Impl& d = *_impl;
    JSContext* ctx = d.ctx;
    d.deadline = Clock::now() + std::chrono::seconds(2);
    JS_FreeValue(ctx, JS_Eval(ctx, "__pending = null;", 17, "<behaviors>", JS_EVAL_TYPE_GLOBAL));
    const std::string name = file.empty() ? "<" + id + ">" : file;
    JSValue ran = JS_Eval(ctx, source.c_str(), source.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ran))
    {
        error = d.error();
        return false;
    }
    JS_FreeValue(ctx, ran);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue pending = JS_GetPropertyStr(ctx, global, "__pending");
    JSValue evaluate = JS_IsObject(pending) ? JS_GetPropertyStr(ctx, pending, "evaluate") : JS_UNDEFINED;
    const bool ok = JS_IsObject(pending) && JS_IsFunction(ctx, evaluate);
    JS_FreeValue(ctx, evaluate);
    if (!ok)
    {
        JS_FreeValue(ctx, pending);
        JS_FreeValue(ctx, global);
        error = "It does not call behavior({...}) with an evaluate function.";
        return false;
    }
    Definition definition;
    definition.id = id;
    definition.builtIn = builtIn;
    definition.file = file;
    JSValue field = JS_GetPropertyStr(ctx, pending, "name");
    definition.name = JS_IsString(field) ? d.text(field) : id;
    JS_FreeValue(ctx, field);
    field = JS_GetPropertyStr(ctx, pending, "description");
    definition.description = JS_IsString(field) ? d.text(field) : std::string();
    JS_FreeValue(ctx, field);
    // The settings, flattened by __describe.
    JSValue describe = JS_GetPropertyStr(ctx, global, "__describe");
    JSValue described = JS_Call(ctx, describe, global, 1, &pending);
    JS_FreeValue(ctx, describe);
    if (JS_IsException(described))
    {
        error = d.error();
        JS_FreeValue(ctx, pending);
        JS_FreeValue(ctx, global);
        return false;
    }
    for (const std::string& line : split(d.text(described), '\x03'))
    {
        const std::vector<std::string> parts = split(line, '\x02');
        if (parts.size() < 8)
            continue;
        ParamSpec spec;
        spec.name = parts[0];
        spec.label = parts[1];
        spec.type = parts[2];
        spec.text = parts[3];
        spec.value = std::atof(parts[3].c_str());
        if (spec.type == "checkbox")
            spec.value = parts[3] == "true" ? 1.0 : 0.0;
        spec.min = std::atof(parts[4].c_str());
        spec.max = std::atof(parts[5].c_str());
        spec.step = std::atof(parts[6].c_str());
        spec.choices = split(parts[7], '\x01');
        definition.params.push_back(spec);
    }
    JS_FreeValue(ctx, described);
    // Kept under its id for __run.
    JSValue table = JS_GetPropertyStr(ctx, global, "__behaviors");
    JS_SetPropertyStr(ctx, table, id.c_str(), pending);
    JS_FreeValue(ctx, table);
    JS_FreeValue(ctx, global);
    // A file read again replaces what it defined before.
    auto& list = d.definitions;
    list.erase(std::remove_if(list.begin(), list.end(), [&id](const Definition& each) { return each.id == id; }),
        list.end());
    list.push_back(definition);
    std::stable_sort(list.begin(), list.end(), [](const Definition& a, const Definition& b) { return a.name < b.name; });
    return true;
}

void Engine::addBuiltIns()
{
    for (const auto& [id, source] : builtInSources())
    {
        std::string error;
        add(id, source, std::string(), true, error);
    }
}

const std::vector<Definition>& Engine::definitions() const
{
    return _impl->definitions;
}

const Definition* Engine::find(const std::string& id) const
{
    for (const Definition& definition : _impl->definitions)
        if (definition.id == id)
            return &definition;
    return nullptr;
}

Applied Engine::defaults(const std::string& id, int seed) const
{
    Applied applied;
    applied.type = id;
    if (const Definition* definition = find(id))
    {
        for (const ParamSpec& spec : definition->params)
        {
            if (spec.type == "seed")
                applied.params[spec.name] = std::to_string(seed);
            else
                applied.params[spec.name] = spec.text;
        }
    }
    return applied;
}

Copies Engine::evaluate(const std::vector<Applied>& list, const Moment& moment, std::string* error)
{
    // Most behaviors change each copy there is; one that repeats makes
    // copies of each, and those after it work on every copy.
    constexpr size_t kMostCopies = 5000;
    Copies copies(1);
    Impl& d = *_impl;
    JSContext* ctx = d.ctx;
    // Run inside another behavior (one asking about another instance or its
    // parents), it has only the time the outer one has left: a behavior
    // asking over and over still stops. Both come back after.
    const Moment* outerMoment = d.moment;
    const Clock::time_point outerDeadline = d.deadline;
    const Clock::time_point own = Clock::now() + std::chrono::milliseconds(400);
    d.moment = &moment;
    d.deadline = outerMoment ? std::min(outerDeadline, own) : own;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue run = JS_GetPropertyStr(ctx, global, "__run");
    const auto readDelta = [&d](JSValueConst value) {
        Delta delta;
        delta.x = d.number(value, "x", 0.0);
        delta.y = d.number(value, "y", 0.0);
        delta.rotation = d.number(value, "rotation", 0.0);
        const double scale = d.number(value, "scale", 1.0);
        delta.scaleX = scale * d.number(value, "scaleX", 1.0);
        delta.scaleY = scale * d.number(value, "scaleY", 1.0);
        delta.alpha = std::max(0.0, d.number(value, "alpha", 1.0));
        delta.frame = static_cast<int>(std::lround(d.number(value, "frame", 0.0)));
        const double hold = d.number(value, "holdFrame", -1.0);
        delta.hold = hold >= 0.0 ? static_cast<int>(std::floor(hold)) : -1;
        // A tint toward a color.
        const std::string color = d.text(value, "color");
        unsigned int rgb = 0;
        if (color.size() == 7 && color[0] == '#' && std::sscanf(color.c_str() + 1, "%6x", &rgb) == 1)
        {
            delta.tint = std::clamp(d.number(value, "tint", 1.0), 0.0, 1.0);
            delta.tintColor[0] = static_cast<unsigned char>((rgb >> 16) & 0xFF);
            delta.tintColor[1] = static_cast<unsigned char>((rgb >> 8) & 0xFF);
            delta.tintColor[2] = static_cast<unsigned char>(rgb & 0xFF);
        }
        return delta;
    };
    for (const Applied& applied : list)
    {
        const Definition* definition = applied.enabled ? find(applied.type) : nullptr;
        if (!definition)
            continue;
        Copies next;
        bool failed = false;
        for (size_t c = 0; c < copies.size() && !failed; ++c)
        {
            JSValue params = JS_NewObject(ctx);
            for (const ParamSpec& spec : definition->params)
            {
                const std::string value = applied.valueAt(spec.name, moment.frame, spec.text);
                JSValue v;
                if (spec.type == "choice" || spec.type == "text" || spec.type == "object" || spec.type == "controller")
                    v = JS_NewString(ctx, value.c_str());
                else if (spec.type == "checkbox")
                    v = JS_NewBool(ctx, value == "true" || value == "1");
                else
                    v = JS_NewFloat64(ctx, value.empty() ? spec.value : std::atof(value.c_str()));
                JS_SetPropertyStr(ctx, params, spec.name.c_str(), v);
            }
            JSValue m = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, m, "frame", JS_NewInt32(ctx, moment.frame));
            JS_SetPropertyStr(ctx, m, "fps", JS_NewFloat64(ctx, moment.frameRate > 0 ? moment.frameRate : 24.0));
            JS_SetPropertyStr(ctx, m, "start", JS_NewInt32(ctx, moment.start));
            JS_SetPropertyStr(ctx, m, "end", JS_NewInt32(ctx, moment.end));
            JS_SetPropertyStr(ctx, m, "copy", JS_NewInt32(ctx, static_cast<int32_t>(c)));
            JS_SetPropertyStr(ctx, m, "copies", JS_NewInt32(ctx, static_cast<int32_t>(copies.size())));
            JS_SetPropertyStr(ctx, m, "keyed", keyedObject(ctx, moment.keyed));
            // The copy as the behaviors before this one left it.
            JSValue current = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, current, "x", JS_NewFloat64(ctx, copies[c].x));
            JS_SetPropertyStr(ctx, current, "y", JS_NewFloat64(ctx, copies[c].y));
            JS_SetPropertyStr(ctx, current, "rotation", JS_NewFloat64(ctx, copies[c].rotation));
            JS_SetPropertyStr(ctx, current, "scaleX", JS_NewFloat64(ctx, copies[c].scaleX));
            JS_SetPropertyStr(ctx, current, "scaleY", JS_NewFloat64(ctx, copies[c].scaleY));
            JS_SetPropertyStr(ctx, current, "alpha", JS_NewFloat64(ctx, copies[c].alpha));
            JS_SetPropertyStr(ctx, current, "frame", JS_NewInt32(ctx, copies[c].frame));
            JS_SetPropertyStr(ctx, m, "current", current);
            JS_SetPropertyStr(ctx, m, "pathLength",
                JS_NewFloat64(ctx, moment.path && !moment.path->empty() ? moment.path->length() : -1.0));
            JSValue args[3] = {JS_NewString(ctx, definition->id.c_str()), params, m};
            JSValue result = JS_Call(ctx, run, global, 3, args);
            for (JSValue& arg : args)
                JS_FreeValue(ctx, arg);
            if (JS_IsException(result))
            {
                if (error)
                    *error = definition->name + ": " + d.error();
                failed = true;
                break;
            }
            JSValue made = JS_GetPropertyStr(ctx, result, "copies");
            if (JS_IsArray(made))
            {
                // A copy of this copy for each one returned.
                int64_t length = 0;
                JS_GetLength(ctx, made, &length);
                for (int64_t i = 0; i < length && next.size() < kMostCopies; ++i)
                {
                    JSValue item = JS_GetPropertyInt64(ctx, made, i);
                    Delta each = copies[c];
                    each.add(readDelta(item));
                    next.push_back(each);
                    JS_FreeValue(ctx, item);
                }
            }
            else
            {
                Delta each = copies[c];
                each.add(readDelta(result));
                next.push_back(each);
            }
            JS_FreeValue(ctx, made);
            JS_FreeValue(ctx, result);
        }
        // A behavior that fails adds nothing.
        if (!failed)
            copies = std::move(next);
    }
    JS_FreeValue(ctx, run);
    JS_FreeValue(ctx, global);
    d.moment = outerMoment;
    if (outerMoment)
        d.deadline = outerDeadline;
    return copies;
}

} // namespace fla::behaviors
