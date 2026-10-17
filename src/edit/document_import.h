#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Document;

/// Importing another document or a published movie: an FLA, an XFL or an
/// SWF. Its library comes into the target's, and its main timeline (the
/// first scene's, or the movie's) as a movie clip symbol named after the
/// file, which the caller places on the stage for an import to the stage.
/// Items the target already has by name come in under free names. Free of
/// Qt.
namespace documentImport {

/// Whether \a path is a file this imports, by its extension.
bool handles(const std::string& path);

struct Result
{
    /// What adds it all to the target, as one undo step.
    CommandPtr command;
    /// The main timeline's symbol, by its name in the target.
    std::string symbol;
    /// Every item that came in, by its name in the target.
    std::vector<std::string> items;
    /// What could not be brought across exactly, from reading an SWF.
    std::string notes;
};

/// Reads the document or movie at \a path for \a target. \a symbolName
/// names the main timeline's symbol (the file's name when empty). False
/// with \a error when it cannot be read or holds nothing to import.
bool read(Document& target, const std::string& path, Result& result, std::string& error,
    const std::string& symbolName = std::string());

} // namespace documentImport
} // namespace fla
