#pragma once

#include "xfl_writer.h"

#include <string>
#include <vector>

namespace fla {

class Document;

/// Writes a document as an uncompressed XFL folder: DOMDocument.xml, one file
/// per symbol under LIBRARY/, the bitmap media, and the .xfl marker Animate uses
/// to recognise the folder as a project.
///
/// This is a format Animate opens directly, so it is the simplest useful save
/// target and the one to get right before packing the same content into a .fla
/// zip.
///
/// Free of Qt.
class XFLFolderWriter
{
public:
    XFLFolderWriter() = default;

    XFLFolderWriter(const XFLFolderWriter&) = delete;
    XFLFolderWriter& operator=(const XFLFolderWriter&) = delete;

    /// Writes \a document into \a directory, creating it if needed.
    ///
    /// \a projectName names the .xfl marker file. An empty name takes the
    /// directory's own name, which is what Animate does.
    ///
    /// Returns false and sets errorString() on failure. A true result with a
    /// non-empty unsupported() means the folder was written but is not a
    /// faithful copy.
    bool write(const Document& document, const std::string& directory,
        const std::string& projectName = std::string());

    const std::string& errorString() const { return _errorString; }

    /// Content the XML writer could not represent. See XFLWriter::unsupported().
    const std::vector<std::string>& unsupported() const { return _unsupported; }

private:
    bool writeFile(const std::string& path, const void* data, size_t size);

    std::string _errorString;
    std::vector<std::string> _unsupported;
};

} // namespace fla
