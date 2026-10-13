#pragma once

#include "tool.h"

#include <functional>
#include <memory>

#include <QPointF>
#include <QTransform>

namespace fla {
class CommandStack;
class DrawingStyle;
class Selection;
class Shape;
}

/// Animate's paint bucket: click inside a region to paint it with the current
/// fill.
///
/// The region is a face of the shape's arrangement, so an area enclosed by
/// strokes but holding no fill takes paint too -- the classic use. Painting a
/// face the same color as its neighbor removes the seam between them, as
/// the merge model always does.
class PaintBucketTool : public Tool
{
public:
    PaintBucketTool(fla::CommandStack& commandStack, const fla::DrawingStyle& style)
        : _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Paint Bucket"; }

    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    /// A gradient is laid along a drag from the press: a linear one from
    /// where it began to where it ended, a radial one out to there.
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

private:
    /// Places the new fill: along the drag, over the stage with Lock Fill,
    /// or fitted to its art, then commits.
    void place(PhoenixView& view, const QPointF* dragEnd);

public:
    void deactivate(PhoenixView& view) override;

private:
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;
    /// The paint waiting for the release, for a gradient to be dragged.
    std::unique_ptr<fla::Shape> _pending;
    fla::Shape* _pendingTarget = nullptr;
    int _pendingFill = -1;
    QPointF _pressAt;
    QPointF _dragAt;
    QTransform _toShape;
};

/// Animate's ink bottle: click a fill to stroke its outline with the current
/// stroke, or click a stroke to restyle it.
class InkBottleTool : public Tool
{
public:
    InkBottleTool(fla::CommandStack& commandStack, const fla::DrawingStyle& style)
        : _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Ink Bottle"; }

    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

private:
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;
};

/// Animate's eyedropper: click a fill or a stroke to make it the current one.
/// Sampling a fill hands over to the paint bucket and sampling a stroke to the
/// ink bottle, as Animate does, through \a onSampled.
class EyedropperTool : public Tool
{
public:
    EyedropperTool(fla::DrawingStyle& style, std::function<void(bool stroke)> onSampled)
        : _style(style)
        , _onSampled(std::move(onSampled))
    {}

    QString name() const override { return "Eyedropper"; }

    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

private:
    fla::DrawingStyle& _style;
    std::function<void(bool stroke)> _onSampled;
};
