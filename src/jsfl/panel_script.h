#pragma once

#include "host.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace jsfl {

/// The script context of an HTML extension panel: its ScriptPath file,
/// loaded once when the panel opens, and every evalScript() the panel sends
/// after, run in the same context so the functions and values the file and
/// earlier calls left are still there. Each call that changes a document is
/// one undo step, named after the panel. Free of Qt.
class PanelScript
{
public:
    /// \a name names the panel's undo steps and its errors.
    PanelScript(Host& host, std::string name);
    ~PanelScript();
    PanelScript(const PanelScript&) = delete;
    PanelScript& operator=(const PanelScript&) = delete;

    /// Makes the context and, given a \a path, runs that file in it. False
    /// with \a error set when the file is missing or throws; the context
    /// is still there for evalScript().
    bool load(const std::string& path, std::string& error);

    /// What a call came to.
    struct Result
    {
        bool ok = true;
        /// The script's value as JavaScript's String() makes it
        /// ("undefined" when it came to nothing).
        std::string value;
        /// "file:line: message" when it threw or was stopped.
        std::string error;
    };
    /// Runs \a code in the panel's context. \a poll, when given, is called
    /// every few hundredths of a second so the window can answer a Stop
    /// button.
    Result evaluate(const std::string& code, const std::function<void()>& poll = {});

    /// Ends the running call at its next check; safe from any thread.
    void stop();
    bool running() const;

    /// Tells the context's listeners (fl.addEventListener) that \a type
    /// happened; what they threw, or empty.
    std::string dispatch(const std::string& type);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace jsfl
