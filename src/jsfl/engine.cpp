// The QuickJS binding: the object model's class tables made into JavaScript
// prototypes, values converted both ways, and a run from start to finish.

#include "engine.h"
#include "panel_script.h"
#include "tool_script.h"

#include "e4x.h"
#include "flfile.h"
#include "model.h"
#include "native_extensions.h"

#include <quickjs.h>

#include "tinyxml2.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace jsfl {

std::string RunResult::report() const
{
    if (ok)
        return {};
    std::string where = file;
    if (line > 0)
        where += ":" + std::to_string(line);
    return where.empty() ? message : where + ": " + message;
}

namespace {

using Clock = std::chrono::steady_clock;

/// A JavaScript exception is already set in the context; the binding
/// returns JS_EXCEPTION so it carries on up the script.
struct PendingException
{
};

JSClassID g_objectClass = 0;

void finalizeObject(JSRuntime*, JSValueConst value)
{
    delete static_cast<ObjectPtr*>(JS_GetOpaque(value, g_objectClass));
}

std::string toStd(JSContext* ctx, JSValueConst value)
{
    size_t length = 0;
    const char* text = JS_ToCStringLen(ctx, &length, value);
    if (!text)
        throw PendingException();
    std::string result(text, length);
    JS_FreeCString(ctx, text);
    return result;
}

bool inherits(const ClassInfo* info, const ClassInfo* owner)
{
    for (; info; info = info->parent)
    {
        if (info == owner)
            return true;
    }
    return false;
}

/// What one script context holds for the binding: the functions it made,
/// the prototypes of the model's classes, and the script objects already
/// made for things that have an identity.
class Binding
{
public:
    using Function = std::function<JSValue(JSContext*, JSValueConst self, int argc, JSValueConst* argv)>;

    explicit Binding(JSContext* ctx)
        : ctx(ctx)
    {}

    ~Binding()
    {
        for (auto& entry : _identities)
            JS_FreeValue(ctx, entry.second);
        for (auto& entry : _prototypes)
            JS_FreeValue(ctx, entry.second);
    }

    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;

    static Binding& of(JSContext* ctx) { return *static_cast<Binding*>(JS_GetContextOpaque(ctx)); }

    JSValue function(int length, Function call)
    {
        _functions.push_back(std::move(call));
        JSValue index = JS_NewInt32(ctx, static_cast<int32_t>(_functions.size() - 1));
        return JS_NewCFunctionData(ctx, &Binding::trampoline, length, 0, 1, &index);
    }

    void defineFunction(JSValueConst object, const char* name, int length, Function call)
    {
        JS_DefinePropertyValueStr(ctx, object, name, function(length, std::move(call)), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }

    JSValue wrap(const ObjectPtr& object)
    {
        if (!object)
            return JS_NULL;
        const ClassInfo& info = object->classInfo();
        const void* identity = object->identity();
        if (identity)
        {
            const auto found = _identities.find({&info, identity, object->scope()});
            if (found != _identities.end())
                return JS_DupValue(ctx, found->second);
        }
        JSValue value = JS_NewObjectProtoClass(ctx, prototype(&info), g_objectClass);
        if (JS_IsException(value))
            throw PendingException();
        JS_SetOpaque(value, new ObjectPtr(object));
        if (identity)
            _identities[{&info, identity, object->scope()}] = JS_DupValue(ctx, value);
        return value;
    }

    /// The model object behind \a value, when it is one.
    static ObjectPtr unwrap(JSValueConst value)
    {
        auto* held = static_cast<ObjectPtr*>(JS_GetOpaque(value, g_objectClass));
        return held ? *held : nullptr;
    }

    JSValue toJS(const Value& value)
    {
        switch (value.type)
        {
        case Value::Type::Undefined: return JS_UNDEFINED;
        case Value::Type::Null: return JS_NULL;
        case Value::Type::Boolean: return JS_NewBool(ctx, value.boolean);
        case Value::Type::Number: return JS_NewNumber(ctx, value.number);
        case Value::Type::String: return JS_NewStringLen(ctx, value.string.data(), value.string.size());
        case Value::Type::Date: return JS_NewDate(ctx, value.number);
        case Value::Type::Object: return wrap(value.object);
        case Value::Type::Array:
        {
            JSValue array = JS_NewArray(ctx);
            for (size_t i = 0; i < value.items.size(); ++i)
                JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), toJS(value.items[i]));
            return array;
        }
        case Value::Type::Map:
        {
            JSValue object = JS_NewObject(ctx);
            for (const auto& field : value.fields)
                JS_SetPropertyStr(ctx, object, field.first.c_str(), toJS(field.second));
            return object;
        }
        }
        return JS_UNDEFINED;
    }

    Value fromJS(JSValueConst value, int depth = 0)
    {
        if (JS_IsUndefined(value))
            return Value();
        if (JS_IsNull(value))
            return Value::null();
        if (JS_IsBool(value))
            return Value(JS_ToBool(ctx, value) > 0);
        if (JS_IsNumber(value))
        {
            double number = 0.0;
            JS_ToFloat64(ctx, &number, value);
            return Value(number);
        }
        if (JS_IsString(value))
            return Value(toStd(ctx, value));
        if (!JS_IsObject(value) || depth > 16)
            return Value();
        if (isE4X(value))
            return Value(toStd(ctx, value));
        if (ObjectPtr object = unwrap(value))
            return Value(object);
        if (JS_IsDate(value))
        {
            double time = 0.0;
            if (JS_ToFloat64(ctx, &time, value) < 0)
                throw PendingException();
            return Value::date(time);
        }
        if (JS_IsFunction(ctx, value))
            return Value();
        if (JS_IsArray(value))
        {
            Value array = Value::array();
            JSValue lengthValue = JS_GetPropertyStr(ctx, value, "length");
            uint32_t length = 0;
            JS_ToUint32(ctx, &length, lengthValue);
            JS_FreeValue(ctx, lengthValue);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(ctx, value, i);
                if (JS_IsException(item))
                    throw PendingException();
                array.items.push_back(fromJS(item, depth + 1));
                JS_FreeValue(ctx, item);
            }
            return array;
        }
        Value map = Value::map();
        JSPropertyEnum* names = nullptr;
        uint32_t count = 0;
        if (JS_GetOwnPropertyNames(ctx, &names, &count, value, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
            throw PendingException();
        for (uint32_t i = 0; i < count; ++i)
        {
            const char* name = JS_AtomToCString(ctx, names[i].atom);
            JSValue field = JS_GetProperty(ctx, value, names[i].atom);
            if (name && !JS_IsException(field))
                map.fields.emplace_back(name, fromJS(field, depth + 1));
            if (name)
                JS_FreeCString(ctx, name);
            JS_FreeValue(ctx, field);
        }
        JS_FreePropertyEnum(ctx, names, count);
        return map;
    }

    JSContext* ctx;

private:
    /// An E4X XML or XMLList, which the model takes as its text.
    bool isE4X(JSValueConst value)
    {
        if (!JS_IsProxy(value))
            return false;
        JSValue handler = JS_GetProxyHandler(ctx, value);
        JSValue flag = JS_IsObject(handler) ? JS_GetPropertyStr(ctx, handler, "e4x") : JS_UNDEFINED;
        const bool e4x = JS_IsBool(flag) && JS_ToBool(ctx, flag) > 0;
        JS_FreeValue(ctx, flag);
        JS_FreeValue(ctx, handler);
        return e4x;
    }

    static JSValue trampoline(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int, JSValueConst* data)
    {
        int32_t index = 0;
        JS_ToInt32(ctx, &index, data[0]);
        Binding& binding = of(ctx);
        try
        {
            return binding._functions[static_cast<size_t>(index)](ctx, self, argc, argv);
        }
        catch (const PendingException&)
        {
            return JS_EXCEPTION;
        }
        catch (const std::bad_alloc&)
        {
            return JS_ThrowOutOfMemory(ctx);
        }
        catch (const std::exception& error)
        {
            return JS_ThrowPlainError(ctx, "%s", error.what());
        }
    }

    /// The model object a member is called on, checked to be of the
    /// member's class. A method of fl, FLfile or the output panel taken
    /// off its object (var trace = fl.trace) still works, as it did.
    ObjectPtr self(JSValueConst value, const ClassInfo* owner)
    {
        ObjectPtr object = unwrap(value);
        if (!object || !inherits(&object->classInfo(), owner))
        {
            const auto found = _singletons.find(owner);
            if (found != _singletons.end())
                return found->second;
            throw Error("This is not a " + owner->name + ".");
        }
        return object;
    }

public:
    /// Makes \a object the one its class's methods use when called with
    /// no object of theirs.
    void singleton(const ObjectPtr& object) { _singletons[&object->classInfo()] = object; }

private:

    Args arguments(int argc, JSValueConst* argv)
    {
        Args args;
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i)
            args.push_back(fromJS(argv[i]));
        return args;
    }

