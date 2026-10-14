#pragma once

#include <string>

namespace jsfl {
namespace e4x {

/// Whether \a source may use E4X: XML literals, .@attributes, ..descendants,
/// .(filters), .* or the XML and XMLList classes. A quick check, so scripts
/// without them skip the rewrite.
bool mayUse(const std::string& source);

/// \a source with its E4X syntax rewritten into plain JavaScript over the
/// XML and XMLList classes of prelude(), line for line. Strings, regular
/// expressions and comments are left alone.
///
///   x.@id         x["@?id"]   (the attribute's value, a string)
///   x.@id.f()     x["@id"]    (the attribute as an XMLList, when used as one)
///   x.@[e]        x["@?" + (e)]
///   x.@*, x.*     x["@*"], x["*"]
///   x..item       x["..item"]
///   x.(pred)      x[__e4x.filter]((__e4xIt) => { with (__e4xIt) return (pred); })
///   @id (in pred) __e4xIt["@?id"]
///   x.* += y      x[__e4x.append] = y
///   <a b={c}>{d}</a>  XML("<a b=" + __e4x.hole(c, 1) + ">" + __e4x.hole(d, 0) + "</a>")
///   .function::f  .f
std::string rewrite(const std::string& source);

/// The script that defines XML, XMLList and the helpers rewrite() calls on.
const char* prelude();

} // namespace e4x
} // namespace jsfl
