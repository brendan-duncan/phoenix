#pragma once

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

class QPainter;

namespace fla {
class StaticText;
struct TextRun;
}

/// Lays out and draws static text the way Flash does: a box with a two pixel
/// gutter, lines broken at returns and, for a fixed-width box, wrapped at
/// spaces; an auto-expanding box grows to fit. Every run keeps its own font
/// and color, and the caret the text tool shows is placed from the same
/// layout the stage draws from, so what is typed lands where it is seen.
namespace TextLayout
{

/// A stretch of one line drawn in one run's style.
struct Segment
{
    int run = 0;
    /// Offset into the joined text of all runs.
    int start = 0;
    int length = 0;
    double x = 0.0;
    double width = 0.0;
};

struct Line
{
    std::vector<Segment> segments;
    int start = 0;
    /// Characters on the line, not counting the break that ends it.
    int length = 0;
    double top = 0.0;
    double ascent = 0.0;
    double height = 0.0;
    /// Where the line starts and how wide its characters run, after the
    /// paragraph's alignment has placed it in the box.
    double x = 0.0;
    double width = 0.0;
};

struct Layout
{
    std::vector<Line> lines;
    /// The box, in the text's own coordinates.
    QRectF bounds;
};

/// The font a run asks for, resolved from Animate's face names (ArialMT,
/// TimesNewRomanPS-BoldItalicMT) to an installed family, at the run's size
/// in pixels.
QFont fontFor(const fla::TextRun& run);

/// Every run's characters, one after another.
QString joinedText(const fla::StaticText& text);

Layout layout(const fla::StaticText& text);

/// The box the text occupies, in its own coordinates: left, top and either
/// the fixed width or the width of the longest line, by the height of the
/// lines.
QRectF localBounds(const fla::StaticText& text);

void draw(QPainter& painter, const fla::StaticText& text);

/// The caret position nearest a point in the text's coordinates, as an
/// offset into the joined text.
int indexAt(const fla::StaticText& text, const QPointF& localPos);

/// Where the caret stands for an offset: a zero-width rectangle the height
/// of its line.
QRectF caretRect(const fla::StaticText& text, int index);

/// The bands a selected range covers, one per line it touches.
std::vector<QRectF> selectionRects(const fla::StaticText& text, int from, int to);

} // namespace TextLayout