    JSValue prototype(const ClassInfo* info)
    {
        const auto found = _prototypes.find(info);
        if (found != _prototypes.end())
            return found->second;
        JSValue proto = info->parent ? JS_NewObjectProto(ctx, prototype(info->parent)) : JS_NewObject(ctx);
        for (size_t i = 0; i < info->properties.size(); ++i)
        {
            const Property& property = info->properties[i];
            JSValue getter = function(0, [this, info, i](JSContext*, JSValueConst self, int, JSValueConst*) {
                ObjectPtr object = this->self(self, info);
                return toJS(info->properties[i].get(*object));
            });
            JSValue setter = JS_UNDEFINED;
            if (property.set)
            {
                setter = function(1, [this, info, i](JSContext*, JSValueConst self, int argc, JSValueConst* argv) {
                    ObjectPtr object = this->self(self, info);
                    info->properties[i].set(*object, argc > 0 ? fromJS(argv[0]) : Value());
                    return JS_UNDEFINED;
                });
            }
            JSAtom atom = JS_NewAtom(ctx, property.name.c_str());
            JS_DefinePropertyGetSet(ctx, proto, atom, getter, setter, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
            JS_FreeAtom(ctx, atom);
        }
        for (size_t i = 0; i < info->methods.size(); ++i)
        {
            const Method& method = info->methods[i];
            defineFunction(proto, method.name.c_str(), method.length,
                [this, info, i](JSContext*, JSValueConst self, int argc, JSValueConst* argv) {
                    ObjectPtr object = this->self(self, info);
                    return toJS(info->methods[i].call(*object, arguments(argc, argv)));
                });
        }
        const std::string label = "[object " + info->name + "]";
        defineFunction(proto, "toString", 0, [label](JSContext* c, JSValueConst, int, JSValueConst*) {
            return JS_NewStringLen(c, label.data(), label.size());
        });
        _prototypes[info] = proto;
        return proto;
    }

    /// A deque, so a function running while another is added (a class's
    /// prototype made on first use) is not moved out from under itself.
    std::deque<Function> _functions;
    std::map<const ClassInfo*, JSValue> _prototypes;
    std::map<const ClassInfo*, ObjectPtr> _singletons;
    std::map<std::tuple<const ClassInfo*, const void*, const void*>, JSValue> _identities;
};

/// \a value with Phoenix's objects in it made null: they belong to the run's
/// session, which ends with it.
Value plainValue(Value value)
{
    if (value.type == Value::Type::Object)
        return Value::null();
    for (Value& item : value.items)
        item = plainValue(std::move(item));
    for (auto& field : value.fields)
        field.second = plainValue(std::move(field.second));
    return value;
}

/// What the interrupt check needs.
struct Guard
{
    std::atomic<bool>* stop = nullptr;
    bool hasDeadline = false;
    Clock::time_point deadline;
    std::function<void()> poll;
    Clock::time_point lastPoll;
    bool timedOut = false;
    bool stopped = false;
};

int interrupt(JSRuntime*, void* opaque)
{
    Guard& guard = *static_cast<Guard*>(opaque);
    const Clock::time_point now = Clock::now();
    if (guard.poll && now - guard.lastPoll > std::chrono::milliseconds(50))
    {
        guard.lastPoll = now;
        guard.poll();
    }
    if (guard.stop && *guard.stop)
    {
        guard.stopped = true;
        return 1;
    }
    if (guard.hasDeadline && now > guard.deadline)
    {
        guard.timedOut = true;
        return 1;
    }
    return 0;
}

bool readFile(const std::string& path, std::string& text)
{
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in)
        return false;
    std::ostringstream content;
    content << in.rdbuf();
    text = content.str();
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        text.erase(0, 3);
    return true;
}

/// Where in the script an error happened, from its stack: the first
/// "file:line:column" frame.
void locate(const std::string& stack, std::string& file, int& line)
{
    std::istringstream lines(stack);
    std::string entry;
    while (std::getline(lines, entry))
    {
        const size_t at = entry.find("at ");
        if (at == std::string::npos)
            continue;
        std::string place = entry.substr(at + 3);
        const size_t open = place.rfind('(');
        if (open != std::string::npos && place.back() == ')')
            place = place.substr(open + 1, place.size() - open - 2);
        // file:line:column, the file possibly with a drive letter.
        const size_t column = place.rfind(':');
        if (column == std::string::npos || column == 0)
            continue;
        const size_t lineAt = place.rfind(':', column - 1);
        if (lineAt == std::string::npos)
            continue;
        const std::string number = place.substr(lineAt + 1, column - lineAt - 1);
        if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos)
            continue;
        const std::string name = place.substr(0, lineAt);
        if (name == "<native>" || name.empty())
            continue;
        file = name;
        line = std::atoi(number.c_str());
        return;
    }
}

