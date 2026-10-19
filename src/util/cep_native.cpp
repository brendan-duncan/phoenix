#include "cep_native.h"

#include "zlib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/sysinfo.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace cep {

namespace {

// ---------------------------------------------------------------------------
// Replies

json::Value ok(json::Value value = json::Value())
{
    json::Value reply;
    reply.set("value", std::move(value));
    return reply;
}

json::Value failure(const std::string& code, const std::string& message, const std::string& syscall = std::string(),
    const std::string& path = std::string())
{
    json::Value reply;
    reply.set("code", code);
    std::string text = code + ": " + message;
    if (!syscall.empty())
        text += ", " + syscall;
    if (!path.empty())
        text += " '" + path + "'";
    reply.set("error", text);
    if (!syscall.empty())
        reply.set("syscall", syscall);
    if (!path.empty())
        reply.set("path", path);
    return reply;
}

/// Node's name for a system error, and its words.
std::pair<std::string, std::string> nodeError(const std::error_code& ec)
{
    const std::error_condition c = ec.default_error_condition();
    if (c == std::errc::no_such_file_or_directory)
        return {"ENOENT", "no such file or directory"};
    if (c == std::errc::file_exists)
        return {"EEXIST", "file already exists"};
    if (c == std::errc::permission_denied || c == std::errc::operation_not_permitted)
        return {"EACCES", "permission denied"};
    if (c == std::errc::not_a_directory)
        return {"ENOTDIR", "not a directory"};
    if (c == std::errc::is_a_directory)
        return {"EISDIR", "illegal operation on a directory"};
    if (c == std::errc::directory_not_empty)
        return {"ENOTEMPTY", "directory not empty"};
    if (c == std::errc::device_or_resource_busy)
        return {"EBUSY", "resource busy or locked"};
    if (c == std::errc::cross_device_link)
        return {"EXDEV", "cross-device link not permitted"};
    if (c == std::errc::invalid_argument)
        return {"EINVAL", "invalid argument"};
    return {"EIO", ec.message()};
}

json::Value failure(const std::error_code& ec, const std::string& syscall, const std::string& path)
{
    const auto [code, message] = nodeError(ec);
    return failure(code, message, syscall, path);
}

fs::path toPath(const std::string& utf8)
{
    return fs::u8path(utf8);
}

std::string fromPath(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

#ifdef _WIN32
std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

double fileTimeMs(const FILETIME& t)
{
    const unsigned long long ticks = (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    return static_cast<double>(ticks / 10000ULL) - 11644473600000.0;
}
#endif

/// "file", "directory", "symlink" or "other".
const char* typeName(fs::file_type type)
{
    switch (type)
    {
    case fs::file_type::regular: return "file";
    case fs::file_type::directory: return "directory";
    case fs::file_type::symlink: return "symlink";
    default: return "other";
    }
}

bool readAll(const fs::path& path, std::string& bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

std::string utf8OrBase64(const std::string& bytes, const std::string& encoding)
{
    return encoding == "utf8" ? bytes : NativeHost::toBase64(bytes);
}

std::string decodeData(const json::Value& data, const std::string& encoding)
{
    return encoding == "utf8" ? data.toString() : NativeHost::fromBase64(data.toString());
}

} // namespace

// ---------------------------------------------------------------------------
// Base64

std::string NativeHost::toBase64(const std::string& bytes)
{
    static const char* const digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3)
    {
        const unsigned n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                           static_cast<unsigned char>(bytes[i + 2]);
        out += digits[(n >> 18) & 63];
        out += digits[(n >> 12) & 63];
        out += digits[(n >> 6) & 63];
        out += digits[n & 63];
    }
    if (i < bytes.size())
    {
        unsigned n = static_cast<unsigned char>(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            n |= static_cast<unsigned char>(bytes[i + 1]) << 8;
        out += digits[(n >> 18) & 63];
        out += digits[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? digits[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string NativeHost::fromBase64(const std::string& text)
{
    std::string out;
    out.reserve(text.size() / 4 * 3);
    unsigned buffer = 0;
    int bits = 0;
    for (const char c : text)
    {
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+' || c == '-')
            v = 62;
        else if (c == '/' || c == '_')
            v = 63;
        else
            continue;
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Processes

struct NativeHost::Process
{
    int id = 0;
    long long pid = 0;
    std::string cwd;
    /// Output kept for a run that waits (spawnSync), rather than posted.
    bool collect = false;
    std::string out, err;

#ifdef _WIN32
    HANDLE handle = nullptr;
    HANDLE in = nullptr;
#else
    pid_t handle = -1;
    int in = -1;
#endif
    std::mutex inMutex;
    std::thread outReader, errReader, waiter;

    std::mutex mutex;
    std::condition_variable ended;
    bool exited = false;
    bool killed = false;
    int code = 0;
    std::string signal;

    void closeInput()
    {
        std::lock_guard<std::mutex> lock(inMutex);
#ifdef _WIN32
        if (in)
            CloseHandle(in);
        in = nullptr;
#else
        if (in >= 0)
            close(in);
        in = -1;
#endif
    }

    bool write(const std::string& bytes)
    {
        std::lock_guard<std::mutex> lock(inMutex);
#ifdef _WIN32
        if (!in)
            return false;
        size_t at = 0;
        while (at < bytes.size())
        {
            DWORD wrote = 0;
            if (!WriteFile(in, bytes.data() + at, static_cast<DWORD>(bytes.size() - at), &wrote, nullptr))
                return false;
            at += wrote;
        }
        return true;
#else
        if (in < 0)
            return false;
        size_t at = 0;
        while (at < bytes.size())
        {
            const ssize_t wrote = ::write(in, bytes.data() + at, bytes.size() - at);
            if (wrote <= 0)
                return false;
            at += static_cast<size_t>(wrote);
        }
        return true;
#endif
    }

    void kill()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (exited)
            return;
        killed = true;
#ifdef _WIN32
        TerminateProcess(handle, 1);
#else
        ::kill(handle, SIGTERM);
#endif
    }

    bool running()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !exited;
    }

    /// Waits for the end, \a timeoutMs at most (no limit when negative);
    /// whether it ended.
    bool wait(int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (timeoutMs < 0)
        {
            ended.wait(lock, [this]() { return exited; });
            return true;
        }
        return ended.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return exited; });
    }

    void join()
    {
        if (waiter.joinable())
            waiter.join();
    }
};

namespace {

#ifdef _WIN32
/// \a arg quoted for a command line, as the C runtime splits it back.
std::wstring quoteArgument(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it)
    {
        size_t slashes = 0;
        while (it != arg.end() && *it == L'\\')
        {
            ++it;
            ++slashes;
        }
        if (it == arg.end())
        {
            out.append(slashes * 2, L'\\');
            break;
        }
        if (*it == L'"')
        {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(*it);
        }
        else
        {
            out.append(slashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}
#else
const char* signalName(int signal)
{
    switch (signal)
    {
    case SIGTERM: return "SIGTERM";
    case SIGKILL: return "SIGKILL";
    case SIGINT: return "SIGINT";
    case SIGHUP: return "SIGHUP";
    case SIGPIPE: return "SIGPIPE";
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    default: return "SIGTERM";
    }
}
#endif

} // namespace

std::shared_ptr<NativeHost::Process> NativeHost::process(int id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _processes.find(id);
    return it == _processes.end() ? nullptr : it->second;
}

json::Value NativeHost::processCall(const std::string& name, const json::Value& args)
{
    if (name == "spawn" || name == "run")
    {
        // file, args, cwd, env (an object, or null for Phoenix's own),
        // shell (true, or the shell's path), and for "run": the input
        // (base64) and a time limit in milliseconds.
        const std::string file = args[0].toString();
        std::vector<std::string> arguments;
        for (const json::Value& a : args[1].toArray())
            arguments.push_back(a.toString());
        const std::string cwd = args[2].toString();
        const json::Value& env = args[3];
        const json::Value& shell = args[4];
        if (file.empty())
            return failure("EINVAL", "the file to run is empty", "spawn", file);
        if (!cwd.empty())
        {
            std::error_code ec;
            if (!fs::is_directory(toPath(cwd), ec))
                return failure("ENOENT", "no such file or directory", "spawn " + file, cwd);
        }

        auto p = std::make_shared<Process>();
        p->cwd = cwd;
        p->collect = name == "run";
        {
            std::lock_guard<std::mutex> lock(_mutex);
            p->id = _nextId++;
        }

#ifdef _WIN32
        std::wstring line;
        std::wstring application;
        if (shell.isBool() ? shell.toBool() : shell.isString())
        {
            std::wstring comspec;
            if (shell.isString() && !shell.toString().empty())
                comspec = widen(shell.toString());
            else
            {
                wchar_t buffer[MAX_PATH] = L"";
                comspec = GetEnvironmentVariableW(L"ComSpec", buffer, MAX_PATH) ? buffer : L"cmd.exe";
            }
            std::string command = file;
            for (const std::string& a : arguments)
                command += " " + a;
            application = comspec;
            line = quoteArgument(comspec) + L" /d /s /c \"" + widen(command) + L"\"";
        }
        else
        {
            line = quoteArgument(widen(file));
            for (const std::string& a : arguments)
                line += L" " + quoteArgument(widen(a));
        }
        std::wstring block;
        if (env.isObject())
        {
            for (const auto& [key, value] : env.toObject())
            {
                if (!value.isNull())
                    block += widen(key) + L"=" + widen(value.isString() ? value.toString() : value.dump()) + L'\0';
            }
            block += L'\0';
        }

        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr, errRead = nullptr,
               errWrite = nullptr;
        const auto closeAll = [&]() {
            for (HANDLE h : {inRead, inWrite, outRead, outWrite, errRead, errWrite})
            {
                if (h)
                    CloseHandle(h);
            }
        };
        if (!CreatePipe(&inRead, &inWrite, &inherit, 0) || !CreatePipe(&outRead, &outWrite, &inherit, 0) ||
            !CreatePipe(&errRead, &errWrite, &inherit, 0))
        {
            closeAll();
            return failure("EMFILE", "could not make pipes", "spawn " + file);
        }
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdInput = inRead;
        startup.hStdOutput = outWrite;
        startup.hStdError = errWrite;
        PROCESS_INFORMATION info{};
        const std::wstring wcwd = widen(cwd);
        const BOOL started = CreateProcessW(application.empty() ? nullptr : application.c_str(), line.data(), nullptr,
            nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.empty() ? nullptr : block.data(),
            wcwd.empty() ? nullptr : wcwd.c_str(), &startup, &info);
        const DWORD startError = started ? 0 : GetLastError();
        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(errWrite);
        if (!started)
        {
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(errRead);
            if (startError == ERROR_FILE_NOT_FOUND || startError == ERROR_PATH_NOT_FOUND)
                return failure("ENOENT", "no such file or directory", "spawn " + file, file);
            if (startError == ERROR_ACCESS_DENIED)
                return failure("EACCES", "permission denied", "spawn " + file, file);
            return failure("EINVAL", "could not start the program", "spawn " + file, file);
        }
        CloseHandle(info.hThread);
        p->handle = info.hProcess;
        p->pid = info.dwProcessId;
        p->in = inWrite;

        const auto reader = [this, p](HANDLE pipe, const char* kind) {
            char buffer[16384];
            DWORD got = 0;
            while (ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) && got > 0)
            {
                const std::string bytes(buffer, got);
                if (p->collect)
                {
                    std::lock_guard<std::mutex> lock(p->mutex);
                    (std::strcmp(kind, "stdout") == 0 ? p->out : p->err) += bytes;
                }
                else
                {
                    json::Value event;
                    event.set("id", p->id);
                    event.set("kind", kind);
                    event.set("data", toBase64(bytes));
                    post(std::move(event));
                }
            }
            CloseHandle(pipe);
        };
        p->outReader = std::thread(reader, outRead, "stdout");
        p->errReader = std::thread(reader, errRead, "stderr");
        p->waiter = std::thread([this, p]() {
            WaitForSingleObject(p->handle, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(p->handle, &code);
            p->outReader.join();
            p->errReader.join();
            finish(p, static_cast<int>(code));
        });
#else
        std::vector<std::string> argv;
        if (shell.isBool() ? shell.toBool() : shell.isString())
        {
            std::string command = file;
            for (const std::string& a : arguments)
                command += " " + a;
            argv = {shell.isString() && !shell.toString().empty() ? shell.toString() : std::string("/bin/sh"), "-c",
                command};
        }
        else
        {
            argv.push_back(file);
            argv.insert(argv.end(), arguments.begin(), arguments.end());
        }
        std::vector<char*> argvPointers;
        for (std::string& a : argv)
            argvPointers.push_back(a.data());
        argvPointers.push_back(nullptr);
        std::vector<std::string> envStrings;
        std::vector<char*> envPointers;
        if (env.isObject())
        {
            for (const auto& [key, value] : env.toObject())
            {
                if (!value.isNull())
                    envStrings.push_back(key + "=" + (value.isString() ? value.toString() : value.dump()));
            }
            for (std::string& e : envStrings)
                envPointers.push_back(e.data());
            envPointers.push_back(nullptr);
        }

        int inPipe[2], outPipe[2], errPipe[2], failPipe[2];
        if (pipe(inPipe) || pipe(outPipe) || pipe(errPipe) || pipe(failPipe))
            return failure("EMFILE", "could not make pipes", "spawn " + file);
        fcntl(failPipe[1], F_SETFD, FD_CLOEXEC);
        std::signal(SIGPIPE, SIG_IGN);
        const pid_t child = fork();
        if (child == 0)
        {
            dup2(inPipe[0], 0);
            dup2(outPipe[1], 1);
            dup2(errPipe[1], 2);
            for (int fd : {inPipe[0], inPipe[1], outPipe[0], outPipe[1], errPipe[0], errPipe[1], failPipe[0]})
                close(fd);
            if (!cwd.empty() && chdir(cwd.c_str()) != 0)
            {
                const int error = errno;
                (void)!::write(failPipe[1], &error, sizeof(error));
                _exit(127);
            }
            if (!envPointers.empty())
                environ = envPointers.data();
            execvp(argvPointers[0], argvPointers.data());
            const int error = errno;
            (void)!::write(failPipe[1], &error, sizeof(error));
            _exit(127);
        }
        close(inPipe[0]);
        close(outPipe[1]);
        close(errPipe[1]);
        close(failPipe[1]);
        int error = 0;
        const bool failed = child < 0 || read(failPipe[0], &error, sizeof(error)) == sizeof(error);
        close(failPipe[0]);
        if (failed)
        {
            close(inPipe[1]);
            close(outPipe[0]);
            close(errPipe[0]);
            if (child > 0)
                waitpid(child, nullptr, 0);
            if (error == ENOENT)
                return failure("ENOENT", "no such file or directory", "spawn " + file, file);
            if (error == EACCES)
                return failure("EACCES", "permission denied", "spawn " + file, file);
            return failure("EINVAL", "could not start the program", "spawn " + file, file);
        }
        p->handle = child;
        p->pid = child;
        p->in = inPipe[1];

        const auto reader = [this, p](int pipe, const char* kind) {
            char buffer[16384];
            ssize_t got = 0;
            while ((got = read(pipe, buffer, sizeof(buffer))) > 0)
            {
                const std::string bytes(buffer, static_cast<size_t>(got));
                if (p->collect)
                {
                    std::lock_guard<std::mutex> lock(p->mutex);
                    (std::strcmp(kind, "stdout") == 0 ? p->out : p->err) += bytes;
                }
                else
                {
                    json::Value event;
                    event.set("id", p->id);
                    event.set("kind", kind);
                    event.set("data", toBase64(bytes));
                    post(std::move(event));
                }
            }
            close(pipe);
        };
        p->outReader = std::thread(reader, outPipe[0], "stdout");
        p->errReader = std::thread(reader, errPipe[0], "stderr");
        p->waiter = std::thread([this, p]() {
            int status = 0;
            waitpid(p->handle, &status, 0);
            p->outReader.join();
            p->errReader.join();
            if (WIFSIGNALED(status))
            {
                {
                    std::lock_guard<std::mutex> lock(p->mutex);
                    p->signal = signalName(WTERMSIG(status));
                }
                finish(p, -1);
            }
            else
                finish(p, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        });
#endif
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _processes[p->id] = p;
        }

        if (name == "spawn")
        {
            json::Value result;
            result.set("id", p->id);
            result.set("pid", static_cast<double>(p->pid));
            return ok(result);
        }

        // A run waits for the program with what it was given as input.
        if (!args[5].toString().empty())
            p->write(fromBase64(args[5].toString()));
        p->closeInput();
        const int timeout = args[6].isNumber() && args[6].toNumber() > 0 ? args[6].toInt() : -1;
        bool timedOut = false;
        if (!p->wait(timeout))
        {
            timedOut = true;
            p->kill();
            p->wait(-1);
        }
        p->join();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _processes.erase(p->id);
        }
        json::Value result;
        result.set("pid", static_cast<double>(p->pid));
        result.set("stdout", toBase64(p->out));
        result.set("stderr", toBase64(p->err));
        if (p->killed || !p->signal.empty())
        {
            result.set("status", nullptr);
            result.set("signal", p->signal.empty() ? std::string("SIGTERM") : p->signal);
        }
        else
        {
            result.set("status", p->code);
            result.set("signal", nullptr);
        }
        if (timedOut)
            result.set("timedOut", true);
        return ok(result);
    }

    const std::shared_ptr<Process> p = process(args[0].toInt());
    if (!p)
        return failure("ESRCH", "no such process", name);
    if (name == "write")
        return p->write(fromBase64(args[1].toString())) ? ok(true) : failure("EPIPE", "broken pipe", "write");
    if (name == "end")
    {
        p->closeInput();
        return ok();
    }
    if (name == "kill")
    {
        const bool was = p->running();
        p->kill();
        return ok(was);
    }
    if (name == "running")
        return ok(p->running());
    if (name == "wait")
    {
        const int timeout = args[1].isNumber() ? args[1].toInt() : -1;
        return ok(p->wait(timeout));
    }
    if (name == "cwd")
        return ok(p->cwd);
    return failure("ENOSYS", "unknown call", name);
}

void NativeHost::finish(const std::shared_ptr<Process>& p, int code)
{
    json::Value event;
    {
        std::lock_guard<std::mutex> lock(p->mutex);
        p->code = code;
        if (p->killed && p->signal.empty())
            p->signal = "SIGTERM";
        event.set("id", p->id);
        event.set("kind", "exit");
        if (p->signal.empty())
            event.set("code", code);
        else
        {
            event.set("code", nullptr);
            event.set("signal", p->signal);
        }
        // Ended: kill() leaves the handle alone from here.
        p->exited = true;
    }
#ifdef _WIN32
    CloseHandle(p->handle);
    p->handle = nullptr;
#endif
    if (!p->collect)
        post(std::move(event));
    p->ended.notify_all();
}

void NativeHost::post(json::Value event)
{
    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const bool first = _events.toArray().empty();
        _events.push(std::move(event));
        if (first)
            wake = _wake;
    }
    if (wake)
        wake();
}

json::Value NativeHost::takeEvents()
{
    std::lock_guard<std::mutex> lock(_mutex);
    json::Value events = _events.isArray() ? _events : json::Value(json::Value::Array());
    _events = json::Value(json::Value::Array());
    return events;
}

void NativeHost::setWake(std::function<void()> wake)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _wake = std::move(wake);
}

// ---------------------------------------------------------------------------
// Files

json::Value NativeHost::fileCall(const std::string& name, const json::Value& args)
{
    const std::string pathText = args[0].toString();
    if (pathText.empty() && name != "mkdtemp")
        return failure("EINVAL", "the path is empty", name);
    const fs::path path = toPath(pathText);
    std::error_code ec;

    if (name == "read")
    {
        const fs::file_status status = fs::status(path, ec);
        if (ec || !fs::exists(status))
            return failure("ENOENT", "no such file or directory", "open", pathText);
        if (fs::is_directory(status))
            return failure("EISDIR", "illegal operation on a directory", "read", pathText);
        std::string bytes;
        if (!readAll(path, bytes))
            return failure("EACCES", "permission denied", "open", pathText);
        return ok(utf8OrBase64(bytes, args[1].toString()));
    }
    if (name == "write")
    {
        // path, data, encoding, flag ("w", "a", "wx", "ax")
        const std::string flag = args[3].isString() ? args[3].toString() : std::string("w");
        const bool append = flag.find('a') != std::string::npos;
        const bool exclusive = flag.find('x') != std::string::npos;
        const fs::file_status status = fs::status(path, ec);
        if (fs::is_directory(status))
            return failure("EISDIR", "illegal operation on a directory", "open", pathText);
        if (exclusive && fs::exists(status))
            return failure("EEXIST", "file already exists", "open", pathText);
        const fs::path parent = path.parent_path();
        if (!parent.empty() && !fs::is_directory(parent, ec))
            return failure("ENOENT", "no such file or directory", "open", pathText);
        std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!out)
            return failure("EACCES", "permission denied", "open", pathText);
        const std::string bytes = decodeData(args[1], args[2].toString());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return out ? ok() : failure("EIO", "could not write", "write", pathText);
    }
    if (name == "stat")
    {
        const bool link = args[1].toBool();
        const fs::file_status status = link ? fs::symlink_status(path, ec) : fs::status(path, ec);
        if (ec || (!fs::exists(status) && status.type() != fs::file_type::symlink))
            return failure("ENOENT", "no such file or directory", link ? "lstat" : "stat", pathText);
        json::Value info;
        info.set("type", typeName(status.type()));
        double size = 0;
        if (status.type() == fs::file_type::regular)
        {
            const auto bytes = fs::file_size(path, ec);
            size = ec ? 0 : static_cast<double>(bytes);
        }
        info.set("size", size);
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            info.set("mtimeMs", fileTimeMs(data.ftLastWriteTime));
            info.set("atimeMs", fileTimeMs(data.ftLastAccessTime));
            info.set("ctimeMs", fileTimeMs(data.ftLastWriteTime));
            info.set("birthtimeMs", fileTimeMs(data.ftCreationTime));
            const bool readOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
            const int kind = status.type() == fs::file_type::directory ? 0040000
                             : status.type() == fs::file_type::symlink ? 0120000
                                                                         : 0100000;
            info.set("mode", kind | (readOnly ? 0444 : 0666));
        }
#else
        struct stat st
        {
        };
        if ((link ? lstat(pathText.c_str(), &st) : ::stat(pathText.c_str(), &st)) == 0)
        {
#if defined(__APPLE__)
            const auto ms = [](const timespec& t) { return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; };
            info.set("mtimeMs", ms(st.st_mtimespec));
            info.set("atimeMs", ms(st.st_atimespec));
            info.set("ctimeMs", ms(st.st_ctimespec));
            info.set("birthtimeMs", ms(st.st_birthtimespec));
#else
            const auto ms = [](const timespec& t) { return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; };
            info.set("mtimeMs", ms(st.st_mtim));
            info.set("atimeMs", ms(st.st_atim));
            info.set("ctimeMs", ms(st.st_ctim));
            info.set("birthtimeMs", ms(st.st_ctim));
#endif
            info.set("mode", static_cast<double>(st.st_mode));
            info.set("uid", static_cast<double>(st.st_uid));
            info.set("gid", static_cast<double>(st.st_gid));
            info.set("ino", static_cast<double>(st.st_ino));
            info.set("dev", static_cast<double>(st.st_dev));
            info.set("nlink", static_cast<double>(st.st_nlink));
        }
#endif
        return ok(info);
    }
    if (name == "readdir")
    {
        const fs::file_status status = fs::status(path, ec);
        if (!fs::exists(status))
            return failure("ENOENT", "no such file or directory", "scandir", pathText);
        if (!fs::is_directory(status))
            return failure("ENOTDIR", "not a directory", "scandir", pathText);
        json::Value list(json::Value::Array{});
        for (auto it = fs::directory_iterator(path, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            json::Value entry;
            entry.set("name", fromPath(it->path().filename()));
            std::error_code typeError;
            entry.set("type", typeName(it->symlink_status(typeError).type()));
            list.push(entry);
        }
        if (ec)
            return failure(ec, "scandir", pathText);
        return ok(list);
    }
    if (name == "mkdir")
    {
        const bool recursive = args[1].toBool();
        if (!recursive)
        {
            if (fs::exists(path, ec))
                return failure("EEXIST", "file already exists", "mkdir", pathText);
            if (!fs::create_directory(path, ec))
                return failure(ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory), "mkdir", pathText);
            return ok();
        }
        if (fs::is_directory(path, ec))
            return ok();
        if (fs::exists(path, ec))
            return failure("EEXIST", "file already exists", "mkdir", pathText);
        // Node gives the first folder it made.
        fs::path first = path;
        for (fs::path up = path.parent_path(); !up.empty() && up != up.parent_path() && !fs::exists(up, ec);
             up = up.parent_path())
            first = up;
        fs::create_directories(path, ec);
        if (ec)
            return failure(ec, "mkdir", pathText);
        return ok(fromPath(first));
    }
    if (name == "rmdir" || name == "rm" || name == "unlink")
    {
        const bool recursive = args[1].toBool();
        const bool force = args[2].toBool();
        const fs::file_status status = fs::symlink_status(path, ec);
        if (!fs::exists(status) && status.type() != fs::file_type::symlink)
        {
            if (force)
                return ok();
            return failure("ENOENT", "no such file or directory", name, pathText);
        }
        const bool directory = status.type() == fs::file_type::directory;
        if (name == "unlink" && directory)
            return failure("EPERM", "operation not permitted", "unlink", pathText);
        if (name == "rmdir" && !directory)
            return failure("ENOTDIR", "not a directory", "rmdir", pathText);
        if (name == "rm" && directory && !recursive)
            return failure("ERR_FS_EISDIR", "path is a directory", "rm", pathText);
        if (recursive)
            fs::remove_all(path, ec);
        else
            fs::remove(path, ec);
        if (ec)
            return failure(ec, name, pathText);
        return ok();
    }
    if (name == "rename")
    {
        const std::string toText = args[1].toString();
        if (!fs::exists(fs::symlink_status(path, ec)))
            return failure("ENOENT", "no such file or directory", "rename", pathText);
        fs::rename(path, toPath(toText), ec);
        if (ec)
            return failure(ec, "rename", pathText);
        return ok();
    }
    if (name == "copy")
    {
        const std::string toText = args[1].toString();
        const bool exclusive = (args[2].toInt() & 1) != 0;
        if (!fs::is_regular_file(path, ec))
            return failure(fs::exists(path, ec) ? "EISDIR" : "ENOENT",
                fs::exists(path, ec) ? "illegal operation on a directory" : "no such file or directory", "copyfile",
                pathText);
        if (exclusive && fs::exists(toPath(toText), ec))
            return failure("EEXIST", "file already exists", "copyfile", toText);
        fs::copy_file(path, toPath(toText), fs::copy_options::overwrite_existing, ec);
        if (ec)
            return failure(ec, "copyfile", pathText);
        return ok();
    }
    if (name == "cp")
    {
        // A folder and everything in it (fs.cpSync with recursive).
        const std::string toText = args[1].toString();
        if (!fs::exists(path, ec))
            return failure("ENOENT", "no such file or directory", "cp", pathText);
        fs::copy(path, toPath(toText),
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
            return failure(ec, "cp", pathText);
        return ok();
    }
    if (name == "access")
    {
        const int mode = args[1].toInt();
        const fs::file_status status = fs::status(path, ec);
        if (!fs::exists(status))
            return failure("ENOENT", "no such file or directory", "access", pathText);
        if ((mode & 2) != 0 && (status.permissions() & fs::perms::owner_write) == fs::perms::none)
            return failure("EACCES", "permission denied", "access", pathText);
        return ok();
    }
    if (name == "exists")
        return ok(fs::exists(path, ec));
    if (name == "realpath")
    {
        const fs::path real = fs::canonical(path, ec);
        if (ec)
            return failure(ec, "realpath", pathText);
        return ok(fromPath(real.lexically_normal()));
    }
    if (name == "truncate")
    {
        if (!fs::is_regular_file(path, ec))
            return failure("ENOENT", "no such file or directory", "open", pathText);
        fs::resize_file(path, static_cast<std::uintmax_t>(std::max(0.0, args[1].toNumber())), ec);
        if (ec)
            return failure(ec, "ftruncate", pathText);
        return ok();
    }
    if (name == "chmod")
    {
        const int mode = args[1].toInt();
        if (!fs::exists(path, ec))
            return failure("ENOENT", "no such file or directory", "chmod", pathText);
#ifdef _WIN32
        // Windows keeps only whether a file can be written.
        if ((mode & 0200) != 0)
            fs::permissions(path, fs::perms::owner_write, fs::perm_options::add, ec);
        else
            fs::permissions(path, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                fs::perm_options::remove, ec);
#else
        fs::permissions(path, static_cast<fs::perms>(mode & 07777), fs::perm_options::replace, ec);
#endif
        if (ec)
            return failure(ec, "chmod", pathText);
        return ok();
    }
    if (name == "mkdtemp")
    {
        // The prefix with six characters after it, made as a folder.
        static std::atomic<unsigned> counter{0};
        const char* const letters = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            unsigned seed = static_cast<unsigned>(
                std::chrono::steady_clock::now().time_since_epoch().count() ^ (++counter * 2654435761u));
            std::string suffix;
            for (int i = 0; i < 6; ++i)
            {
                suffix += letters[seed % 36];
                seed = seed / 36 + 7919u * (seed % 36 + 1);
            }
            const fs::path made = toPath(pathText + suffix);
            if (fs::create_directory(made, ec))
                return ok(fromPath(made));
            if (ec)
                return failure(ec, "mkdtemp", pathText);
        }
        return failure("EEXIST", "file already exists", "mkdtemp", pathText);
    }
    return failure("ENOSYS", "unknown call", name);
}

// ---------------------------------------------------------------------------
// The system

json::Value NativeHost::systemInfo() const
{
    json::Value info;
#ifdef _WIN32
    info.set("platform", "win32");
    info.set("type", "Windows_NT");
    info.set("eol", "\r\n");
    std::string release = "10.0";
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
    {
        using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
        if (auto get = reinterpret_cast<RtlGetVersion>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))))
        {
            OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);
            if (get(&version) == 0)
                release = std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
                          std::to_string(version.dwBuildNumber);
        }
    }
    info.set("release", release);
    wchar_t name[256] = L"";
    DWORD size = 256;
    if (GetComputerNameW(name, &size))
        info.set("hostname", narrow(name));
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        info.set("totalmem", static_cast<double>(memory.ullTotalPhys));
        info.set("freemem", static_cast<double>(memory.ullAvailPhys));
    }
    info.set("uptime", static_cast<double>(GetTickCount64()) / 1000.0);
    info.set("pid", static_cast<double>(GetCurrentProcessId()));
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    info.set("arch", system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
                     : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "ia32"
                                                                                       : "x64");
    const auto envOr = [](const wchar_t* key) {
        wchar_t buffer[1024] = L"";
        const DWORD n = GetEnvironmentVariableW(key, buffer, 1024);
        return n > 0 && n < 1024 ? narrow(buffer) : std::string();
    };
    info.set("homedir", envOr(L"USERPROFILE"));
    info.set("username", envOr(L"USERNAME"));
    json::Value env;
    if (wchar_t* block = GetEnvironmentStringsW())
    {
        for (const wchar_t* at = block; *at; at += wcslen(at) + 1)
        {
            const std::wstring entry(at);
            const size_t equals = entry.find(L'=', 1);
            if (equals != std::wstring::npos)
                env.set(narrow(entry.substr(0, equals)), narrow(entry.substr(equals + 1)));
        }
        FreeEnvironmentStringsW(block);
    }
    info.set("env", env);
