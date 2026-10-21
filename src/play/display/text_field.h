#pragma once

#include "display_object.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class StaticText;

namespace display {

/// flash.text.TextFormat: each property unset (null) or set. A field's
/// characters each carry a format with every property set.
struct TextFormat
{
    std::optional<std::string> font;
    std::optional<double> size;
    std::optional<uint32_t> color;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    /// "left", "center", "right" or "justify".
    std::optional<std::string> align;
    std::optional<double> leading;
    std::optional<double> letterSpacing;
    std::optional<double> leftMargin;
    std::optional<double> rightMargin;
    std::optional<double> indent;
    std::optional<double> blockIndent;
    std::optional<bool> kerning;
    std::optional<bool> bullet;
    std::optional<std::string> url;
    std::optional<std::string> target;

    /// Flash Player's defaults: Times New Roman, 12, black, left, and no
    /// leading, spacing, margins or indents.
    static TextFormat defaults();
    /// \a over's set properties over these.
    void merge(const TextFormat& over);
    /// Only the properties both have the same (what getTextFormat gives
    /// for a range of mixed formats).
    void keepCommon(const TextFormat& other);
    bool operator==(const TextFormat& other) const;
    bool operator!=(const TextFormat& other) const { return !(*this == other); }
};

/// A line of a field as laid out: its characters (offsets into the text)
/// and where it sits in the field's text space (before scrolling, the
/// gutter included).
struct TextLine
{
    int start = 0;
    /// Characters on it, the break ending it included.
    int length = 0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double ascent = 0.0;
    double descent = 0.0;
    double leading = 0.0;
};

/// flash.text.TextField: text made in code, or a dynamic or input field a
/// timeline placed. Lines are laid out by whatever renders the movie (see
/// Backend::layoutText), or plainly when nothing does.
class TextField : public InteractiveObject
{
public:
    enum class Type
    {
        Dynamic,
        Input
    };
    enum class AutoSize
    {
        None,
        Left,
        Center,
        Right
    };

    TextField();

    /// text: paragraphs end with "\r", as the player keeps them ("\n" and
    /// "\r\n" become it).
    std::string text() const;
    void setText(const std::string& text);
    /// htmlText: the text with its formats as the player writes them; set,
    /// the tags it knows (p, br, b, i, u, font, a, textformat, li, span)
    /// make the formats.
    std::string htmlText() const;
    void setHtmlText(const std::string& html);

    /// A StyleSheet's style: the format it gives, and how it shows
    /// ("block" on lines of its own, "none" hidden, or inline).
    struct Style
    {
        TextFormat format;
        std::string display;
    };
    /// A style sheet's styles, by selector: a tag's name ("p") or a class
    /// (".note"), lower case; none for no style sheet. HTML text set after
    /// takes them, and with a style sheet htmlText reads back the text as it
    /// was given.
    void setStyles(std::optional<std::map<std::string, Style>> styles);
    bool styled() const { return _styled; }
    void appendText(const std::string& text);
    /// replaceText: [\a begin, \a end) replaced, in the format of the first
    /// character replaced (or the default format).
    void replaceText(int begin, int end, const std::string& text);
    void replaceSelectedText(const std::string& text);
    int length() const { return static_cast<int>(_chars.size()); }

    TextFormat defaultTextFormat() const { return _default; }
    void setDefaultTextFormat(const TextFormat& format);
    /// The format the range shares (-1: the whole text).
    TextFormat getTextFormat(int begin = -1, int end = -1) const;
    void setTextFormat(const TextFormat& format, int begin = -1, int end = -1);
    uint32_t textColor() const;
    void setTextColor(uint32_t color);

    Type type() const { return _type; }
    void setType(Type type);
    AutoSize autoSize() const { return _autoSize; }
    void setAutoSize(AutoSize size);
    bool multiline() const { return _multiline; }
    void setMultiline(bool on);
    bool wordWrap() const { return _wordWrap; }
    void setWordWrap(bool on);
    bool border() const { return _border; }
    void setBorder(bool on);
    uint32_t borderColor() const { return _borderColor; }
    void setBorderColor(uint32_t color);
    bool background() const { return _background; }
    void setBackground(bool on);
    uint32_t backgroundColor() const { return _backgroundColor; }
    void setBackgroundColor(uint32_t color);
    bool displayAsPassword() const { return _password; }
    void setDisplayAsPassword(bool on);
    bool selectable = true;
    bool mouseWheelEnabled = true;
    bool condenseWhite = false;
    bool embedFonts = false;
    bool alwaysShowSelection = false;
    /// 0 for no limit.
    int maxChars = 0;
    /// restrict: which characters typing may put in ("A-Z 0-9", "^a-z");
    /// empty for any.
    std::string restrictTo;