const char* const kPrelude = R"JS(
(function (global) {
    function hidden(object, name, value) {
        Object.defineProperty(object, name, {value: value, writable: true, configurable: true, enumerable: false});
    }
    // The values of an object's properties, for the old "for each" loop.
    hidden(global, "__jsflEach", function (object) {
        var values = [];
        if (object === null || object === undefined)
            return values;
        for (var key in object)
            values.push(object[key]);
        return values;
    });
    if (!Object.prototype.toSource)
        hidden(Object.prototype, "toSource", function () {
            try { return JSON.stringify(this); } catch (e) { return String(this); }
        });
    if (typeof global.uneval !== "function")
        hidden(global, "uneval", function (value) {
            return value === undefined ? "undefined" : JSON.stringify(value);
        });
    // Phoenix's own operations, the ones AI assistants use, by name with an
    // object of arguments: fl.phoenix.call("in_betweens", {layer: "Arm",
    // frame: 1, onTwos: true}).
    fl.phoenix = {
        call: function (name, args) {
            return JSON.parse(fl.phoenixCall(String(name), JSON.stringify(args || {})));
        },
        operations: function () {
            return JSON.parse(fl.phoenixCall("", "{}"));
        }
    };
    fl.Math = {
        concatMatrix: function (m, n) {
            return {a: m.a * n.a + m.b * n.c, b: m.a * n.b + m.b * n.d, c: m.c * n.a + m.d * n.c,
                d: m.c * n.b + m.d * n.d, tx: m.tx * n.a + m.ty * n.c + n.tx, ty: m.tx * n.b + m.ty * n.d + n.ty};
        },
        invertMatrix: function (m) {
            var det = m.a * m.d - m.b * m.c;
            if (!det)
                return {a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0};
            return {a: m.d / det, b: -m.b / det, c: -m.c / det, d: m.a / det,
                tx: (m.c * m.ty - m.d * m.tx) / det, ty: (m.b * m.tx - m.a * m.ty) / det};
        },
        pointDistance: function (p, q) {
            var dx = q.x - p.x, dy = q.y - p.y;
            return Math.sqrt(dx * dx + dy * dy);
        },
        transformPoint: function (m, p) {
            return {x: p.x * m.a + p.y * m.c + m.tx, y: p.x * m.b + p.y * m.d + m.ty};
        }
    };
    fl.fileExists = function (uri) { return FLfile.exists(uri); };
    // The ActionScript errors of the last publish: Phoenix reports them in
    // the Output panel instead, so there are none to save here.
    fl.compilerErrors = {
        clear: function () {},
        save: function (uri, append) {
            if (!append || !FLfile.exists(uri))
                FLfile.write(uri, "");
        }
    };
    // fl.tools outside a tool's own script (a tool's script has its own,
    // with the pen and keys as it works): the toolbox's tools and the one
    // in use.
    if (!fl.tools) {
        var toolObj = function (name) {
            this.__name = name;
            this.depth = 0;
            this.iconID = 0;
            this.position = 0;
        };
        ["setToolName", "setMenuString", "setToolTip", "setIcon", "setOptionsFile", "setPI", "showPIControl",
            "enablePIControl", "showTransformHandles"].forEach(function (name) {
            toolObj.prototype[name] = function () {};
        });
        fl.tools = {
            get toolObjs() {
                return fl.__toolNames().map(function (name, i) {
                    var tool = new toolObj(name);
                    tool.position = i;
                    return tool;
                });
            },
            get activeTool() {
                var name = fl.__activeTool();
                if (name === null)
                    return null;
                var tool = new toolObj(name);
                tool.position = fl.__toolNames().indexOf(name);
                return tool;
            },
            penLoc: {x: 0, y: 0}, penDownLoc: {x: 0, y: 0},
            mouseIsDown: false, altIsDown: false, ctlIsDown: false, shiftIsDown: false,
            getKeyDown: function () { return 0; },
            setCursor: function () {},
            snapPoint: function (point) { return {x: point.x, y: point.y}; },
            // Held to the nearest of the eight directions while Shift is down.
            constrainPoint: function (from, to) {
                if (!this.shiftIsDown)
                    return {x: to.x, y: to.y};
                var dx = to.x - from.x, dy = to.y - from.y;
                var step = Math.PI / 4;
                var angle = Math.round(Math.atan2(dy, dx) / step) * step;
                var ux = Math.cos(angle), uy = Math.sin(angle);
                if (Math.abs(ux) > 1e-9 && Math.abs(uy) > 1e-9) {
                    var d = Math.max(Math.abs(dx), Math.abs(dy));
                    return {x: from.x + (ux > 0 ? d : -d), y: from.y + (uy > 0 ? d : -d)};
                }
                var length = dx * ux + dy * uy;
                return {x: from.x + ux * length, y: from.y + uy * length};
            }
        };
    }
    global.an = fl;
})(this);
)JS";

bool identStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

bool identPart(char c)
{
    return identStart(c) || (c >= '0' && c <= '9');
}

bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// From \a at, past a string or template literal starting there.
size_t skipString(const std::string& s, size_t at)
{
    const char quote = s[at];
    size_t i = at + 1;
    while (i < s.size() && s[i] != quote)
    {
        if (s[i] == '\\')
            ++i;
        else if (quote != '`' && s[i] == '\n')
            break;
        ++i;
    }
    return std::min(i + 1, s.size());
}

/// The matching close parenthesis for the one at \a open, skipping strings
/// and comments; npos when there is none.
size_t matchParen(const std::string& s, size_t open, size_t& topLevelIn)
{
    int depth = 0;
    topLevelIn = std::string::npos;
    for (size_t i = open; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '"' || c == '\'' || c == '`')
        {
            i = skipString(s, i) - 1;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*')
        {
            const size_t end = s.find("*/", i + 2);
            if (end == std::string::npos)
                return std::string::npos;
            i = end + 1;
            continue;
        }
        if (c == '(' || c == '[' || c == '{')
            ++depth;
        else if (c == ')' || c == ']' || c == '}')
        {
            if (--depth == 0)
                return i;
        }
        else if (depth == 1 && topLevelIn == std::string::npos && c == 'i' && i + 1 < s.size() && s[i + 1] == 'n' &&
                 i > 0 && !identPart(s[i - 1]) && (i + 2 >= s.size() || !identPart(s[i + 2])))
            topLevelIn = i;
    }
    return std::string::npos;
}

/// The script as QuickJS compiles it: its E4X rewritten, with XML and
/// XMLList defined for it, then its old dialect.
std::string prepare(JSContext* ctx, const std::string& source)
{
    if (!e4x::mayUse(source))
        return Engine::modernize(source);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue helpers = JS_GetPropertyStr(ctx, global, "__e4x");
    if (JS_IsUndefined(helpers))
    {
        const char* prelude = e4x::prelude();
        JS_FreeValue(ctx, JS_Eval(ctx, prelude, std::strlen(prelude), "<phoenix-e4x>", JS_EVAL_TYPE_GLOBAL));
    }
    JS_FreeValue(ctx, helpers);
    JS_FreeValue(ctx, global);
    return Engine::modernize(e4x::rewrite(source));
}

/// fl.addEventListener and its kin, in every script's context.
const char* const kListeners = R"JS(
(function () {
    fl.__listeners = [];
    fl.__nextListener = 1;
    fl.addEventListener = function (type, listener) {
        if (typeof listener !== "function")
            throw new TypeError("addEventListener needs a function to call.");
        var id = fl.__nextListener++;
        fl.__listeners.push({ type: String(type), id: id, listener: listener });
        return id;
    };
    fl.removeEventListener = function (type, id) {
        var before = fl.__listeners.length;
        fl.__listeners = fl.__listeners.filter(function (entry) {
            return !(entry.type == type && (id === undefined || entry.id == id));
        });
        return fl.__listeners.length < before;
    };
    fl.__hears = function (type) {
        return fl.__listeners.some(function (entry) { return entry.type == type; });
    };
    fl.__dispatch = function (type) {
        var errors = [];
        fl.__listeners.slice().forEach(function (entry) {
            if (entry.type != type)
                return;
            try {
                entry.listener();
            } catch (e) {
                errors.push(type + " listener: " + e + (e && e.stack ? "\n" + e.stack : ""));
            }
        });
        return errors.join("\n");
    };
})();
)JS";

std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

