#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/// Flash CS4 and earlier documents: the binary .fla, a compound file whose
/// streams hold MFC-serialized objects (CPicPage, CPicLayer, CPicFrame,
/// CPicShape...). Phoenix reads one by converting it to the files of an XFL
/// document, which the XFL parser then reads like any other.
///
/// What is converted: the stage size, frame rate and background; library
/// folders, symbols (graphic, button, movie clip) and their timelines;
/// scenes; layers (normal, guide, folder; name, color, visibility, lock,
/// folder membership); frames (duration, classic tween and ease, label,
/// script, sound); shapes and groups (solid, gradient and bitmap fills,
/// strokes, edges to the 1/256 twip); symbol instances (matrix,
/// transformation point, loop and first frame, color effect, name); static
/// text; bitmaps (JPEG and lossless) and sounds.
///
/// The format is undocumented. The layouts here follow the independent
/// reverse engineering in fla-decoder (MIT, Ed Moore) and were checked
/// field by field against documents saved both ways (see FLA_FORMAT.md).
/// Free of Qt.
namespace BinaryFla {

/// Converts the streams of a binary FLA (by name: "Contents", "Page 1",
/// "Symbol 3", "Media 2"...) into XFL files by path: DOMDocument.xml,
/// LIBRARY/<item>.xml and bin/<media>.dat. False with \a error set when the
/// document cannot be read.
bool convert(const std::map<std::string, std::vector<uint8_t>>& streams,
    std::map<std::string, std::vector<uint8_t>>& files, std::string& error);

} // namespace BinaryFla
