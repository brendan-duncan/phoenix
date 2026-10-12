#include "text_layout.h"

#include "../data/static_text.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QScreen>
#include <QSettings>

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
    /// Whether the family itself is installed, rather than mapped or
    /// standing in.
    bool installed = true;
};

QHash<QString, Face>& faceCache()
{
    static QHash<QString, Face> cache;
    return cache;
}

QHash<QString, QString>& mapping()
{
    static QHash<QString, QString> map = []() {
        QHash<QString, QString> read;
        QSettings settings;
        settings.beginGroup("fontMapping");
        for (const QString& key : settings.childKeys())
            read.insert(QString(key).replace("%2F", "/"), settings.value(key).toString());
        settings.endGroup();
        return read;
    }();
    return map;
}

/// Animate writes PostScript-style names: the family run together, then a
/// dash and the style, with MT or PS tacked on by the foundry. Taking that
/// apart gives a family Qt can find.
Face resolveFace(const QString& face)
{
    QHash<QString, Face>& cache = faceCache();
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
    const QString mapped = mapping().value(face);
    if (families.contains(spaced, Qt::CaseInsensitive))
        result.family = spaced;
    else if (families.contains(family, Qt::CaseInsensitive))
        result.family = family;
    else if (!mapped.isEmpty())
        result.family = mapped;
    else
        result.family = "Arial";
    result.installed = families.contains(spaced, Qt::CaseInsensitive) || families.contains(family, Qt::CaseInsensitive);

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

double keptNumber(const fla::TextRun& run, const char* name)
{
    const std::string value = keptAttribute(run, name);
    return value.empty() ? 0.0 : std::atof(value.c_str());
}

/// How far a superscript or subscript run sits above or below the line's
/// baseline, as a share of its full size.
double baselineShift(const fla::TextRun& run)
{
    const std::string position = keptAttribute(run, "characterPosition");
    const double size = run.size > 0.0 ? run.size : 12.0;
    if (position == "superscript")
        return -0.35 * size;
    if (position == "subscript")
        return 0.15 * size;
    return 0.0;
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
        _innerWidth = innerWidth;
        _y = _text.top + kGutter;
        _paragraphStart = true;
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
                    _paragraphStart = true;
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
                    _line.length > 0 && _x + width > lineRoom() + 1.0e-6 &&
                    !word.trimmed().isEmpty();
                if (wraps)
                {
                    finishLine(offset + i, 0);
                    _paragraphStart = false;
                    startLine(offset + i, runIndex);
                }

                append(static_cast<int>(runIndex), offset + i, end - i, width);
                i = end;
            }
            offset += runText.length();
        }
        finishLine(offset, 0);
        if (!_wrapped.empty())
            _wrapped.back() = false;

        // Each line placed in the box by its paragraph's alignment, within
        // its margins: a growing box is as wide as its widest line.
        for (size_t i = 0; i < _lines.size(); ++i)
        {
            Line& line = _lines[i];
            const double room = (_text.autoExpand ? _widest : innerWidth) - _lineInsets[i].first - _lineInsets[i].second;
            double shift = 0.0;
            switch (_alignments[i])
            {
            case fla::TextRun::Alignment::Center:
                shift = (room - line.width) / 2.0;
                break;
            case fla::TextRun::Alignment::Right:
                shift = room - line.width;
                break;
            case fla::TextRun::Alignment::Justify:
                // A wrapped line of a justified paragraph reaches both
                // margins, its spaces sharing the room left; the
                // paragraph's last line stays to the left.
                if (_wrapped[i] && !_text.autoExpand)
                {
                    int spaces = 0;
                    for (const Segment& segment : line.segments)
                        for (const QChar c : _joined.mid(segment.start, segment.length).trimmed())
                            spaces += c.isSpace() ? 1 : 0;
                    if (spaces > 0 && room > line.width)
                    {
                        line.wordSpacing = (room - line.width) / spaces;
                        double carried = 0.0;
                        for (Segment& segment : line.segments)
                        {
                            segment.x += carried;
                            int inside = 0;
                            for (const QChar c : _joined.mid(segment.start, segment.length))
                                inside += c.isSpace() ? 1 : 0;
                            const double grow = inside * line.wordSpacing;
                            segment.width += grow;
                            carried += grow;
                        }
                        line.width = room;
                    }
                }
                break;
            case fla::TextRun::Alignment::Left:
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
        (void)width;
        const double height = (_y - _text.top) + kGutter;
        result.bounds = QRectF(_text.left, _text.top, std::max(_text.autoExpand ? _widest + 2.0 * kGutter : _text.width, 2.0 * kGutter), height);
        return result;
    }

private:
    /// The paragraph a line belongs to takes its margins and first-line
    /// indent from the run the line starts in.
    const fla::TextRun* paragraphRun() const
    {
        return _text.runs.empty() ? nullptr : &_text.runs[std::min(_lineRun, _text.runs.size() - 1)];
    }

    /// How wide the line may run before it wraps, inside its margins.
    double lineRoom() const
    {
        return _innerWidth - _inset.first - _inset.second;
    }

    void startLine(int start, size_t runIndex)
    {
        _line = Line();
        _line.start = start;
        _line.top = _y;
        _lineRun = runIndex;
        _inset = {0.0, 0.0};
        if (const fla::TextRun* run = paragraphRun())
        {
            _inset.first = keptNumber(*run, "leftMargin") + (_paragraphStart ? keptNumber(*run, "indent") : 0.0);
            _inset.second = keptNumber(*run, "rightMargin");
        }
        _line.x = _text.left + kGutter + _inset.first;
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
            segment.x = _text.left + kGutter + _inset.first + _x;
            segment.width = width;
            _line.segments.push_back(segment);
        }
        _x += width;
        _line.length += length;
    }

    void finishLine(int end, int breakLength)
    {
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
        _lineInsets.push_back(_inset);
        _wrapped.push_back(breakLength == 0);
        _widest = std::max(_widest, _x + _inset.first + _inset.second);
        _y += _spacing;
    }

    const fla::StaticText& _text;
    QString _joined;
    std::vector<QFontMetricsF> _metrics;
    std::vector<Line> _lines;
    /// The alignment of each line's paragraph, from the run it starts in.
    std::vector<fla::TextRun::Alignment> _alignments;
    /// Each line's left and right insets (margins, and the indent of a
    /// paragraph's first line), and whether it was wrapped rather than
    /// ended by a return.
    std::vector<std::pair<double, double>> _lineInsets;
    std::vector<bool> _wrapped;
    std::pair<double, double> _inset{0.0, 0.0};
    double _innerWidth = 0.0;
    bool _paragraphStart = true;
    size_t _lineRun = 0;
    Line _line;
    double _x = 0.0;
    double _y = 0.0;
    double _ascent = 0.0;
    double _spacing = 0.0;
    double _widest = 0.0;
};

