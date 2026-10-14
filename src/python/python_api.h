#pragma once

#include <cstddef>
#include <string>
#include <vector>

/// The part of Python's C API Phoenix uses, loaded when the first Python
/// script runs rather than linked: Phoenix starts and builds with no Python
/// at all, and runs scripts with whichever Python 3.8 or later the person
/// has. Only the stable ABI is used (the functions and structures every
/// Python 3 keeps the same), declared here so no Python headers are needed
/// to build.
namespace python {

using Py_ssize_t = std::ptrdiff_t;

struct PyTypeObject;

/// The head every Python object starts with (the ordinary build, not the
/// free-threaded one).
struct PyObject
{
    Py_ssize_t refcnt;
    PyTypeObject* type;
};

using getter = PyObject* (*)(PyObject*, void*);
using setter = int (*)(PyObject*, PyObject*, void*);
using PyCFunction = PyObject* (*)(PyObject*, PyObject*);
using destructor = void (*)(PyObject*);
using freefunc = void (*)(void*);

struct PyGetSetDef
{
    const char* name;
    getter get;
    setter set;
    const char* doc;
    void* closure;
};

struct PyMethodDef
{
    const char* name;
    PyCFunction meth;
    int flags;
    const char* doc;
};

struct PyType_Slot
{
    int slot;
    void* pfunc;
};

struct PyType_Spec
{
    const char* name;
    int basicsize;
    int itemsize;
    unsigned int flags;
    PyType_Slot* slots;
};

// Type slots (typeslots.h).
constexpr int Py_tp_call = 50;
constexpr int Py_tp_dealloc = 52;
constexpr int Py_tp_doc = 56;
constexpr int Py_tp_hash = 59;
constexpr int Py_tp_repr = 66;
constexpr int Py_tp_richcompare = 67;
constexpr int Py_tp_getset = 73;
constexpr int Py_tp_free = 74;

constexpr unsigned long Py_TPFLAGS_BASETYPE = 1ul << 10;
constexpr unsigned long Py_TPFLAGS_LONG_SUBCLASS = 1ul << 24;
constexpr unsigned long Py_TPFLAGS_LIST_SUBCLASS = 1ul << 25;
constexpr unsigned long Py_TPFLAGS_TUPLE_SUBCLASS = 1ul << 26;
constexpr unsigned long Py_TPFLAGS_BYTES_SUBCLASS = 1ul << 27;
constexpr unsigned long Py_TPFLAGS_UNICODE_SUBCLASS = 1ul << 28;
constexpr unsigned long Py_TPFLAGS_DICT_SUBCLASS = 1ul << 29;

constexpr int METH_VARARGS = 0x0001;
constexpr int Py_file_input = 257;
constexpr int Py_EQ = 2;
constexpr int Py_NE = 3;

// The functions, as name, return type and parameters.
#define PHOENIX_PYTHON_FUNCTIONS(X)                                                                                    \
    X(const char*, Py_GetVersion, ())                                                                                  \
    X(void, Py_InitializeEx, (int))                                                                                    \
    X(int, Py_IsInitialized, ())                                                                                       \
    X(void*, PyEval_SaveThread, ())                                                                                    \
    X(void, PyEval_RestoreThread, (void*))                                                                             \
    X(int, PyGILState_Ensure, ())                                                                                      \
    X(void, PyGILState_Release, (int))                                                                                 \
    X(int, Py_AddPendingCall, (int (*)(void*), void*))                                                                 \
    X(void, Py_IncRef, (PyObject*))                                                                                    \
    X(void, Py_DecRef, (PyObject*))                                                                                    \
    X(PyObject*, PyType_FromSpecWithBases, (PyType_Spec*, PyObject*))                                                  \
    X(void*, PyType_GetSlot, (PyTypeObject*, int))                                                                     \
    X(unsigned long, PyType_GetFlags, (PyTypeObject*))                                                                 \
    X(PyObject*, PyType_GenericAlloc, (PyTypeObject*, Py_ssize_t))                                                     \
    X(PyObject*, PyTuple_New, (Py_ssize_t))                                                                            \
    X(Py_ssize_t, PyTuple_Size, (PyObject*))                                                                           \
    X(PyObject*, PyTuple_GetItem, (PyObject*, Py_ssize_t))                                                             \
    X(int, PyTuple_SetItem, (PyObject*, Py_ssize_t, PyObject*))                                                        \
    X(PyObject*, PyList_New, (Py_ssize_t))                                                                             \
    X(Py_ssize_t, PyList_Size, (PyObject*))                                                                            \
    X(PyObject*, PyList_GetItem, (PyObject*, Py_ssize_t))                                                              \
    X(int, PyList_SetItem, (PyObject*, Py_ssize_t, PyObject*))                                                         \
    X(PyObject*, PyDict_New, ())                                                                                       \
    X(int, PyDict_SetItemString, (PyObject*, const char*, PyObject*))                                                  \
    X(PyObject*, PyDict_GetItemString, (PyObject*, const char*))                                                       \
    X(int, PyDict_Next, (PyObject*, Py_ssize_t*, PyObject**, PyObject**))                                              \
    X(void, PyDict_Clear, (PyObject*))                                                                                 \
    X(PyObject*, PyLong_FromLongLong, (long long))                                                                     \
    X(double, PyLong_AsDouble, (PyObject*))                                                                            \
    X(PyObject*, PyFloat_FromDouble, (double))                                                                         \
    X(double, PyFloat_AsDouble, (PyObject*))                                                                           \
    X(PyObject*, PyBool_FromLong, (long))                                                                              \
    X(PyObject*, PyNumber_Float, (PyObject*))                                                                          \
    X(PyObject*, PyUnicode_FromStringAndSize, (const char*, Py_ssize_t))                                               \
    X(PyObject*, PyUnicode_AsUTF8String, (PyObject*))                                                                  \
    X(int, PyBytes_AsStringAndSize, (PyObject*, char**, Py_ssize_t*))                                                  \
    X(void, PyErr_SetString, (PyObject*, const char*))                                                                 \
    X(PyObject*, PyErr_Occurred, ())                                                                                   \
    X(void, PyErr_Fetch, (PyObject**, PyObject**, PyObject**))                                                         \
    X(void, PyErr_NormalizeException, (PyObject**, PyObject**, PyObject**))                                            \
    X(void, PyErr_Clear, ())                                                                                           \
    X(PyObject*, PyErr_NewException, (const char*, PyObject*, PyObject*))                                              \
    X(PyObject*, PyObject_GetAttrString, (PyObject*, const char*))                                                     \
    X(int, PyObject_SetAttrString, (PyObject*, const char*, PyObject*))                                                \
    X(PyObject*, PyObject_CallObject, (PyObject*, PyObject*))                                                          \
    X(PyObject*, PyObject_Str, (PyObject*))                                                                            \
    X(int, PyObject_IsInstance, (PyObject*, PyObject*))                                                                \
    X(PyObject*, PyObject_GetIter, (PyObject*))                                                                        \
    X(PyObject*, PyIter_Next, (PyObject*))                                                                             \
    X(PyObject*, PyImport_ImportModule, (const char*))                                                                 \
    X(PyObject*, PyImport_GetModuleDict, ())                                                                           \
    X(PyObject*, PyModule_New, (const char*))                                                                          \
    X(PyObject*, PyModule_GetDict, (PyObject*))                                                                        \
    X(PyObject*, Py_CompileString, (const char*, const char*, int))                                                    \
    X(PyObject*, PyEval_EvalCode, (PyObject*, PyObject*, PyObject*))                                                   \
    X(PyObject*, PyCFunction_NewEx, (PyMethodDef*, PyObject*, PyObject*))

/// What was found of Python and the functions it gives.
struct Api
{
#define PHOENIX_PYTHON_POINTER(ret, name, params) ret(*name) params = nullptr;
    PHOENIX_PYTHON_FUNCTIONS(PHOENIX_PYTHON_POINTER)
#undef PHOENIX_PYTHON_POINTER

