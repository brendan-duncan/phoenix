#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fla {
class Document;
class FLADocument;
struct ByteLocation;
}

/// Saving and opening Phoenix documents (.phx): the model of
/// src/data written as docs/phoenix_format.proto describes, in the
/// container of container.h. Free of Qt.
namespace phx {

struct SaveOptions
{
    /// Written in the META chunk, "Phoenix 0.3.0".
    std::string application;
    /// A PNG of the first frame for file browsers; none when empty.
    std::vector<uint8_t> thumbnailPng;
    /// Whether the document's media are to be read from the new file from
    /// now on: yes for a save, no for a copy (an autosave) that may go.
    bool relocateMedia = true;
};

/// Writes \a document to \a path. False with \a error when it could not;
/// the file at \a path is then as it was.
bool save(const fla::Document& document, const std::string& path, const SaveOptions& options, std::string& error);

/// Reads the document at \a path; null with \a error when it cannot be read.
/// \a warning says what was wrong but could be worked around (a damaged
/// part, features from a newer Phoenix); \a readOnly is set when the
/// document uses something this Phoenix must understand to edit it safely.
fla::FLADocument* load(const std::string& path, std::string& error, std::string& warning, bool& readOnly);

/// Reads the bytes of the media chunk at \a location, as Phoenix documents
/// load their media when first needed. False when the file is gone or the
/// part damaged.
bool loadMedia(const fla::ByteLocation& location, std::vector<uint8_t>& out);

/// The file described as text: the header, the index and each chunk's
/// fields, for looking into a file (`Phoenix --phx-dump`).
std::string dump(const std::string& path);

} // namespace phx
