// The Python binding: the object model's class tables made into Python
// types, values converted both ways, and a run from start to finish. The
// JavaScript binding (jsfl/engine.cpp) does the same for QuickJS.

#include "python_engine.h"

#include "python_api.h"

#include "../jsfl/model.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <memory>

namespace python {

namespace {

using jsfl::Args;
using jsfl::ClassInfo;
using jsfl::ObjectPtr;
using jsfl::Value;
using Clock = std::chrono::steady_clock;

Api* g_api = nullptr;
#define PY (*g_api)

/// A Python exception is already set; what unwinds to Python returns null
/// so it carries on up the script.
struct PythonError
{
};

/// An object of the model, as Python holds it. Objects are good only in the
/// run that made them: the session they reach the documents through ends
/// with it.
struct Instance
{
    PyObject head;
    ObjectPtr* object;
    unsigned generation;
};

/// A method taken from an object (doc.getTimeline), ready to call.
struct Bound
{
    PyObject head;
    ObjectPtr* object;
    const jsfl::Method* method;
    unsigned generation;
};

/// A property or method of a class, as a type's attribute stands for it.
struct Member
{
    const ClassInfo* owner;
    size_t index;
    bool method;
};

/// What the interpreter keeps for Phoenix, made once.
struct Binding
{
    PyObject* module = nullptr;
    PyObject* support = nullptr;
    PyObject* error = nullptr;
    PyObject* root = nullptr;
    PyObject* bound = nullptr;
    PyTypeObject* boolType = nullptr;
    PyTypeObject* floatType = nullptr;
    std::map<const ClassInfo*, PyObject*> types;
    // Kept for as long as the types that point at them.
    std::deque<Member> members;
    std::deque<std::vector<PyGetSetDef>> getsets;
    std::deque<std::string> names;
};
Binding g;

// The run going on, if any.
jsfl::Host* g_host = nullptr;
bool g_inRun = false;
unsigned g_generation = 0;

/// A reference Phoenix owns, given back when it goes.
class Ref
{
public:
    Ref() = default;
    explicit Ref(PyObject* object) : _object(object) {}
    ~Ref()
    {
        if (_object)
            PY.Py_DecRef(_object);
    }
    Ref(Ref&& other) noexcept : _object(other.release()) {}
    Ref& operator=(Ref&& other) noexcept
    {
        if (this != &other)
        {
            if (_object)
                PY.Py_DecRef(_object);
            _object = other.release();
        }
        return *this;
    }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;

