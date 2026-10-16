#include "text_field.h"

#include "backend.h"
#include "stage.h"

#include "../../data/static_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fla {
namespace display {

namespace {

constexpr double kGutter = 2.0;

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string escaped(const std::u32string& text)
{
    std::u32string out;
    for (char32_t c : text)
    {
        switch (c)
        {
        case U'<': out += U"&lt;"; break;
        case U'>': out += U"&gt;"; break;
        case U'&': out += U"&amp;"; break;
        case U'"': out += U"&quot;"; break;
        default: out += c; break;
        }
    }
    return toUtf8(out);
}

std::string colorText(uint32_t color)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "#%06X", color & 0xffffff);
    return buffer;
}

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

/// The attributes of a tag: name="value" pairs, names in lower case.
std::vector<std::pair<std::string, std::string>> attributesOf(const std::string& tag)
{
    std::vector<std::pair<std::string, std::string>> attributes;
    size_t at = 0;
    while (at < tag.size() && !std::isspace(static_cast<unsigned char>(tag[at])))
        ++at;
    while (at < tag.size())
    {
        while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at])))
            ++at;
        const size_t nameStart = at;
        while (at < tag.size() && tag[at] != '=' && !std::isspace(static_cast<unsigned char>(tag[at])))
            ++at;
        std::string name = lower(tag.substr(nameStart, at - nameStart));
        while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at])))
            ++at;
        std::string value;
        if (at < tag.size() && tag[at] == '=')
        {
            ++at;
            while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at])))
                ++at;
            if (at < tag.size() && (tag[at] == '"' || tag[at] == '\''))
            {
                const char quote = tag[at++];
                const size_t end = tag.find(quote, at);
                value = tag.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? tag.size() : end + 1;
            }
            else
            {
                const size_t start = at;
                while (at < tag.size() && !std::isspace(static_cast<unsigned char>(tag[at])))
                    ++at;
                value = tag.substr(start, at - start);
            }
        }
        if (!name.empty())
            attributes.emplace_back(name, value);
    }
    return attributes;
}

std::u32string decodeEntities(const std::string& text)
{
    std::u32string out;
    const std::u32string source = toCodePoints(text);
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] != U'&')
        {
            out += source[i];
            continue;
        }
        const size_t end = source.find(U';', i);
        if (end == std::u32string::npos || end - i > 10)
        {
            out += source[i];
            continue;
        }
        const std::string name = toUtf8(source.substr(i + 1, end - i - 1));
        char32_t c = 0;
        if (name == "lt")
            c = U'<';
        else if (name == "gt")
            c = U'>';
        else if (name == "amp")
            c = U'&';
        else if (name == "quot")
            c = U'"';
        else if (name == "apos")
            c = U'\'';
        else if (name == "nbsp")
            c = 0xa0;
        else if (!name.empty() && name[0] == '#')
            c = static_cast<char32_t>(name.size() > 1 && (name[1] == 'x' || name[1] == 'X')
                    ? std::strtoul(name.c_str() + 2, nullptr, 16)
                    : std::strtoul(name.c_str() + 1, nullptr, 10));
        if (c == 0)
        {
            out += source[i];
            continue;
        }
        out += c;
        i = end;
    }
    return out;
}

/// Each character's advance in the plain layout used without a renderer.
double advanceOf(char32_t c, const TextFormat& format)
{
    const double size = format.size.value_or(12.0);
    if (c == U'\r')
        return 0.0;
    return size * 0.6 + format.letterSpacing.value_or(0.0);
}

} // namespace

std::u32string toCodePoints(const std::string& utf8)
{
    std::u32string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();)
    {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        char32_t point = c;
        size_t extra = 0;
        if (c >= 0xf0)
        {
            point = c & 0x07;
            extra = 3;
        }
        else if (c >= 0xe0)
        {
            point = c & 0x0f;
            extra = 2;
        }
        else if (c >= 0xc0)
        {
            point = c & 0x1f;
            extra = 1;
        }
        ++i;
        for (size_t k = 0; k < extra && i < utf8.size(); ++k, ++i)
            point = (point << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3f);
        out += point;
    }
    return out;
}

