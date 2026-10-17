#pragma once

#include "html5_export.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class Document;

/// Publishing a movie whose scripts are ActionScript 1 or 2 to HTML5: its
/// frame scripts, instances' handlers, #initclip code and the ActionScript 2
/// classes its scripts reach on the class path, translated to JavaScript by
/// libas3 (as3::js::as2) into one module that runs on avm1.js and the
/// runtime's ActionScript 2 layer (resources/html5/as2.js). Free of Qt.
namespace html5as2 {

/// Whether \a document's scripts are ActionScript 1 or 2: its publish
/// settings say so, or say ActionScript 3 while its scripts are older (a
/// file saved again with new settings), as Test Movie decides.
bool usesActionScript2(const Document& document);

/// The SWF version the movie's scripts are for: from 7 names are case
/// sensitive. The publish settings' player version, 8 when they give none.
int swfVersion(const Document& document);

struct Result
{
    /// The module: avm1.movie(function ($, movie) { ... });
    std::string code;
    /// The classes it defines.
    std::vector<std::string> classes;
};

/// The movie's scripts \a scripts (gathered by html5::content) and the
/// classes \a findClass finds, as JavaScript. What does not translate goes
/// into \a notes and is left out.
Result translate(const Document& document, const html5::ActionScript2& scripts,
    const std::function<std::optional<std::string>(const std::string& qualifiedName)>& findClass,
    std::vector<std::string>* notes);

/// avm1.js, the helpers translated scripts need, from libas3.
const std::string& avm1Runtime();

} // namespace html5as2

} // namespace fla