/// Whether the text runs in columns, and which way they follow each other.
bool isVertical(const fla::StaticText& text, bool* leftToRight = nullptr)
{
    std::string orientation;
    for (const auto& attribute : text.extraAttributes)
        if (attribute.first == "orientation")
            orientation = attribute.second;
    if (leftToRight)
        *leftToRight = orientation == "vertical left to right";
    return orientation.rfind("vertical", 0) == 0;
}

/// One character of vertical text and the cell it stands in.
struct Cell
{
    int index = 0;
    int run = 0;
    QRectF rect;
};

/// Lays vertical text out: each character in a cell as tall as its font,
/// down a column until a return or (in a fixed box) the box's height, the
/// columns side by side as wide as their widest font.
std::vector<Cell> verticalCells(const fla::StaticText& text, QRectF* bounds)
{
    bool leftToRight = false;
    isVertical(text, &leftToRight);
    const QString joined = joinedText(text);
    std::vector<int> runOf;
    for (size_t r = 0; r < text.runs.size(); ++r)
        for (int k = 0; k < QString::fromStdString(text.runs[r].text).length(); ++k)
            runOf.push_back(static_cast<int>(r));
    const double room = text.autoExpand ? 0.0 : std::max(0.0, text.height - 2.0 * kGutter);

    // Columns first, in the order they are read, measured.
    struct Column
    {
        std::vector<Cell> cells;
        double width = 0.0;
        double height = 0.0;
    };
    std::vector<Column> columns(1);
    double y = 0.0;
    for (int i = 0; i < joined.length(); ++i)
    {
        const int run = i < static_cast<int>(runOf.size()) ? runOf[static_cast<size_t>(i)] : 0;
        const fla::TextRun fallback;
        const QFontMetricsF fm(fontFor(text.runs.empty() ? fallback : text.runs[static_cast<size_t>(run)]));
        if (isBreak(joined[i]))
        {
            // The break sits at the column's foot.
            Cell cell{i, run, QRectF(0.0, y, fm.height(), 0.0)};
            columns.back().cells.push_back(cell);
            columns.back().width = std::max(columns.back().width, fm.height());
            columns.emplace_back();
            y = 0.0;
            continue;
        }
        if (room > 0.0 && y > 0.0 && y + fm.height() > room + 1.0e-6)
        {
            columns.emplace_back();
            y = 0.0;
        }
        Cell cell{i, run, QRectF(0.0, y, fm.height(), fm.height())};
        columns.back().cells.push_back(cell);
        columns.back().width = std::max(columns.back().width, fm.height());
        y += fm.height();
        columns.back().height = y;
    }
    for (Column& column : columns)
        if (column.width <= 0.0)
        {
            const fla::TextRun fallback;
            column.width = QFontMetricsF(fontFor(text.runs.empty() ? fallback : text.runs.back())).height();
        }

    double total = 0.0;
    double tallest = 0.0;
    for (const Column& column : columns)
    {
        total += column.width;
        tallest = std::max(tallest, column.height);
    }
    const double width = text.autoExpand ? total + 2.0 * kGutter : text.width;
    const double height = text.autoExpand ? tallest + 2.0 * kGutter : text.height;
    if (bounds)
        *bounds = QRectF(text.left, text.top, std::max(width, 2.0 * kGutter), std::max(height, 2.0 * kGutter));

    // Then placed: from the right edge leftward, or the left rightward.
    std::vector<Cell> cells;
    double x = leftToRight ? text.left + kGutter : text.left + width - kGutter;
    for (const Column& column : columns)
    {
        if (!leftToRight)
            x -= column.width;
        for (Cell cell : column.cells)
        {
            cell.rect = QRectF(x, text.top + kGutter + cell.rect.top(), column.width, cell.rect.height());
            cells.push_back(cell);
        }
        // An empty column still holds the caret.
        if (column.cells.empty())
            cells.push_back(Cell{static_cast<int>(joined.length()), 0, QRectF(x, text.top + kGutter, column.width, 0.0)});
        if (leftToRight)
            x += column.width;
    }
    return cells;
}

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

