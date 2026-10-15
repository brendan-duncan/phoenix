#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Symbol;
class SymbolInstance;

/// Symbol templates, Phoenix's own: a symbol's named parts become fields
/// each instance can fill in differently, so one lower third, title card or
/// sign serves many names, words and colors.
///
/// - A dynamic text field with an instance name is a text field: an
///   instance can give it other words.
/// - A movie clip with an instance name is a color field: an instance can
///   tint it all one color.
/// - One with more than one frame is a slider instead: an instance picks
///   the frame it holds on (a rating, a level, a pose).
/// - One that holds just a bitmap is an image field: an instance gives an
///   image file to draw in the bitmap's place, at its size.
///
/// The fields reach into nested symbols, and an outer instance's values win
/// over an inner one's. Kept in the instance's phoenixTemplate attribute,
/// which other apps that open the FLA ignore: they show the symbol as
/// drawn. Free of Qt.
namespace templates {

extern const char* const kAttribute;

struct Field
{
    enum class Kind
    {
        Text,
        Color,
        Frame,
        Image
    };
    std::string name;
    Kind kind = Kind::Text;
    /// What it shows as drawn: the text, the bitmap's name for an image,
    /// or empty for a color or a slider.
    std::string value;
    /// A slider's frames, from 1.
    int frames = 1;
};

/// What a movie clip of \a symbol is as a field when it is named: a color,
/// a slider (with its \a frames) or an image.
Field::Kind clipKind(const Symbol& symbol, int* frames = nullptr, std::string* bitmap = nullptr);

/// \a symbol's fields, in the order they are first met (its layers top to
/// bottom, then the symbols it places), each name once.
std::vector<Field> fieldsOf(const Symbol& symbol);

using Values = std::map<std::string, std::string>;

/// The values \a instance gives its symbol's fields, and its attributes
/// with \a values kept in them (none when \a values is empty).
Values valuesOf(const SymbolInstance& instance);
std::vector<std::pair<std::string, std::string>> withValues(const SymbolInstance& instance, const Values& values);

/// Whether \a instance gives any field a value.
bool any(const SymbolInstance& instance);

/// "name=value&..." with %XX for the characters that would break it, and
/// back.
std::string encode(const Values& values);
Values decode(const std::string& text);

/// A color field's value as red, green and blue: "#RRGGBB" or "RRGGBB".
/// False when it is not one (an empty value leaves the part as drawn).
bool colorOf(const std::string& value, unsigned char rgb[3]);

/// A slider's value as a frame from 1. False when it is not a whole number
/// of 1 or more.
bool frameOf(const std::string& value, int& frame);

/// Rows of a CSV file (comma separated, "quoted" fields with "" for a
/// quote, line breaks inside quotes kept), the first row being the field
/// names: one set of values a row after it. Empty cells are left out.
std::vector<Values> rowsOfCsv(const std::string& text);

} // namespace templates

} // namespace fla
