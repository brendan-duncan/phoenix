// Finding the person's Python and loading its shared library at run time.

#include "python_api.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace python {

namespace fs = std::filesystem;

namespace {

/// The oldest Python whose stable ABI has everything used here.
constexpr int kMinimumMinor = 8;

std::string utf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path pathOf(const std::string& text)
{
    return fs::u8path(text);
}

bool present(const fs::path& path)
{
    std::error_code error;
    return fs::exists(path, error);
}

bool isDirectory(const fs::path& path)
{
    std::error_code error;
    return fs::is_directory(path, error);
}

/// A place Python may be: its shared library, and what goes with it.
struct Candidate
{
    fs::path library;
    fs::path home;
    fs::path executable;
    fs::path environment;
};

#ifdef _WIN32

/// The `home` a virtual environment's pyvenv.cfg names: the Python it was
/// made from.
fs::path environmentHome(const fs::path& environment)
{
    std::ifstream in(environment / "pyvenv.cfg");
    std::string line;
    while (std::getline(in, line))
    {
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        const auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r"));
            s.erase(s.find_last_not_of(" \t\r") + 1);
        };
        trim(key);
        trim(value);
        if (key == "home")
            return pathOf(value);
    }
    return {};
}

std::wstring wide(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

/// Python's folder on Windows: python3.dll (or python3X.dll) beside
/// python.exe, with its modules in Lib.
bool fromFolder(const fs::path& folder, Candidate& found)
{
    if (!isDirectory(folder) || !present(folder / "Lib" / "os.py"))
        return false;
    fs::path library = folder / "python3.dll";
    if (!present(library))
    {
        library.clear();
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(folder, error))
        {
            const std::string name = utf8(entry.path().filename());
            if (name.size() > 11 && name.compare(0, 7, "python3") == 0 && name.compare(name.size() - 4, 4, ".dll") == 0)
                library = entry.path();
        }
        if (library.empty())
            return false;
    }
    found.library = library;
    found.home = folder;
    if (present(folder / "python.exe"))
        found.executable = folder / "python.exe";
    return true;
}