    PyObject* get() const { return _object; }
    PyObject* release()
    {
        PyObject* object = _object;
        _object = nullptr;
        return object;
    }
    explicit operator bool() const { return _object != nullptr; }

private:
    PyObject* _object = nullptr;
};

/// \a object as a new reference, or a PythonError when making it failed.
Ref checked(PyObject* object)
{
    if (!object)
        throw PythonError();
    return Ref(object);
}

PyObject* newNone()
{
    PY.Py_IncRef(PY.None);
    return PY.None;
}

unsigned long flagsOf(PyObject* object)
{
    return PY.PyType_GetFlags(object->type);
}

std::string toStd(PyObject* text)
{
    Ref bytes = checked(PY.PyUnicode_AsUTF8String(text));
    char* data = nullptr;
    Py_ssize_t size = 0;
    if (PY.PyBytes_AsStringAndSize(bytes.get(), &data, &size) < 0)
        throw PythonError();
    return std::string(data, static_cast<size_t>(size));
}

/// str(object).
std::string textOf(PyObject* object)
{
    Ref text = checked(PY.PyObject_Str(object));
    return toStd(text.get());
}

PyObject* fromStd(const std::string& text)
{
    return checked(PY.PyUnicode_FromStringAndSize(text.data(), static_cast<Py_ssize_t>(text.size()))).release();
}

/// Calls a function of Phoenix's Python side (kSupport).
Ref callSupport(const char* name, std::vector<PyObject*> args)
{
    PyObject* function = PY.PyDict_GetItemString(g.support, name);
    if (!function)
        throw jsfl::Error(std::string("Phoenix's Python support lacks ") + name + ".");
    Ref tuple = checked(PY.PyTuple_New(static_cast<Py_ssize_t>(args.size())));
    for (size_t i = 0; i < args.size(); ++i)
    {
        PY.Py_IncRef(args[i]);
        PY.PyTuple_SetItem(tuple.get(), static_cast<Py_ssize_t>(i), args[i]);
    }
    return checked(PY.PyObject_CallObject(function, tuple.get()));
}

bool isPhoenixObject(PyObject* object)
{
    const int is = PY.PyObject_IsInstance(object, g.root);
    if (is < 0)
        PY.PyErr_Clear();
    return is == 1;
}

/// The model object \a self holds, if its run is still going on.
ObjectPtr objectOf(PyObject* self)
{
    const auto* instance = reinterpret_cast<const Instance*>(self);
    if (!instance->object)
        throw jsfl::Error("This object was not made by Phoenix; get objects from fl.");
    if (!g_inRun || instance->generation != g_generation)
        throw jsfl::Error("This object belongs to a script run that has ended; get it again from fl.");
    return *instance->object;
}

/// Runs \a body for Python: C++ errors become Python exceptions.
template <class F>
PyObject* guarded(F&& body)
{
    try
    {
        return body();
    }
    catch (const PythonError&)
    {
        return nullptr;
    }
    catch (const std::bad_alloc&)
    {
        PY.PyErr_SetString(g.error, "Phoenix ran out of memory.");
    }
    catch (const std::exception& error)
    {
        PY.PyErr_SetString(g.error, error.what());
    }
    catch (...)
    {
        PY.PyErr_SetString(g.error, "An unknown error in Phoenix.");
    }
    return nullptr;
}

PyObject* typeFor(const ClassInfo* info);

PyObject* wrap(const ObjectPtr& object)
{
    if (!object)
        return newNone();
    auto* type = reinterpret_cast<PyTypeObject*>(typeFor(&object->classInfo()));
    PyObject* made = PY.PyType_GenericAlloc(type, 0);
    if (!made)
        throw PythonError();
    auto* instance = reinterpret_cast<Instance*>(made);
    instance->object = new ObjectPtr(object);
    instance->generation = g_generation;
    return made;
}

/// A new reference to \a value in Python: numbers with no fraction as int,
/// arrays as lists, plain objects as dicts, dates as datetimes.
PyObject* toPy(const Value& value)
{
    switch (value.type)
    {
    case Value::Type::Undefined:
    case Value::Type::Null:
        return newNone();
    case Value::Type::Boolean:
        return PY.PyBool_FromLong(value.boolean ? 1 : 0);
    case Value::Type::Number:
    {
        const double number = value.number;
        if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < 9007199254740992.0)
            return checked(PY.PyLong_FromLongLong(static_cast<long long>(number))).release();
        return checked(PY.PyFloat_FromDouble(number)).release();
    }
    case Value::Type::String:
        return fromStd(value.string);
    case Value::Type::Date:
    {
        Ref milliseconds = checked(PY.PyFloat_FromDouble(value.number));
        return callSupport("make_date", {milliseconds.get()}).release();
    }
    case Value::Type::Object:
        return wrap(value.object);
    case Value::Type::Array:
    {
        Ref list = checked(PY.PyList_New(static_cast<Py_ssize_t>(value.items.size())));
        for (size_t i = 0; i < value.items.size(); ++i)
            PY.PyList_SetItem(list.get(), static_cast<Py_ssize_t>(i), toPy(value.items[i]));
        return list.release();
    }
    case Value::Type::Map:
    {
        Ref dict = checked(PY.PyDict_New());
        for (const auto& field : value.fields)
        {
            Ref item(toPy(field.second));
            if (PY.PyDict_SetItemString(dict.get(), field.first.c_str(), item.get()) < 0)
                throw PythonError();
        }
        return dict.release();
    }
    }
    return newNone();
}

/// \a object as the model takes it.
Value fromPy(PyObject* object, int depth = 0)
{
    if (object == PY.None)
        return Value::null();
    if (object->type == g.boolType)
        return Value(PY.PyLong_AsDouble(object) != 0.0);
    const unsigned long flags = flagsOf(object);
    if (flags & Py_TPFLAGS_LONG_SUBCLASS)
    {
        const double number = PY.PyLong_AsDouble(object);
        if (number == -1.0 && PY.PyErr_Occurred())
            throw PythonError();
        return Value(number);
    }
    if (object->type == g.floatType)
        return Value(PY.PyFloat_AsDouble(object));
    if (flags & Py_TPFLAGS_UNICODE_SUBCLASS)
        return Value(toStd(object));
    if (isPhoenixObject(object))
        return Value(objectOf(object));
    if (depth > 16)
        return Value();
    if (flags & (Py_TPFLAGS_LIST_SUBCLASS | Py_TPFLAGS_TUPLE_SUBCLASS))
    {
        const bool list = (flags & Py_TPFLAGS_LIST_SUBCLASS) != 0;
        const Py_ssize_t count = list ? PY.PyList_Size(object) : PY.PyTuple_Size(object);
        Value array = Value::array();
        for (Py_ssize_t i = 0; i < count; ++i)
            array.items.push_back(fromPy(list ? PY.PyList_GetItem(object, i) : PY.PyTuple_GetItem(object, i), depth + 1));
        return array;
    }
    if (flags & Py_TPFLAGS_DICT_SUBCLASS)
    {
        Value map = Value::map();
        Py_ssize_t at = 0;
        PyObject* key = nullptr;
        PyObject* item = nullptr;
        while (PY.PyDict_Next(object, &at, &key, &item))
            map.set(textOf(key), fromPy(item, depth + 1));
        return map;
    }
    if (flags & Py_TPFLAGS_BYTES_SUBCLASS)
        return Value(textOf(object));
    Ref milliseconds = callSupport("to_ms", {object});
    if (milliseconds.get() != PY.None)
        return Value::date(PY.PyFloat_AsDouble(milliseconds.get()));
    // Other numbers (a Decimal, a numpy float), then other collections (a
    // range, a set, a generator), then its text.
    if (PyObject* number = PY.PyNumber_Float(object))
    {
        Ref held(number);
        return Value(PY.PyFloat_AsDouble(number));
    }
    PY.PyErr_Clear();
    if (PyObject* iterator = PY.PyObject_GetIter(object))
    {
        Ref held(iterator);
        Value array = Value::array();
        while (PyObject* item = PY.PyIter_Next(iterator))
        {
            Ref each(item);
            array.items.push_back(fromPy(item, depth + 1));
        }
        if (PY.PyErr_Occurred())
            throw PythonError();
        return array;
    }
    PY.PyErr_Clear();
    return Value(textOf(object));
}

/// lowerCamelCase as snake_case: getDocumentDOM, get_document_dom.
std::string snake(const std::string& name)
{
    std::string out;
    for (size_t i = 0; i < name.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isupper(c))
        {
            const bool afterLower = i > 0 && (std::islower(static_cast<unsigned char>(name[i - 1])) ||
                                                 std::isdigit(static_cast<unsigned char>(name[i - 1])));
            const bool endsAcronym = i > 0 && std::isupper(static_cast<unsigned char>(name[i - 1])) && i + 1 < name.size() &&
                                     std::islower(static_cast<unsigned char>(name[i + 1]));
            if (afterLower || endsAcronym)
                out += '_';
            out += static_cast<char>(std::tolower(c));
        }
        else
            out += static_cast<char>(c);
    }
    return out;
}