#else
#if defined(__APPLE__)
    info.set("platform", "darwin");
    info.set("type", "Darwin");
#else
    info.set("platform", "linux");
    info.set("type", "Linux");
#endif
    info.set("eol", "\n");
    char name[256] = "";
    if (gethostname(name, sizeof(name)) == 0)
        info.set("hostname", std::string(name));
#if defined(__aarch64__) || defined(__arm64__)
    info.set("arch", "arm64");
#else
    info.set("arch", "x64");
#endif
    const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        info.set("totalmem", static_cast<double>(pages) * static_cast<double>(pageSize));
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    struct sysinfo system
    {
    };
    if (sysinfo(&system) == 0)
    {
        info.set("freemem", static_cast<double>(system.freeram) * system.mem_unit);
        info.set("uptime", static_cast<double>(system.uptime));
    }
#endif
    info.set("pid", static_cast<double>(getpid()));
    const char* home = std::getenv("HOME");
    info.set("homedir", std::string(home ? home : ""));
    if (const passwd* user = getpwuid(getuid()))
        info.set("username", std::string(user->pw_name));
    json::Value env;
    for (char** at = environ; at && *at; ++at)
    {
        const std::string entry(*at);
        const size_t equals = entry.find('=');
        if (equals != std::string::npos && equals > 0)
            env.set(entry.substr(0, equals), entry.substr(equals + 1));
    }
    info.set("env", env);
