#include "text_layout.h"

#include "../data/static_text.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QScreen>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace TextLayout
{

namespace
{

/// Flash leaves two pixels between the box and the glyphs on every side.
const double kGutter = 2.0;

struct Face
{
    QString family;
    bool bold = false;
    bool italic = false;
};

/// Animate writes PostScript-style names: the family run together, then a
/// dash and the style, with MT or PS tacked on by the foundry. Taking that
/// apart gives a family Qt can find.
Face resolveFace(const QString& face)
{
    static QHash<QString, Face> cache;
    const auto cached = cache.find(face);
    if (cached != cache.end())
        return cached.value();

    Face result;
    QString family = face;
    QString style;
    const int dash = face.indexOf('-');
    if (dash >= 0)
    {
        family = face.left(dash);
        style = face.mid(dash + 1);
    }

    for (const char* suffix : {"PSMT", "MT", "PS"})
    {
        if (family.endsWith(suffix) && family.length() > static_cast<int>(strlen(suffix)))
        {
            family.chop(static_cast<int>(strlen(suffix)));
            break;
        }
    }

    // ArialNarrow -> Arial Narrow, TimesNewRoman -> Times New Roman.
    QString spaced;
    for (int i = 0; i < family.length(); ++i)
    {
        const QChar c = family[i];
        if (i > 0 && c.isUpper() && family[i - 1].isLower())
            spaced += ' ';
        spaced += c;
    }

    result.bold = style.contains("Bold", Qt::CaseInsensitive) || family.endsWith("Bold");
    result.italic = style.contains("Italic", Qt::CaseInsensitive) ||
        style.contains("Oblique", Qt::CaseInsensitive);

    const QStringList families = QFontDatabase::families();
    if (families.contains(spaced, Qt::CaseInsensitive))
        result.family = spaced;
    else if (families.contains(family, Qt::CaseInsensitive))
        result.family = family;
    else
        result.family = "Arial";

    cache.insert(face, result);
    return result;
}

double logicalDpi()
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    return screen ? screen->logicalDotsPerInch() : 96.0;
}

QColor colorOf(const fla::TextRun& run)
{
    return QColor(run.fillColor[0], run.fillColor[1], run.fillColor[2], run.fillColor[3]);
}

bool isBreak(QChar c)
{
    return c == QChar('\r') || c == QChar('\n');
}

/// A DOMTextAttrs attribute Phoenix keeps as read rather than models
/// (bold, italic, lineSpacing...); empty when the run has none.
std::string keptAttribute(const fla::TextRun& run, const char* name)
{
    for (const auto& attribute : run.extraAttributes)
    {
        if (attribute.first == name)
            return attribute.second;
    }
    return std::string();
}

/// Builds the lines, remembering the fonts so the caret and drawing agree.
class Builder
{
public:
    explicit Builder(const fla::StaticText& text)
        : _text(text)
        , _joined(joinedText(text))
    {
        for (const fla::TextRun& run : text.runs)
            _metrics.emplace_back(fontFor(run));
        if (_metrics.empty())
        {
            fla::TextRun fallback;
            _metrics.emplace_back(fontFor(fallback));
        }
    }

    Layout build()
    {
        const double innerWidth = _text.autoExpand ? 0.0 : std::max(0.0, _text.width - 2.0 * kGutter);
        _y = _text.top + kGutter;
        startLine(0, 0);

        int offset = 0;
        for (size_t runIndex = 0; runIndex < _text.runs.size(); ++runIndex)
        {
            const QString runText = QString::fromStdString(_text.runs[runIndex].text);
            const QFontMetricsF& fm = _metrics[runIndex];
            int i = 0;
            while (i < runText.length())
            {
                const QChar c = runText[i];
                if (isBreak(c))
                {
                    noteFont(runIndex);
                    int consumed = 1;
                    if (c == QChar('\r') && i + 1 < runText.length() && runText[i + 1] == QChar('\n'))
                        consumed = 2;
                    finishLine(offset + i, consumed);
                    i += consumed;
                    // A break that ends the run starts a line of the next.
                    startLine(offset + i, i < runText.length() ? runIndex : runIndex + 1);
                    continue;
                }

                // A word is the non-spaces and the spaces that follow them,
                // which is what wrapping keeps together.
                int end = i;
                while (end < runText.length() && !isBreak(runText[end]) && !runText[end].isSpace())
                    ++end;
                while (end < runText.length() && runText[end].isSpace() && !isBreak(runText[end]))
                    ++end;
                const QString word = runText.mid(i, end - i);
                const double width = fm.horizontalAdvance(word);

                const bool wraps = !_text.autoExpand && innerWidth > 0.0 &&
                    _line.length > 0 && _x + width > innerWidth + 1.0e-6 &&
                    !word.trimmed().isEmpty();
                if (wraps)
                {
                    finishLine(offset + i, 0);
                    startLine(offset + i, runIndex);
                }

                append(static_cast<int>(runIndex), offset + i, end - i, width);
                i = end;
            }
            offset += runText.length();
        }
        finishLine(offset, 0);

        // Each line placed in the box by its paragraph's alignment: a
        // growing box is as wide as its widest line.
        const double room = _text.autoExpand ? _widest : innerWidth;
        for (size_t i = 0; i < _lines.size(); ++i)
        {
            Line& line = _lines[i];
            double shift = 0.0;
            switch (_alignments[i])
            {
            case fla::TextRun::Alignment::Center:
                shift = (room - line.width) / 2.0;
                break;
            case fla::TextRun::Alignment::Right:
                shift = room - line.width;
                break;
            case fla::TextRun::Alignment::Left:
            case fla::TextRun::Alignment::Justify:
                break;
            }
            shift = std::max(0.0, shift);
            line.x += shift;
            for (Segment& segment : line.segments)
                segment.x += shift;
        }

        Layout result;
        result.lines = _lines;
        const double width = _text.autoExpand ? _widest + 2.0 * kGutter : _text.width;
        const double height = (_y - _text.top) + kGutter;
        result.bounds = QRectF(_text.left, _text.top, std::max(width, 2.0 * kGutter), height);
        return result;
    }

private:
    void startLine(int start, size_t runIndex)
    {
        _line = Line();
        _line.start = start;
        _line.top = _y;
        _line.x = _text.left + kGutter;
        _lineRun = runIndex;
        _x = 0.0;
        _ascent = 0.0;
        _spacing = 0.0;
    }

    void noteFont(size_t runIndex)
    {
        const size_t index = std::min(runIndex, _metrics.size() - 1);
        const QFontMetricsF& fm = _metrics[index];
        _ascent = std::max(_ascent, fm.ascent());
        // A run's line spacing is the room Animate leaves between lines, on
        // top of the font's height; without one the font's own spacing
        // stands.
        double spacing = fm.lineSpacing();
        if (index < _text.runs.size())
        {
            const std::string extra = keptAttribute(_text.runs[index], "lineSpacing");
            if (!extra.empty())
                spacing = fm.ascent() + fm.descent() + std::atof(extra.c_str());
        }
        _spacing = std::max(_spacing, spacing);
    }

    void append(int run, int start, int length, double width)
    {
        noteFont(static_cast<size_t>(run));
        if (!_line.segments.empty() && _line.segments.back().run == run &&
            _line.segments.back().start + _line.segments.back().length == start)
        {
            _line.segments.back().length += length;
            _line.segments.back().width += width;
        }
        else
        {
            Segment segment;
            segment.run = run;
            segment.start = start;
            segment.length = length;
            segment.x = _text.left + kGutter + _x;
            segment.width = width;
            _line.segments.push_back(segment);
        }
        _x += width;
        _line.length += length;
    }

    void finishLine(int end, int breakLength)
    {
        (void)breakLength;
        // An empty line is as tall as the run it starts in.
        if (_line.segments.empty())
            noteFont(std::min(_lineRun, _metrics.size() - 1));
        _line.length = end - _line.start;
        _line.ascent = _ascent;
        _line.height = _spacing;
        // Spaces a wrapped line ends on take no part in its alignment.
        double trailing = 0.0;
        if (!_line.segments.empty())
        {
            const Segment& last = _line.segments.back();
            const QString characters = _joined.mid(last.start, last.length);
            int spaces = 0;
            while (spaces < characters.length() && characters[characters.length() - 1 - spaces].isSpace())
                ++spaces;
            if (spaces > 0)
                trailing = _metrics[static_cast<size_t>(last.run)].horizontalAdvance(characters.right(spaces));
        }
        _line.width = _x - trailing;
        _lines.push_back(_line);
        // A line takes its paragraph's alignment from the run its characters
        // come from, or the run it starts in when it has none.
        const size_t run = _line.segments.empty() ? _lineRun : static_cast<size_t>(_line.segments.front().run);
        _alignments.push_back(run < _text.runs.size() ? _text.runs[run].alignment : fla::TextRun::Alignment::Left);
        _widest = std::max(_widest, _x);
        _y += _spacing;
    }

    const fla::StaticText& _text;
    QString _joined;
    std::vector<QFontMetricsF> _metrics;
    std::vector<Line> _lines;
    /// The alignment of each line's paragraph, from the run it starts in.
    std::vector<fla::TextRun::Alignment> _alignments;
    size_t _lineRun = 0;
    Line _line;
    double _x = 0.0;
    double _y = 0.0;
    double _ascent = 0.0;
    double _spacing = 0.0;
    double _widest = 0.0;
};

const Line& lineFor(const Layout& layout, int index)
{
    // The last line that starts at or before the index; an index at the very
    // end of a wrapped line belongs to the line that follows it.
    size_t chosen = 0;
    for (size_t i = 0; i < layout.lines.size(); ++i)
    {
        if (layout.lines[i].start <= index)
            chosen = i;
    }
    return layout.lines[chosen];
}

double xOfIndex(const fla::StaticText& text, const Line& line, int index, const QString& joined)
{
    (void)text;
    double x = line.x;
    for (const Segment& segment : line.segments)
    {
        x = segment.x;
        if (index <= segment.start)
            return segment.x;
        const QFontMetricsF fm(fontFor(text.runs[segment.run]));
        if (index < segment.start + segment.length)
            return segment.x + fm.horizontalAdvance(joined.mid(segment.start, index - segment.start));
        x = segment.x + segment.width;
    }
    return x;
}

} // namespace