QHash<QString, QString> fontMapping()
{
    return mapping();
}

void setFontMapping(const QHash<QString, QString>& newMapping)
{
    mapping() = newMapping;
    faceCache().clear();
    QSettings settings;
    settings.remove("fontMapping");
    settings.beginGroup("fontMapping");
    for (auto it = newMapping.begin(); it != newMapping.end(); ++it)
        settings.setValue(QString(it.key()).replace("/", "%2F"), it.value());
    settings.endGroup();
}

bool isInstalled(const QString& face)
{
    return resolveFace(face).installed;
}

QString familyOf(const QString& face)
{
    return resolveFace(face).family;
}

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
    // exactly that many. Super- and subscripts are set smaller.
    double pixels = run.size > 0.0 ? run.size : 12.0;
    const std::string position = keptAttribute(run, "characterPosition");
    if (position == "superscript" || position == "subscript")
        pixels *= 0.6;
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
    if (isVertical(text))
    {
        QRectF bounds;
        verticalCells(text, &bounds);
        return bounds;
    }
    return layout(text).bounds;
}

void draw(QPainter& painter, const fla::StaticText& text)
{
    if (isVertical(text))
    {
        QRectF bounds;
        const std::vector<Cell> cells = verticalCells(text, &bounds);
        const QString joined = joinedText(text);
        painter.setBrush(Qt::NoBrush);
        if (text.textType != fla::StaticText::TextType::Static)
        {
            QPen outline(QColor(0, 0, 0), 0.0);
            outline.setStyle(Qt::DotLine);
            painter.setPen(outline);
            painter.drawRect(bounds);
        }
        for (const Cell& cell : cells)
        {
            if (cell.index >= joined.length() || isBreak(joined[cell.index]) || text.runs.empty())
                continue;
            const fla::TextRun& run = text.runs[static_cast<size_t>(cell.run)];
            painter.setFont(fontFor(run));
            painter.setPen(QPen(colorOf(run), 1.0));
            painter.drawText(cell.rect, Qt::AlignCenter, QString(joined[cell.index]));
        }
        return;
    }
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
            QFont font = fontFor(run);
            if (line.wordSpacing > 0.0)
                font.setWordSpacing(line.wordSpacing);
            painter.setFont(font);
            painter.setPen(QPen(colorOf(run), 1.0));
            painter.drawText(QPointF(segment.x, line.top + line.ascent + baselineShift(run)),
                joined.mid(segment.start, segment.length));
        }
    }
}