// The types' slots.

void freeObject(PyObject* self)
{
    PyTypeObject* type = self->type;
    reinterpret_cast<freefunc>(PY.PyType_GetSlot(type, Py_tp_free))(self);
    // Instances of types made at run time hold their type.
    PY.Py_DecRef(reinterpret_cast<PyObject*>(type));
}

void deallocInstance(PyObject* self)
{
    auto* instance = reinterpret_cast<Instance*>(self);
    delete instance->object;
    instance->object = nullptr;
    freeObject(self);
}

void deallocBound(PyObject* self)
{
    auto* bound = reinterpret_cast<Bound*>(self);
    delete bound->object;
    bound->object = nullptr;
    freeObject(self);
}

const jsfl::Property* findProperty(const ClassInfo* info, const char* name)
{
    for (; info; info = info->parent)
    {
        for (const jsfl::Property& property : info->properties)
        {
            if (property.name == name)
                return &property;
        }
    }
    return nullptr;
}

PyObject* reprInstance(PyObject* self)
{
    return guarded([self]() -> PyObject* {
        const auto* instance = reinterpret_cast<const Instance*>(self);
        std::string text = "<phoenix.";
        if (!instance->object)
            return fromStd(text + "Object>");
        const ClassInfo& info = (*instance->object)->classInfo();
        text += info.name;
        // What it is called, when it has a name and its run goes on.
        if (g_inRun && instance->generation == g_generation)
        {
            if (const jsfl::Property* name = findProperty(&info, "name"))
            {
                try
                {
                    const Value value = name->get(**instance->object);
                    if (value.type == Value::Type::String)
                        text += " '" + value.string + "'";
                }
                catch (const std::exception&)
                {
                }
            }
        }
        return fromStd(text + ">");
    });
}

/// fl.documents[0] == fl.getDocumentDOM(): the same thing reached twice
/// compares equal, as in JSFL.
bool same(const ObjectPtr* a, const ObjectPtr* b)
{
    if (!a || !b)
        return a == b;
    if (a->get() == b->get())
        return true;
    const void* identity = (*a)->identity();
    return identity && identity == (*b)->identity() && (*a)->scope() == (*b)->scope() &&
           &(*a)->classInfo() == &(*b)->classInfo();
}

PyObject* compareInstances(PyObject* a, PyObject* b, int op)
{
    if ((op != Py_EQ && op != Py_NE) || !isPhoenixObject(b))
    {
        PY.Py_IncRef(PY.NotImplemented);
        return PY.NotImplemented;
    }
    const bool equal = same(reinterpret_cast<Instance*>(a)->object, reinterpret_cast<Instance*>(b)->object);
    return PY.PyBool_FromLong(equal == (op == Py_EQ) ? 1 : 0);
}

Py_ssize_t hashInstance(PyObject* self)
{
    const ObjectPtr* object = reinterpret_cast<Instance*>(self)->object;
    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(object ? object->get() : nullptr);
    if (object && (*object)->identity())
        key = reinterpret_cast<std::uintptr_t>((*object)->identity()) ^ (reinterpret_cast<std::uintptr_t>((*object)->scope()) >> 4);
    const auto hash = static_cast<Py_ssize_t>(key >> 3);
    return hash == -1 ? -2 : hash;
}

PyObject* getMember(PyObject* self, void* closure)
{
    return guarded([self, closure]() -> PyObject* {
        const Member& member = *static_cast<const Member*>(closure);
        const ObjectPtr object = objectOf(self);
        if (!member.method)
            return toPy(member.owner->properties[member.index].get(*object));
        PyObject* made = PY.PyType_GenericAlloc(reinterpret_cast<PyTypeObject*>(g.bound), 0);
        if (!made)
            throw PythonError();
        auto* bound = reinterpret_cast<Bound*>(made);
        bound->object = new ObjectPtr(object);
        bound->method = &member.owner->methods[member.index];
        bound->generation = g_generation;
        return made;
    });
}