    /// The field's box in its own space: (0, 0, 100, 100) for one made in
    /// code; a timeline's has its own corner. width and height are its
    /// size, not a scale.
    Rect box() const { return _box; }
    void setBox(const Rect& box);
    double fieldWidth() const { return _box.width; }
    double fieldHeight() const { return _box.height; }
    void setFieldWidth(double width);
    void setFieldHeight(double height);
    void setWidth(double width) override { setFieldWidth(width); }
    void setHeight(double height) override { setFieldHeight(height); }

    // Lines and scrolling. Lines and scrollV count from 1 as ActionScript
    // has them; the line methods' indices from 0.
    int numLines() const;
    double textWidth() const;
    double textHeight() const;
    int scrollV() const { return _scrollV; }
    void setScrollV(int line);
    int maxScrollV() const;
    int bottomScrollV() const;
    double scrollH() const { return _scrollH; }
    void setScrollH(double pixels);
    double maxScrollH() const;
    std::string getLineText(int line) const;
    int getLineLength(int line) const;
    int getLineOffset(int line) const;
    int getLineIndexOfChar(int index) const;
    /// -1 when (\a x, \a y) in its own space is on no line.
    int getLineIndexAtPoint(double x, double y) const;
    /// -1 when on no character.
    int getCharIndexAtPoint(double x, double y) const;
    /// The character's box in the field's own space; empty when none.
    Rect getCharBoundaries(int index) const;
    const std::vector<TextLine>& lines() const;

    // Selection and typing.
    int caretIndex() const { return _caret; }
    int selectionBeginIndex() const { return std::min(_anchor, _caret); }
    int selectionEndIndex() const { return std::max(_anchor, _caret); }
    void setSelection(int begin, int end);
    /// A key typed while it has the focus (an input field): characters go
    /// in, Backspace and Delete take out, arrows, Home and End move the
    /// caret (with shift, the selection), Enter breaks the line where it is
    /// multiline. Dispatches textInput, change and scroll; true when the
    /// key was the field's.
    bool keyTyped(uint32_t keyCode, uint32_t charCode, bool shift, bool ctrl);
    /// A press at (\a x, \a y) in its own space puts the caret there (with
    /// \a shift, extends the selection); a drag after it selects.
    void pressAt(double x, double y, bool shift);
    void dragTo(double x, double y);
    /// Whether a caret shows: an input field with the focus.
    bool showsCaret() const;

    /// The characters as code points, and each one's format.
    const std::u32string& characters() const { return _chars; }
    const TextFormat& formatAt(int index) const;
    /// What is drawn: the characters, or stars for a password.
    std::u32string shownCharacters() const;

    /// For a field a timeline placed: its element, read for its box, its
    /// formats and its words.
    void setFromElement(const StaticText& element);

    Rect contentBounds(bool strokes) const override;
    bool contentHits(double x, double y) const override;

    /// Bumped as its text or formats change, for its lines to be laid out
    /// again.
    uint64_t layoutRevision() const { return _layoutRevision; }
    /// For a field a timeline placed: layoutRevision when the movie runtime
    /// last set it (to its words, or what a script or typing put in it).
    /// Changed past that, something else has changed it, and it is drawn
    /// from here rather than as the runtime shows it.
    uint64_t timelineRevision = 0;

private:
    void textChanged();
    void autoSizeNow();
    /// Scrolls so the caret's line shows; true when scrollV moved.
    bool scrollToCaret();
    void insert(int begin, int end, const std::u32string& text, const TextFormat& format);
    bool allowed(char32_t c) const;

    std::u32string _chars;
    std::vector<TextFormat> _formats;
    TextFormat _default;
    std::map<std::string, Style> _styles;
    bool _styled = false;
    /// The text given last, HTML or plain, as htmlText reads it back with
    /// a style sheet.
    std::optional<std::string> _given;
    Type _type = Type::Dynamic;
    AutoSize _autoSize = AutoSize::None;
    bool _multiline = false;
    bool _wordWrap = false;
    bool _border = false;
    uint32_t _borderColor = 0;
    bool _background = false;
    uint32_t _backgroundColor = 0xffffff;
    bool _password = false;
    Rect _box{0, 0, 100, 100};
    int _scrollV = 1;
    double _scrollH = 0.0;
    int _caret = 0;
    int _anchor = 0;
    uint64_t _layoutRevision = 1;
    mutable uint64_t _laidOut = 0;
    mutable std::vector<TextLine> _lines;
};

/// UTF-8 to code points and back.
std::u32string toCodePoints(const std::string& utf8);
std::string toUtf8(const std::u32string& text);

} // namespace display
} // namespace fla