/// What every script sees: fl, FLfile, the dialogs and Phoenix's prelude.
void installGlobals(JSContext* ctx, Binding& binding, Session& session, Host& host)
{
    JSValue global = JS_GetGlobalObject(ctx);
    // fl, FLfile and the dialogs.
    const std::shared_ptr<Flash> flash = makeFlash(session);
    JSValue fl = binding.wrap(flash);
    // fl.runScript works on script values directly, so a function
    // called by name gets the caller's own objects.
    binding.defineFunction(fl, "runScript", 2, [&binding, &session](JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 1)
            throw Error("fl.runScript needs a script URI.");
        const std::string file = uri::toPath(toStd(c, argv[0]));
        std::string text;
        if (!readFile(file, text))
            throw Error("Could not read the script " + file + ".");
        const std::string saved = session.scriptPath;
        session.scriptPath = file;
        const std::string modern = prepare(c, text);
        JSValue ran = JS_Eval(c, modern.c_str(), modern.size(), file.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ran))
        {
            session.scriptPath = saved;
            throw PendingException();
        }
        JS_FreeValue(c, ran);
        JSValue answer = JS_UNDEFINED;
        if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
        {
            const std::string name = toStd(c, argv[1]);
            JSValue scope = JS_GetGlobalObject(c);
            JSValue function = JS_GetPropertyStr(c, scope, name.c_str());
            JS_FreeValue(c, scope);
            if (!JS_IsFunction(c, function))
            {
                JS_FreeValue(c, function);
                session.scriptPath = saved;
                throw Error("fl.runScript: " + file + " has no function " + name + ".");
            }
            answer = JS_Call(c, function, JS_UNDEFINED, argc - 2, argv + 2);
            JS_FreeValue(c, function);
            if (JS_IsException(answer))
            {
                session.scriptPath = saved;
                throw PendingException();
            }
        }
        session.scriptPath = saved;
        (void)binding;
        return answer;
    });
    JS_SetPropertyStr(ctx, global, "fl", fl);
    const std::shared_ptr<FileSystem> files = makeFileSystem();
    JS_SetPropertyStr(ctx, global, "FLfile", binding.wrap(files));
    binding.singleton(flash);
    binding.singleton(files);
    binding.singleton(std::make_shared<OutputPanel>(session));
    binding.defineFunction(global, "alert", 1, [&host](JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
        host.alert(argc > 0 ? toStd(c, argv[0]) : std::string());
        return JS_UNDEFINED;
    });
    binding.defineFunction(global, "confirm", 1, [&host](JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
        return JS_NewBool(c, host.confirm(argc > 0 ? toStd(c, argv[0]) : std::string()));
    });
    binding.defineFunction(global, "prompt", 2, [&host](JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
        std::string answer;
        const std::string initial = argc > 1 && !JS_IsUndefined(argv[1]) ? toStd(c, argv[1]) : std::string();
        if (!host.prompt(argc > 0 ? toStd(c, argv[0]) : std::string(), initial, answer))
            return JS_NULL;
        return JS_NewStringLen(c, answer.data(), answer.size());
    });

    // Handler code from XML dialogs, and what their SWF panels give
    // MMExecute, runs here, beside the script's own functions; what it
    // throws is reported, not passed on.
    session.evaluate = [ctx](const std::string& code, std::string* value) -> std::string {
        const std::string modern = Engine::modernize(code);
        JSValue ran = JS_Eval(ctx, modern.c_str(), modern.size(), "<dialog>", JS_EVAL_TYPE_GLOBAL);
        if (!JS_IsException(ran))
        {
            if (value)
            {
                try
                {
                    *value = toStd(ctx, ran);
                }
                catch (const PendingException&)
                {
                    value->clear();
                }
            }
            JS_FreeValue(ctx, ran);
            return {};
        }
        JSValue error = JS_GetException(ctx);
        std::string message = "Error";
        try
        {
            message = toStd(ctx, error);
        }
        catch (const PendingException&)
        {
        }
        JS_FreeValue(ctx, error);
        return message;
    };

    JSValue prelude = JS_Eval(ctx, kPrelude, std::strlen(kPrelude), "<phoenix>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(ctx, prelude);
    JS_FreeValue(ctx, JS_Eval(ctx, kListeners, std::strlen(kListeners), "<phoenix-listeners>", JS_EVAL_TYPE_GLOBAL));

    // Native extension libraries, each an object of functions.
    native::install(ctx, host.configDirectory(), [&host](const std::string& message) { host.trace(message); });
    JS_FreeValue(ctx, global);
}

} // namespace

/// A script's context kept after its run: one that listens for events.
struct Engine::Kept
{
    std::string name;
    std::unique_ptr<Session> session;
    Guard guard;
    JSRuntime* runtime = nullptr;
    JSContext* ctx = nullptr;
    std::unique_ptr<Binding> binding;

    ~Kept()
    {
        if (ctx)
        {
            binding.reset();
            JS_FreeContext(ctx);
        }
        if (runtime)
            JS_FreeRuntime(runtime);
        if (session)
            session->detach();
    }

