#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

/// Photoshop documents read into their layers: each pixel layer's pixels
/// and place, the groups they are in, which are hidden, how opaque each is
/// and how it blends. RGB, CMYK and grayscale documents of 8, 16 or 32 bits,
/// large documents (PSB) too; masks are applied to the pixels, and a
/// document with no layers gives its flattened image as one. Free of Qt.
namespace psd {

struct Layer
{
    std::string name;
    /// A group, with \a children; else a pixel layer with \a rgba.
    bool group = false;
    std::vector<Layer> children;
    /// Where the pixels sit on the canvas, and their size.
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    /// Straight RGBA, row by row, top first.
    std::vector<uint8_t> rgba;
    /// 0 to 255, applied by whoever draws it.
    uint8_t opacity = 255;
    bool hidden = false;
    /// The blend mode as the FLA names it ("multiply", "screen"...), empty
    /// for normal; and whether the layer was clipped to the one below.
    std::string blend;
    bool clipped = false;
};

struct Image
{
    int width = 0;
    int height = 0;
    /// Top first, as the Layers panel lists them.
    std::vector<Layer> layers;
    /// What could not be read, in words, for the person importing.
    std::vector<std::string> notes;
    bool ok = false;
};

Image read(const std::vector<uint8_t>& bytes);

/// Unpacks PackBits run-length data into \a count bytes.
std::vector<uint8_t> unpackBits(const uint8_t* data, size_t size, size_t count);

} // namespace psd

} // namespace fla
