// The QuickJS binding: the object model's class tables made into JavaScript
// prototypes, values converted both ways, and a run from start to finish.

#include "engine.h"

#include "e4x.h"
#include "flfile.h"
#include "model.h"

#include <quickjs.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

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

    std::vector<Function> _functions;
    std::map<const ClassInfo*, JSValue> _prototypes;
    std::map<const ClassInfo*, ObjectPtr> _singletons;
    std::map<std::tuple<const ClassInfo*, const void*, const void*>, JSValue> _identities;
};

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
        }
    };
    fl.fileExists = function (uri) { return FLfile.exists(uri); };
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

} // namespace

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

    Session session(_host, options.undoName);
    session.scriptPath = path;

    JSRuntime* runtime = JS_NewRuntime();
    JS_SetMemoryLimit(runtime, options.memoryLimit);
    JS_SetMaxStackSize(runtime, size_t(2) * 1024 * 1024);
    JS_NewClassID(runtime, &g_objectClass);
    JSClassDef objectClass{};
    objectClass.class_name = "PhoenixObject";
    objectClass.finalizer = &finalizeObject;
    JS_NewClass(runtime, g_objectClass, &objectClass);

    Guard guard;
    guard.stop = &_stop;
    guard.poll = options.poll;
    guard.lastPoll = Clock::now();
    if (options.timeLimit > 0.0)
    {
        guard.hasDeadline = true;
        guard.deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(options.timeLimit * 1000.0));
    }
    JS_SetInterruptHandler(runtime, &interrupt, &guard);

    JSContext* ctx = JS_NewContext(runtime);
    {
        Binding binding(ctx);
        JS_SetContextOpaque(ctx, &binding);
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
        Host& host = _host;
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

        // Handler code from XML dialogs runs here, beside the script's own
        // functions; what it throws is reported, not passed on.
        session.evaluate = [ctx](const std::string& code) -> std::string {
            const std::string modern = Engine::modernize(code);
            JSValue ran = JS_Eval(ctx, modern.c_str(), modern.size(), "<dialog>", JS_EVAL_TYPE_GLOBAL);
            if (!JS_IsException(ran))
            {
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
        session.evaluate = nullptr;
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    session.finish();
    _running = false;
    return result;
}

} // namespace jsfl
