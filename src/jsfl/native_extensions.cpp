// Native extension libraries (C-level extensibility): loading them from
// External Libraries, and mm_jsapi.h's table of functions made on QuickJS.
//
// A jsval is tagged as the C API's inline macros make them: odd for an
// integer (the value shifted up one), 6 in the low three bits for a
// boolean. Everything else is a handle into the table of values of the
// native call in progress: 0 in the low bits for an object (0 itself is
// null; a JSObject* is the same number), 2 for a number that is not a small
// integer, 4 for a string. Handles, and the strings handed out, last until
// the native function returns, so a library never holds QuickJS's values.

#include "native_extensions.h"

#include <quickjs.h>

// mm_jsapi.h's JSContext and JSObject are not QuickJS's.
#define JSContext MM_JSContext
#define JSObject MM_JSObject
#define MM_JSAPI_HOST
#include "mm_jsapi.h"
#undef MM_JSAPI_HOST
#undef JSObject
#undef JSContext

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace jsfl::native {

namespace {

// Opening shared libraries, the one part that differs by platform.

#ifdef _WIN32

void* openLibrary(const fs::path& path, std::string& error)
{
    // Its own folder first, for libraries it brings along.
    HMODULE module = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module)
    {
        wchar_t* text = nullptr;
        const DWORD code = GetLastError();
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
            reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        error = text ? fs::path(text).u8string() : "error " + std::to_string(code);
        if (text)
            LocalFree(text);
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r' || error.back() == ' ' || error.back() == '.'))
            error.pop_back();
    }
    return module;
}

void* findSymbol(void* library, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

#else

void* openLibrary(const fs::path& path, std::string& error)
{
    fs::path file = path;
    // A macOS bundle is a folder; its code is in Contents/MacOS.
    std::error_code ignored;
    if (fs::is_directory(path, ignored))
    {
        const fs::path code = path / "Contents" / "MacOS";
        file = code / path.stem();
        if (!fs::exists(file, ignored))
        {
            for (const auto& entry : fs::directory_iterator(code, ignored))
            {
                if (entry.is_regular_file(ignored))
                {
                    file = entry.path();
                    break;
                }
            }
        }
    }
    void* library = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        const char* text = dlerror();
        error = text ? text : "it could not be opened";
    }
    return library;
}

void* findSymbol(void* library, const char* name)
{
    return dlsym(library, name);
}

#endif

typedef void (*InitWrapper)(MM_Environment* env, unsigned int envSize);

struct Function
{
    std::string name;
    JSNative call = nullptr;
    unsigned int nargs = 0;
};

struct Library
{
    std::string name;
    /// The External Libraries folder it came from.
    fs::path folder;
    std::vector<Function> functions;
};

std::mutex g_mutex;
/// Never unloaded: their code may still be in use, and they keep state.
std::vector<std::unique_ptr<Library>> g_libraries;
std::vector<fs::path> g_tried;
int g_enabled = -1;

/// The library whose MM_Init is running, which JS_DefineFunction adds to.
thread_local Library* g_loading = nullptr;

// The tags of a jsval.
const jsval kVoid = static_cast<jsval>(-2147483647L);
const jsval kObjectTag = 0;
const jsval kDoubleTag = 2;
const jsval kStringTag = 4;
const jsval kBooleanTag = 6;
const long kIntLimit = (1L << 30) - 1;
/// The most handles one call may make, so each fits a 32-bit jsval.
const size_t kMostHandles = size_t(1) << 27;

/// One native function call in progress: what MM_JSContext points at.
struct Frame
{
    JSContext* ctx;
    Frame* outer;
    std::vector<JSValue> values;
    std::deque<std::u16string> strings;
    std::deque<std::string> bytes;
    std::string error;
    JSValue exception = JS_UNDEFINED;
    bool threw = false;

    Frame(JSContext* ctx, Frame* outer)
        : ctx(ctx)
        , outer(outer)
    {
        values.push_back(JS_UNDEFINED);
    }

    ~Frame()
    {
        for (JSValue& value : values)
            JS_FreeValue(ctx, value);
        JS_FreeValue(ctx, exception);
    }

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    /// A handle for \a value, which the frame now owns, with \a tag.
    bool keep(JSValue value, jsval tag, jsval& out)
    {
        if (values.size() >= kMostHandles)
        {
            JS_FreeValue(ctx, value);
            return false;
        }
        values.push_back(value);
        out = static_cast<jsval>((values.size() - 1) << 3) | tag;
        return true;
    }

    /// \a value as a jsval; it stays the caller's.
    bool toJsval(JSValueConst value, jsval& out)
    {
        if (JS_IsUndefined(value))
            out = kVoid;
        else if (JS_IsNull(value))
            out = 0;
        else if (JS_IsBool(value))
            out = (static_cast<jsval>(JS_ToBool(ctx, value) > 0) << 3) | kBooleanTag;
        else if (JS_IsNumber(value))
        {
            double number = 0.0;
            JS_ToFloat64(ctx, &number, value);
            if (number == std::floor(number) && std::fabs(number) <= kIntLimit && !(number == 0.0 && std::signbit(number)))
                out = (static_cast<jsval>(number) * 2) | 1;
            else
                return keep(JS_DupValue(ctx, value), kDoubleTag, out);
        }
        else if (JS_IsString(value))
            return keep(JS_DupValue(ctx, value), kStringTag, out);
        else if (JS_IsObject(value))
            return keep(JS_DupValue(ctx, value), kObjectTag, out);
        else
            return keep(JS_DupValue(ctx, value), kDoubleTag, out);
        return true;
    }

    /// The value \a v stands for, a new reference; false when it is not
    /// one this call made.
    bool fromJsval(jsval v, JSValue& out)
    {
        if (v == kVoid)
            out = JS_UNDEFINED;
        else if (v & 1)
            out = JS_NewInt64(ctx, static_cast<int64_t>(v >> 1));
        else if ((v & 7) == kBooleanTag)
            out = JS_NewBool(ctx, (v >> 3) != 0);
        else if (v == 0)
            out = JS_NULL;
        else
        {
            const size_t index = static_cast<size_t>(static_cast<unsigned long>(v) >> 3);
            if (index == 0 || index >= values.size())
                return false;
            out = JS_DupValue(ctx, values[index]);
        }
        return true;
    }

    /// The object \a object stands for, borrowed; null when it is not one.
    JSValue* object(MM_JSObject* object)
    {
        const uintptr_t bits = reinterpret_cast<uintptr_t>(object);
        const size_t index = static_cast<size_t>(bits >> 3);
        if ((bits & 7) != 0 || index == 0 || index >= values.size() || !JS_IsObject(values[index]))
            return nullptr;
        return &values[index];
    }

    MM_JSObject* objectHandle(jsval v) { return reinterpret_cast<MM_JSObject*>(static_cast<uintptr_t>(static_cast<unsigned long>(v))); }

    /// Takes the exception pending in the context, for when the call ends.
    void takeException()
    {
        JS_FreeValue(ctx, exception);
        exception = JS_GetException(ctx);
        threw = true;
    }

    unsigned short* hold(std::u16string text, unsigned int* length)
    {
        if (length)
            *length = static_cast<unsigned int>(text.size());
        strings.push_back(std::move(text));
        return reinterpret_cast<unsigned short*>(&strings.back()[0]);
    }
};

/// The calls in progress on this thread, innermost first.
thread_local Frame* g_frame = nullptr;

Frame* frameOf(MM_JSContext* cx)
{
    for (Frame* frame = g_frame; frame; frame = frame->outer)
    {
        if (reinterpret_cast<MM_JSContext*>(frame) == cx)
            return frame;
    }
    return nullptr;
}

/// UTF-8 (or CESU-8, surrogates encoded one at a time) as UTF-16.
std::u16string utf16(const char* text, size_t length)
{
    std::u16string out;
    out.reserve(length);
    const auto* s = reinterpret_cast<const unsigned char*>(text);
    size_t i = 0;
    while (i < length)
    {
        const unsigned char c = s[i];
        uint32_t code = 0xFFFD;
        size_t extra = 0;
        if (c < 0x80)
            code = c;
        else if ((c & 0xE0) == 0xC0)
        {
            code = c & 0x1F;
            extra = 1;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            code = c & 0x0F;
            extra = 2;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            code = c & 0x07;
            extra = 3;
        }
        ++i;
        for (size_t k = 0; k < extra; ++k, ++i)
        {
            if (i >= length || (s[i] & 0xC0) != 0x80)
            {
                code = 0xFFFD;
                break;
            }
            code = (code << 6) | (s[i] & 0x3F);
        }
        if (code >= 0x10000)
        {
            code -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (code >> 10));
            out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        }
        else
            out += static_cast<char16_t>(code);
    }
    return out;
}

