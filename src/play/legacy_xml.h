#pragma once

// The XML of ActionScript 1 and 2, and of ActionScript 3's flash.xml
// (XMLDocument, XMLNode): a tree of elements and text, parsed as the player
// parses it.

#include <string>

namespace as3 {
class Val;
class VM;
}

namespace fla {
namespace play {

/// \a text parsed: [status, xmlDecl, docTypeDecl, nodes], each node
/// [1, name, [attribute, value...], children], [3, text] or (with
/// \a comments) [8, comment]. The status is 0, or what went wrong as
/// XML.status gives it (-2 to -10).
as3::Val parseLegacyXml(as3::VM& vm, const std::string& text, bool ignoreWhite, bool comments);

} // namespace play
} // namespace fla