std::string toUtf8(const std::u32string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text)
    {
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800)
        {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        else
        {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

TextFormat TextFormat::defaults()
{
    TextFormat format;
    format.font = "Times New Roman";
    format.size = 12.0;
    format.color = 0;
    format.bold = false;
    format.italic = false;
    format.underline = false;
    format.align = "left";
    format.leading = 0.0;
    format.letterSpacing = 0.0;
    format.leftMargin = 0.0;
    format.rightMargin = 0.0;
    format.indent = 0.0;
    format.blockIndent = 0.0;
    format.kerning = false;
    format.bullet = false;
    format.url = "";
    format.target = "";
    return format;
}

void TextFormat::merge(const TextFormat& over)
{
    if (over.font)
        font = over.font;
    if (over.size)
        size = over.size;
    if (over.color)
        color = over.color;
    if (over.bold)
        bold = over.bold;
    if (over.italic)
        italic = over.italic;
    if (over.underline)
        underline = over.underline;
    if (over.align)
        align = over.align;
    if (over.leading)
        leading = over.leading;
    if (over.letterSpacing)
        letterSpacing = over.letterSpacing;
    if (over.leftMargin)
        leftMargin = over.leftMargin;
    if (over.rightMargin)
        rightMargin = over.rightMargin;
    if (over.indent)
        indent = over.indent;
    if (over.blockIndent)
        blockIndent = over.blockIndent;
    if (over.kerning)
        kerning = over.kerning;
    if (over.bullet)
        bullet = over.bullet;
    if (over.url)
        url = over.url;
    if (over.target)
        target = over.target;
}

void TextFormat::keepCommon(const TextFormat& other)
{
    const auto keep = [](auto& mine, const auto& theirs) {
        if (mine != theirs)
            mine.reset();
    };
    keep(font, other.font);
    keep(size, other.size);
    keep(color, other.color);
    keep(bold, other.bold);
    keep(italic, other.italic);
    keep(underline, other.underline);
    keep(align, other.align);
    keep(leading, other.leading);
    keep(letterSpacing, other.letterSpacing);
    keep(leftMargin, other.leftMargin);
    keep(rightMargin, other.rightMargin);
    keep(indent, other.indent);
    keep(blockIndent, other.blockIndent);
    keep(kerning, other.kerning);
    keep(bullet, other.bullet);
    keep(url, other.url);
    keep(target, other.target);
}

bool TextFormat::operator==(const TextFormat& o) const
{
    return font == o.font && size == o.size && color == o.color && bold == o.bold && italic == o.italic &&
        underline == o.underline && align == o.align && leading == o.leading && letterSpacing == o.letterSpacing &&
        leftMargin == o.leftMargin && rightMargin == o.rightMargin && indent == o.indent &&
        blockIndent == o.blockIndent && kerning == o.kerning && bullet == o.bullet && url == o.url && target == o.target;
}

TextField::TextField()
    : InteractiveObject(ObjectKind::TextField)
    , _default(TextFormat::defaults())
{}

void TextField::textChanged()
{
    ++_layoutRevision;
    _caret = std::min(_caret, length());
    _anchor = std::min(_anchor, length());
    autoSizeNow();
    _scrollV = std::min(_scrollV, maxScrollV());
    invalidate();
}

std::string TextField::text() const
{
    return toUtf8(_chars);
}

void TextField::setText(const std::string& text)
{
    std::u32string chars = toCodePoints(text);
    std::u32string normalized;
    for (size_t i = 0; i < chars.size(); ++i)
    {
        if (chars[i] == U'\r' && i + 1 < chars.size() && chars[i + 1] == U'\n')
            continue;
        normalized += chars[i] == U'\n' ? U'\r' : chars[i];
    }
    _chars = normalized;
    _formats.assign(_chars.size(), _default);
    textChanged();
}

void TextField::appendText(const std::string& text)
{
    replaceText(length(), length(), text);
}

void TextField::replaceText(int begin, int end, const std::string& text)
{
    begin = std::max(0, std::min(begin, length()));
    end = std::max(begin, std::min(end, length()));
    std::u32string chars;
    for (char32_t c : toCodePoints(text))
        chars += c == U'\n' ? U'\r' : c;
    const TextFormat format =
        begin < length() ? _formats[static_cast<size_t>(begin)] : (length() > 0 ? _formats.back() : _default);
    insert(begin, end, chars, format);
    textChanged();
}

void TextField::replaceSelectedText(const std::string& text)
{
    const int begin = selectionBeginIndex();
    replaceText(begin, selectionEndIndex(), text);
    _caret = _anchor = begin + static_cast<int>(toCodePoints(text).size());
}

void TextField::insert(int begin, int end, const std::u32string& text, const TextFormat& format)
{
    _chars.replace(static_cast<size_t>(begin), static_cast<size_t>(end - begin), text);
    _formats.erase(_formats.begin() + begin, _formats.begin() + end);
    _formats.insert(_formats.begin() + begin, text.size(), format);
}

void TextField::setDefaultTextFormat(const TextFormat& format)
{
    TextFormat merged = _default;
    merged.merge(format);
    _default = merged;
}

const TextFormat& TextField::formatAt(int index) const
{
    if (index >= 0 && index < length())
        return _formats[static_cast<size_t>(index)];
    return _default;
}

TextFormat TextField::getTextFormat(int begin, int end) const
{
    if (begin < 0 && end < 0)
    {
        begin = 0;
        end = length();
    }
    else if (end < 0)
        end = begin + 1;
    begin = std::max(0, begin);
    end = std::min(end, length());
    if (begin >= end)
        return _default;
    TextFormat common = _formats[static_cast<size_t>(begin)];
    for (int i = begin + 1; i < end; ++i)
        common.keepCommon(_formats[static_cast<size_t>(i)]);
    return common;
}

void TextField::setTextFormat(const TextFormat& format, int begin, int end)
{
    if (begin < 0 && end < 0)
    {
        begin = 0;
        end = length();
    }
    else if (end < 0)
        end = begin + 1;
    begin = std::max(0, begin);
    end = std::min(end, length());
    if (begin >= end)
        return;
    for (int i = begin; i < end; ++i)
        _formats[static_cast<size_t>(i)].merge(format);
    // Alignment, margins, indents and bullets belong to whole paragraphs.
    if (format.align || format.leftMargin || format.rightMargin || format.indent || format.blockIndent ||
        format.bullet || format.leading)
    {
        int from = begin;
        while (from > 0 && _chars[static_cast<size_t>(from - 1)] != U'\r')
            --from;
        int to = end;
        while (to < length() && _chars[static_cast<size_t>(to - 1)] != U'\r')
            ++to;
        TextFormat paragraph;
        paragraph.align = format.align;
        paragraph.leftMargin = format.leftMargin;
        paragraph.rightMargin = format.rightMargin;
        paragraph.indent = format.indent;
        paragraph.blockIndent = format.blockIndent;
        paragraph.bullet = format.bullet;
        paragraph.leading = format.leading;
        for (int i = from; i < to; ++i)
            _formats[static_cast<size_t>(i)].merge(paragraph);
    }
    textChanged();
}

uint32_t TextField::textColor() const
{
    return formatAt(0).color.value_or(0);
}

void TextField::setTextColor(uint32_t color)
{
    TextFormat format;
    format.color = color & 0xffffff;
    _default.color = color & 0xffffff;
    for (TextFormat& each : _formats)
        each.merge(format);
    textChanged();
}

std::string TextField::htmlText() const
{
    std::string html;
    int at = 0;
    const int n = length();
    // A paragraph at a time, each in runs of one format.
    do
    {
        int end = at;
        while (end < n && _chars[static_cast<size_t>(end)] != U'\r')
            ++end;
        const TextFormat& first = formatAt(at < n ? at : n - 1);
        std::string align = first.align.value_or("left");
        for (char& c : align)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        html += "<P ALIGN=\"" + align + "\">";
        int run = at;
        if (run == end)
        {
            const TextFormat& f = first;
            html += "<FONT FACE=\"" + f.font.value_or("Times New Roman") + "\" SIZE=\"" + number(f.size.value_or(12)) +
                "\" COLOR=\"" + colorText(f.color.value_or(0)) + "\" LETTERSPACING=\"" +
                number(f.letterSpacing.value_or(0)) + "\" KERNING=\"" + (f.kerning.value_or(false) ? "1" : "0") +
                "\"></FONT>";
        }
        while (run < end)
        {
            int stop = run + 1;
            while (stop < end && _formats[static_cast<size_t>(stop)] == _formats[static_cast<size_t>(run)])
                ++stop;
            const TextFormat& f = _formats[static_cast<size_t>(run)];
            std::string inner = escaped(_chars.substr(static_cast<size_t>(run), static_cast<size_t>(stop - run)));
            if (f.underline.value_or(false))
                inner = "<U>" + inner + "</U>";
            if (f.italic.value_or(false))
                inner = "<I>" + inner + "</I>";
            if (f.bold.value_or(false))
                inner = "<B>" + inner + "</B>";
            if (!f.url.value_or("").empty())
                inner = "<A HREF=\"" + *f.url + "\" TARGET=\"" + f.target.value_or("") + "\">" + inner + "</A>";
            html += "<FONT FACE=\"" + f.font.value_or("Times New Roman") + "\" SIZE=\"" + number(f.size.value_or(12)) +
                "\" COLOR=\"" + colorText(f.color.value_or(0)) + "\" LETTERSPACING=\"" +
                number(f.letterSpacing.value_or(0)) + "\" KERNING=\"" + (f.kerning.value_or(false) ? "1" : "0") + "\">" +
                inner + "</FONT>";
            run = stop;
        }
        html += "</P>";
        at = end + 1;
    } while (at <= n && at > 0 && at - 1 < n);
    return html;
}

void TextField::setHtmlText(const std::string& html)
{
    std::u32string chars;
    std::vector<TextFormat> formats;
    std::vector<TextFormat> stack{_default};
    std::vector<std::string> open;
    // A paragraph's formats (align) go on all of its characters as it ends.
    size_t paragraphStart = 0;
    std::optional<std::string> paragraphAlign;
    const auto endParagraph = [&]() {
        if (paragraphAlign)
            for (size_t i = paragraphStart; i < formats.size(); ++i)
                formats[i].align = paragraphAlign;
        paragraphAlign.reset();
    };
    const auto add = [&](const std::u32string& text) {
        for (char32_t c : text)
        {
            if (c == U'\n' || c == U'\r')
            {
                if (condenseWhite)
                    c = U' ';
                else
                    c = U'\r';
            }
            if (condenseWhite && c == U' ' && !chars.empty() && (chars.back() == U' ' || chars.back() == U'\r'))
                continue;
            chars += c;
            formats.push_back(stack.back());
            if (c == U'\r')
            {
                endParagraph();
                paragraphStart = chars.size();
            }
        }
    };
    const auto breakLine = [&]() {
        chars += U'\r';
        formats.push_back(stack.back());
        endParagraph();
        paragraphStart = chars.size();
    };
    size_t at = 0;
    while (at < html.size())
    {
        const size_t lt = html.find('<', at);
        add(decodeEntities(html.substr(at, lt == std::string::npos ? std::string::npos : lt - at)));
        if (lt == std::string::npos)
            break;
        const size_t gt = html.find('>', lt);
        if (gt == std::string::npos)
            break;
        std::string tag = html.substr(lt + 1, gt - lt - 1);
        at = gt + 1;
        const bool closing = !tag.empty() && tag[0] == '/';
        if (closing)
            tag = tag.substr(1);
        const bool selfClosing = !tag.empty() && tag.back() == '/';
        if (selfClosing)
            tag.pop_back();
        size_t nameEnd = 0;
        while (nameEnd < tag.size() && !std::isspace(static_cast<unsigned char>(tag[nameEnd])))
            ++nameEnd;
        const std::string name = lower(tag.substr(0, nameEnd));
        if (name == "br")
        {
            breakLine();
            continue;
        }
        if (closing)
        {
            // Back to the format outside the tag closed.
            for (size_t i = open.size(); i-- > 0;)
                if (open[i] == name)
                {
                    open.resize(i);
                    stack.resize(i + 1);
                    break;
                }
            if (name == "p" || name == "li")
                breakLine();
            continue;
        }
        TextFormat format = stack.back();
        const auto attributes = attributesOf(tag);
        const auto attribute = [&attributes](const char* key) -> std::optional<std::string> {
            for (const auto& [k, v] : attributes)
                if (k == key)
                    return v;
            return std::nullopt;
        };
        if (name == "b")
            format.bold = true;
        else if (name == "i")
            format.italic = true;
        else if (name == "u")
            format.underline = true;
        else if (name == "p")
        {
            // A paragraph starts on a line of its own.
            if (!chars.empty() && chars.back() != U'\r')
                breakLine();
            if (auto align = attribute("align"))
                paragraphAlign = lower(*align);
        }
        else if (name == "li")
        {
            if (!chars.empty() && chars.back() != U'\r')
                breakLine();
            format.bullet = true;
        }
        else if (name == "font")
        {
            if (auto face = attribute("face"))
                format.font = *face;
            if (auto size = attribute("size"))
            {
                const double value = std::atof(size->c_str());
                if (!size->empty() && ((*size)[0] == '+' || (*size)[0] == '-'))
                    format.size = format.size.value_or(12.0) + value;
                else
                    format.size = value;
            }
            if (auto color = attribute("color"))
            {
                const std::string hex = !color->empty() && (*color)[0] == '#' ? color->substr(1) : *color;
                format.color = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16)) & 0xffffff;
            }
            if (auto spacing = attribute("letterspacing"))
                format.letterSpacing = std::atof(spacing->c_str());
            if (auto kerning = attribute("kerning"))
                format.kerning = std::atoi(kerning->c_str()) != 0;
        }
        else if (name == "a")
        {
            if (auto href = attribute("href"))
                format.url = *href;
            if (auto target = attribute("target"))
                format.target = *target;
        }
        else if (name == "textformat")
        {
            if (auto v = attribute("leading"))
                format.leading = std::atof(v->c_str());
            if (auto v = attribute("indent"))
                format.indent = std::atof(v->c_str());
            if (auto v = attribute("leftmargin"))
                format.leftMargin = std::atof(v->c_str());
            if (auto v = attribute("rightmargin"))
                format.rightMargin = std::atof(v->c_str());
            if (auto v = attribute("blockindent"))
                format.blockIndent = std::atof(v->c_str());
        }
        if (!selfClosing)
        {
            open.push_back(name);
            stack.push_back(format);
        }
    }
    endParagraph();
    // The last paragraph's end is not kept.
    if (!chars.empty() && chars.back() == U'\r')
    {
        chars.pop_back();
        formats.pop_back();
    }
    _chars = chars;
    _formats = formats;
    textChanged();
}