/// \a length units of \a text, or up to its terminating zero when
/// \a length is 0.
std::u16string utf16From(const unsigned short* text, unsigned int length)
{
    if (!text)
        return {};
    size_t count = length;
    if (count == 0)
    {
        while (text[count] != 0)
            ++count;
    }
    return std::u16string(reinterpret_cast<const char16_t*>(text), count);
}

std::string utf8(const std::u16string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        uint32_t code = text[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
        {
            code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (code < 0x80)
            out += static_cast<char>(code);
        else if (code < 0x800)
        {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else if (code < 0x10000)
        {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

// The table's functions. Each checks that it is handed a call in progress
// and values of that call, and fails rather than trusting the library.

JSBool defineFunction(MM_JSObject* libObj, unsigned short* name, JSNative call, unsigned int nargs)
{
    Library* library = g_loading;
    if (!library || reinterpret_cast<MM_JSObject*>(library) != libObj || !name || !call)
        return 0;
    Function function;
    function.name = utf8(utf16From(name, 0));
    function.call = call;
    function.nargs = nargs;
    if (function.name.empty())
        return 0;
    for (Function& existing : library->functions)
    {
        if (existing.name == function.name)
        {
            existing = function;
            return 1;
        }
    }
    library->functions.push_back(function);
    return 1;
}

unsigned short* valueToString(MM_JSContext* cx, jsval v, unsigned int* pLength)
{
    Frame* frame = frameOf(cx);
    JSValue value;
    if (!frame || !frame->fromJsval(v, value))
        return nullptr;
    size_t length = 0;
    const char* text = JS_ToCStringLen2(frame->ctx, &length, value, true);
    JS_FreeValue(frame->ctx, value);
    if (!text)
    {
        frame->takeException();
        return nullptr;
    }
    std::u16string wide = utf16(text, length);
    JS_FreeCString(frame->ctx, text);
    return frame->hold(std::move(wide), pLength);
}

unsigned char* valueToBytes(MM_JSContext* cx, jsval v, unsigned int* pLength)
{
    Frame* frame = frameOf(cx);
    JSValue value;
    if (!frame || !frame->fromJsval(v, value))
        return nullptr;
    size_t length = 0;
    const char* text = JS_ToCStringLen(frame->ctx, &length, value);
    JS_FreeValue(frame->ctx, value);
    if (!text)
    {
        frame->takeException();
        return nullptr;
    }
    frame->bytes.emplace_back(text, length);
    JS_FreeCString(frame->ctx, text);
    if (pLength)
        *pLength = static_cast<unsigned int>(length);
    return reinterpret_cast<unsigned char*>(&frame->bytes.back()[0]);
}

bool toNumber(MM_JSContext* cx, jsval v, double& number)
{
    if (v != kVoid && (v & 1))
    {
        number = static_cast<double>(v >> 1);
        return true;
    }
    Frame* frame = frameOf(cx);
    JSValue value;
    if (!frame || !frame->fromJsval(v, value))
        return false;
    const int ok = JS_ToFloat64(frame->ctx, &number, value);
    JS_FreeValue(frame->ctx, value);
    if (ok < 0)
    {
        frame->takeException();
        return false;
    }
    return true;
}

JSBool valueToInteger(MM_JSContext* cx, jsval v, long* lp)
{
    double number = 0.0;
    if (!lp || !toNumber(cx, v, number))
        return 0;
    if (std::isnan(number))
        number = 0.0;
    const double low = static_cast<double>(std::numeric_limits<long>::min());
    const double high = static_cast<double>(std::numeric_limits<long>::max());
    *lp = number <= low ? std::numeric_limits<long>::min() : number >= high ? std::numeric_limits<long>::max() : static_cast<long>(number);
    return 1;
}

JSBool valueToDouble(MM_JSContext* cx, jsval v, double* dp)
{
    return dp && toNumber(cx, v, *dp) ? 1 : 0;
}

JSBool valueToBoolean(MM_JSContext* cx, jsval v, JSBool* bp)
{
    Frame* frame = frameOf(cx);
    JSValue value;
    if (!bp || !frame || !frame->fromJsval(v, value))
        return 0;
    const int truth = JS_ToBool(frame->ctx, value);
    JS_FreeValue(frame->ctx, value);
    if (truth < 0)
    {
        frame->takeException();
        return 0;
    }
    *bp = truth ? 1 : 0;
    return 1;
}

JSBool valueToObject(MM_JSContext* cx, jsval v, MM_JSObject** op)
{
    Frame* frame = frameOf(cx);
    JSValue value;
    if (!op || !frame || !frame->fromJsval(v, value))
        return 0;
    if (JS_IsNull(value) || JS_IsUndefined(value))
    {
        *op = nullptr;
        return 1;
    }
    if (JS_IsObject(value))
    {
        JS_FreeValue(frame->ctx, value);
        *op = frame->objectHandle(v);
        return 1;
    }
    JSValue object = JS_ToObject(frame->ctx, value);
    JS_FreeValue(frame->ctx, value);
    if (JS_IsException(object))
    {
        frame->takeException();
        return 0;
    }
    jsval handle = 0;
    if (!frame->keep(object, kObjectTag, handle))
        return 0;
    *op = frame->objectHandle(handle);
    return 1;
}

JSBool keepNew(Frame* frame, JSValue value, jsval tag, jsval* vp)
{
    if (JS_IsException(value))
    {
        frame->takeException();
        return 0;
    }
    jsval handle = 0;
    if (!frame->keep(value, tag, handle))
        return 0;
    *vp = handle;
    return 1;
}

JSBool stringToValue(MM_JSContext* cx, unsigned short* b, unsigned int sz, jsval* vp)
{
    Frame* frame = frameOf(cx);
    if (!frame || !vp || (!b && sz > 0))
        return 0;
    static const uint16_t empty = 0;
    return keepNew(frame, JS_NewStringUTF16(frame->ctx, b ? reinterpret_cast<const uint16_t*>(b) : &empty, sz), kStringTag, vp);
}

JSBool bytesToValue(MM_JSContext* cx, unsigned char* b, unsigned int sz, jsval* vp)
{
    Frame* frame = frameOf(cx);
    if (!frame || !vp || (!b && sz > 0))
        return 0;
    return keepNew(frame, JS_NewStringLen(frame->ctx, b ? reinterpret_cast<const char*>(b) : "", sz), kStringTag, vp);
}

JSBool doubleToValue(MM_JSContext* cx, double dv, jsval* vp)
{
    Frame* frame = frameOf(cx);
    if (!frame || !vp)
        return 0;
    return keepNew(frame, JS_NewFloat64(frame->ctx, dv), kDoubleTag, vp);
}

unsigned short* objectType(MM_JSObject* obj)
{
    // No context: the object belongs to the innermost call.
    Frame* frame = g_frame;
    JSValue* object = frame ? frame->object(obj) : nullptr;
    if (!object)
        return nullptr;
    JSContext* ctx = frame->ctx;
    std::string type = "Object";
    if (JS_IsArray(*object))
        type = "Array";
    else if (JS_IsFunction(ctx, *object))
        type = "Function";
    else
    {
        // Object.prototype.toString's [object Type].
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue objectClass = JS_GetPropertyStr(ctx, global, "Object");
        JSValue prototype = JS_GetPropertyStr(ctx, objectClass, "prototype");
        JSValue toString = JS_GetPropertyStr(ctx, prototype, "toString");
        JSValue text = JS_Call(ctx, toString, *object, 0, nullptr);
        if (JS_IsString(text))
        {
            const char* chars = JS_ToCString(ctx, text);
            if (chars)
            {
                const std::string label = chars;
                if (label.size() > 9 && label.compare(0, 8, "[object ") == 0)
                    type = label.substr(8, label.size() - 9);
                JS_FreeCString(ctx, chars);
            }
        }
        else if (JS_IsException(text))
            JS_FreeValue(ctx, JS_GetException(ctx));
        JS_FreeValue(ctx, text);
        JS_FreeValue(ctx, toString);
        JS_FreeValue(ctx, prototype);
        JS_FreeValue(ctx, objectClass);
        JS_FreeValue(ctx, global);
    }
    return frame->hold(utf16(type.data(), type.size()), nullptr);
}

MM_JSObject* newArrayObject(MM_JSContext* cx, unsigned int length, jsval* vp)
{
    Frame* frame = frameOf(cx);
    if (!frame)
        return nullptr;
    JSValue array = JS_NewArray(frame->ctx);
    if (JS_IsException(array))
    {
        frame->takeException();
        return nullptr;
    }
    for (unsigned int i = 0; vp && i < length; ++i)
    {
        JSValue item;
        if (!frame->fromJsval(vp[i], item) || JS_SetPropertyUint32(frame->ctx, array, i, item) < 0)
        {
            JS_FreeValue(frame->ctx, array);
            return nullptr;
        }
    }
    if (!vp && length > 0)
    {
        JSValue size = JS_NewUint32(frame->ctx, length);
        JS_SetPropertyStr(frame->ctx, array, "length", size);
    }
    jsval handle = 0;
    if (!frame->keep(array, kObjectTag, handle))
        return nullptr;
    return frame->objectHandle(handle);
}

long getArrayLength(MM_JSContext* cx, MM_JSObject* obj)
{
    Frame* frame = frameOf(cx);
    JSValue* object = frame ? frame->object(obj) : nullptr;
    if (!object)
        return -1;
    int64_t length = 0;
    if (JS_GetLength(frame->ctx, *object, &length) < 0)
    {
        frame->takeException();
        return -1;
    }
    return static_cast<long>(std::min<int64_t>(length, std::numeric_limits<long>::max()));
}

JSBool getElement(MM_JSContext* cx, MM_JSObject* obj, unsigned int idx, jsval* vp)
{
    Frame* frame = frameOf(cx);
    JSValue* object = frame ? frame->object(obj) : nullptr;
    if (!object || !vp)
        return 0;
    JSValue item = JS_GetPropertyUint32(frame->ctx, *object, idx);
    if (JS_IsException(item))
    {
        frame->takeException();
        return 0;
    }
    const bool ok = frame->toJsval(item, *vp);
    JS_FreeValue(frame->ctx, item);
    return ok ? 1 : 0;
}

JSBool setElement(MM_JSContext* cx, MM_JSObject* obj, unsigned int idx, jsval* vp)
{
    Frame* frame = frameOf(cx);
    JSValue* object = frame ? frame->object(obj) : nullptr;
    JSValue item;
    if (!object || !vp || !frame->fromJsval(*vp, item))
        return 0;
    if (JS_SetPropertyUint32(frame->ctx, *object, idx, item) < 0)
    {
        frame->takeException();
        return 0;
    }
    return 1;
}

JSBool executeScript(MM_JSContext* cx, MM_JSObject*, unsigned short* script, unsigned int sz, unsigned short*, unsigned int,
    jsval* rval)
{
    // The file name the header passes is __FILE__ cast, not UTF-16: unread.
    Frame* frame = frameOf(cx);
    if (!frame || !script)
        return 0;
    const std::string source = utf8(utf16From(script, sz));
    JSValue result = JS_Eval(frame->ctx, source.c_str(), source.size(), "<native extension>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result))
    {
        frame->takeException();
        return 0;
    }
    bool ok = true;
    if (rval)
        ok = frame->toJsval(result, *rval);
    JS_FreeValue(frame->ctx, result);
    return ok ? 1 : 0;
}

JSBool reportError(MM_JSContext* cx, unsigned short* error, unsigned int sz)
{
    Frame* frame = frameOf(cx);
    if (!frame)
        return 0;
    frame->error = utf8(utf16From(error, sz));
    return 1;
}

MM_Environment environment(Library* library)
{
    MM_Environment env;
    env.libObj = reinterpret_cast<MM_JSObject*>(library);
    env.defineFunction = &defineFunction;
    env.valueToString = &valueToString;
    env.valueToBytes = &valueToBytes;
    env.valueToInteger = &valueToInteger;
    env.valueToDouble = &valueToDouble;
    env.valueToBoolean = &valueToBoolean;
    env.valueToObject = &valueToObject;
    env.stringToValue = &stringToValue;
    env.bytesToValue = &bytesToValue;
    env.doubleToValue = &doubleToValue;
    env.objectType = &objectType;
    env.newArrayObject = &newArrayObject;
    env.getArrayLength = &getArrayLength;
    env.getElement = &getElement;
    env.setElement = &setElement;
    env.executeScript = &executeScript;
    env.reportError = &reportError;
    return env;
}

/// A script's call of a library function: data holds the library's and
/// the function's index.
JSValue callNative(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int, JSValueConst* data)
{
    int32_t libraryIndex = 0;
    int32_t functionIndex = 0;
    JS_ToInt32(ctx, &libraryIndex, data[0]);
    JS_ToInt32(ctx, &functionIndex, data[1]);
    Library* library = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (libraryIndex >= 0 && static_cast<size_t>(libraryIndex) < g_libraries.size())
            library = g_libraries[static_cast<size_t>(libraryIndex)].get();
    }
    if (!library || functionIndex < 0 || static_cast<size_t>(functionIndex) >= library->functions.size())
        return JS_ThrowInternalError(ctx, "This native function is gone.");
    const Function& function = library->functions[static_cast<size_t>(functionIndex)];
    const std::string fullName = library->name + "." + function.name;

    Frame frame(ctx, g_frame);
    // At least as many arguments as the function asked for, the rest
    // undefined.
    std::vector<jsval> args(std::max<size_t>(static_cast<size_t>(std::max(argc, 0)), function.nargs), kVoid);
    for (int i = 0; i < argc; ++i)
    {
        if (!frame.toJsval(argv[i], args[static_cast<size_t>(i)]))
            return JS_ThrowRangeError(ctx, "%s: too many values.", fullName.c_str());
    }
    jsval selfValue = 0;
    if (JS_IsObject(self) && !frame.toJsval(self, selfValue))
        return JS_ThrowRangeError(ctx, "%s: too many values.", fullName.c_str());
    jsval rval = kVoid;

    g_frame = &frame;
    const JSBool ok = function.call(reinterpret_cast<MM_JSContext*>(&frame), frame.objectHandle(selfValue), static_cast<unsigned int>(std::max(argc, 0)),
        args.data(), &rval);
    g_frame = frame.outer;

    // Only the low 32 bits: a library built with an int JSBool leaves the
    // rest unset.
    const bool succeeded = (static_cast<unsigned long>(ok) & 0xFFFFFFFFUL) != 0;
    // Stopping the script (or running out of memory) goes on through,
    // whatever the library made of it.
    if (frame.threw && (!succeeded || JS_IsUncatchableError(frame.exception)))
    {
        JSValue exception = frame.exception;
        frame.exception = JS_UNDEFINED;
        return JS_Throw(ctx, exception);
    }
    if (!succeeded)
    {
        if (!frame.error.empty())
            return JS_ThrowPlainError(ctx, "%s: %s", fullName.c_str(), frame.error.c_str());
        return JS_ThrowPlainError(ctx, "%s failed.", fullName.c_str());
    }
    JSValue result;
    if (!frame.fromJsval(rval, result))
        return JS_ThrowTypeError(ctx, "%s returned a value that is not one of its own.", fullName.c_str());
    return result;
}

/// Loads \a path, telling \a report why when it cannot.
void load(const fs::path& path, const fs::path& folder, const std::function<void(const std::string&)>& report)
{
    const std::string file = path.filename().u8string();
    std::string error;
    void* handle = openLibrary(path, error);
    if (!handle)
    {
        report("Native extension " + file + " was not loaded: " + error + ".");
        return;
    }
    auto wrapper = reinterpret_cast<InitWrapper>(findSymbol(handle, "MM_InitWrapper"));
    if (!wrapper)
    {
        // Left open: closing a library can run its code too.
        report("Native extension " + file + " was not loaded: it has no MM_InitWrapper (see mm_jsapi.h's MM_STATE).");
        return;
    }
    auto library = std::make_unique<Library>();
    library->name = path.stem().u8string();
    library->folder = folder;
    MM_Environment env = environment(library.get());
    g_loading = library.get();
    wrapper(&env, static_cast<unsigned int>(sizeof(env)));
    g_loading = nullptr;
    if (library->functions.empty())
        report("Native extension " + file + " defines no functions.");
    g_libraries.push_back(std::move(library));
}

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

bool enabled()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_enabled < 0)
    {
        const char* value = std::getenv("PHOENIX_NATIVE_EXTENSIONS");
        const std::string setting = value ? lower(value) : std::string();
        g_enabled = setting == "off" || setting == "0" || setting == "no" || setting == "false" ? 0 : 1;
    }
    return g_enabled == 1;
}

void setEnabled(bool on)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_enabled = on ? 1 : 0;
}

bool isLibraryFile(const std::string& fileName)
{
    const std::string extension = lower(fs::u8path(fileName).extension().u8string());
#if defined(_WIN32)
    return extension == ".dll";
#elif defined(__APPLE__)
    return extension == ".dylib" || extension == ".bundle" || extension == ".so";
#else
    return extension == ".so";
#endif
}

void install(JSContext* ctx, const std::string& configDirectory, const std::function<void(const std::string&)>& report)
{
    if (configDirectory.empty() || !enabled())
        return;
    const fs::path folder = fs::u8path(configDirectory) / "External Libraries";
    std::error_code error;
    if (!fs::is_directory(folder, error))
        return;
    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(folder, error))
    {
        if (isLibraryFile(entry.path().filename().u8string()))
            found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());

    std::vector<std::pair<int32_t, Library*>> libraries;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const fs::path& path : found)
        {
            if (std::find(g_tried.begin(), g_tried.end(), path) != g_tried.end())
                continue;
            g_tried.push_back(path);
            load(path, folder, report);
        }
        for (size_t i = 0; i < g_libraries.size(); ++i)
        {
            if (g_libraries[i]->folder == folder)
                libraries.emplace_back(static_cast<int32_t>(i), g_libraries[i].get());
        }
    }

    JSValue global = JS_GetGlobalObject(ctx);
    for (const auto& entry : libraries)
    {
        Library& library = *entry.second;
        JSAtom name = JS_NewAtom(ctx, library.name.c_str());
        const int taken = JS_HasProperty(ctx, global, name);
        if (taken != 0)
        {
            if (taken > 0)
                report("Native extension " + library.name + " was left out: a script global has that name.");
            JS_FreeAtom(ctx, name);
            continue;
        }
        JSValue object = JS_NewObject(ctx);
        for (size_t i = 0; i < library.functions.size(); ++i)
        {
            const Function& function = library.functions[i];
            JSValue data[2] = {JS_NewInt32(ctx, entry.first), JS_NewInt32(ctx, static_cast<int32_t>(i))};
            JSValue call = JS_NewCFunctionData(ctx, &callNative, static_cast<int>(function.nargs), 0, 2, data);
            JS_DefinePropertyValueStr(ctx, object, function.name.c_str(), call, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        }
        JS_DefinePropertyValue(ctx, global, name, object, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        JS_FreeAtom(ctx, name);
    }
    JS_FreeValue(ctx, global);
}

} // namespace jsfl::native
