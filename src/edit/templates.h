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
/// - A slider of two frames is a checkbox (off the first, on the second),
///   and one whose frames are labeled is a list of the labels.
/// - A text field can be given a font as well as words: the value of
///   "<name>.font" is the family it is drawn in.
///
/// Two things more make a template fit what it is given:
///
/// - Responsive time: frames labeled "stretch" and "outro" in a graphic
///   mark its middle and its ending. An instance of it on a longer or
///   shorter span plays the start and the ending at their own speed and
///   stretches or squeezes the middle to fit.
/// - Pinning: an instance pinned to a text field (phoenixPin) moves with
///   one of its edges, or stretches to its size, as the words given make
///   it longer or shorter than drawn: a bar behind a name that fits it.
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
    /// A slider's frame labels and the frames they are on, from 1.
    std::vector<std::pair<std::string, int>> labels;
};

/// What a movie clip of \a symbol is as a field when it is named: a color,
/// a slider (with its \a frames) or an image.
Field::Kind clipKind(const Symbol& symbol, int* frames = nullptr, std::string* bitmap = nullptr);

/// \a symbol's frame labels, each once, with the frames they are on from 1.
std::vector<std::pair<std::string, int>> labelsOf(const Symbol& symbol);

/// The key a text field's font is kept under among the values.
std::string fontKey(const std::string& field);

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
/// The same, or the frame of a label of \a symbol's with that name.
bool frameIn(const Symbol& symbol, const std::string& value, int& frame);

/// Responsive time: where the middle that stretches starts and where the
/// ending starts, as frames from 0; -1 for none.
struct Responsive
{
    int stretch = -1;
    int outro = -1;
    bool any() const { return stretch >= 0 || outro >= 0; }
};
Responsive responsiveOf(const Symbol& symbol);
/// How far into a symbol of \a length frames to be \a elapsed frames into
/// a span of \a span: the start and the ending as they are, the middle
/// stretched or squeezed between them.
int responsiveElapsed(const Responsive& responsive, int length, int span, int elapsed);

/// Pinning an instance to a text field: \a text names the field, and
/// \a mode is "left", "right", "top" or "bottom" (moved with that edge) or
/// "width", "height" or "both" (stretched to keep the room around it).
struct Pin
{
    std::string text;
    std::string mode;
    bool any() const { return !text.empty() && !mode.empty(); }
};
extern const char* const kPinAttribute;
Pin pinOf(const SymbolInstance& instance);
std::vector<std::pair<std::string, std::string>> withPin(const SymbolInstance& instance, const Pin& pin);

/// Rows of a CSV file (comma separated, "quoted" fields with "" for a
/// quote, line breaks inside quotes kept), the first row being the field
/// names: one set of values a row after it. Empty cells are left out.
std::vector<Values> rowsOfCsv(const std::string& text);

} // namespace templates

} // namespace fla
