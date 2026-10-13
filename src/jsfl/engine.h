#pragma once

#include "host.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

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

    RunResult runFile(const std::string& path, const RunOptions& options = RunOptions());
    RunResult runString(const std::string& source, const std::string& name, const RunOptions& options = RunOptions());

    /// Ends the running script at its next check; safe from any thread.
    void stop() { _stop = true; }
    bool running() const { return _running; }

    /// The source rewritten from the old JSFL dialect (for each loops,
    /// setters without a parameter) into what QuickJS compiles, line for
    /// line.
    static std::string modernize(const std::string& source);

private:
    RunResult run(const std::string& source, const std::string& name, const std::string& path, const RunOptions& options);

    Host& _host;
    std::atomic<bool> _stop{false};
    bool _running = false;
};

} // namespace jsfl