    /// Runs \a code, \a seconds at most when given; the error as
    /// "file:line: message", or empty. \a out gets the value as text.
    std::string evaluate(const std::string& code, std::string* out = nullptr, double seconds = 0.0)
    {
        guard.hasDeadline = seconds > 0.0;
        if (guard.hasDeadline)
            guard.deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
        guard.timedOut = false;
        guard.stopped = false;
        JSValue value = JS_Eval(ctx, code.c_str(), code.size(), "<phoenix>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(value))
        {
            JSValue error = JS_GetException(ctx);
            std::string message = "Error";
            try
            {
                message = toStd(ctx, error);
            }
            catch (const PendingException&)
            {
            }
            JS_FreeValue(ctx, error);
            return name + ": " + (guard.timedOut ? std::string("a listener took too long and was stopped.") : message);
        }
        if (out)
        {
            try
            {
                *out = JS_IsUndefined(value) || JS_IsNull(value) ? std::string() : toStd(ctx, value);
            }
            catch (const PendingException&)
            {
                out->clear();
            }
        }
        JS_FreeValue(ctx, value);
        return {};
    }
};

std::string Engine::modernize(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 64);
    // Whether a '/' here starts a regular expression rather than dividing.
    bool regexAllowed = true;
    size_t i = 0;
    while (i < s.size())
    {
        const char c = s[i];
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/')
        {
            const size_t end = s.find_first_of("\r\n", i);
            const size_t stop = end == std::string::npos ? s.size() : end;
            out.append(s, i, stop - i);
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*')
        {
            const size_t end = s.find("*/", i + 2);
            const size_t stop = end == std::string::npos ? s.size() : end + 2;
            out.append(s, i, stop - i);
            i = stop;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`')
        {
            const size_t stop = skipString(s, i);
            out.append(s, i, stop - i);
            i = stop;
            regexAllowed = false;
            continue;
        }
        if (c == '/' && regexAllowed)
        {
            size_t j = i + 1;
            bool inClass = false;
            while (j < s.size() && s[j] != '\n' && s[j] != '\r' && (inClass || s[j] != '/'))
            {
                if (s[j] == '\\')
                    ++j;
                else if (s[j] == '[')
                    inClass = true;
                else if (s[j] == ']')
                    inClass = false;
                ++j;
            }
            j = std::min(j + 1, s.size());
            while (j < s.size() && identPart(s[j]))
                ++j;
            out.append(s, i, j - i);
            i = j;
            regexAllowed = false;
            continue;
        }
        if (identStart(c))
        {
            size_t j = i;
            while (j < s.size() && identPart(s[j]))
                ++j;
            const std::string word = s.substr(i, j - i);
            const bool member = !out.empty() && out.back() == '.';
            if (word == "for" && !member)
            {
                // for each (x in y) -> for (x of __jsflEach(y))
                size_t k = j;
                while (k < s.size() && space(s[k]))
                    ++k;
                if (s.compare(k, 4, "each") == 0 && (k + 4 >= s.size() || !identPart(s[k + 4])))
                {
                    size_t open = k + 4;
                    while (open < s.size() && space(s[open]))
                        ++open;
                    size_t in = std::string::npos;
                    const size_t close = open < s.size() && s[open] == '(' ? matchParen(s, open, in) : std::string::npos;
                    if (close != std::string::npos && in != std::string::npos)
                    {
                        out += "for";
                        out.append(s, j, k - j);
                        out += modernize(s.substr(open, in - open));
                        out += "of __jsflEach(";
                        out += modernize(s.substr(in + 2, close - in - 2));
                        out += "))";
                        i = close + 1;
                        regexAllowed = false;
                        continue;
                    }
                }
            }
            if (word == "set" && !member)
            {
                // A setter with no parameter: set x() {} -> set x(__jsflValue) {}
                size_t k = j;
                while (k < s.size() && space(s[k]))
                    ++k;
                size_t nameEnd = k;
                while (nameEnd < s.size() && identPart(s[nameEnd]))
                    ++nameEnd;
                size_t open = nameEnd;
                while (open < s.size() && space(s[open]))
                    ++open;
                size_t close = open + 1;
                while (close < s.size() && space(s[close]))
                    ++close;
                if (nameEnd > k && k > j && open < s.size() && s[open] == '(' && close < s.size() && s[close] == ')')
                {
                    out.append(s, i, open + 1 - i);
                    out += "__jsflValue";
                    out.append(s, open + 1, close + 1 - (open + 1));
                    i = close + 1;
                    regexAllowed = false;
                    continue;
                }
            }
            if (word == "class" && !member)
            {
                // The old engine took "class" as a name (var class, a
                // parameter); a class declaration has a name after it.
                size_t k = j;
                while (k < s.size() && (s[k] == ' ' || s[k] == '\t'))
                    ++k;
                if (k >= s.size() || !identStart(s[k]) || s.compare(k, 7, "extends") == 0)
                {
                    const bool key = k < s.size() && s[k] == ':' && !out.empty() &&
                                     (out.back() == '{' || out.back() == ',' || space(out.back()));
                    if (!key)
                    {
                        out += "__jsflClass";
                        i = j;
                        regexAllowed = false;
                        continue;
                    }
                }
            }
            out += word;
            i = j;
            static const char* const kBeforeExpression[] = {"return", "typeof", "case", "do", "else", "in", "of", "new",
                "delete", "void", "throw", "instanceof", "yield"};
            regexAllowed = false;
            for (const char* keyword : kBeforeExpression)
                regexAllowed = regexAllowed || word == keyword;
            continue;
        }
        out += c;
        ++i;
        if (!space(c))
            regexAllowed = !(c == ')' || c == ']' || c == '}' || (c >= '0' && c <= '9') || c == '.');
    }
    return out;
}

Engine::Engine(Host& host)
    : _host(host)
{
}

RunResult Engine::runFile(const std::string& path, const RunOptions& options)
{
    std::string source;
    if (!readFile(path, source))
    {
        RunResult result;
        result.ok = false;
        result.file = path;
        result.message = "Could not read the script.";
        return result;
    }
    return run(source, path, path, options);
}

RunResult Engine::runString(const std::string& source, const std::string& name, const RunOptions& options)
{
    return run(source, name, std::string(), options);
}

RunResult Engine::run(const std::string& source, const std::string& name, const std::string& path, const RunOptions& options)
{
    RunResult result;
    if (_running)
    {
        result.ok = false;
        result.message = "A script is already running.";
        return result;
    }
    _running = true;
    _stop = false;

    auto kept = std::make_unique<Kept>();
    kept->name = name;
    kept->session = std::make_unique<Session>(_host, options.undoName);
    Session& session = *kept->session;
    session.scriptPath = path;

    JSRuntime* runtime = kept->runtime = JS_NewRuntime();
    JS_SetMemoryLimit(runtime, options.memoryLimit);
    JS_SetMaxStackSize(runtime, size_t(2) * 1024 * 1024);
    JS_NewClassID(runtime, &g_objectClass);
    JSClassDef objectClass{};
    objectClass.class_name = "PhoenixObject";
    objectClass.finalizer = &finalizeObject;
    JS_NewClass(runtime, g_objectClass, &objectClass);

    Guard& guard = kept->guard;
    guard.stop = &_stop;
    guard.poll = options.poll;
    guard.lastPoll = Clock::now();
    if (options.timeLimit > 0.0)
    {
        guard.hasDeadline = true;
        guard.deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(options.timeLimit * 1000.0));
    }
    JS_SetInterruptHandler(runtime, &interrupt, &guard);

    JSContext* ctx = kept->ctx = JS_NewContext(runtime);
    kept->binding = std::make_unique<Binding>(ctx);
    {
        Binding& binding = *kept->binding;
        JS_SetContextOpaque(ctx, &binding);
        JSValue global = JS_GetGlobalObject(ctx);

        installGlobals(ctx, binding, session, _host);

        const std::string modern = prepare(ctx, source);
        const int flags = JS_EVAL_TYPE_GLOBAL | (options.compileOnly ? JS_EVAL_FLAG_COMPILE_ONLY : 0);
        JSValue value = JS_Eval(ctx, modern.c_str(), modern.size(), name.c_str(), flags);
        if (JS_IsException(value))
        {
            // The old engine let a script return from its top level: such
            // a script runs as a function, its lines where they were.
            JSValue error = JS_GetException(ctx);
            std::string message;
            try
            {
                message = toStd(ctx, error);
            }
            catch (const PendingException&)
            {
            }
            if (message.find("return not in a function") != std::string::npos)
            {
                JS_FreeValue(ctx, error);
                const std::string wrapped = "(function () {" + modern + "\n})();";
                value = JS_Eval(ctx, wrapped.c_str(), wrapped.size(), name.c_str(), flags);
            }
            else
                JS_Throw(ctx, error);
        }
        // The function asked for, called once the script has run.
        if (!JS_IsException(value) && !options.compileOnly && !options.callFunction.empty())
        {
            JS_FreeValue(ctx, value);
            JSValue function = JS_GetPropertyStr(ctx, global, options.callFunction.c_str());
            if (!JS_IsFunction(ctx, function))
                value = JS_ThrowReferenceError(ctx, "%s has no function %s.", name.c_str(), options.callFunction.c_str());
            else
            {
                std::vector<JSValue> argv;
                try
                {
                    for (const Value& argument : options.callArguments)
                        argv.push_back(binding.toJS(argument));
                    value = JS_Call(ctx, function, JS_UNDEFINED, static_cast<int>(argv.size()), argv.data());
                }
                catch (const PendingException&)
                {
                    value = JS_EXCEPTION;
                }
                for (JSValue argument : argv)
                    JS_FreeValue(ctx, argument);
            }
            JS_FreeValue(ctx, function);
        }
        if (!JS_IsException(value) && !options.compileOnly)
        {
            try
            {
                result.returned = plainValue(binding.fromJS(value));
            }
            catch (const PendingException&)
            {
                JS_FreeValue(ctx, JS_GetException(ctx));
            }
        }
        if (JS_IsException(value))
        {
            JSValue error = JS_GetException(ctx);
            result.ok = false;
            try
            {
                result.message = toStd(ctx, error);
            }
            catch (const PendingException&)
            {
                result.message = "Error";
            }
            if (JS_IsError(error))
            {
                JSValue stack = JS_GetPropertyStr(ctx, error, "stack");
                if (JS_IsString(stack))
                    result.stack = toStd(ctx, stack);
                JS_FreeValue(ctx, stack);
            }
            locate(result.stack, result.file, result.line);
            if (result.file.empty())
                result.file = name;
            if (guard.stopped || guard.timedOut)
            {
                result.interrupted = true;
                result.message = guard.timedOut ? "The script took too long and was stopped." : "The script was stopped.";
            }
            else if (result.message.find("out of memory") != std::string::npos)
            {
                result.interrupted = true;
                result.message = "The script ran out of memory (it may use " +
                                 std::to_string(options.memoryLimit / (1024 * 1024)) + " MB).";
            }
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, value);
        JS_FreeValue(ctx, global);
    }
    // A script that listens for events stays loaded to hear them; any
    // other ends here.
    std::string listening;
    if (result.ok && !options.compileOnly)
        kept->evaluate("String(fl.__listeners.length)", &listening);
    _running = false;
    if (!listening.empty() && listening != "0")
    {
        session.settle();
        guard.poll = nullptr;
        guard.hasDeadline = false;
        _kept.push_back(std::move(kept));
    }
    else
    {
        session.evaluate = nullptr;
        session.finish();
        kept.reset();
    }
    flushEvents();
    return result;
}

Engine::~Engine() = default;

size_t Engine::listeningScripts() const
{
    return _kept.size();
}

void Engine::dispatch(const std::string& type)
{
    if (_kept.empty())
        return;
    // Events a script's own work raises wait for it to finish, so its
    // changes and a listener's are never one tangled step.
    if (_running || _dispatching)
    {
        if (_pending.empty() || _pending.back() != type)
            _pending.push_back(type);
        return;
    }
    _dispatching = true;
    for (size_t i = 0; i < _kept.size(); ++i)
    {
        Kept& kept = *_kept[i];
        // One not listening for it is left alone: settling brings the whole
        // window up to date, and playing raises an event every frame.
        std::string hears;
        kept.evaluate("String(fl.__hears(" + quoted(type) + "))", &hears);
        if (hears != "true")
            continue;
        std::string errors;
        const std::string failed = kept.evaluate("fl.__dispatch(" + quoted(type) + ")", &errors, 5.0);
        if (!failed.empty())
            _host.trace(failed);
        if (!errors.empty())
            _host.trace(errors);
        kept.session->settle();
        // A script that took away its last listener is done.
        std::string listening;
        kept.evaluate("String(fl.__listeners.length)", &listening);
        if (listening == "0")
        {
            _kept.erase(_kept.begin() + static_cast<std::ptrdiff_t>(i));
            --i;
        }
    }
    _dispatching = false;
    flushEvents();
}

void Engine::flushEvents()
{
    while (!_pending.empty() && !_running && !_dispatching)
    {
        const std::string type = _pending.front();
        _pending.erase(_pending.begin());
        dispatch(type);
    }
}

void Engine::forgetListeners()
{
    _pending.clear();
    _kept.clear();
}


// ---------------------------------------------------------------------------
// Tools

namespace {

/// fl.tools, ToolObj and fl.drawingLayer, kept in the tool's own context.
const char* const kToolPrelude = R"JS(
(function () {
    function ToolObj() {
        this.__name = ""; this.__menu = ""; this.__tip = ""; this.__icon = ""; this.__options = "";
        this.__pi = "shape"; this.__handles = false;
        this.depth = 0; this.iconID = 0; this.position = 0;
    }
    ToolObj.prototype.setToolName = function (name) { this.__name = String(name); };
    ToolObj.prototype.setMenuString = function (text) { this.__menu = String(text); };
    ToolObj.prototype.setToolTip = function (text) { this.__tip = String(text); };
    ToolObj.prototype.setIcon = function (file) { this.__icon = String(file); };
    ToolObj.prototype.setOptionsFile = function (file) { this.__options = String(file); };
    ToolObj.prototype.setPI = function (pi) { this.__pi = String(pi); };
    ToolObj.prototype.showPIControl = function () {};
    ToolObj.prototype.enablePIControl = function () {};
    ToolObj.prototype.showTransformHandles = function (show) { this.__handles = !!show; };

    var tools = {
        activeTool: null, toolObjs: [],
        penLoc: { x: 0, y: 0 }, penDownLoc: { x: 0, y: 0 },
        mouseIsDown: false, altIsDown: false, ctlIsDown: false, shiftIsDown: false,
        __key: 0, __cursor: 0,
        getKeyDown: function () { return this.__key; },
        setCursor: function (cursor) { this.__cursor = Number(cursor) || 0; },
        snapPoint: function (point) { return { x: point.x, y: point.y }; },
        // While Shift is down, the second point moved to the nearest of
        // the eight directions from the first, keeping its distance along
        // that direction.
        constrainPoint: function (from, to) {
            if (!this.shiftIsDown)
                return { x: to.x, y: to.y };
            var dx = to.x - from.x, dy = to.y - from.y;
            var step = Math.PI / 4;
            var angle = Math.round(Math.atan2(dy, dx) / step) * step;
            var ux = Math.cos(angle), uy = Math.sin(angle);
            if (Math.abs(ux) > 1e-9 && Math.abs(uy) > 1e-9) {
                var d = Math.max(Math.abs(dx), Math.abs(dy));
                return { x: from.x + (ux > 0 ? d : -d), y: from.y + (uy > 0 ? d : -d) };
            }
            var length = dx * ux + dy * uy;
            return { x: from.x + ux * length, y: from.y + uy * length };
        },
        __set: function (x, y, dx, dy, down, alt, ctl, shift, key) {
            this.penLoc = { x: x, y: y }; this.penDownLoc = { x: dx, y: dy };
            this.mouseIsDown = down; this.altIsDown = alt; this.ctlIsDown = ctl; this.shiftIsDown = shift;
            this.__key = key;
        },
        __configure: function () {
            var tool = new ToolObj();
            this.activeTool = tool;
            this.toolObjs.push(tool);
            if (typeof configureTool === "function")
                configureTool();
        }
    };

    function Path() { this.__contours = [{ pieces: [], closed: false }]; }
    Path.prototype.__contour = function () { return this.__contours[this.__contours.length - 1]; };
    Path.prototype.__last = function () {
        var c = this.__contour(), p = c.pieces[c.pieces.length - 1];
        return p ? { x: p[p.length - 2], y: p[p.length - 1] } : null;
    };
    Path.prototype.__start = function (x, y) {
        var c = this.__contour(), last = this.__last();
        if (!last) c.pieces.push(["M", x, y]);
        else if (last.x != x || last.y != y) c.pieces.push(["L", x, y]);
    };
    Path.prototype.addPoint = function (x, y) {
        var c = this.__contour();
        c.pieces.push([c.pieces.length ? "L" : "M", Number(x), Number(y)]);
    };
    Path.prototype.addCurve = function (x1, y1, x2, y2, x3, y3) {
        this.__start(Number(x1), Number(y1));
        this.__contour().pieces.push(["Q", Number(x2), Number(y2), Number(x3), Number(y3)]);
    };
    Path.prototype.addCubicCurve = function (x1, y1, x2, y2, x3, y3, x4, y4) {
        this.__start(Number(x1), Number(y1));
        this.__contour().pieces.push(["C", Number(x2), Number(y2), Number(x3), Number(y3), Number(x4), Number(y4)]);
    };
    Path.prototype.close = function () { this.__contour().closed = true; };
    Path.prototype.newContour = function () { this.__contours.push({ pieces: [], closed: false }); };
    Path.prototype.clear = function () { this.__contours = [{ pieces: [], closed: false }]; };
    Path.prototype.getNumberOfPoints = function () {
        var n = 0;
        this.__contours.forEach(function (c) { n += c.pieces.length; });
        return n;
    };
    Path.prototype.__svg = function () {
        var d = [];
        this.__contours.forEach(function (c) {
            c.pieces.forEach(function (p) { d.push(p.join(" ")); });
            if (c.closed && c.pieces.length) d.push("Z");
        });
        return d.join(" ");
    };
    Path.prototype.makeShape = function (suppressFill, suppressStroke) {
        var svg = this.__svg();
        if (svg.length) fl.getDocumentDOM().addNewPath(svg, !suppressFill, !suppressStroke);
    };

    var drawing = {
        __paths: [], __current: null, __color: 0x00A0FF, __persistent: false,
        __path: function () {
            if (!this.__current) {
                this.__current = { color: this.__color, pieces: [] };
                this.__paths.push(this.__current);
            }
            return this.__current;
        },
        beginDraw: function (persistent) { this.__persistent = !!persistent; this.__paths = []; this.__current = null; },
        endDraw: function () { if (!this.__persistent) { this.__paths = []; } this.__current = null; },
        beginFrame: function () { this.__paths = []; this.__current = null; },
        endFrame: function () { this.__current = null; },
        setColor: function (color) {
            this.__color = typeof color === "string" ? parseInt(color.replace("#", "").slice(0, 6), 16) : Number(color);
            this.__current = null;
        },
        moveTo: function (x, y) { this.__path().pieces.push(["M", Number(x), Number(y)]); },
        lineTo: function (x, y) { this.__path().pieces.push(["L", Number(x), Number(y)]); },
        curveTo: function (cx, cy, x, y) { this.__path().pieces.push(["Q", Number(cx), Number(cy), Number(x), Number(y)]); },
        cubicCurveTo: function (x1, y1, x2, y2, x, y) {
            this.__path().pieces.push(["C", Number(x1), Number(y1), Number(x2), Number(y2), Number(x), Number(y)]);
        },
        frameRect: function (r) {
            this.moveTo(r.left, r.top); this.lineTo(r.right, r.top); this.lineTo(r.right, r.bottom);
            this.lineTo(r.left, r.bottom); this.lineTo(r.left, r.top);
        },
        drawPath: function (path) {
            var self = this;
            path.__contours.forEach(function (c) {
                c.pieces.forEach(function (p) { self.__path().pieces.push(p.slice()); });
                if (c.closed && c.pieces.length) self.__path().pieces.push(["L", c.pieces[0][1], c.pieces[0][2]]);
            });
        },
        newPath: function () { return new Path(); },
        // One line a path: its color, then its pieces.
        __flat: function () {
            return this.__paths.map(function (p) {
                return [p.color].concat(p.pieces.map(function (piece) { return piece.join(" "); })).join(";");
            }).join("\n");
        }
    };

    fl.tools = tools;
    fl.drawingLayer = drawing;
    this.ToolObj = ToolObj;
})();
)JS";

std::string errorOf(JSContext* ctx)
{
    JSValue error = JS_GetException(ctx);
    std::string message = "Error";
    try
    {
        message = toStd(ctx, error);
    }
    catch (const PendingException&)
    {
    }
    std::string stack;
    if (JS_IsError(error))
    {
        JSValue value = JS_GetPropertyStr(ctx, error, "stack");
        if (JS_IsString(value))
            stack = toStd(ctx, value);
        JS_FreeValue(ctx, value);
    }
    JS_FreeValue(ctx, error);
    std::string file;
    int line = 0;
    locate(stack, file, line);
    if (file.empty())
        return message;
    return file + (line > 0 ? ":" + std::to_string(line) : std::string()) + ": " + message;
}

std::string literal(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
        {
            out += "\\n";
            continue;
        }
        if (c == '\r')
        {
            out += "\\r";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

std::string numberText(double value)
{
    if (!std::isfinite(value))
        return "0";
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

} // namespace

struct ToolScript::Impl
{
    Host& host;
    std::string path;
    std::string name, menu, tip, icon;
    std::vector<Option> options;
    std::unique_ptr<Session> session;
    JSRuntime* runtime = nullptr;
    JSContext* ctx = nullptr;
    std::unique_ptr<Binding> binding;
    std::atomic<bool> stop{false};
    Guard guard;
    /// A call is running: what it sets off (its own selectTool, a frame it
    /// moves to) comes back while it runs.
    bool busy = false;

    Impl(Host& host, std::string path)
        : host(host)
        , path(std::move(path))
    {}

    ~Impl()
    {
        if (ctx)
        {
            binding.reset();
            JS_FreeContext(ctx);
        }
        if (runtime)
            JS_FreeRuntime(runtime);
        // Every call settled its steps; the window may be closing now.
        if (session)
            session->detach();
    }

    /// Runs \a code, a call's worth of time at most; the error, or empty.
    /// \a out, when given, gets the value as text.
    std::string eval(const std::string& code, const std::string& file, std::string* out = nullptr)
    {
        guard.hasDeadline = true;
        guard.deadline = Clock::now() + std::chrono::seconds(10);
        guard.timedOut = false;
        guard.stopped = false;
        JSValue value = JS_Eval(ctx, code.c_str(), code.size(), file.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(value))
        {
            if (guard.timedOut)
            {
                JS_FreeValue(ctx, JS_GetException(ctx));
                return file + ": the tool took too long and was stopped.";
            }
            return errorOf(ctx);
        }
        if (out)
        {
            try
            {
                *out = JS_IsUndefined(value) || JS_IsNull(value) ? std::string() : toStd(ctx, value);
            }
            catch (const PendingException&)
            {
                out->clear();
            }
        }
        JS_FreeValue(ctx, value);
        return {};
    }

    std::string text(const std::string& expression)
    {
        std::string out;
        eval("String(" + expression + ")", "<tool>", &out);
        return out;
    }

    /// The step a call made ends, and the window shows it.
    void settle()
    {
        session->setUndoName(name.empty() ? "Tool" : name);
        session->settle();
    }
};

ToolScript::ToolScript(Host& host, std::string path)
    : _impl(std::make_unique<Impl>(host, std::move(path)))
{}

ToolScript::~ToolScript() = default;

bool ToolScript::load(std::string& error)
{
    Impl& d = *_impl;
    std::string source;
    if (!readFile(d.path, source))
    {
        error = "Could not read " + d.path + ".";
        return false;
    }
    d.session = std::make_unique<Session>(d.host, "Tool");
    d.session->scriptPath = d.path;
    d.runtime = JS_NewRuntime();
    JS_SetMemoryLimit(d.runtime, size_t(256) * 1024 * 1024);
    JS_SetMaxStackSize(d.runtime, size_t(2) * 1024 * 1024);
    JS_NewClassID(d.runtime, &g_objectClass);
    JSClassDef objectClass{};
    objectClass.class_name = "PhoenixObject";
    objectClass.finalizer = &finalizeObject;
    JS_NewClass(d.runtime, g_objectClass, &objectClass);
    d.guard.stop = &d.stop;
    d.guard.lastPoll = Clock::now();
    JS_SetInterruptHandler(d.runtime, &interrupt, &d.guard);
    d.ctx = JS_NewContext(d.runtime);
    d.binding = std::make_unique<Binding>(d.ctx);
    JS_SetContextOpaque(d.ctx, d.binding.get());
    // One session for the tool's life: each call settles it, ending the
    // undo step the call made.
    installGlobals(d.ctx, *d.binding, *d.session, d.host);
    std::string failed = d.eval(kToolPrelude, "<phoenix-tools>");
    if (failed.empty())
        failed = d.eval(prepare(d.ctx, source), d.path);
    if (failed.empty())
        failed = d.eval("fl.tools.__configure()", d.path);
    if (!failed.empty())
    {
        error = failed;
        return false;
    }
    d.name = d.text("fl.tools.activeTool.__name");
    d.menu = d.text("fl.tools.activeTool.__menu");
    d.tip = d.text("fl.tools.activeTool.__tip");
    const std::filesystem::path folder = std::filesystem::u8path(d.path).parent_path();
    const std::string icon = d.text("fl.tools.activeTool.__icon");
    if (!icon.empty())
        d.icon = (folder / std::filesystem::u8path(icon)).u8string();
    if (d.name.empty())
        d.name = std::filesystem::u8path(d.path).stem().u8string();

    // The options file: each property a setting the tool reads off its
    // ToolObj, starting at its default.
    const std::string optionsFile = d.text("fl.tools.activeTool.__options");
    if (!optionsFile.empty())
    {
        std::string xml;
        if (readFile((folder / std::filesystem::u8path(optionsFile)).u8string(), xml))
        {
            tinyxml2::XMLDocument doc;
            if (doc.Parse(xml.c_str(), xml.size()) == tinyxml2::XML_SUCCESS && doc.RootElement())
            {
                for (const tinyxml2::XMLElement* p = doc.RootElement()->FirstChildElement("property"); p;
                     p = p->NextSiblingElement("property"))
                {
                    Option option;
                    option.name = p->Attribute("name") ? p->Attribute("name") : "";
                    option.variable = p->Attribute("variable") ? p->Attribute("variable") : "";
                    option.type = p->Attribute("type") ? p->Attribute("type") : "Number";
                    option.value = p->Attribute("defaultValue") ? p->Attribute("defaultValue") : "";
                    if (const char* list = p->Attribute("list"))
                    {
                        std::string item;
                        std::istringstream in(list);
                        while (std::getline(in, item, ','))
                            option.list.push_back(item);
                    }
                    option.min = p->DoubleAttribute("min", option.min);
                    option.max = p->DoubleAttribute("max", option.max);
                    if (option.variable.empty())
                        continue;
                    d.options.push_back(option);
                }
            }
        }
    }
    std::vector<std::pair<std::string, std::string>> defaults;
    for (const Option& option : d.options)
        defaults.emplace_back(option.variable, option.value);
    failed = setOptions(defaults);
    d.settle();
    if (!failed.empty())
    {
        error = failed;
        return false;
    }
    return true;
}

const std::string& ToolScript::toolName() const { return _impl->name; }
const std::string& ToolScript::menuString() const { return _impl->menu; }
const std::string& ToolScript::toolTip() const { return _impl->tip; }
const std::string& ToolScript::iconPath() const { return _impl->icon; }
const std::string& ToolScript::path() const { return _impl->path; }

bool ToolScript::has(const std::string& function) const
{
    if (!_impl->ctx)
        return false;
    return _impl->text("typeof " + function) == "function";
}

std::string ToolScript::call(const std::string& function, const State& state)
{
    Impl& d = *_impl;
    if (!d.ctx || !has(function))
        return {};
    // A call made while another runs (deactivate, when the script picks
    // another tool) is part of that one, which settles once at its end.
    const bool nested = d.busy;
    d.busy = true;
    std::string failed = d.eval("fl.tools.__set(" + numberText(state.penX) + "," + numberText(state.penY) + "," +
            numberText(state.downX) + "," + numberText(state.downY) + "," + (state.mouseDown ? "true" : "false") + "," +
            (state.alt ? "true" : "false") + "," + (state.ctrl ? "true" : "false") + "," + (state.shift ? "true" : "false") +
            "," + std::to_string(state.key) + ")",
        "<tool>");
    if (failed.empty())
        failed = d.eval(function + "({x:" + numberText(state.penX) + ",y:" + numberText(state.penY) + "})", d.path);
    if (!nested)
    {
        d.busy = false;
        d.settle();
    }
    return failed;
}

std::string ToolScript::dispatch(const std::string& type)
{
    Impl& d = *_impl;
    // The events a call sets off are its own doing, not news to it.
    if (!d.ctx || d.busy)
        return {};
    // A tool not listening for it is left alone: settling brings the whole
    // window up to date, and playing raises an event every frame.
    if (d.text("fl.__hears(" + quoted(type) + ")") != "true")
        return {};
    std::string errors;
    std::string failed = d.eval("fl.__dispatch(" + quoted(type) + ")", d.path, &errors);
    d.settle();
    return failed.empty() ? errors : failed;
}

std::vector<ToolScript::Preview> ToolScript::preview() const
{
    std::vector<Preview> paths;
    if (!_impl->ctx)
        return paths;
    const std::string flat = _impl->text("fl.drawingLayer.__flat()");
    std::istringstream lines(flat);
    std::string line;
    while (std::getline(lines, line))
    {
        std::istringstream parts(line);
        std::string part;
        Preview path;
        bool first = true;
        while (std::getline(parts, part, ';'))
        {
            if (first)
            {
                path.color = static_cast<uint32_t>(std::strtoul(part.c_str(), nullptr, 10));
                first = false;
                continue;
            }
            std::istringstream fields(part);
            Preview::Piece piece;
            std::string kind;
            fields >> kind;
            piece.kind = kind.empty() ? 'L' : kind[0];
            double value;
            while (fields >> value)
                piece.points.push_back(value);
            path.pieces.push_back(piece);
        }
        if (!path.pieces.empty())
            paths.push_back(path);
    }
    return paths;
}

int ToolScript::cursor() const
{
    if (!_impl->ctx)
        return 0;
    return std::atoi(_impl->text("fl.tools.__cursor").c_str());
}

std::vector<ToolScript::Option> ToolScript::options() const
{
    std::vector<Option> options = _impl->options;
    if (_impl->ctx)
        for (Option& option : options)
            option.value = _impl->text("fl.tools.activeTool[" + literal(option.variable) + "]");
    return options;
}

std::string ToolScript::setOptions(const std::vector<std::pair<std::string, std::string>>& values)
{
    Impl& d = *_impl;
    if (!d.ctx)
        return {};
    for (const auto& [variable, value] : values)
    {
        std::string type = "Number";
        for (const Option& option : d.options)
            if (option.variable == variable)
                type = option.type;
        std::string code;
        if (type == "Boolean")
            code = value == "true" || value == "1" ? "true" : "false";
        else if (type == "Number" || type == "Strings")
        {
            char* end = nullptr;
            const double number = std::strtod(value.c_str(), &end);
            code = end && end != value.c_str() ? numberText(number) : literal(value);
        }
        else
            code = literal(value);
        const std::string failed = d.eval("fl.tools.activeTool[" + literal(variable) + "] = " + code, "<tool>");
        if (!failed.empty())
            return failed;
    }
    std::string failed;
    if (has("notifySettingsChanged"))
        failed = d.eval("notifySettingsChanged()", d.path);
    d.settle();
    return failed;
}


// ---------------------------------------------------------------------------
// Extension panels

struct PanelScript::Impl
{
    Host& host;
    std::string name;
    std::unique_ptr<Session> session;
    JSRuntime* runtime = nullptr;
    JSContext* ctx = nullptr;
    std::unique_ptr<Binding> binding;
    std::atomic<bool> stop{false};
    bool running = false;
    Guard guard;

    Impl(Host& host, std::string name)
        : host(host)
        , name(std::move(name))
    {}

    ~Impl()
    {
        if (ctx)
        {
            binding.reset();
            JS_FreeContext(ctx);
        }
        if (runtime)
            JS_FreeRuntime(runtime);
        if (session)
            session->detach();
    }

    PanelScript::Result eval(const std::string& code, const std::string& file, const std::function<void()>& poll)
    {
        PanelScript::Result result;
        stop = false;
        guard.poll = poll;
        guard.lastPoll = Clock::now();
        guard.timedOut = false;
        guard.stopped = false;
        running = true;
        JSValue value = JS_Eval(ctx, code.c_str(), code.size(), file.c_str(), JS_EVAL_TYPE_GLOBAL);
        running = false;
        guard.poll = nullptr;
        if (JS_IsException(value))
        {
            result.ok = false;
            if (guard.stopped)
            {
                JS_FreeValue(ctx, JS_GetException(ctx));
                result.error = file + ": the script was stopped.";
            }
            else
                result.error = errorOf(ctx);
        }
        else
        {
            try
            {
                result.value = toStd(ctx, value);
            }
            catch (const PendingException&)
            {
                JS_FreeValue(ctx, JS_GetException(ctx));
            }
        }
        JS_FreeValue(ctx, value);
        session->setUndoName(name);
        session->settle();
        return result;
    }
};

PanelScript::PanelScript(Host& host, std::string name)
    : _impl(std::make_unique<Impl>(host, std::move(name)))
{}

PanelScript::~PanelScript() = default;

bool PanelScript::load(const std::string& path, std::string& error)
{
    Impl& d = *_impl;
    if (!d.ctx)
    {
        d.session = std::make_unique<Session>(d.host, d.name);
        d.session->scriptPath = path;
        d.runtime = JS_NewRuntime();
        JS_SetMemoryLimit(d.runtime, size_t(512) * 1024 * 1024);
        JS_SetMaxStackSize(d.runtime, size_t(2) * 1024 * 1024);
        JS_NewClassID(d.runtime, &g_objectClass);
        JSClassDef objectClass{};
        objectClass.class_name = "PhoenixObject";
        objectClass.finalizer = &finalizeObject;
        JS_NewClass(d.runtime, g_objectClass, &objectClass);
        d.guard.stop = &d.stop;
        JS_SetInterruptHandler(d.runtime, &interrupt, &d.guard);
        d.ctx = JS_NewContext(d.runtime);
        d.binding = std::make_unique<Binding>(d.ctx);
        JS_SetContextOpaque(d.ctx, d.binding.get());
        installGlobals(d.ctx, *d.binding, *d.session, d.host);
    }
    if (path.empty())
        return true;
    std::string source;
    if (!readFile(path, source))
    {
        error = "Could not read " + path + ".";
        return false;
    }
    const Result result = d.eval(prepare(d.ctx, source), path, {});
    error = result.error;
    return result.ok;
}

PanelScript::Result PanelScript::evaluate(const std::string& code, const std::function<void()>& poll)
{
    Impl& d = *_impl;
    if (!d.ctx)
    {
        std::string error;
        load(std::string(), error);
    }
    if (d.running)
    {
        Result busy;
        busy.ok = false;
        busy.error = d.name + ": a script is already running.";
        return busy;
    }
    return d.eval(prepare(d.ctx, code), d.name, poll);
}

void PanelScript::stop()
{
    _impl->stop = true;
}

bool PanelScript::running() const
{
    return _impl->running;
}

std::string PanelScript::dispatch(const std::string& type)
{
    Impl& d = *_impl;
    if (!d.ctx || d.running)
        return {};
    const Result result = d.eval("fl.__dispatch(" + quoted(type) + ")", d.name, {});
    if (!result.ok)
        return result.error;
    return result.value == "undefined" ? std::string() : result.value;
}

} // namespace jsfl
