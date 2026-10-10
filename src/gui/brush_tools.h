#pragma once

#include "tool.h"

#include "../data/point.h"
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
/// With \a pressures, one per point, the width follows the pen's pressure
/// along the stroke.
fla::Shape* brushOutlineShape(const std::vector<fla::Point>& points, double width,
    bool smooth, fla::Frame* parent, const fla::DrawingStyle& style, bool asFill,
    const std::vector<double>* pressures = nullptr);

/// Animate's brush: paints a fill along the drag, as wide as the brush size.
/// The stroke is a shape with a fill and no outline, and merges like any other
/// drawing, so passes of one color run together without seams.
///
/// The brush modes decide where the stroke lands: everywhere (Normal), under
/// the lines (Fills), only on empty stage (Behind), only within the selected
/// fills (Selection) or only within the fill it began in (Inside). The last
/// three cut the stroke down to that area before it is placed.
class BrushTool : public Tool
{
public:
    BrushTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Brush"; }

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

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    std::vector<fla::Point> _points;
    /// The pen pressure at each point, for a stroke that varies its width.
    std::vector<double> _pressures;
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
    bool _erasing = false;
    QPointF _hover;
    bool _hoverValid = false;

    /// For Erase Inside: where the pass began, as for the brush.
    fla::Shape* _insideShape = nullptr;
    int _insideFace = -1;
    std::unique_ptr<fla::RegionSplitter> _insideRegions;
};
