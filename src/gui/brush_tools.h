#pragma once

#include "tool.h"

#include "../data/point.h"
#include "../edit/brush_stroke.h"
#include "../edit/brush_texture.h"
#include "../edit/region_split.h"

#include <QPainterPath>
#include <QPointF>

#include <memory>
#include <vector>

namespace fla {
class CommandStack;
class DrawingStyle;
class Frame;
class RegionSplitter;
class Selection;
class Shape;
}

/// The outline a round brush of \a width leaves along \a points, as a shape
/// filled with \a fill (which the shape takes ownership of). The outline is
/// resolved so it has no self-crossings, and each loop is written with the
/// fill on the correct side for the merge.
fla::Shape* brushOutlineShape(const std::vector<fla::Point>& points, double width,
    bool smooth, fla::Frame* parent, const fla::DrawingStyle& style, bool asFill);

/// The area a brush stroke paints: its tip swept from dab to dab, the hulls
/// gathered with the winding rule. \a resolve merges them into one outline
/// with no overlaps, which a finished stroke needs; a preview can fill them
/// as they are.
QPainterPath sweptOutline(const std::vector<fla::BrushDab>& dabs, double zoom, bool resolve);

/// The brushes: paint a fill along the drag. The stroke is a shape with a
/// fill and no outline, and merges like any other drawing, so passes of one
/// color run together without seams.
///
/// - **Brush** (Animate's Fluid Brush): width from pressure and
///   speed, tapered ends, smoothing, and an elliptical tip with roundness
///   and angle.
/// - **Nib Brush** (Animate's Classic Brush, Flash's brush): a shaped nib,
///   with Use Pressure and Use Tilt, which turns the nib.
/// - **Ink Brush**, Phoenix's own: a stabilizer for steady lines, a
///   pressure response curve, and tilt that broadens the line.
///
/// With a pen the stroke is built from the pen's own readings, position,
/// pressure and tilt together, filtered and evenly spaced, and the tip is
/// swept between them, so the edge stays clean however the readings jitter.
/// The preview is the stroke that will land.
///
/// The brush modes decide where the stroke lands: everywhere (Normal), under
/// the lines (Fills), only on empty stage (Behind), only within the selected
/// fills (Selection) or only within the fill it began in (Inside). The last
/// three cut the stroke down to that area before it is placed.
class BrushTool : public Tool
{
public:
    enum class Kind
    {
        Fluid,
        Classic,
        Ink
    };

    BrushTool(Kind kind, fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _kind(kind)
        , _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override;

    Kind kind() const { return _kind; }

    QCursor cursor() const override { return Qt::BlankCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    void finish(PhoenixView& view);

    /// The bitmap brush: paints \a dabs into the frame's paint canvas,
    /// making one if the frame has none, growing it to take the stroke.
    void paintBitmap(PhoenixView& view, fla::Frame* frame, const std::vector<fla::BrushDab>& dabs);

    /// The settings of this brush.
    const fla::BrushSettings& settings() const;

    /// The settings as a stroke on \a view uses them: with the size taken
    /// down under a camera zoom when the brush keeps its size on screen.
    fla::BrushSettings effective(PhoenixView& view) const;

    /// Keeps a sample to the ruler guide the stroke began on, if any.
    void snapToGuide(fla::BrushSample& sample) const;

    /// With Auto Fill on, the loop a stroke closes, as its dabs' centers,
    /// to be filled in; empty when it is off or the stroke stays open.
    std::vector<fla::Point> closedLoop(const std::vector<fla::BrushDab>& dabs, double size, double zoom) const;

    /// The texture the style gives a stroke.
    fla::texture::Spec textureSpec() const;

    /// The bitmap brush's tip: its coverage of \a color, 128 pixels
    /// square, from the soft round tip, a library bitmap or the tip's
    /// outline, with the tip texture on it.
    QImage bitmapTip(PhoenixView& view, const QColor& color, const fla::BrushDab& dab) const;

    /// Adds the pen's readings since the last, or, from a mouse, the
    /// cursor's position at full pressure.
    void takeSamples(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos);

    Kind _kind;
    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    std::vector<fla::BrushSample> _samples;
    /// With shift held, the stroke keeps to one direction: 1 across, 2 down.
    int _axis = 0;
    /// The ruler guide the stroke began on: 1 a vertical one at _guideAt,
    /// 2 a horizontal one, 0 none.
    int _guideAxis = 0;
    double _guideAt = 0.0;
    /// Whether the stroke under way comes from a pen.
    bool _pen = false;
    bool _drawing = false;
    QPointF _hover;
    bool _hoverValid = false;

    /// For Paint Inside: the shape and the face of its arrangement the
    /// stroke began in, held from the press to the release.
    fla::Shape* _insideShape = nullptr;
    int _insideFace = -1;
    std::unique_ptr<fla::RegionSplitter> _insideRegions;
};

/// Animate's eraser: takes artwork away along the drag, as wide as the
/// eraser size, from merge-drawn shapes. Its modes decide what goes: lines
/// and fills (Normal), fills only (Fills), lines only (Lines), the selected
/// fills only (Selected Fills) or the fill the pass began in (Inside). With
/// the faucet on, a click removes a whole fill or stroke instead.
class EraserTool : public Tool
{
public:
    EraserTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Eraser"; }

    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    void finish(PhoenixView& view);

    /// The faucet's click: the fill or the stroke under it goes.
    void faucet(PhoenixView& view, const QPointF& documentPos);

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    std::vector<fla::Point> _points;
    /// The pass as the pen gave it, with pressure and tilt.
    std::vector<fla::BrushSample> _samples;
    bool _pen = false;
    void takeSamples(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos);
    /// The eraser's nib, its own or the Nib Brush's.
    fla::BrushSettings nib() const;
    bool _erasing = false;
    QPointF _hover;
    bool _hoverValid = false;

    /// For Erase Inside: where the pass began, as for the brush.
    fla::Shape* _insideShape = nullptr;
    int _insideFace = -1;
    std::unique_ptr<fla::RegionSplitter> _insideRegions;
};