/// The Pythons installed for the user and for everyone, newest first.
void registered(std::vector<fs::path>& folders)
{
    std::vector<std::pair<int, fs::path>> versions;
    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (HKEY root : roots)
    {
        HKEY core = nullptr;
        if (RegOpenKeyExW(root, L"Software\\Python\\PythonCore", 0, KEY_READ, &core) != ERROR_SUCCESS)
            continue;
        for (DWORD index = 0;; ++index)
        {
            wchar_t name[128];
            DWORD length = 128;
            if (RegEnumKeyExW(core, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            // "3.12", "3.12-32" (a 32-bit Python, which a 64-bit Phoenix
            // cannot load) or "3.13t" (free-threaded, which has another ABI).
            const std::wstring version(name, length);
            if (version.compare(0, 2, L"3.") != 0)
                continue;
            wchar_t* end = nullptr;
            const long minor = std::wcstol(version.c_str() + 2, &end, 10);
            if (end == version.c_str() + 2 || *end != L'\0')
                continue;
            const std::wstring sub = version + L"\\InstallPath";
            wchar_t path[1024];
            DWORD size = sizeof(path);
            if (RegGetValueW(core, sub.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS)
                versions.emplace_back(static_cast<int>(minor), fs::path(path));
        }
        RegCloseKey(core);
    }
    std::stable_sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& version : versions)
        folders.push_back(version.second);
}

void* openLibrary(const fs::path& path)
{
    // Its own folder first, for the python3X.dll that python3.dll leads to.
    return LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

void* symbol(void* library, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

std::string environment(const char* name)
{
    wchar_t buffer[2048];
    const std::wstring key = wide(name);
    const DWORD length = GetEnvironmentVariableW(key.c_str(), buffer, 2048);
    if (length == 0 || length >= 2048)
        return {};
    return utf8(fs::path(std::wstring(buffer, length)));
}

#else

/// What `python3` on the PATH says of itself: its shared library and home.
bool askPython(const std::string& program, Candidate& found)
{
    const std::string command = "'" + program +
                                "' -c \"import sys, sysconfig as s; v = s.get_config_var; f = v('PYTHONFRAMEWORKPREFIX'); "
                                "print(sys.base_prefix); print(sys.prefix); print(sys.executable); "
                                "print(f + '/' + v('LDLIBRARY') if f else (v('LIBDIR') or '') + '/' + (v('INSTSONAME') or "
                                "v('LDLIBRARY') or ''))\" 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return false;
    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);
    std::istringstream lines(output);
    std::string base;
    std::string prefix;
    std::string executable;
    std::string library;
    if (!std::getline(lines, base) || !std::getline(lines, prefix) || !std::getline(lines, executable) ||
        !std::getline(lines, library))
        return false;
    if (library.empty() || library.back() == '/' || !present(pathOf(library)))
        return false;
    found.library = pathOf(library);
    found.home = pathOf(base);
    found.executable = pathOf(executable);
    if (prefix != base)
        found.environment = pathOf(prefix);
    return true;
}

void* openLibrary(const fs::path& path)
{
    // Global, so the extension modules Python loads find its functions.
    return dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
}

void* symbol(void* library, const char* name)
{
    return dlsym(library, name);
}

std::string environment(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

#endif

/// Where \a hint points: a shared library, a python executable, a virtual
/// environment or Python's folder.
bool fromHint(const fs::path& hint, Candidate& found)
{
    if (hint.empty() || !present(hint))
        return false;
    const std::string name = utf8(hint.filename());
    const auto endsWith = [&name](const char* suffix) {
        const std::string tail(suffix);
        return name.size() >= tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0;
    };
    if (endsWith(".dll") || endsWith(".dylib") || name.find(".so") != std::string::npos || name == "Python" ||
        name == "Python3")
    {
        found.library = hint;
        return true;
    }
#ifdef _WIN32
    const fs::path folder = isDirectory(hint) ? hint : hint.parent_path();
    // A virtual environment: its pyvenv.cfg is beside its python or a
    // folder up (Scripts\python.exe), and names the Python it came from.
    for (const fs::path& at : {folder, folder.parent_path()})
    {
        if (!present(at / "pyvenv.cfg"))
            continue;
        Candidate base;
        if (!fromFolder(environmentHome(at), base))
            return false;
        found = base;
        found.environment = at;
        found.executable = isDirectory(hint) ? at / "Scripts" / "python.exe" : hint;
        return true;
    }
    return fromFolder(folder, found);
#else
    // Python says where its library is, a virtual environment's too.
    const fs::path program = isDirectory(hint) ? hint / "bin" / "python3" : hint;
    return askPython(utf8(program), found);
#endif
}

/// Every place worth trying, best first.
std::vector<Candidate> candidates(const std::string& hint)
{
    std::vector<Candidate> list;
    Candidate found;
    if (!hint.empty() && fromHint(pathOf(hint), found))
        list.push_back(found);
#ifdef _WIN32
    std::vector<fs::path> folders;
    registered(folders);
    // The PATH, for a Python installed without registering itself.
    const std::string path = environment("PATH");
    std::stringstream entries(path);
    std::string entry;
    while (std::getline(entries, entry, ';'))
    {
        if (!entry.empty() && entry.find("WindowsApps") == std::string::npos)
            folders.push_back(pathOf(entry));
    }
    for (const fs::path& folder : folders)
    {
        Candidate each;
        if (fromFolder(folder, each))
            list.push_back(each);
    }
#else
    Candidate asked;
    if (askPython("python3", asked))
        list.push_back(asked);
    // Where installers put it, should python3 not be on the PATH a GUI app
    // gets.
    for (int minor = 14; minor >= kMinimumMinor; --minor)
    {
        const std::string version = "3." + std::to_string(minor);
        Candidate each;
#ifdef __APPLE__
        const char* const prefixes[] = {"/Library/Frameworks/Python.framework/Versions/",
            "/opt/homebrew/opt/python@", "/usr/local/opt/python@"};
        for (const char* prefix : prefixes)
        {
            const std::string root = prefix;
            const fs::path library = root.find("opt/python@") != std::string::npos
                ? pathOf(root + version + "/Frameworks/Python.framework/Versions/" + version + "/Python")
                : pathOf(root + version + "/Python");
            if (present(library))
            {
                each.library = library;
                list.push_back(each);
            }
        }
#else
        // Found by the loader's own search.
        each.library = "libpython" + version + ".so.1.0";
        list.push_back(each);
#endif
    }
#endif
    return list;
}

bool parseVersion(const char* text, int& major, int& minor)
{
    if (!text)
        return false;
    char* end = nullptr;
    major = static_cast<int>(std::strtol(text, &end, 10));
    if (end == text || *end != '.')
        return false;
    const char* rest = end + 1;
    minor = static_cast<int>(std::strtol(rest, &end, 10));
    return end != rest;
}

bool bind(void* library, Api& api, std::string& missing)
{
#define PHOENIX_PYTHON_BIND(ret, name, params)                                                                         \
    api.name = reinterpret_cast<ret(*) params>(symbol(library, #name));                                                \
    if (!api.name)                                                                                                     \
    {                                                                                                                  \
        missing = #name;                                                                                               \
        return false;                                                                                                  \
    }
    PHOENIX_PYTHON_FUNCTIONS(PHOENIX_PYTHON_BIND)
#undef PHOENIX_PYTHON_BIND
    api.Py_SetPythonHome = reinterpret_cast<void (*)(const wchar_t*)>(symbol(library, "Py_SetPythonHome"));

    // Objects Python exports as data: None is one; each exception is a
    // pointer to one.
    api.None = static_cast<PyObject*>(symbol(library, "_Py_NoneStruct"));
    api.NotImplemented = static_cast<PyObject*>(symbol(library, "_Py_NotImplementedStruct"));
    const auto exception = [library, &missing](const char* name) -> PyObject* {
        auto** at = static_cast<PyObject**>(symbol(library, name));
        if (!at)
            missing = name;
        return at ? *at : nullptr;
    };
    api.TypeError = exception("PyExc_TypeError");
    api.AttributeError = exception("PyExc_AttributeError");
    api.RuntimeError = exception("PyExc_RuntimeError");
    api.KeyboardInterrupt = exception("PyExc_KeyboardInterrupt");
    api.Exception = exception("PyExc_Exception");
    if (!api.None)
        missing = "_Py_NoneStruct";
    if (!api.NotImplemented)
        missing = "_Py_NotImplementedStruct";
    return missing.empty();
}

} // namespace

Api* load(const std::string& hint, std::string& error)
{
    static bool tried = false;
    static Api api;
    static bool ok = false;
    static std::string why;
    if (tried)
    {
        error = why;
        return ok ? &api : nullptr;
    }
    tried = true;

    const std::string wanted = hint.empty() ? environment("PHOENIX_PYTHON") : hint;
    if (wanted == "off" || wanted == "0" || wanted == "none")
    {
        why = "Python scripts are turned off (PHOENIX_PYTHON=" + wanted + ").";
        error = why;
        return nullptr;
    }
    std::string problems;
    for (const Candidate& candidate : candidates(wanted))
    {
        void* library = openLibrary(candidate.library);
        if (!library)
            continue;
        Api found;
        std::string missing;
        if (!bind(library, found, missing))
        {
            problems += "\n" + utf8(candidate.library) + " lacks " + missing + ".";
            continue;
        }
        int major = 0;
        int minor = 0;
        const char* version = found.Py_GetVersion();
        if (!parseVersion(version, major, minor) || major != 3 || minor < kMinimumMinor)
        {
            problems += "\n" + utf8(candidate.library) + " is Python " + std::string(version ? version : "?").substr(0, 8) +
                        "; 3." + std::to_string(kMinimumMinor) + " or later is needed.";
            continue;
        }
        found.library = utf8(candidate.library);
        found.home = utf8(candidate.home);
        found.executable = utf8(candidate.executable);
        found.environment = utf8(candidate.environment);
        found.version = version;
        found.version = found.version.substr(0, found.version.find(' '));
        api = found;
        ok = true;
        return &api;
    }
    why = "Python scripts need Python 3." + std::to_string(kMinimumMinor) +
          " or later, and none was found. Install it from python.org, or set PHOENIX_PYTHON to the python "
          "executable (or virtual environment) to use." +
          problems;
    error = why;
    return nullptr;
}

} // namespace python
