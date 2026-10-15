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
        Color
    };
    std::string name;
    Kind kind = Kind::Text;
    /// What it shows as drawn: the text, or empty for a color.
    std::string value;
};

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

/// Rows of a CSV file (comma separated, "quoted" fields with "" for a
/// quote, line breaks inside quotes kept), the first row being the field
/// names: one set of values a row after it. Empty cells are left out.
std::vector<Values> rowsOfCsv(const std::string& text);

} // namespace templates

} // namespace fla
