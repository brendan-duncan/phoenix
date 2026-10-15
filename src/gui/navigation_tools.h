#pragma once

#include "tool.h"

#include <QPointF>
#include <QRectF>

/// The hand tool: drag to pan the stage.
///
/// The view already pans whenever a tool declines a press, so the hand is the
/// tool that declines everything and just supplies the cursor.
class HandTool : public Tool
{
public:
    QString name() const override { return "Hand"; }

    QCursor cursor() const override { return Qt::OpenHandCursor; }
};

/// The Rotation tool (shift+H): turns the view of the stage, as a hand
/// turns paper to draw at a better angle. Click to set the pivot, marked
/// with a crosshair; drag to turn about it, with shift in 15 degree steps;
/// double-click to set the stage straight again. Nothing in the document
/// changes.
class RotationTool : public Tool
{
public:
    QString name() const override { return "Rotation"; }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    bool _pressed = false;
    bool _turning = false;
    QPointF _pressAt;
    double _startAngle = 0.0;
    double _startRotation = 0.0;
};

/// The Time Scrub tool (C): drag left or right across the stage to
/// move the playhead back or forward a frame for every few pixels, to see
/// the animation run under the hand.
class TimeScrubTool : public Tool
{
public:
    QString name() const override { return "Time Scrub"; }

    QCursor cursor() const override { return Qt::SizeHorCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    bool _scrubbing = false;
    double _startX = 0.0;
    int _startFrame = 0;
    QPointF _cursor;
};

/// The zoom tool: click to zoom in by a step about the point, alt-click
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
