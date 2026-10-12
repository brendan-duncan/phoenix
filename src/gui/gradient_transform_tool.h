#pragma once

#include "tool.h"

#include <QPointF>
#include <QSizeF>
#include <QTransform>

#include <memory>

namespace fla {
class CommandStack;
class FillStyle;
class Frame;
class Shape;
}

/// Animate's Gradient Transform tool (F), in the free transform tool's slot:
/// click a fill painted with a gradient or a bitmap and its handles show.
///
/// - The center circle moves the fill.
/// - The square on the edge scales it along its axis; a bitmap has a second
///   square for its height.
/// - A radial gradient's circle-with-arrow scales it evenly, and its
///   triangle moves the focal point along the axis.
/// - The round handle turns it.
///
/// Each drag is one undo step.
class GradientTransformTool : public Tool
{
public:
    explicit GradientTransformTool(fla::CommandStack& commandStack)
        : _commandStack(commandStack)
    {}

    QString name() const override { return "Gradient Transform"; }

    QCursor cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    bool showsFillHighlight() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    enum class Handle
    {
        None,
        Center,
        Width,
        Height,
        Size,
        Rotate,
        Focal,
        /// Shear the fill: across, from the top edge's middle, and down,
        /// from the left edge's.
        SkewX,
        SkewY
    };

    /// The fill being edited as a center and two half-axes in the shape's
    /// space, read from and written back to its matrix.
    struct Frame
    {
        QPointF center;
        QPointF axisX;
        QPointF axisY;
    };
    bool readFrame(Frame& frame) const;
    void writeFrame(const Frame& frame);
    QPointF handlePoint(Handle handle, const Frame& frame) const;
    Handle handleAt(PhoenixView& view, const QPointF& documentPos) const;

    /// The fill, found again by its index: an edit replaces the shape's
    /// fill styles.
    fla::FillStyle* fill() const;

    fla::CommandStack& _commandStack;
    fla::Shape* _shape = nullptr;
    /// The frame the shape is on, to check it is still there before use.
    const fla::Frame* _frame = nullptr;
    int _fillIndex = -1;
    /// A bitmap fill's bitmap size, in pixels.
    QSizeF _bitmapSize;
    /// The shape's own space to the space the tools work in.
    QTransform _toDocument;

    Handle _dragging = Handle::None;
    QPointF _pressLocal;
    Frame _pressFrame;
    double _pressFocal = 0.0;
    /// The fill's matrix and focal point when the drag began, put back
    /// before the edit is pushed so the command records both states.
    double _pressMatrix[6] = {1, 0, 0, 1, 0, 0};
};