#endif
    std::error_code ec;
    std::string temp = fromPath(fs::temp_directory_path(ec));
    while (temp.size() > 1 && (temp.back() == '/' || temp.back() == '\\'))
        temp.pop_back();
    info.set("tmpdir", temp);
    info.set("cwd", fromPath(fs::current_path(ec)));
    info.set("cpus", static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
    info.set("execPath", _options.execPath);
    info.set("root", _options.root);
    info.set("page", _options.page);
    return info;
}

json::Value NativeHost::zlib(const json::Value& args)
{
    // operation ("deflate", "inflate", "gzip", "gunzip", "deflateRaw",
    // "inflateRaw", "unzip"), the bytes as base64, the level.
    const std::string op = args[0].toString();
    const std::string input = fromBase64(args[1].toString());
    const int level = args[2].isNumber() ? args[2].toInt() : Z_DEFAULT_COMPRESSION;
    const bool compress = op == "deflate" || op == "gzip" || op == "deflateRaw";
    const int bits = op == "gzip" || op == "gunzip" ? 15 + 16 : op == "unzip" ? 15 + 32 : op.find("Raw") != std::string::npos ? -15 : 15;
    z_stream stream{};
    if ((compress ? deflateInit2(&stream, level, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY) : inflateInit2(&stream, bits)) != Z_OK)
        return failure("ERR_ZLIB_INITIALIZATION_FAILED", "could not start zlib", op);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::string out;
    char buffer[65536];
    int result = Z_OK;
    do
    {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        result = compress ? deflate(&stream, Z_FINISH) : inflate(&stream, Z_NO_FLUSH);
        out.append(buffer, sizeof(buffer) - stream.avail_out);
        if (!compress && result == Z_BUF_ERROR && stream.avail_in == 0)
            break;
    } while (result == Z_OK);
    if (compress)
        deflateEnd(&stream);
    else
        inflateEnd(&stream);
    if (result != Z_STREAM_END)
        return failure(compress ? "Z_STREAM_ERROR" : "Z_DATA_ERROR",
            compress ? "could not compress" : "incorrect header check or truncated data", op);
    return ok(toBase64(out));
}