int setMember(PyObject* self, PyObject* value, void* closure)
{
    PyObject* done = guarded([self, value, closure]() -> PyObject* {
        const Member& member = *static_cast<const Member*>(closure);
        if (!value)
            throw jsfl::Error("Phoenix's properties cannot be deleted.");
        const ObjectPtr object = objectOf(self);
        member.owner->properties[member.index].set(*object, fromPy(value));
        return newNone();
    });
    if (!done)
        return -1;
    PY.Py_DecRef(done);
    return 0;
}

PyObject* callBound(PyObject* self, PyObject* args, PyObject* keywords)
{
    return guarded([self, args, keywords]() -> PyObject* {
        const auto* bound = reinterpret_cast<const Bound*>(self);
        Py_ssize_t at = 0;
        PyObject* key = nullptr;
        PyObject* item = nullptr;
        if (keywords && PY.PyDict_Next(keywords, &at, &key, &item))
        {
            PY.PyErr_SetString(PY.TypeError, (bound->method->name + "() takes no keyword arguments").c_str());
            return nullptr;
        }
        if (!g_inRun || bound->generation != g_generation)
            throw jsfl::Error("This method belongs to a script run that has ended; get it again from fl.");
        Args list;
        const Py_ssize_t count = PY.PyTuple_Size(args);
        for (Py_ssize_t i = 0; i < count; ++i)
            list.push_back(fromPy(PY.PyTuple_GetItem(args, i)));
        const ObjectPtr object = *bound->object;
        return toPy(bound->method->call(*object, list));
    });
}

PyObject* reprBound(PyObject* self)
{
    return guarded([self]() -> PyObject* {
        const auto* bound = reinterpret_cast<const Bound*>(self);
        return fromStd("<phoenix method " + bound->method->name + ">");
    });
}

/// The Python type for \a info, made the first time an object of it is
/// needed: an attribute for each property and method (and its snake_case
/// name), its parent class as its base.
PyObject* typeFor(const ClassInfo* info)
{
    const auto found = g.types.find(info);
    if (found != g.types.end())
        return found->second;
    PyObject* base = info->parent ? typeFor(info->parent) : g.root;

    std::vector<std::string> taken;
    for (const ClassInfo* at = info; at; at = at->parent)
    {
        for (const jsfl::Property& property : at->properties)
            taken.push_back(property.name);
        for (const jsfl::Method& method : at->methods)
            taken.push_back(method.name);
    }
    std::vector<PyGetSetDef>& defs = g.getsets.emplace_back();
    const auto add = [&](const std::string& name, bool method, size_t index, bool writable) {
        g.members.push_back({info, index, method});
        Member* member = &g.members.back();
        defs.push_back({name.c_str(), &getMember, writable ? &setMember : nullptr, nullptr, member});
        const std::string alias = snake(name);
        if (alias != name && std::find(taken.begin(), taken.end(), alias) == taken.end())
        {
            taken.push_back(alias);
            g.names.push_back(alias);
            defs.push_back({g.names.back().c_str(), &getMember, writable ? &setMember : nullptr, nullptr, member});
        }
    };
    for (size_t i = 0; i < info->properties.size(); ++i)
        add(info->properties[i].name, false, i, static_cast<bool>(info->properties[i].set));
    for (size_t i = 0; i < info->methods.size(); ++i)
        add(info->methods[i].name, true, i, false);
    defs.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});

    g.names.push_back("phoenix." + info->name);
    PyType_Slot slots[] = {
        {Py_tp_dealloc, reinterpret_cast<void*>(&deallocInstance)},
        {Py_tp_getset, defs.data()},
        {Py_tp_repr, reinterpret_cast<void*>(&reprInstance)},
        {Py_tp_richcompare, reinterpret_cast<void*>(&compareInstances)},
        {Py_tp_hash, reinterpret_cast<void*>(&hashInstance)},
        {0, nullptr},
    };
    PyType_Spec spec = {g.names.back().c_str(), static_cast<int>(sizeof(Instance)), 0,
        static_cast<unsigned int>(Py_TPFLAGS_BASETYPE), slots};
    Ref bases = checked(PY.PyTuple_New(1));
    PY.Py_IncRef(base);
    PY.PyTuple_SetItem(bases.get(), 0, base);
    PyObject* type = PY.PyType_FromSpecWithBases(&spec, bases.get());
    if (!type)
        throw PythonError();
    g.types[info] = type;
    return type;
}

// What the phoenix module gives besides the objects.

const char* argumentText(PyObject* args, Py_ssize_t index, std::string& text)
{
    if (PY.PyTuple_Size(args) <= index)
        return nullptr;
    PyObject* item = PY.PyTuple_GetItem(args, index);
    if (item == PY.None)
        return nullptr;
    text = textOf(item);
    return text.c_str();
}

PyObject* moduleTrace(PyObject*, PyObject* args)
{
    return guarded([args]() -> PyObject* {
        std::string text;
        argumentText(args, 0, text);
        if (g_host && g_inRun)
            g_host->trace(text);
        return newNone();
    });
}

PyObject* moduleAlert(PyObject*, PyObject* args)
{
    return guarded([args]() -> PyObject* {
        std::string text;
        argumentText(args, 0, text);
        if (g_host && g_inRun)
            g_host->alert(text);
        return newNone();
    });
}

