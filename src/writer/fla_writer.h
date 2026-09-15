#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;

/// Writes a document as a .fla, which is the XFL content packed into a zip.
///
/// Same files as XFLFolderWriter produces, in a different container. Use the
/// folder form when the content should stay inspectable on disk.
///
/// Free of Qt.
class FLAWriter
{
public:
    FLAWriter() = default;

    FLAWriter(const FLAWriter&) = delete;
    FLAWriter& operator=(const FLAWriter&) = delete;

    /// Writes \a document to \a filePath, replacing anything already there.
    ///
    /// Returns false and sets errorString() on failure. A true result with a
    /// non-empty unsupported() means the file was written but is not a faithful
    /// copy.
    bool write(const Document& document, const std::string& filePath);

    const std::string& errorString() const { return _errorString; }

    const std::vector<std::string>& unsupported() const { return _unsupported; }

private:
    std::string _errorString;
    std::vector<std::string> _unsupported;
};

} // namespace fla
