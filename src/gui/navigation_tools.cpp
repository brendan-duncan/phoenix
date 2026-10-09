#include "navigation_tools.h"

#include "cursors.h"
#include "phoenix_view.h"

#include <QKeyEvent>
#include <QMouseEvent>

namespace {

/// A drag shorter than this, in screen pixels, is a click.
constexpr double kClickThresholdPixels = 4.0;

} // namespace

QCursor ZoomTool::cursor() const
{
    return Cursors::cursor(_zoomOut ? Cursors::Kind::ZoomOut : Cursors::Kind::ZoomIn);
}

bool ZoomTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    _dragging = true;
    _start = documentPos;
    _end = documentPos;
    _zoomOut = (event->modifiers() & Qt::AltModifier) != 0;
    view.update();
    return true;
}

bool ZoomTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    const bool zoomOut = (event->modifiers() & Qt::AltModifier) != 0;
    if (zoomOut != _zoomOut)
    {
        _zoomOut = zoomOut;
        view.setCursor(cursor());
    }

    if (!_dragging)
        return false;

    _end = documentPos;
    view.update();
    return true;
}

bool ZoomTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_dragging)
        return false;

    _dragging = false;
    _end = documentPos;

    const QRectF rect = QRectF(_start, _end).normalized();
    const double threshold = kClickThresholdPixels / qMax(view.zoom(), 1.0e-6);

    if (rect.width() > threshold || rect.height() > threshold)
    {
        // A marquee fills the view with what it swept out. Alt does not turn
        // it round: a marquee is always a zoom in.
        view.zoomToRect(rect);
    }
    else
    {
        const double factor = _zoomOut ? 0.5 : 2.0;
        view.zoomTo(view.zoom() * factor, view.mapToWidget(documentPos));
    }

    view.update();
    return true;
}

bool ZoomTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || !_dragging)
        return false;

    _dragging = false;
    view.update();
    return true;
}

void ZoomTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_dragging)
        return;

    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(_start, _end).normalized());
}

void ZoomTool::deactivate(PhoenixView& view)
{
    if (!_dragging)
        return;

    _dragging = false;
    view.update();
}
