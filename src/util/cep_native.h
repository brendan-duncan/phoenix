#pragma once

#include "json.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

/// The native side of an HTML extension panel's Node.js modules and of
/// cep.process: files, the system, processes and zlib, answered for the
/// panel's page (resources/cep/node.js and adobe_cep.js turn these calls into
/// fs, os, child_process, zlib and cep.process). Free of Qt, so tests run a
/// page's scripts against it in QuickJS.
///
/// Calls are by name with an array of arguments, and answer at once:
/// {"value": ...} on success, {"error": message, "code": "ENOENT", ...} on
/// failure, as Node's errors carry. Processes run in the background: what
/// they write and their ends wait in a queue (takeEvents) and wake() is
/// called, from any thread, when something is added to it.
namespace cep {

class NativeHost
{
public:
    struct Options
    {
        /// Whether the panel's manifest asks for Node.js; the "node."
        /// calls are refused otherwise.
        bool node = false;
        /// The extension's folder and the page's file, for __dirname and
        /// require.
        std::string root;
        std::string page;
        /// Phoenix's own program, for process.execPath.
        std::string execPath;
    };

    explicit NativeHost(Options options);
    ~NativeHost();
    NativeHost(const NativeHost&) = delete;
    NativeHost& operator=(const NativeHost&) = delete;

    /// Answers a call: "node.fs.*", "node.os.info", "node.zlib" (Node.js
    /// panels only) and "proc.*" (processes, for cep.process and
    /// child_process).
    json::Value call(const std::string& name, const json::Value& args);
    /// Whether \a name is one of the calls answered here.
    static bool handles(const std::string& name);

    /// Called from any thread when events are waiting.
    void setWake(std::function<void()> wake);
    /// The events waiting, oldest first, as an array of {"id", "kind"
    /// ("stdout", "stderr", "exit"), "data" (base64), "code", "signal"}.
    json::Value takeEvents();

    /// Base64 of \a bytes, and back.
    static std::string toBase64(const std::string& bytes);
    static std::string fromBase64(const std::string& text);

    struct Process;

private:
    json::Value fileCall(const std::string& name, const json::Value& args);
    json::Value processCall(const std::string& name, const json::Value& args);
    json::Value systemInfo() const;
    json::Value zlib(const json::Value& args);
    void post(json::Value event);
    /// A process ended with \a code: its exit event, and its waiters woken.
    void finish(const std::shared_ptr<Process>& p, int code);
    std::shared_ptr<Process> process(int id);

    Options _options;
    std::mutex _mutex;
    std::function<void()> _wake;
    json::Value _events;
    std::map<int, std::shared_ptr<Process>> _processes;
    int _nextId = 1;
};

} // namespace cep
