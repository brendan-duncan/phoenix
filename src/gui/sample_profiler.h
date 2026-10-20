#pragma once

#include <QString>

#include <memory>

/// A sampling profiler for the thread that starts it (x64 Windows only; a
/// no-op elsewhere): every millisecond it stops the thread, notes where it
/// is and what called it, and lets it go on. The report names the
/// functions that took the most time, on their own and with what they
/// called. For Phoenix benchmark-stage --profile.
class SampleProfiler
{
public:
    SampleProfiler();
    ~SampleProfiler();

    /// Starts sampling the calling thread; false where it cannot.
    bool start();
    void stop();
    /// The report, in plain text: the \a top functions each way.
    QString report(int top = 40) const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