    /// Optional: gone from some future Python, when PYTHONHOME is set
    /// instead.
    void (*Py_SetPythonHome)(const wchar_t*) = nullptr;

    // Python's own objects, from its data exports.
    PyObject* None = nullptr;
    PyObject* NotImplemented = nullptr;
    PyObject* TypeError = nullptr;
    PyObject* AttributeError = nullptr;
    PyObject* RuntimeError = nullptr;
    PyObject* KeyboardInterrupt = nullptr;
    PyObject* Exception = nullptr;

    /// The shared library loaded ("python3.dll", "libpython3.12.so").
    std::string library;
    /// The folder Python's own modules are under, when it had to be given
    /// (Windows); empty to let Python work it out.
    std::string home;
    /// The python executable that goes with it, for sys.executable (so a
    /// script's subprocesses run Python, not Phoenix); may be empty.
    std::string executable;
    /// A virtual environment whose packages scripts see; may be empty.
    std::string environment;
    /// "3.12.1".
    std::string version;
};

/// Finds and loads Python, once; the answer is kept. \a hint is where the
/// person pointed (PHOENIX_PYTHON: a python executable, a virtual
/// environment, Python's folder or its shared library), tried first. Null
/// with \a error when there is no usable Python.
Api* load(const std::string& hint, std::string& error);

} // namespace python
