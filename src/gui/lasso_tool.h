#pragma once

#include "tool.h"

#include <QPointF>
#include <QPolygonF>

namespace fla {
class Selection;
}

/// Animate's lasso: draw a loop round what to select.
///
/// Selects every object the loop touches, and cuts merge-drawn artwork along
/// the loop to take just the fills and strokes inside it, as Animate does.
/// Shift adds to the selection.
class LassoTool : public Tool
{
public:
    explicit LassoTool(fla::Selection& selection)
        : _selection(selection)
    {}

    QString name() const override { return "Lasso"; }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    fla::Selection& _selection;
    QPolygonF _loop;
    bool _drawing = false;
    bool _additive = false;
};