void TextField::setType(Type type)
{
    _type = type;
    invalidate();
}

void TextField::setAutoSize(AutoSize size)
{
    _autoSize = size;
    textChanged();
}

void TextField::setMultiline(bool on)
{
    _multiline = on;
    textChanged();
}

void TextField::setWordWrap(bool on)
{
    _wordWrap = on;
    textChanged();
}

void TextField::setBorder(bool on)
{
    _border = on;
    invalidate();
}

void TextField::setBorderColor(uint32_t color)
{
    _borderColor = color & 0xffffff;
    invalidate();
}

void TextField::setBackground(bool on)
{
    _background = on;
    invalidate();
}

void TextField::setBackgroundColor(uint32_t color)
{
    _backgroundColor = color & 0xffffff;
    invalidate();
}

void TextField::setDisplayAsPassword(bool on)
{
    _password = on;
    textChanged();
}

void TextField::setBox(const Rect& box)
{
    _box = box;
    textChanged();
}

void TextField::setFieldWidth(double width)
{
    if (!std::isfinite(width))
        return;
    _box.width = std::max(0.0, width);
    textChanged();
}

void TextField::setFieldHeight(double height)
{
    if (!std::isfinite(height))
        return;
    _box.height = std::max(0.0, height);
    textChanged();
}