PyObject* moduleConfirm(PyObject*, PyObject* args)
{
    return guarded([args]() -> PyObject* {
        std::string text;
        argumentText(args, 0, text);
        return PY.PyBool_FromLong(g_host && g_inRun && g_host->confirm(text) ? 1 : 0);
    });
}

PyObject* modulePrompt(PyObject*, PyObject* args)
{
    return guarded([args]() -> PyObject* {
        std::string text;
        std::string initial;
        argumentText(args, 0, text);
        argumentText(args, 1, initial);
        std::string answer;
        if (!g_host || !g_inRun || !g_host->prompt(text, initial, answer))
            return newNone();
        return fromStd(answer);
    });
}

PyMethodDef g_functions[] = {
    {"trace", &moduleTrace, METH_VARARGS, "Writes a line to the Output panel (or standard output)."},
    {"alert", &moduleAlert, METH_VARARGS, "Shows a message."},
    {"confirm", &moduleConfirm, METH_VARARGS, "Asks OK or Cancel; True for OK."},
    {"prompt", &modulePrompt, METH_VARARGS, "Asks for text; None when canceled."},
};

/// Phoenix's side of a run, in Python: output, errors and dates.
const char* const kSupport = R"PY(
import datetime
import os
import sys
import traceback


class Output:
    """Where print() goes during a run: whole lines, to the Output panel."""

    encoding = "utf-8"
    errors = "replace"

    def __init__(self, trace):
        self._trace = trace
        self._buffer = ""

    def write(self, text):
        text = str(text)
        self._buffer += text
        if "\n" in self._buffer:
            lines = self._buffer.split("\n")
            self._buffer = lines.pop()
            for line in lines:
                self._trace(line)
        return len(text)

    def writelines(self, lines):
        for line in lines:
            self.write(line)

    def flush(self):
        pass

    def finish(self):
        if self._buffer:
            line, self._buffer = self._buffer, ""
            self._trace(line)

    def isatty(self):
        return False

    def writable(self):
        return True


_runs = []


def begin(path, trace):
    output = Output(trace)
    folder = os.path.dirname(os.path.abspath(path)) if path else None
    _runs.append((sys.stdout, sys.stderr, getattr(sys, "argv", None), folder, output))
    sys.stdout = sys.stderr = output
    sys.argv = [path or ""]
    if folder:
        sys.path.insert(0, folder)


def end():
    stdout, stderr, argv, folder, output = _runs.pop()
    output.finish()
    sys.stdout, sys.stderr = stdout, stderr
    if argv is not None:
        sys.argv = argv
    if folder and folder in sys.path:
        sys.path.remove(folder)


def make_date(milliseconds):
    return datetime.datetime.fromtimestamp(milliseconds / 1000.0, datetime.timezone.utc)


def to_ms(value):
    if isinstance(value, datetime.datetime):
        return value.timestamp() * 1000.0
    return None


def _installed(filename):
    """Whether a file is Python's own or an installed package's."""
    if filename.startswith("<"):
        return True
    path = os.path.normcase(os.path.abspath(filename))
    for root in {sys.base_prefix, sys.prefix}:
        root = os.path.normcase(os.path.abspath(root))
        if path.startswith(root + os.sep):
            return True
    return False


def describe(error, script):
    """The error's message, the file and line it points at, its traceback."""
    kind = type(error).__name__
    if isinstance(error, SyntaxError):
        message = "%s: %s" % (kind, error.msg)
        file, line = error.filename or script, error.lineno or 0
    else:
        text = str(error)
        message = "%s: %s" % (kind, text) if text else kind
        file, line = script, 0
        frames = traceback.extract_tb(error.__traceback__)
        # The innermost line of the script's own code, rather than of
        # Python's or a package's.
        own = [frame for frame in frames if not _installed(frame.filename)]
        chosen = own[-1] if own else (frames[-1] if frames else None)
        if chosen is not None:
            file, line = chosen.filename, chosen.lineno
    stack = "".join(traceback.format_exception(type(error), error, error.__traceback__)).rstrip()
    return message, file, line or 0, stack


def use_environment(folder, executable):
    """A virtual environment's packages, and its python for subprocesses."""
    import site
    if folder:
        if os.name == "nt":
            site.addsitedir(os.path.join(folder, "Lib", "site-packages"))
        else:
            version = "python%d.%d" % sys.version_info[:2]
            site.addsitedir(os.path.join(folder, "lib", version, "site-packages"))
    if executable:
        sys.executable = executable
)PY";

/// Stops a run that should end: polled from a pending call, which Python
/// makes between the script's own steps.
struct Guard
{
    std::atomic<bool>* stop = nullptr;
    bool hasDeadline = false;
    Clock::time_point deadline;
    std::function<void()> poll;
    std::atomic<bool> queued{false};
    bool stopped = false;
    bool timedOut = false;
};
std::atomic<Guard*> g_guard{nullptr};

