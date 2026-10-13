#pragma once

#include <string>

/// file:/// URIs as JSFL writes them, and the platform paths they name.
namespace jsfl::uri {

/// "file:///C|/Work/a%20b.fla" (or "file:///C:/Work/a b.fla") to
/// "C:/Work/a b.fla"; on macOS and Linux "file:///Users/me/a.fla" to
/// "/Users/me/a.fla". Anything that is not a file: URI is taken as a path
/// already.
std::string toPath(const std::string& uri);

/// The other way: "C:\Work\a b.fla" to "file:///C|/Work/a%20b.fla".
std::string fromPath(const std::string& path);

/// Whether \a text is a file: URI.
bool isUri(const std::string& text);

} // namespace jsfl::uri
