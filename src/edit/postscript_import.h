#pragma once

#include "vector_import.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

namespace pdf {
class File;
struct Object;
} // namespace pdf

/// Artwork written as PostScript: EPS files, Illustrator files older than
/// version 9 (PostScript rather than PDF), and the native Illustrator data
/// a newer file keeps beside its PDF page (all it has when it was saved
/// without PDF content). Free of Qt.
namespace vectors {

/// Whether \a bytes start as PostScript ("%!") or as an EPS with a binary
/// header (a preview beside the PostScript).
bool looksLikePostScript(const std::vector<uint8_t>& bytes);

/// An EPS or PostScript file's artwork, or an Illustrator file older than
/// version 9: Illustrator's own format is read by its operators (paths,
/// compound paths, colors, layers, hidden layers left out); any other
/// PostScript runs in a small interpreter (paths, fills and strokes in
/// gray, RGB and CMYK, transforms, procedures and the operators programs
/// use to draw). The page is the file's bounding box.
Artwork readPostScriptArt(const std::vector<uint8_t>& bytes);

/// The native Illustrator data a PDF-based Illustrator file keeps on \a page
/// (its AIPrivateData streams, in order); empty when it has none.
std::vector<uint8_t> illustratorPrivateData(pdf::File& file, const pdf::Object& page);

/// That data's artwork: uncompressed, or zlib-compressed after its
/// "%AI12_CompressedData" mark.
Artwork readIllustratorPrivateData(const std::vector<uint8_t>& data);

/// Illustrator's native format (as PostScript text): its paths and colors.
Artwork readIllustrator(const std::string& text);

/// Any PostScript program's artwork, run in the interpreter, its page
/// \a box (left, bottom, right, top in PostScript's points).
Artwork readPostScript(const std::string& text, const double box[4]);

} // namespace vectors

} // namespace fla
