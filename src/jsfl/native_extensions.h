#pragma once

#include <functional>
#include <string>

typedef struct JSContext JSContext;

/// Native extension libraries: shared libraries in the Configuration
/// folder's External Libraries folder that add functions scripts call as
/// <library name>.<function>(...), through the C API in mm_jsapi.h.
///
/// A library is loaded once, the first time a script runs after it
/// appears, and stays loaded: its MM_InitWrapper is called with Phoenix's
/// table of functions, and the functions it defines are given to every
/// script after. Native code runs inside Phoenix, so a faulty library can
/// take Phoenix down with it; setting PHOENIX_NATIVE_EXTENSIONS=off (or
/// run-script --no-native-extensions) leaves them all unloaded.
namespace jsfl::native {

/// Whether libraries load: on unless PHOENIX_NATIVE_EXTENSIONS is off, 0,
/// no or false, or setEnabled(false) was called.
bool enabled();
void setEnabled(bool on);

/// Whether \a fileName is a library this platform loads: .dll on Windows,
/// .dylib, .bundle or .so on macOS, .so elsewhere.
bool isLibraryFile(const std::string& fileName);

/// Loads the libraries in \a configDirectory's External Libraries folder
/// not tried before, telling \a report of each that could not be loaded,
/// and puts an object for each loaded one on \a ctx's global object.
void install(JSContext* ctx, const std::string& configDirectory, const std::function<void(const std::string&)>& report);

} // namespace jsfl::native
