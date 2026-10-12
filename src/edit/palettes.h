#pragma once

#include "command.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Document;
class Element;

/// Linked palettes, as in Harmony: fills, strokes and text linked by name
/// to a palette's colors, so changing a color recolors everything that
/// uses it, and swapping to another palette with the same color names (day
/// and night, say) recolors the whole document at once. What is saved is
/// the colors themselves, so Animate shows the art as last colored; the
/// palettes and the links are kept where Animate ignores them. Free of Qt.
namespace palettes {

struct Color
{
    std::string name;
    uint8_t rgba[4] = {0, 0, 0, 255};
};

struct Palette
{
    std::string name;
    std::vector<Color> colors;
};

struct Palettes
{
    std::vector<Palette> list;
    /// The one in use: its colors are what linked art is colored with.
    std::string active;

    const Palette* activePalette() const;
    Palette* activePalette();
};

Palettes load(const Document& document);
/// \a document's attributes with \a palettes kept in them.
std::vector<std::pair<std::string, std::string>> saved(const Document& document, const Palettes& palettes);

/// How many fills, strokes and text runs are linked to each color name.
std::vector<std::pair<std::string, int>> usage(const Document& document);

/// Recolors everything linked to the active palette's colors. Null when
/// nothing changes.
CommandPtr recolor(Document& document, const Palettes& palettes);

/// Links every solid fill, stroke and text run in the document whose color
/// is \a color's (red, green and blue) to it. Null when none is.
CommandPtr linkMatching(Document& document, const Color& color);

/// Colors the solid fills and strokes of \a elements (and their text) with
/// \a color and links them to it.
CommandPtr paint(const std::vector<Element*>& elements, const Color& color, bool fills, bool strokes);

/// Takes the links off \a elements.
CommandPtr unlink(const std::vector<Element*>& elements);

} // namespace palettes

} // namespace fla
