#pragma once

#include "../jsfl/engine.h"

#include <atomic>
#include <map>
#include <string>
#include <vector>

namespace python {

/// Runs Python scripts over the same object model JSFL scripts use: `fl`,
/// `FLfile`, documents, timelines, layers, frames, elements and the
/// library, with the same names (fl.getDocumentDOM(), timeline.layers) and
/// snake_case ones beside them (fl.get_document_dom()).
///
/// Python is the person's own, loaded the first time a script runs (see
/// python_api.h); one interpreter serves every run, so modules a script
/// imports stay imported. Each run gets fresh globals, its print() goes to
/// the host's trace, and its changes are one undo step, as with JSFL.
class Engine
{
public:
    explicit Engine(jsfl::Host& host);

    jsfl::RunResult runFile(const std::string& path, const jsfl::RunOptions& options = jsfl::RunOptions());
    jsfl::RunResult runString(const std::string& source, const std::string& name,
        const jsfl::RunOptions& options = jsfl::RunOptions());

    /// Ends the running script at its next check (as a KeyboardInterrupt);
    /// safe from any thread.
    void stop() { _stop = true; }
    bool running() const { return _running; }

    /// Whether Python can be loaded, and if not, why; the first call looks
    /// for it.
    static bool available(std::string* why = nullptr);
    /// The Python in use, "3.12.1 at C:\Python312\python3.dll"; empty
    /// before it is loaded.
    static std::string description();

    /// Calls the listeners scripts added with fl.addEventListener for
    /// \a type ("documentOpened"...), each with a fresh fl; their changes
    /// are one undo step. An event during a run waits for it to end.
    void dispatch(const std::string& type);
    /// Whether any script still listens for events.
    bool listening() const { return !_events.empty(); }
    /// Drops every listener, and the scripts kept for them.
    void forgetListeners();

private:
    jsfl::RunResult run(const std::string& source, const std::string& name, const std::string& path,
        const jsfl::RunOptions& options);
    void flushEvents();
    /// The events listened for, and the first script listening to each,
    /// from Python's side (its lock held).
    void readEvents();

    jsfl::Host& _host;
    std::atomic<bool> _stop{false};
    bool _running = false;
    std::map<std::string, std::string> _events;
    std::vector<std::string> _pending;
};

/// Whether \a path names a Python script (.py).
bool isScript(const std::string& path);

} // namespace python
