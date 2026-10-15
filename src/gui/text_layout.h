#pragma once

#include <QFont>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "../edit/text_animators.h"

#include <vector>

class QPainter;

namespace fla {
class StaticText;
struct TextRun;
}

/// Lays out and draws static text: a box with a two pixel
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
    /// Extra room after each space, spreading a justified line to the box.
    double wordSpacing = 0.0;
};

struct Layout
{
    std::vector<Line> lines;
    /// The box, in the text's own coordinates.
    QRectF bounds;
};

/// The font a run asks for, resolved from the FLA's face names (ArialMT,
/// TimesNewRomanPS-BoldItalicMT) to an installed family, at the run's size
/// in pixels.
QFont fontFor(const fla::TextRun& run);

/// Font Mapping: a font a document names that is not installed, drawn in
/// one that is, by face. Read from the settings at first use; setting it
/// redraws text in the new mapping.
QHash<QString, QString> fontMapping();
void setFontMapping(const QHash<QString, QString>& mapping);

/// The document's font symbols, by name, and the font each stands for:
/// text whose face is a symbol's name and a star draws in its font.
void setFontSymbols(const QHash<QString, QString>& fonts);

/// Whether a face (as the FLA names it) resolves to an installed family,
/// mapped or not.
bool isInstalled(const QString& face);
/// The family a face is drawn in.
QString familyOf(const QString& face);

/// Every run's characters, one after another.
QString joinedText(const fla::StaticText& text);

Layout layout(const fla::StaticText& text);

/// The box the text occupies, in its own coordinates: left, top and either
/// the fixed width or the width of the longest line, by the height of the
/// lines.
QRectF localBounds(const fla::StaticText& text);

void draw(QPainter& painter, const fla::StaticText& text);

/// Whether dynamic and input fields show their box as a dotted outline: on
/// the stage, not in what is exported or played.
void setFieldOutlines(bool on);
bool fieldOutlines();

/// The text's letters as text animators number them: each character's
/// place among all of them, its word and its line.
std::vector<fla::textanim::Letter> letters(const fla::StaticText& text);

/// Draws the text a letter at a time, each changed as \a changes says
/// (one for each character of the joined text): moved, turned and scaled
/// about its middle, faded, or another character shown in its place.
void drawAnimated(QPainter& painter, const fla::StaticText& text, const std::vector<fla::textanim::Change>& changes);

/// The caret position nearest a point in the text's coordinates, as an
/// offset into the joined text.
int indexAt(const fla::StaticText& text, const QPointF& localPos);

/// Where the caret stands for an offset: a zero-width rectangle the height
/// of its line.
QRectF caretRect(const fla::StaticText& text, int index);

/// The bands a selected range covers, one per line it touches.
std::vector<QRectF> selectionRects(const fla::StaticText& text, int from, int to);

} // namespace TextLayout