std::u32string TextField::shownCharacters() const
{
    if (!_password)
        return _chars;
    std::u32string stars;
    for (char32_t c : _chars)
        stars += c == U'\r' ? U'\r' : U'*';
    return stars;
}

const std::vector<TextLine>& TextField::lines() const
{
    if (_laidOut == _layoutRevision)
        return _lines;
    _laidOut = _layoutRevision;
    if (Backend* renderer = backend())
    {
        _lines = renderer->layoutText(*this);
        if (_lines.empty())
        {
            TextLine line;
            line.x = kGutter;
            line.y = kGutter;
            line.height = formatAt(0).size.value_or(12.0) * 1.15;
            _lines.push_back(line);
        }
        return _lines;
    }
    // Without a renderer: every character six tenths of its size across.
    _lines.clear();
    const std::u32string shown = shownCharacters();
    const double wrapAt = _box.width - 2.0 * kGutter;
    double y = kGutter;
    int start = 0;
    const int n = static_cast<int>(shown.size());
    while (start <= n)
    {
        int end = start;
        double width = 0.0;
        int lastSpace = -1;
        while (end < n && shown[static_cast<size_t>(end)] != U'\r')
        {
            const double advance = advanceOf(shown[static_cast<size_t>(end)], formatAt(end));
            if (_wordWrap && width + advance > wrapAt && end > start)
            {
                if (lastSpace >= start)
                    end = lastSpace + 1;
                break;
            }
            if (shown[static_cast<size_t>(end)] == U' ')
                lastSpace = end;
            width += advance;
            ++end;
        }
        const bool broke = end < n && shown[static_cast<size_t>(end)] == U'\r';
        TextLine line;
        line.start = start;
        line.length = end - start + (broke ? 1 : 0);
        double size = 0.0, leading = 0.0;
        width = 0.0;
        for (int i = start; i < end; ++i)
        {
            size = std::max(size, formatAt(i).size.value_or(12.0));
            leading = std::max(leading, formatAt(i).leading.value_or(0.0));
            width += advanceOf(shown[static_cast<size_t>(i)], formatAt(i));
        }
        if (size == 0.0)
        {
            size = formatAt(std::min(start, n - 1)).size.value_or(12.0);
            leading = formatAt(std::min(start, n - 1)).leading.value_or(0.0);
        }
        line.ascent = size * 0.9;
        line.descent = size * 0.25;
        line.leading = leading;
        line.height = line.ascent + line.descent + leading;
        line.width = width;
        const std::string align = formatAt(start).align.value_or("left");
        line.x = kGutter;
        if (align == "center")
            line.x = kGutter + std::max(0.0, (wrapAt - width) / 2.0);
        else if (align == "right")
            line.x = kGutter + std::max(0.0, wrapAt - width);
        line.y = y;
        y += line.height;
        _lines.push_back(line);
        if (end >= n && !broke)
            break;
        start = end + (broke ? 1 : 0);
        if (start > n)
            break;
    }
    return _lines;
}