int indexAt(const fla::StaticText& text, const QPointF& localPos)
{
    if (isVertical(text))
    {
        // The nearest cell edge: above a cell's middle is before it.
        const std::vector<Cell> cells = verticalCells(text, nullptr);
        int best = 0;
        double bestDistance = 1.0e300;
        for (const Cell& cell : cells)
        {
            const double dx = localPos.x() < cell.rect.left() ? cell.rect.left() - localPos.x()
                : localPos.x() > cell.rect.right()            ? localPos.x() - cell.rect.right()
                                                              : 0.0;
            for (int end = 0; end < 2; ++end)
            {
                const double y = end == 0 ? cell.rect.top() : cell.rect.bottom();
                const double distance = dx * 4.0 + std::abs(localPos.y() - y);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = cell.index + (end == 1 && cell.rect.height() > 0.0 ? 1 : 0);
                }
            }
        }
        return best;
    }
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
    if (isVertical(text))
    {
        for (const Cell& cell : verticalCells(text, nullptr))
            if (cell.index >= from && cell.index < to)
                bands.push_back(cell.rect.height() > 0.0 ? cell.rect : cell.rect.adjusted(0.0, 0.0, 0.0, 3.0));
        return bands;
    }
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
    if (isVertical(text))
    {
        // Across the column, at the top of the cell it comes before, or at
        // the foot of the last.
        const std::vector<Cell> cells = verticalCells(text, nullptr);
        if (cells.empty())
            return QRectF(text.left + kGutter, text.top + kGutter, 12.0, 0.0);
        for (const Cell& cell : cells)
            if (cell.index == index)
                return QRectF(cell.rect.left(), cell.rect.top(), cell.rect.width(), 0.0);
        const Cell& last = cells.back();
        return QRectF(last.rect.left(), last.rect.bottom(), last.rect.width(), 0.0);
    }
    const Layout laid = layout(text);
    if (laid.lines.empty())
        return QRectF(text.left + kGutter, text.top + kGutter, 0.0, 12.0);
    const QString joined = joinedText(text);
    const Line& line = lineFor(laid, index);
    const double x = xOfIndex(text, line, index, joined);
    return QRectF(x, line.top, 0.0, line.height);
}

} // namespace TextLayout
