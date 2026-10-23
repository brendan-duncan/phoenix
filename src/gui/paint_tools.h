#pragma once

#include "tool.h"

#include <functional>
#include <memory>
#include <vector>

#include <QPointF>
#include <QTransform>

namespace fla {
class CommandStack;
class DrawingStyle;
class Selection;
class Shape;
}

/// The paint bucket: click inside a region to paint it with the current
/// fill.
///
/// The region is a face of the shape's arrangement, so an area enclosed by
/// strokes but holding no fill takes paint too -- the classic use. Painting a
/// face the same color as its neighbor removes the seam between them, as
/// the merge model always does.
///
/// In Scribble mode a drag paints every region it passes over, and in Lasso
/// mode every region inside the loop it draws, each drag one step to undo.
/// With Fill Across set, a click paints the region on several drawings.
class PaintBucketTool : public Tool
{
public:
    PaintBucketTool(fla::CommandStack& commandStack, const fla::DrawingStyle& style);
    ~PaintBucketTool() override;

    QString name() const override { return "Paint Bucket"; }
    bool drawsOnStage() const override { return true; }

    /// Where Fill Across asks for the frames selected in the timeline:
    /// false when there are none.
    void setSelectedFrames(std::function<bool(int& first, int& last)> selectedFrames)
    {
        _selectedFrames = std::move(selectedFrames);
    }
    /// Where the bucket tells what a scribble or Fill Across did, for the
    /// status bar.
    void setReporter(std::function<void(const QString&)> report) { _report = std::move(report); }

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

    /// A click's paint, held for the release: the region under \a documentPos.
    void pressRegion(PhoenixView& view, const QPointF& documentPos);

    /// One shape a scribble or a loop paints, divided into its regions.
    struct Target;
    /// The shape's target, made the first time it is met. Null for a
    /// shape with nothing to divide.
    Target* targetFor(PhoenixView& view, fla::Shape* shape);
    /// The scribble reaches \a documentPos, taking the regions on its way.
    void scribbleTo(PhoenixView& view, const QPointF& documentPos);
    /// The loop reaches \a documentPos; the regions inside are taken anew.
    void lassoTo(PhoenixView& view, const QPointF& documentPos);
    /// Paints what the scribble or the loop took, as one step.
    void finishScribble(PhoenixView& view);
    void clearScribble();

    /// Fill Across: the region under \a documentPos on each drawing of the
    /// range, as one step. False when the bucket should paint as usual.
    bool fillAcrossFrames(PhoenixView& view, const QPointF& documentPos);

    void report(const QString& text) const
    {
        if (_report)
            _report(text);
    }

public:
    void deactivate(PhoenixView& view) override;

private:
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;
    std::function<bool(int&, int&)> _selectedFrames;
    std::function<void(const QString&)> _report;

    /// A scribble or loop in progress: its points on the stage, and the
    /// shapes it reaches.
    bool _scribbling = false;
    std::vector<QPointF> _scribble;
    std::vector<std::unique_ptr<Target>> _targets;
    /// On a color art layer, the one target: the paint under the lines.
    bool _colorArt = false;
    double _gap = 0.0;
    /// The paint waiting for the release, for a gradient to be dragged.
    std::unique_ptr<fla::Shape> _pending;
    fla::Shape* _pendingTarget = nullptr;
    int _pendingFill = -1;
    QPointF _pressAt;
    QPointF _dragAt;
    QTransform _toShape;
};

/// The ink bottle: click a fill to stroke its outline with the current
/// stroke, or click a stroke to restyle it.
class InkBottleTool : public Tool
{
public:
    InkBottleTool(fla::CommandStack& commandStack, const fla::DrawingStyle& style)
        : _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Ink Bottle"; }
    bool drawsOnStage() const override { return true; }

    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

private:
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;
};

/// The eyedropper: click a fill or a stroke to make it the current one.
/// Sampling a fill hands over to the paint bucket and sampling a stroke to the
/// ink bottle, through \a onSampled.
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