int TextField::numLines() const
{
    return std::max(1, static_cast<int>(lines().size()));
}

double TextField::textWidth() const
{
    double width = 0.0;
    for (const TextLine& line : lines())
        width = std::max(width, line.width);
    return width;
}

double TextField::textHeight() const
{
    const std::vector<TextLine>& all = lines();
    if (all.empty() || _chars.empty())
        return 0.0;
    return all.back().y + all.back().height - all.back().leading - all.front().y;
}

int TextField::maxScrollV() const
{
    const std::vector<TextLine>& all = lines();
    const double room = _box.height - 2.0 * kGutter;
    double used = 0.0;
    int fit = 0;
    for (size_t i = all.size(); i-- > 0;)
    {
        used += all[i].height;
        if (used - all[i].leading > room && fit > 0)
            break;
        ++fit;
    }
    return std::max(1, static_cast<int>(all.size()) - fit + 1);
}

int TextField::bottomScrollV() const
{
    const std::vector<TextLine>& all = lines();
    if (all.empty())
        return 1;
    const int first = std::max(0, std::min(_scrollV - 1, static_cast<int>(all.size()) - 1));
    const double room = _box.height - 2.0 * kGutter;
    double used = 0.0;
    int last = first;
    for (int i = first; i < static_cast<int>(all.size()); ++i)
    {
        used += all[static_cast<size_t>(i)].height;
        if (used - all[static_cast<size_t>(i)].leading > room && i > first)
            break;
        last = i;
    }
    return last + 1;
}

