#pragma once

#include "tool.h"

#include "../data/point.h"

#include <QPainterPath>
#include <QPointF>

#include <vector>

namespace fla {
class CommandStack;
class DrawingStyle;
class Frame;
class Selection;
class Shape;
}

/// The outline a round brush of \a width leaves along \a points, as a shape
/// filled with \a fill (which the shape takes ownership of). The outline is
/// resolved so it has no self-crossings, and each loop is written with the
/// fill on the correct side for the merge.
fla::Shape* brushOutlineShape(const std::vector<fla::Point>& points, double width,
    bool smooth, fla::Frame* parent, const fla::DrawingStyle& style, bool asFill);

/// Animate's brush: paints a fill along the drag, as wide as the brush size.
/// The stroke is a shape with a fill and no outline, and merges like any other
/// drawing, so passes of one colour run together without seams.
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
    bool _drawing = false;
    QPointF _hover;
    bool _hoverValid = false;
};

/// Animate's eraser in its normal mode: takes fills and strokes away along
/// the drag, as wide as the eraser size, from merge-drawn artwork.
class EraserTool : public Tool
{
public:
    EraserTool(fla::CommandStack& commandStack, const fla::DrawingStyle& style)
        : _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Eraser"; }

    QCursor cursor() const override { return Qt::BlankCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    void finish(PhoenixView& view);

    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    std::vector<fla::Point> _points;
    bool _erasing = false;
    QPointF _hover;
    bool _hoverValid = false;
};
