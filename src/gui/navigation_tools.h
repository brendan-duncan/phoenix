#pragma once

#include "tool.h"

#include <QPointF>
#include <QRectF>

/// Animate's hand tool: drag to pan the stage.
///
/// The view already pans whenever a tool declines a press, so the hand is the
/// tool that declines everything and just supplies the cursor.
class HandTool : public Tool
{
public:
    QString name() const override { return "Hand"; }

    QCursor cursor() const override { return Qt::OpenHandCursor; }
};

/// Animate's zoom tool: click to zoom in by a step about the point, alt-click
/// to zoom out, or drag a marquee to fill the view with it.
class ZoomTool : public Tool
{
public:
    QString name() const override { return "Zoom"; }

    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

private:
    bool _dragging = false;
    QPointF _start;
    QPointF _end;

    /// Whether alt is held, which turns the magnifier round.
    bool _zoomOut = false;
};
