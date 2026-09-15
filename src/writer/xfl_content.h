#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Document;

/// One file making up an XFL document.
///
/// Binary payloads are borrowed from the document rather than copied, so an
/// entry list must not outlive the document it was built from.
struct XFLEntry
{
    /// Path inside the XFL, forward-slashed and relative to its root.
    std::string path;

    /// Contents for a text entry, used when binary is null.
    std::string text;

    /// Contents for a binary entry, borrowed from the document.
    const std::vector<uint8_t>* binary = nullptr;

    bool isBinary() const { return binary != nullptr; }
};

/// Turns a document into the list of files an XFL is made of: DOMDocument.xml,
/// one file per symbol under LIBRARY/, the bitmap media, and the .xfl marker.
///
/// Both save targets are the same content in a different container -- a folder
/// on disk or a zipped .fla -- so they share this step.
class XFLContent
{
public:
    /// Builds the entry list for \a document.
    ///
    /// \a projectName names the .xfl marker file. Pass an empty string to leave
    /// the marker out, which is what the zipped form wants.
    ///
    /// Returns false and sets \a error if the document cannot be laid out at
    /// all, for instance when a library path would escape the archive.
    /// \a unsupported collects content that was written incompletely.
    static bool build(const Document& document, const std::string& projectName,
        std::vector<XFLEntry>& entries, std::vector<std::string>& unsupported,
        std::string& error);

    /// Whether \a path is a usable location inside an XFL: relative, and with no
    /// ".." that would climb out of it. Library paths come from the file being
    /// edited, so they are not automatically trustworthy.
    static bool isSafeRelativePath(const std::string& path);
};

/// Content that saving \a document would not preserve, without writing anything.
///
/// Lets a caller warn before overwriting a file rather than after. An empty
/// result means a save would be a faithful copy.
std::vector<std::string> unsupportedContent(const Document& document);

} // namespace fla
