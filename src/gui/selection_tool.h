#pragma once

#include "tool.h"

#include <QPointF>
#include <QRectF>

namespace fla {
class Selection;
}

/// Animate's arrow tool, in the part of its job that exists so far: picking
/// things on the stage.
///
/// Click selects the topmost object, shift-click adds or removes, dragging on
/// empty stage sweeps out a marquee, and clicking empty stage clears.
///
/// Moving, scaling and rotating the selection is not here yet; that is the free
/// transform work.
class SelectionTool : public Tool
{
public:
    explicit SelectionTool(fla::Selection& selection)
        : _selection(selection)
    {}

    QString name() const override { return "Selection"; }

    QCursor cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    /// The marquee so far, normalised so it is valid whichever way it was drawn.
    QRectF marqueeRect() const;

    fla::Selection& _selection;

    bool _marqueeActive = false;
    QPointF _marqueeStart;
    QPointF _marqueeEnd;

    /// Whether the press that started this gesture had shift held, which decides
    /// between replacing the selection and adding to it.
    bool _additive = false;
};