QFont fontFor(const fla::TextRun& run)
{
    const Face face = resolveFace(run.face.empty() ? QString("Arial") : QString::fromStdString(run.face));
    QFont font(face.family);
    // The face's own style, or Animate's faux bold and italic.
    font.setBold(face.bold || keptAttribute(run, "bold") == "true");
    font.setItalic(face.italic || keptAttribute(run, "italic") == "true");
    font.setKerning(run.autoKern);
    if (run.letterSpacing != 0.0)
        font.setLetterSpacing(QFont::AbsoluteSpacing, run.letterSpacing);
    // Animate's size is in pixels; a point size at the screen's dpi draws
    // exactly that many.
    const double pixels = run.size > 0.0 ? run.size : 12.0;
    font.setPointSizeF(pixels * 72.0 / logicalDpi());
    return font;
}

QString joinedText(const fla::StaticText& text)
{
    QString joined;
    for (const fla::TextRun& run : text.runs)
        joined += QString::fromStdString(run.text);
    return joined;
}

Layout layout(const fla::StaticText& text)
{
    return Builder(text).build();
}

QRectF localBounds(const fla::StaticText& text)
{
    return layout(text).bounds;
}

void draw(QPainter& painter, const fla::StaticText& text)
{
    const Layout laid = layout(text);
    const QString joined = joinedText(text);
    painter.setBrush(Qt::NoBrush);

    // A dynamic or input field shows its box on the stage as a dotted
    // outline, the way Animate marks a field that is not plain artwork.
    if (text.textType != fla::StaticText::TextType::Static)
    {
        QPen outline(QColor(0, 0, 0), 0.0);
        outline.setStyle(Qt::DotLine);
        painter.setPen(outline);
        painter.drawRect(laid.bounds);
    }
    for (const Line& line : laid.lines)
    {
        for (const Segment& segment : line.segments)
        {
            const fla::TextRun& run = text.runs[segment.run];
            painter.setFont(fontFor(run));
            painter.setPen(QPen(colorOf(run), 1.0));
            painter.drawText(QPointF(segment.x, line.top + line.ascent),
                joined.mid(segment.start, segment.length));
        }
    }
}

