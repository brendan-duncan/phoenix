#pragma once

#include "command.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fla {

class Document;
class Element;
class Symbol;
class Timeline;

namespace find {

/// What Find and Replace looks for, as Animate's panel offers it.
enum class Kind
{
    Text,
    Font,
    Color,
    Symbol
};

struct Query
{
    Kind kind = Kind::Text;
    /// The text, the font face or the symbol name looked for, and what
    /// takes its place.
    std::string find;
    std::string replace;
    bool matchCase = false;
    bool wholeWord = false;
    /// For Color: the color looked for, matched on red, green and blue
    /// (each keeps its own alpha), and the one that replaces it, in fills,
    /// in strokes, in gradients' colors and in text.
    uint8_t color[3] = {0, 0, 0};
    uint8_t replaceColor[3] = {0, 0, 0};
};

/// One object that has what was looked for, and where it is.
struct Match
{
    /// "Scene 1 > Layer 1 > Frame 3", or the symbol's name for a symbol.
    std::string where;
    /// The symbol it is in, or null when it is on a scene.
    Symbol* symbol = nullptr;
    /// The scene's index, when it is on a scene.
    int scene = -1;
    int layer = 0;
    int frame = 0;
    /// The object on the frame (a group, when it is inside one) and the
    /// object itself.
    Element* topElement = nullptr;
    Element* element = nullptr;
    /// How many times it has what was looked for.
    int count = 0;
};

/// Every object in the document that has what \a query looks for: on every
/// scene and inside every symbol, inside groups too.
std::vector<Match> findAll(Document& document, const Query& query);

/// Replaces everything \a query finds, as one undoable step; null when it
/// finds nothing. \a replaced, when given, is how many were replaced.
CommandPtr replaceAll(Document& document, const Query& query, int* replaced = nullptr);

/// The font faces the document's text uses, sorted.
std::vector<std::string> fontsUsed(Document& document);

/// \a text with every \a find replaced by \a replace, matched as \a query
/// says; \a count is how many.
std::string replaced(const std::string& text, const Query& query, int& count);

} // namespace find

} // namespace fla
