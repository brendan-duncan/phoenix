#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

/// SWF movies read as documents, for recovering work when only the
/// published movie survives. The movie is converted to the files of an XFL
/// document, which the XFL parser then reads like any other, as an old
/// binary FLA is.
///
/// What comes back: the stage size, frame rate and background; shapes
/// (solid, gradient and bitmap fills, strokes with their caps and joins),
/// morph shapes as a shape on each frame they change; movie clips, buttons
/// and the main timeline, a layer for each depth with a keyframe wherever
/// what is at the depth changes (instance names, color effects, filters,
/// blend modes, clip depths as mask layers); frame labels; bitmaps (JPEG,
/// JPEG with alpha, lossless); sounds, event and streamed (MP3, ADPCM,
/// raw); static text as its glyph outlines, dynamic and input text as text
/// fields; 9-slice grids, and class names (SymbolClass, ExportAssets) as
/// symbol names and linkage; videos as items of their size, rate and
/// length, placed where the movie shows them on hidden layers (their
/// pictures are not decoded). A shape placed only once goes in where it is
/// placed; one placed more often becomes a graphic symbol.
///
/// The frames come baked, a placement on each; where a symbol moves, turns,
/// scales or changes color across frames just as a classic tween between
/// two of them would show it (evenly or with an ease), those frames go back
/// to a classic tween, and a morph shape whose ratio runs on the same way
/// to a shape tween; a placement that only moves to where it already is
/// holds on the keyframe before.
///
/// What cannot come back, and the report says so: tweens that are not that
/// clean, layer names, library folders and symbol names (unless the movie
/// exported them), guide layers, and scripts: compiled ActionScript cannot
/// be turned back into source, so a frame that had a script gets a comment
/// saying so. Free of Qt.
namespace SwfImport {

/// Pixels of an image the converter cannot decode itself (a JPEG it must
/// join with its alpha, a PNG or GIF in a DefineBitsJPEG2): 0xAARRGGBB,
/// premultiplied, top row first.
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
};

/// Decodes JPEG, PNG and GIF bytes; false when it cannot. Set once by the
/// application, which has an image library; without one, JPEGs keep no
/// alpha and PNG or GIF bitmaps are left out.
using ImageDecoder = std::function<bool(const std::vector<uint8_t>& bytes, Image& image)>;
void setImageDecoder(ImageDecoder decoder);

/// Choices for a conversion.
struct Options
{
    /// Classic tweens where the baked frames show one; false keeps a
    /// keyframe wherever anything changes.
    bool recognizeTweens = true;
};

/// Whether \a head (the file's first bytes) starts an SWF movie.
bool isSwf(const unsigned char* head, size_t size);

/// Converts the SWF movie \a swf into XFL files by path: DOMDocument.xml,
/// LIBRARY/<item>.xml and bin/<media>.dat. False with \a error set when it
/// cannot be read; \a report, when given, lists what could not be brought
/// back.
bool convert(const std::vector<uint8_t>& swf, std::map<std::string, std::vector<uint8_t>>& files, std::string& error,
    std::string* report = nullptr, const Options& options = Options());

} // namespace SwfImport
