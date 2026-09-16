#pragma once

#include "tool.h"

#include <QPointF>
#include <QRectF>

namespace fla {
class CommandStack;
class DrawingStyle;
class Element;
class Frame;
class Selection;
}

/// The shape tools: rectangle, oval, line and polystar.
///
/// All four are the same gesture -- press, drag, release -- differing only in
/// what they build from the two corners, so they share one class rather than
/// four that would be nearly identical.
///
/// Rectangles and ovals become the primitive object types the format already
/// has. Lines and polystars become ordinary shapes with an edge, because the
/// format has no primitive for either.
class PrimitiveTool : public Tool
{
public:
    enum class Kind
    {
        Rectangle,
        Oval,
        Line,
        PolyStar
    };

    PrimitiveTool(Kind kind, fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _kind(kind)
        , _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override;

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    /// The rectangle the drag has swept out, normalised.
    QRectF dragRect() const;

    /// Snaps a drag position, unless the modifiers say otherwise.
    QPointF snap(PhoenixView& view, const QPointF& documentPos) const;

    /// Constrains a drag to a square, or a line to 45 degree steps, for shift.
    QPointF constrain(const QPointF& documentPos) const;

    /// Builds the finished object. Returns null when the drag was too small to
    /// be anything. The caller owns the result.
    fla::Element* createElement(fla::Frame* frame, const QPointF& start,
        const QPointF& end) const;

    fla::Element* createRectangle(fla::Frame* frame, const QRectF& rect) const;

    fla::Element* createOval(fla::Frame* frame, const QRectF& rect) const;

    fla::Element* createLine(fla::Frame* frame, const QPointF& start,
        const QPointF& end) const;

    fla::Element* createPolyStar(fla::Frame* frame, const QPointF& centre,
        const QPointF& edge) const;

    Kind _kind;
    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    bool _dragging = false;
    QPointF _start;
    QPointF _end;

    /// Whether the last move had shift held, so the preview and the finished
    /// object agree.
    bool _constrained = false;
};