// ---------------------------------------------------------------------------

NativeHost::NativeHost(Options options)
    : _options(std::move(options))
    , _events(json::Value::Array())
{
}

NativeHost::~NativeHost()
{
    std::map<int, std::shared_ptr<Process>> processes;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _wake = nullptr;
        processes.swap(_processes);
    }
    for (auto& [id, p] : processes)
    {
        p->kill();
        p->closeInput();
    }
    for (auto& [id, p] : processes)
        p->join();
}

bool NativeHost::handles(const std::string& name)
{
    return name.rfind("node.", 0) == 0 || name.rfind("proc.", 0) == 0;
}

json::Value NativeHost::call(const std::string& name, const json::Value& args)
{
    if (name.rfind("proc.", 0) == 0)
        return processCall(name.substr(5), args);
    if (name.rfind("node.", 0) != 0)
        return failure("ENOSYS", "unknown call", name);
    if (!_options.node)
        return failure("ERR_ACCESS_DENIED", "the panel's manifest does not ask for Node.js", name);
    const std::string rest = name.substr(5);
    if (rest.rfind("fs.", 0) == 0)
        return fileCall(rest.substr(3), args);
    if (rest == "os.info")
        return ok(systemInfo());
    if (rest == "zlib")
        return zlib(args);
    return failure("ENOSYS", "unknown call", name);
}

} // namespace cep
