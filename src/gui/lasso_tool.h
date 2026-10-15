#pragma once

#include "tool.h"

#include <QPointF>
#include <QPolygonF>

namespace fla {
class Selection;
}

/// The lasso tools: a loop round what to select.
///
/// Selects every object the loop touches, and cuts merge-drawn artwork along
/// the loop to take just the fills and strokes inside it.
/// Shift adds to the selection.
///
/// - The **Lasso** draws the loop freehand, as the cursor is dragged.
/// - The **Polygon** tool draws it with straight sides: each click adds a
///   corner, and a double-click, Enter or a click on the first corner
///   closes it. Escape lets it go.
class LassoTool : public Tool
{
public:
    explicit LassoTool(fla::Selection& selection, bool polygon = false)
        : _selection(selection)
        , _polygon(polygon)
    {}

    QString name() const override { return _polygon ? "Polygon" : "Lasso"; }

    bool wantsEnter() const override { return _polygon && !_loop.isEmpty(); }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    /// Selects what the loop holds and lets it go.
    void close(PhoenixView& view);

    fla::Selection& _selection;
    bool _polygon = false;
    QPolygonF _loop;
    bool _drawing = false;
    bool _additive = false;
    /// The polygon's next corner, where the cursor is.
    QPointF _hover;
};
