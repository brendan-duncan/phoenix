#pragma once

#include "tool.h"

#include "../edit/editable_path.h"

#include <QPointF>

namespace fla {
class CommandStack;
class DrawingStyle;
class Element;
class Frame;
class Selection;
}

/// Animate's pen tool: build a path a point at a time.
///
/// Click places a corner point. Press and drag pulls out handles, making a
/// smooth point whose curve runs through it. Holding alt while dragging breaks
/// the tangent, so the two sides can go their own ways. Clicking the first point
/// closes the path; Enter or a double click finishes it open; Escape abandons it.
///
/// Nothing reaches the document until the path is finished, so an abandoned path
/// leaves no trace and a finished one is a single undo step.
class PenTool : public Tool
{
public:
    PenTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Pen"; }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// Turns the path so far into a shape in the document. Does nothing when
    /// there is not enough to be worth keeping.
    void finish(PhoenixView& view, bool close);

    /// Throws the path away without touching the document.
    void cancel(PhoenixView& view);

    /// Builds the shape for the finished path. The caller owns the result.
    fla::Element* createShape(fla::Frame* frame) const;

    /// The path so far as something a painter can draw.
    QPainterPath previewPath(bool includeCursor) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    fla::EditablePath _path;

    /// True once the first point is down and the path is being built.
    bool _active = false;

    /// True between press and release, while handles are being pulled out of the
    /// anchor just placed.
    bool _draggingHandle = false;

    /// Where the cursor is, for the rubber band from the last anchor.
    QPointF _cursor;

    /// Whether the cursor is close enough to the first anchor that clicking
    /// would close the path, so the overlay can say so.
    bool _overFirstAnchor = false;
};
