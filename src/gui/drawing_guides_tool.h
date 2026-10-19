#pragma once

#include "drawing_aids_view.h"
#include "tool.h"

namespace fla {
class DrawingStyle;
}

/// The Drawing Guides tool: places the guides freehand strokes snap to
/// (rulers, ellipses, perspective) and moves them and the symmetry axis.
///
/// A click on empty stage adds the kind of guide chosen in the tool's
/// options, a drag laying out its size; a drag on a guide moves it, on one
/// of its handles moves that point. Delete removes the guide picked. The
/// symmetry axis's center moves it and its square handle turns it.
///
/// Guides are settings of the document, like ruler guides: changing them
/// is not a step to undo.
class DrawingGuidesTool : public Tool
{
public:
    explicit DrawingGuidesTool(fla::DrawingStyle& style)
        : _style(style)
    {}

    QString name() const override { return "Drawing Guides"; }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// Moves what was taken hold of to \a stagePos.
    void drag(PhoenixView& view, const QPointF& stagePos, bool steps);

    fla::DrawingStyle& _style;
    drawaids::Handle _held;
    bool _dragging = false;
    /// A guide being laid out by the drag that adds it.
    bool _adding = false;
    QPointF _pressed;
    fla::aids::DrawingGuide _before;
};
