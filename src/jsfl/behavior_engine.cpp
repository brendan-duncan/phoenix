#include "behavior_engine.h"

#include <quickjs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
    var seed = typeof params.seed === "number" ? params.seed : 1;
    var ctx = {
        frame: m.frame, fps: m.fps, time: m.frame / m.fps, start: m.start, end: m.end, keyed: m.keyed,
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

Delta Engine::evaluate(const std::vector<Applied>& list, const Moment& moment, std::string* error)
{
    Delta total;
    Impl& d = *_impl;
    JSContext* ctx = d.ctx;
    d.moment = &moment;
    d.deadline = Clock::now() + std::chrono::milliseconds(200);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue run = JS_GetPropertyStr(ctx, global, "__run");
    for (const Applied& applied : list)
    {
        const Definition* definition = applied.enabled ? find(applied.type) : nullptr;
        if (!definition)
            continue;
        JSValue params = JS_NewObject(ctx);
        for (const ParamSpec& spec : definition->params)
        {
            const auto found = applied.params.find(spec.name);
            const std::string value = found != applied.params.end() ? found->second : spec.text;
            JSValue v;
            if (spec.type == "choice")
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
        JS_SetPropertyStr(ctx, m, "keyed", keyedObject(ctx, moment.keyed));
        JSValue args[3] = {JS_NewString(ctx, definition->id.c_str()), params, m};
        JSValue result = JS_Call(ctx, run, global, 3, args);
        for (JSValue& arg : args)
            JS_FreeValue(ctx, arg);
        if (JS_IsException(result))
        {
            if (error)
                *error = definition->name + ": " + d.error();
            continue;
        }
        Delta delta;
        delta.x = d.number(result, "x", 0.0);
        delta.y = d.number(result, "y", 0.0);
        delta.rotation = d.number(result, "rotation", 0.0);
        const double scale = d.number(result, "scale", 1.0);
        delta.scaleX = scale * d.number(result, "scaleX", 1.0);
        delta.scaleY = scale * d.number(result, "scaleY", 1.0);
        delta.alpha = std::max(0.0, d.number(result, "alpha", 1.0));
        JS_FreeValue(ctx, result);
        total.add(delta);
    }
    JS_FreeValue(ctx, run);
    JS_FreeValue(ctx, global);
    d.moment = nullptr;
    return total;
}

} // namespace fla::behaviors