int check(void*)
{
    Guard* guard = g_guard.load();
    if (!guard)
        return 0;
    guard->queued = false;
    try
    {
        if (guard->poll)
            guard->poll();
    }
    catch (...)
    {
    }
    if (guard->stop && *guard->stop)
    {
        guard->stopped = true;
        PY.PyErr_SetString(PY.KeyboardInterrupt, "The script was stopped.");
        return -1;
    }
    if (guard->hasDeadline && Clock::now() > guard->deadline)
    {
        guard->timedOut = true;
        PY.PyErr_SetString(PY.KeyboardInterrupt, "The script took too long and was stopped.");
        return -1;
    }
    return 0;
}

/// Asks Python for a check every few hundredths of a second while a script
/// runs. It takes Python's lock to ask: before 3.13, Python only notices a
/// call asked for from another thread when its own thread next takes the
/// lock back.
class Watchdog
{
public:
    explicit Watchdog(Guard& guard)
        : _thread([this, &guard]() {
            std::unique_lock<std::mutex> lock(_mutex);
            while (!_wake.wait_for(lock, std::chrono::milliseconds(50), [this]() { return _done; }))
            {
                if (guard.queued.exchange(true))
                    continue;
                lock.unlock();
                const int state = PY.PyGILState_Ensure();
                if (PY.Py_AddPendingCall(&check, nullptr) != 0)
                    guard.queued = false;
                PY.PyGILState_Release(state);
                lock.lock();
            }
        })
    {}
    ~Watchdog()
    {
        // Lets go of Python's lock while the thread ends, as it may be
        // waiting for it.
        void* state = PY.PyEval_SaveThread();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _done = true;
        }
        _wake.notify_all();
        _thread.join();
        PY.PyEval_RestoreThread(state);
    }

private:
    std::mutex _mutex;
    std::condition_variable _wake;
    bool _done = false;
    std::thread _thread;
};

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

/// The pending exception as a run's error.
void capture(jsfl::RunResult& result, const std::string& name)
{
    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PY.PyErr_Fetch(&type, &value, &traceback);
    result.ok = false;
    result.message = "Error";
    if (!type)
        return;
    PY.PyErr_NormalizeException(&type, &value, &traceback);
    Ref heldType(type);
    Ref heldValue(value);
    Ref heldTraceback(traceback);
    if (value && traceback)
        PY.PyObject_SetAttrString(value, "__traceback__", traceback);
    try
    {
        if (!g.support)
        {
            result.message = value ? textOf(value) : "Error";
            return;
        }
        Ref script(fromStd(name));
        Ref described = callSupport("describe", {value ? value : PY.None, script.get()});
        result.message = textOf(PY.PyTuple_GetItem(described.get(), 0));
        result.file = textOf(PY.PyTuple_GetItem(described.get(), 1));
        result.line = static_cast<int>(PY.PyLong_AsDouble(PY.PyTuple_GetItem(described.get(), 2)));
        result.stack = textOf(PY.PyTuple_GetItem(described.get(), 3));
    }
    catch (...)
    {
        PY.PyErr_Clear();
    }
}

/// Python's own modules are where \a home says.
bool homeHolds(const std::string& home, const std::string& version)
{
    std::error_code error;
#ifdef _WIN32
    (void)version;
    return std::filesystem::exists(std::filesystem::u8path(home) / "Lib" / "os.py", error);
#else
    // lib/python3.12/os.py
    const size_t second = version.find('.', version.find('.') + 1);
    const std::string release = "python" + version.substr(0, second);
    return std::filesystem::exists(std::filesystem::u8path(home) / "lib" / release / "os.py", error);
#endif
}

void setUp(const Api& api)
{
    {
        Ref sample = checked(PY.PyBool_FromLong(1));
        g.boolType = sample.get()->type;
        Ref number = checked(PY.PyFloat_FromDouble(0.5));
        g.floatType = number.get()->type;
    }

    // import phoenix: the module scripts share.
    g.module = checked(PY.PyModule_New("phoenix")).release();
    PyObject* modules = PY.PyImport_GetModuleDict();
    if (PY.PyDict_SetItemString(modules, "phoenix", g.module) < 0)
        throw PythonError();
    PyObject* moduleDict = PY.PyModule_GetDict(g.module);
    Ref doc(fromStd("Phoenix's documents, as JSFL scripts see them: phoenix.fl, phoenix.FLfile."));
    PY.PyDict_SetItemString(moduleDict, "__doc__", doc.get());
    g.error = checked(PY.PyErr_NewException("phoenix.Error", PY.Exception, nullptr)).release();
    PY.PyDict_SetItemString(moduleDict, "Error", g.error);
    for (PyMethodDef& function : g_functions)
    {
        Ref made = checked(PY.PyCFunction_NewEx(&function, nullptr, nullptr));
        PY.PyDict_SetItemString(moduleDict, function.name, made.get());
    }

    static PyType_Slot rootSlots[] = {
        {Py_tp_dealloc, reinterpret_cast<void*>(&deallocInstance)},
        {Py_tp_repr, reinterpret_cast<void*>(&reprInstance)},
        {Py_tp_richcompare, reinterpret_cast<void*>(&compareInstances)},
        {Py_tp_hash, reinterpret_cast<void*>(&hashInstance)},
        {Py_tp_doc, const_cast<char*>("An object of Phoenix's: a document, timeline, layer, frame, element or item.")},
        {0, nullptr},
    };
    static PyType_Spec rootSpec = {"phoenix.Object", static_cast<int>(sizeof(Instance)), 0,
        static_cast<unsigned int>(Py_TPFLAGS_BASETYPE), rootSlots};
    g.root = checked(PY.PyType_FromSpecWithBases(&rootSpec, nullptr)).release();
    PY.PyDict_SetItemString(moduleDict, "Object", g.root);

    static PyType_Slot boundSlots[] = {
        {Py_tp_dealloc, reinterpret_cast<void*>(&deallocBound)},
        {Py_tp_call, reinterpret_cast<void*>(&callBound)},
        {Py_tp_repr, reinterpret_cast<void*>(&reprBound)},
        {0, nullptr},
    };
    static PyType_Spec boundSpec = {"phoenix.Method", static_cast<int>(sizeof(Bound)), 0, 0, boundSlots};
    g.bound = checked(PY.PyType_FromSpecWithBases(&boundSpec, nullptr)).release();

    // Phoenix's Python side.
    PyObject* support = checked(PY.PyModule_New("phoenix._support")).release();
    g.support = PY.PyModule_GetDict(support);
    Ref builtins = checked(PY.PyImport_ImportModule("builtins"));
    PY.PyDict_SetItemString(g.support, "__builtins__", builtins.get());
    Ref code = checked(PY.Py_CompileString(kSupport, "<phoenix>", Py_file_input));
    Ref ran = checked(PY.PyEval_EvalCode(code.get(), g.support, g.support));

    if (!api.environment.empty() || !api.executable.empty())
    {
        Ref environment(fromStd(api.environment));
        Ref executable(fromStd(api.executable));
        callSupport("use_environment", {environment.get(), executable.get()});
    }
}