void TextField::setScrollV(int line)
{
    const int clamped = std::max(1, std::min(line, maxScrollV()));
    if (clamped == _scrollV)
        return;
    _scrollV = clamped;
    invalidate();
}

double TextField::maxScrollH() const
{
    return std::max(0.0, textWidth() - (_box.width - 2.0 * kGutter));
}

void TextField::setScrollH(double pixels)
{
    _scrollH = std::max(0.0, std::min(pixels, maxScrollH()));
    invalidate();
}

std::string TextField::getLineText(int line) const
{
    const std::vector<TextLine>& all = lines();
    if (line < 0 || line >= static_cast<int>(all.size()))
        return std::string();
    const TextLine& l = all[static_cast<size_t>(line)];
    return toUtf8(_chars.substr(static_cast<size_t>(l.start), static_cast<size_t>(l.length)));
}

int TextField::getLineLength(int line) const
{
    const std::vector<TextLine>& all = lines();
    return line < 0 || line >= static_cast<int>(all.size()) ? -1 : all[static_cast<size_t>(line)].length;
}

int TextField::getLineOffset(int line) const
{
    const std::vector<TextLine>& all = lines();
    return line < 0 || line >= static_cast<int>(all.size()) ? -1 : all[static_cast<size_t>(line)].start;
}

int TextField::getLineIndexOfChar(int index) const
{
    const std::vector<TextLine>& all = lines();
    if (index < 0 || index > length())
        return -1;
    for (size_t i = 0; i < all.size(); ++i)
        if (index < all[i].start + all[i].length)
            return static_cast<int>(i);
    return all.empty() ? -1 : static_cast<int>(all.size()) - 1;
}

int TextField::getLineIndexAtPoint(double x, double y) const
{
    const std::vector<TextLine>& all = lines();
    if (all.empty() || !_box.contains(x, y))
        return -1;
    const double top = all[static_cast<size_t>(std::max(0, std::min(_scrollV - 1, static_cast<int>(all.size()) - 1)))].y;
    const double ty = y - _box.y + top - kGutter;
    for (size_t i = 0; i < all.size(); ++i)
        if (ty >= all[i].y - kGutter && ty < all[i].y - kGutter + all[i].height)
            return static_cast<int>(i);
    return -1;
}

int TextField::getCharIndexAtPoint(double x, double y) const
{
    const int line = getLineIndexAtPoint(x, y);
    if (line < 0)
        return -1;
    const TextLine& l = lines()[static_cast<size_t>(line)];
    const std::vector<TextLine>& all = lines();
    const double top = all[static_cast<size_t>(std::max(0, std::min(_scrollV - 1, static_cast<int>(all.size()) - 1)))].y;
    const double tx = x - _box.x + _scrollH;
    const double ty = y - _box.y + top - kGutter;
    for (int i = l.start; i < l.start + l.length; ++i)
    {
        const Rect box = getCharBoundaries(i);
        if (box.isEmpty())
            continue;
        const double bx = box.x - _box.x + _scrollH;
        if (tx >= bx && tx < bx + box.width && ty >= l.y - kGutter)
            return i;
    }
    return -1;
}

Rect TextField::getCharBoundaries(int index) const
{
    if (index < 0 || index >= length())
        return Rect();
    Rect box;
    if (Backend* renderer = backend())
        box = renderer->textCharBox(*this, index);
    else
    {
        const int lineIndex = getLineIndexOfChar(index);
        if (lineIndex < 0)
            return Rect();
        const TextLine& line = lines()[static_cast<size_t>(lineIndex)];
        const std::u32string shown = shownCharacters();
        double x = line.x;
        for (int i = line.start; i < index; ++i)
            x += advanceOf(shown[static_cast<size_t>(i)], formatAt(i));
        box = Rect(x, line.y, advanceOf(shown[static_cast<size_t>(index)], formatAt(index)), line.height);
    }
    // In the field's own space, scrolled.
    const std::vector<TextLine>& all = lines();
    const double top = all.empty()
        ? kGutter
        : all[static_cast<size_t>(std::max(0, std::min(_scrollV - 1, static_cast<int>(all.size()) - 1)))].y;
    return Rect(box.x + _box.x - _scrollH, box.y + _box.y - (top - kGutter), box.width, box.height);
}

void TextField::setSelection(int begin, int end)
{
    _anchor = std::max(0, std::min(begin, length()));
    _caret = std::max(0, std::min(end, length()));
    invalidate();
}

bool TextField::showsCaret() const
{
    const Stage* on = stage();
    return _type == Type::Input && on && on->focus() == this;
}