int indexAt(const fla::StaticText& text, const QPointF& localPos)
{
    const Layout laid = layout(text);
    if (laid.lines.empty())
        return 0;
    const QString joined = joinedText(text);

    const Line* line = &laid.lines.front();
    for (const Line& candidate : laid.lines)
    {
        if (localPos.y() >= candidate.top)
            line = &candidate;
    }

    int best = line->start;
    double bestDistance = std::abs(localPos.x() - line->x);
    for (const Segment& segment : line->segments)
    {
        const QFontMetricsF fm(fontFor(text.runs[segment.run]));
        for (int k = 0; k <= segment.length; ++k)
        {
            const double x = segment.x + fm.horizontalAdvance(joined.mid(segment.start, k));
            const double distance = std::abs(localPos.x() - x);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = segment.start + k;
            }
        }
    }
    return best;
}

std::vector<QRectF> selectionRects(const fla::StaticText& text, int from, int to)
{
    std::vector<QRectF> bands;
    if (to <= from)
        return bands;
    const Layout laid = layout(text);
    const QString joined = joinedText(text);
    for (const Line& line : laid.lines)
    {
        const int lineEnd = line.start + line.length;
        const int a = std::max(from, line.start);
        const int b = std::min(to, lineEnd);
        if (b <= a && !(from <= line.start && to > lineEnd))
            continue;
        const double x1 = xOfIndex(text, line, std::max(a, line.start), joined);
        double x2 = xOfIndex(text, line, std::max(b, line.start), joined);
        // A line break inside the range shows as a sliver past the line's end.
        if (to > lineEnd && from <= lineEnd)
            x2 = std::max(x2, x1 + 3.0);
        bands.push_back(QRectF(x1, line.top, x2 - x1, line.height));
    }
    return bands;
}

QRectF caretRect(const fla::StaticText& text, int index)
{
    const Layout laid = layout(text);
    if (laid.lines.empty())
        return QRectF(text.left + kGutter, text.top + kGutter, 0.0, 12.0);
    const QString joined = joinedText(text);
    const Line& line = lineFor(laid, index);
    const double x = xOfIndex(text, line, index, joined);
    return QRectF(x, line.top, 0.0, line.height);
}

} // namespace TextLayout