/// Loads Python and readies it, once. False with \a error when there is no
/// Python to run scripts with.
bool initialize(std::string& error)
{
    static bool tried = false;
    static bool ok = false;
    static std::string why;
    if (tried)
    {
        error = why;
        return ok;
    }
    tried = true;
    Api* api = load(std::string(), why);
    if (!api)
    {
        error = why;
        return false;
    }
    g_api = api;
    const bool ours = !PY.Py_IsInitialized();
    if (ours)
    {
        // Where its own modules are, so it need not work that out from
        // Phoenix's own location.
        if (!api->home.empty() && homeHolds(api->home, api->version))
        {
            static std::wstring home = std::filesystem::u8path(api->home).wstring();
            if (PY.Py_SetPythonHome)
                PY.Py_SetPythonHome(home.c_str());
            else
#ifdef _WIN32
                SetEnvironmentVariableW(L"PYTHONHOME", home.c_str());
#else
                setenv("PYTHONHOME", api->home.c_str(), 1);
#endif
        }
        // No signal handlers: Phoenix keeps its own.
        PY.Py_InitializeEx(0);
    }
    const int state = PY.PyGILState_Ensure();
    try
    {
        setUp(*api);
        ok = true;
    }
    catch (...)
    {
        jsfl::RunResult result;
        capture(result, "<phoenix>");
        why = "Python " + api->version + " could not be readied for Phoenix: " + result.message;
    }
    PY.PyGILState_Release(state);
    // Python leaves the thread that started it holding the lock; let it go
    // between runs, so threads a script starts can carry on.
    if (ours)
        PY.PyEval_SaveThread();
    error = why;
    return ok;
}

} // namespace

bool isScript(const std::string& path)
{
    if (path.size() < 3)
        return false;
    std::string tail = path.substr(path.size() - 3);
    std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".py";
}

Engine::Engine(jsfl::Host& host)
    : _host(host)
{
}

bool Engine::available(std::string* why)
{
    std::string error;
    const bool found = load(std::string(), error) != nullptr;
    if (why)
        *why = error;
    return found;
}

std::string Engine::description()
{
    std::string error;
    const Api* api = load(std::string(), error);
    return api ? api->version + " (" + api->library + ")" : std::string();
}

jsfl::RunResult Engine::runFile(const std::string& path, const jsfl::RunOptions& options)
{
    std::string source;
    if (!readFile(path, source))
    {
        jsfl::RunResult result;
        result.ok = false;
        result.file = path;
        result.message = "Could not read the script.";
        return result;
    }
    return run(source, path, path, options);
}

jsfl::RunResult Engine::runString(const std::string& source, const std::string& name, const jsfl::RunOptions& options)
{
    return run(source, name, std::string(), options);
}