bool TextField::allowed(char32_t c) const
{
    if (restrictTo.empty())
        return true;
    // restrict: ranges and characters allowed, those after ^ refused; one
    // that starts with ^ allows all but those.
    const std::u32string rule = toCodePoints(restrictTo);
    bool allowing = true;
    bool allowed = rule[0] == U'^';
    for (size_t i = 0; i < rule.size(); ++i)
    {
        char32_t a = rule[i];
        if (a == U'^')
        {
            allowing = !allowing;
            continue;
        }
        if (a == U'\\' && i + 1 < rule.size())
            a = rule[++i];
        char32_t b = a;
        if (i + 2 < rule.size() && rule[i + 1] == U'-')
        {
            b = rule[i + 2];
            i += 2;
        }
        if (c >= a && c <= b)
            allowed = allowing;
    }
    return allowed;
}

bool TextField::scrollToCaret()
{
    const int line = getLineIndexOfChar(_caret) + 1;
    const int before = _scrollV;
    if (line < _scrollV)
        _scrollV = std::max(1, line);
    else
        while (line > bottomScrollV() && _scrollV < maxScrollV())
            ++_scrollV;
    return _scrollV != before;
}

bool TextField::keyTyped(uint32_t keyCode, uint32_t charCode, bool shift, bool ctrl)
{
    if (_type != Type::Input && !selectable)
        return false;
    const bool editable = _type == Type::Input;
    const int scrolled = _scrollV;
    bool edited = false;
    bool moved = false;
    const auto move = [&](int to) {
        _caret = std::max(0, std::min(to, length()));
        if (!shift)
            _anchor = _caret;
        moved = true;
    };
    const auto lineOf = [&](int index) { return std::max(0, getLineIndexOfChar(index)); };
    const auto removeSelection = [&]() {
        const int begin = selectionBeginIndex();
        const int end = selectionEndIndex();
        if (begin == end)
            return false;
        insert(begin, end, std::u32string(), _default);
        _caret = _anchor = begin;
        return true;
    };
    if (ctrl && (keyCode == 65 || charCode == 1))
    {
        _anchor = 0;
        _caret = length();
        invalidate();
        return true;
    }
    switch (keyCode)
    {
    case 37: // left
        move(!shift && selectionBeginIndex() != selectionEndIndex() ? selectionBeginIndex() : _caret - 1);
        break;
    case 39: // right
        move(!shift && selectionBeginIndex() != selectionEndIndex() ? selectionEndIndex() : _caret + 1);
        break;
    case 36: // home
        move(ctrl ? 0 : getLineOffset(lineOf(_caret)));
        break;
    case 35: // end
    {
        const int line = lineOf(_caret);
        const int end = getLineOffset(line) + getLineLength(line);
        const bool broke = end > 0 && end <= length() && _chars[static_cast<size_t>(end - 1)] == U'\r';
        move(ctrl ? length() : end - (broke ? 1 : 0));
        break;
    }
    case 38: // up
    case 40: // down
    {
        const int line = lineOf(_caret) + (keyCode == 38 ? -1 : 1);
        if (line < 0)
            move(0);
        else if (line >= numLines())
            move(length());
        else
        {
            // Nearest the same distance across.
            const Rect here = _caret < length() ? getCharBoundaries(_caret) : Rect();
            const double x = here.isEmpty() ? 0.0 : here.x;
            int best = getLineOffset(line);
            double nearest = 1e300;
            for (int i = getLineOffset(line); i < getLineOffset(line) + getLineLength(line); ++i)
            {
                const Rect box = getCharBoundaries(i);
                if (std::fabs(box.x - x) < nearest)
                {
                    nearest = std::fabs(box.x - x);
                    best = i;
                }
            }
            move(best);
        }
        break;
    }
    case 8: // backspace
        if (!editable)
            return false;
        if (!removeSelection() && _caret > 0)
        {
            insert(_caret - 1, _caret, std::u32string(), _default);
            _caret = _anchor = _caret - 1;
        }
        edited = true;
        break;
    case 46: // delete
        if (!editable)
            return false;
        if (!removeSelection() && _caret < length())
            insert(_caret, _caret + 1, std::u32string(), _default);
        edited = true;
        break;
    default:
    {
        if (!editable)
            return false;
        char32_t c = 0;
        if (keyCode == 13 || charCode == 13)
        {
            if (!_multiline)
                return true;
            c = U'\r';
        }
        else if (charCode >= 32 && charCode != 127 && !ctrl)
            c = static_cast<char32_t>(charCode);
        if (c == 0)
            return false;
        if (c != U'\r' && !allowed(c))
            return true;
        if (maxChars > 0 && length() - (selectionEndIndex() - selectionBeginIndex()) >= maxChars)
            return true;
        // textInput first: a listener may stop it going in.
        TextEvent input(events::kTextInput, true, true);
        input.text = toUtf8(std::u32string(1, c));
        dispatchEvent(input);
        if (input.isDefaultPrevented())
            return true;
        const TextFormat format = _caret > 0 ? formatAt(_caret - 1) : (length() > 0 ? formatAt(0) : _default);
        removeSelection();
        insert(_caret, _caret, std::u32string(1, c), format);
        _caret = _anchor = _caret + 1;
        edited = true;
        break;
    }
    }
    if (edited)
    {
        ++_layoutRevision;
        autoSizeNow();
        scrollToCaret();
        invalidate();
        Event change(events::kChange, true);
        dispatchEvent(change);
    }
    else if (moved)
    {
        scrollToCaret();
        invalidate();
    }
    if (_scrollV != scrolled)
    {
        Event scroll(events::kScroll, true);
        dispatchEvent(scroll);
    }
    return true;
}

