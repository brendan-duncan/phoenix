#pragma once

/// What happened when Phoenix crashes, kept to look at afterwards: on
/// Windows, a minidump and a line of text (the exception, and where, as a
/// module and an offset into it) in %LOCALAPPDATA%\Phoenix\Crashes, one pair
/// a crash. A debugger opens the dump with the build's symbols beside it.
/// Elsewhere it does nothing.
namespace CrashDump {

/// Set up as early as possible in main.
void install();

/// The folder crash reports are written to, empty where there is none.
const char* folder();

} // namespace CrashDump