jsfl::RunResult Engine::run(const std::string& source, const std::string& name, const std::string& path,
    const jsfl::RunOptions& options)
{
    jsfl::RunResult result;
    if (_running || g_inRun)
    {
        result.ok = false;
        result.message = "A script is already running.";
        return result;
    }
    std::string error;
    if (!initialize(error))
    {
        result.ok = false;
        result.file = name;
        result.message = error;
        return result;
    }
    _running = true;
    _stop = false;

    jsfl::Session session(_host, options.undoName);
    session.scriptPath = path;

    const int state = PY.PyGILState_Ensure();
    ++g_generation;
    g_inRun = true;
    g_host = &_host;

    Guard guard;
    guard.stop = &_stop;
    guard.poll = options.poll;
    if (options.timeLimit > 0.0)
    {
        guard.hasDeadline = true;
        guard.deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(options.timeLimit * 1000.0));
    }
    g_guard = &guard;

    bool begun = false;
    Ref globals;
    PyObject* moduleDict = PY.PyModule_GetDict(g.module);
    try
    {
        globals = checked(PY.PyDict_New());
        Ref builtins = checked(PY.PyImport_ImportModule("builtins"));
        PY.PyDict_SetItemString(globals.get(), "__builtins__", builtins.get());
        Ref mainName(fromStd("__main__"));
        PY.PyDict_SetItemString(globals.get(), "__name__", mainName.get());
        Ref file(fromStd(path.empty() ? name : path));
        PY.PyDict_SetItemString(globals.get(), "__file__", file.get());

        // fl and FLfile, and the dialogs, as JSFL scripts have them; also
        // in the phoenix module, for code the script imports.
        const std::shared_ptr<jsfl::Flash> flash = jsfl::makeFlash(session);
        PyObject* runGlobals = globals.get();
        flash->runScript = [&session, runGlobals](const std::string& other, const std::string& function, const Args& args) {
            if (!isScript(other))
                throw jsfl::Error("A Python script runs other Python scripts, not " + other + ".");
            std::string text;
            if (!readFile(other, text))
                throw jsfl::Error("Could not read the script " + other + ".");
            Ref scope = checked(PY.PyDict_New());
            for (const char* shared : {"__builtins__", "fl", "FLfile", "alert", "confirm", "prompt"})
            {
                if (PyObject* item = PY.PyDict_GetItemString(runGlobals, shared))
                    PY.PyDict_SetItemString(scope.get(), shared, item);
            }
            Ref otherName(fromStd(other));
            PY.PyDict_SetItemString(scope.get(), "__file__", otherName.get());
            Ref moduleName(fromStd("__phoenix_script__"));
            PY.PyDict_SetItemString(scope.get(), "__name__", moduleName.get());
            const std::string saved = session.scriptPath;
            session.scriptPath = other;
            struct Restore
            {
                jsfl::Session& session;
                const std::string& saved;
                ~Restore() { session.scriptPath = saved; }
            } restore{session, saved};
            Ref code = checked(PY.Py_CompileString(text.c_str(), other.c_str(), Py_file_input));
            Ref ran = checked(PY.PyEval_EvalCode(code.get(), scope.get(), scope.get()));
            if (function.empty())
                return Value();
            PyObject* callable = PY.PyDict_GetItemString(scope.get(), function.c_str());
            if (!callable)
                throw jsfl::Error("fl.runScript: " + other + " has no function " + function + ".");
            Ref tuple = checked(PY.PyTuple_New(static_cast<Py_ssize_t>(args.size())));
            for (size_t i = 0; i < args.size(); ++i)
                PY.PyTuple_SetItem(tuple.get(), static_cast<Py_ssize_t>(i), toPy(args[i]));
            Ref answer = checked(PY.PyObject_CallObject(callable, tuple.get()));
            return fromPy(answer.get());
        };
        Ref fl(wrap(flash));
        Ref files(wrap(jsfl::makeFileSystem()));
        for (PyObject* scope : {globals.get(), moduleDict})
        {
            PY.PyDict_SetItemString(scope, "fl", fl.get());
            PY.PyDict_SetItemString(scope, "FLfile", files.get());
        }
        for (const char* function : {"alert", "confirm", "prompt"})
            PY.PyDict_SetItemString(globals.get(), function, PY.PyDict_GetItemString(moduleDict, function));

        Ref scriptPath(fromStd(path));
        callSupport("begin", {scriptPath.get(), PY.PyDict_GetItemString(moduleDict, "trace")});
        begun = true;

        Watchdog watchdog(guard);
        Ref code = checked(PY.Py_CompileString(source.c_str(), name.c_str(), Py_file_input));
        Ref value = checked(PY.PyEval_EvalCode(code.get(), globals.get(), globals.get()));
    }
    catch (const PythonError&)
    {
        capture(result, name);
    }
    catch (const std::exception& problem)
    {
        PY.PyErr_Clear();
        result.ok = false;
        result.message = problem.what();
    }
    if (!result.ok)
    {
        if (result.file.empty())
            result.file = name;
        if (guard.stopped || guard.timedOut)
        {
            result.interrupted = true;
            result.message = guard.timedOut ? "The script took too long and was stopped." : "The script was stopped.";
        }
        else if (result.message.rfind("MemoryError", 0) == 0)
            result.interrupted = true;
    }
    g_guard = nullptr;

    // What the script printed last, and Python as it was.
    if (begun)
    {
        try
        {
            callSupport("end", {});
        }
        catch (...)
        {
            PY.PyErr_Clear();
        }
    }
    PY.PyDict_SetItemString(moduleDict, "fl", PY.None);
    PY.PyDict_SetItemString(moduleDict, "FLfile", PY.None);
    // The script's functions hold its globals; emptied, they let go of
    // what they reached.
    if (globals)
        PY.PyDict_Clear(globals.get());
    globals = Ref();
    PY.PyErr_Clear();

    g_inRun = false;
    g_host = nullptr;
    PY.PyGILState_Release(state);
    session.finish();
    _running = false;
    return result;
}

} // namespace python
