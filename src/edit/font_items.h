#pragma once

#include "../data/document.h"

#include <string>
#include <vector>

namespace fla {

/// Font symbols: library items that name a font and the characters of it
/// to embed, so dynamic and input text show the same glyphs wherever the
/// movie plays. Text uses one by its name and a star ("Title Font*") as
/// its face. The items live in the document's <fonts> section, which this
/// reads and writes as it is kept (a raw section). Free of Qt.
namespace fonts {

struct FontItem
{
    std::string name;
    /// The font, as text names it (ArialMT, Arial-BoldMT).
    std::string font;
    std::string itemId;
    int id = 0;
    /// The embedded ranges, by the FLA's numbers: 1 uppercase, 2
    /// lowercase, 3 numerals, 4 punctuation, 5 basic Latin.
    std::vector<int> ranges;
    /// Characters embedded besides the ranges.
    std::string characters;
};

std::vector<FontItem> itemsOf(const Document& document);

/// The document's raw sections with its fonts section replaced by \a items
/// (or removed for none).
std::vector<Document::RawSection> withItems(const Document& document, const std::vector<FontItem>& items);

/// Whether a face names a font symbol, and the font the symbol stands for
/// (the face itself when it is not one, or names none).
bool isSymbol(const std::string& face);
std::string fontOf(const Document& document, const std::string& face);

} // namespace fonts

} // namespace fla