void TextField::pressAt(double x, double y, bool shift)
{
    if (!selectable && _type != Type::Input)
        return;
    const std::vector<TextLine>& all = lines();
    const double top = all.empty()
        ? kGutter
        : all[static_cast<size_t>(std::max(0, std::min(_scrollV - 1, static_cast<int>(all.size()) - 1)))].y;
    const double tx = x - _box.x + _scrollH;
    const double ty = y - _box.y + top - kGutter;
    int index = length();
    if (Backend* renderer = backend())
        index = renderer->textIndexAt(*this, tx, ty);
    else
    {
        // The line under it, then the gap nearest across it.
        size_t line = all.empty() ? 0 : all.size() - 1;
        for (size_t i = 0; i < all.size(); ++i)
            if (ty < all[i].y + all[i].height)
            {
                line = i;
                break;
            }
        if (!all.empty())
        {
            const TextLine& l = all[line];
            const std::u32string shown = shownCharacters();
            double at = l.x;
            index = l.start;
            const int end = l.start + l.length - ((l.length > 0 && l.start + l.length - 1 < length() &&
                                                       shown[static_cast<size_t>(l.start + l.length - 1)] == U'\r')
                                                         ? 1
                                                         : 0);
            while (index < end)
            {
                const double advance = advanceOf(shown[static_cast<size_t>(index)], formatAt(index));
                if (tx < at + advance / 2.0)
                    break;
                at += advance;
                ++index;
            }
        }
    }
    _caret = std::max(0, std::min(index, length()));
    if (!shift)
        _anchor = _caret;
    invalidate();
}

void TextField::dragTo(double x, double y)
{
    pressAt(x, y, true);
}

void TextField::autoSizeNow()
{
    if (_autoSize == AutoSize::None)
        return;
    const double height = textHeight() + 2.0 * kGutter;
    if (!_wordWrap)
    {
        const double width = textWidth() + 2.0 * kGutter;
        if (_autoSize == AutoSize::Center)
            _box.x += (_box.width - width) / 2.0;
        else if (_autoSize == AutoSize::Right)
            _box.x += _box.width - width;
        _box.width = width;
        ++_layoutRevision;
    }
    _box.height = height;
}

void TextField::setFromElement(const StaticText& element)
{
    _box = Rect(element.left, element.top, std::max(element.width, 1.0), std::max(element.height, 1.0));
    _type = element.textType == StaticText::TextType::Input ? Type::Input : Type::Dynamic;
    _multiline = element.lineType == "multiline" || element.lineType == "multiline no wrap";
    _wordWrap = element.lineType == "multiline";
    _password = element.lineType == "password";
    for (const auto& [name, value] : element.extraAttributes)
    {
        if (name == "border")
            _border = value == "true";
        else if (name == "isSelectable")
            selectable = value != "false";
        else if (name == "maxCharacters")
            maxChars = std::atoi(value.c_str());
    }
    _chars.clear();
    _formats.clear();
    bool first = true;
    for (const TextRun& run : element.runs)
    {
        TextFormat format = TextFormat::defaults();
        format.font = run.face.empty() ? std::string("Times New Roman") : run.face;
        format.size = run.size > 0.0 ? run.size : 12.0;
        format.color = (uint32_t(run.fillColor[0]) << 16) | (uint32_t(run.fillColor[1]) << 8) | run.fillColor[2];
        format.letterSpacing = run.letterSpacing;
        format.kerning = run.autoKern;
        switch (run.alignment)
        {
        case TextRun::Alignment::Center: format.align = "center"; break;
        case TextRun::Alignment::Right: format.align = "right"; break;
        case TextRun::Alignment::Justify: format.align = "justify"; break;
        default: format.align = "left"; break;
        }
        for (const auto& [name, value] : run.extraAttributes)
        {
            if (name == "bold")
                format.bold = value == "true";
            else if (name == "italic")
                format.italic = value == "true";
            else if (name == "url")
                format.url = value;
            else if (name == "lineSpacing")
                format.leading = std::atof(value.c_str());
        }
        if (first)
            _default = format;
        first = false;
        for (char32_t c : toCodePoints(run.text))
        {
            _chars += c == U'\n' ? U'\r' : c;
            _formats.push_back(format);
        }
    }
    textChanged();
}

Rect TextField::contentBounds(bool) const
{
    return _box;
}

bool TextField::contentHits(double x, double y) const
{
    return _box.contains(x, y);
}

} // namespace display
} // namespace fla
