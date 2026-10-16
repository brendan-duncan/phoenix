#pragma once

#include "host.h"
#include "object.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace jsfl {

/// How one run may go.
struct RunOptions
{
    /// The undo step the run's changes make, named after the script.
    std::string undoName = "Script";
    /// The most memory the script's own values may take.
    size_t memoryLimit = size_t(512) * 1024 * 1024;
    /// Seconds before the run is stopped; 0 for no limit.
    double timeLimit = 0.0;
    /// Called every few hundredths of a second while the script runs, so
    /// the window can answer a Stop button; set \a stop to end the run.
    std::function<void()> poll;
    /// Only compile the script, to find syntax errors without running it.
    bool compileOnly = false;
    /// Once the script has run, its function of this name is called with
    ///  callArguments (fl.runScript with a function, from Python).
    std::string callFunction;
    Args callArguments;
};

/// How a run went.
struct RunResult
{
    bool ok = true;
    /// Stopped by stop(), the time limit or the memory limit.
    bool interrupted = false;
    /// The error as the script would print it ("TypeError: x is not a
    /// function"), and where: the script's file (or name) and line.
    std::string message;
    std::string file;
    int line = 0;
    /// The script's call stack at the error.
    std::string stack;
    /// What the script came to (its last statement's value), or what
    /// callFunction gave back. Plain values only: Phoenix's objects in it
    /// are null, as they end with the run.
    Value returned;
    /// "file:line: message", or empty when it went well.
    std::string report() const;
};

/// Runs JSFL scripts with QuickJS. Each run gets a runtime of its own,
/// with a memory limit and an interrupt check, and a Session that makes the
/// run's changes one undo step.
class Engine
{
public:
    explicit Engine(Host& host);
    ~Engine();

    RunResult runFile(const std::string& path, const RunOptions& options = RunOptions());
    RunResult runString(const std::string& source, const std::string& name, const RunOptions& options = RunOptions());

    /// Ends the running script at its next check; safe from any thread.
    void stop() { _stop = true; }
    bool running() const { return _running; }

    /// Tells the scripts listening (fl.addEventListener) that \a type
    /// happened: "documentNew", "documentOpened", "documentClosed",
    /// "documentSaved", "documentChanged", "timelineChanged",
    /// "layerChanged", "frameChanged", "selectionChanged", "prePublish",
    /// "postPublish". While a script runs, events wait until it ends.
    void dispatch(const std::string& type);
    /// How many scripts are kept loaded to hear events.
    size_t listeningScripts() const;
    /// Lets every listening script go (the window closing).
    void forgetListeners();

    /// The source rewritten from the old JSFL dialect (for each loops,
    /// setters without a parameter) into what QuickJS compiles, line for
    /// line.
    static std::string modernize(const std::string& source);

private:
    RunResult run(const std::string& source, const std::string& name, const std::string& path, const RunOptions& options);

    void flushEvents();

    struct Kept;
    Host& _host;
    std::atomic<bool> _stop{false};
    bool _running = false;
    bool _dispatching = false;
    std::vector<std::unique_ptr<Kept>> _kept;
    std::vector<std::string> _pending;
};

} // namespace jsfl
