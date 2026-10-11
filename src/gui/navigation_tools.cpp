#include "navigation_tools.h"

#include "cursors.h"
#include "phoenix_view.h"
#include "player.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

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

// ---------------------------------------------------------------- rotation

namespace {

double degreesAround(const QPointF& center, const QPointF& point)
{
    return std::atan2(point.y() - center.y(), point.x() - center.x()) * 180.0 / 3.14159265358979323846;
}

} // namespace

bool RotationTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    if (event->button() != Qt::LeftButton)
        return false;
    _pressed = true;
    _turning = false;
    _pressAt = event->position();
    _startAngle = degreesAround(view.stageToWidget(view.rotationPivot()), _pressAt);
    _startRotation = view.rotation();
    return true;
}

bool RotationTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    if (!_pressed)
        return false;
    const QPointF at = event->position();
    if (!_turning && std::hypot(at.x() - _pressAt.x(), at.y() - _pressAt.y()) < kClickThresholdPixels)
        return true;
    if (!_turning)
    {
        _turning = true;
        view.beginInteraction();
    }
    // Turned by however far the cursor has gone round the pivot.
    double degrees = _startRotation + degreesAround(view.stageToWidget(view.rotationPivot()), at) - _startAngle;
    if (event->modifiers() & Qt::ShiftModifier)
        degrees = std::round(degrees / 15.0) * 15.0;
    view.setRotation(degrees);
    return true;
}

bool RotationTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    if (event->button() != Qt::LeftButton || !_pressed)
        return false;
    _pressed = false;
    if (_turning)
    {
        _turning = false;
        view.endInteraction();
    }
    else
    {
        // A click puts the pivot where it landed.
        view.setRotationPivot(view.widgetToStage(event->position()));
    }
    view.update();
    return true;
}

bool RotationTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    view.setRotation(0.0);
    return true;
}

void RotationTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)scale;
    // The pivot, drawn in screen pixels whatever the zoom and turn.
    const QPointF pivot = view.stageToWidget(view.rotationPivot());
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255), 3.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(pivot, 6.0, 6.0);
    painter.drawLine(pivot - QPointF(10.0, 0.0), pivot + QPointF(10.0, 0.0));
    painter.drawLine(pivot - QPointF(0.0, 10.0), pivot + QPointF(0.0, 10.0));
    painter.setPen(QPen(QColor(41, 128, 238), 1.5));
    painter.drawEllipse(pivot, 6.0, 6.0);
    painter.drawLine(pivot - QPointF(10.0, 0.0), pivot + QPointF(10.0, 0.0));
    painter.drawLine(pivot - QPointF(0.0, 10.0), pivot + QPointF(0.0, 10.0));
    if (_turning || view.rotation() != 0.0)
    {
        // The angle beside the pivot.
        const QString text = QString::number(view.rotation(), 'f', 0) + QString::fromUtf8("°");
        QFont font = painter.font();
        font.setPointSizeF(9.0);
        painter.setFont(font);
        const QRectF box(pivot + QPointF(12.0, 8.0), QSizeF(48.0, 18.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(30, 30, 30, 200));
        painter.drawRoundedRect(box, 3.0, 3.0);
        painter.setPen(QColor(235, 235, 235));
        painter.drawText(box, Qt::AlignCenter, text);
    }
    painter.restore();
}

void RotationTool::deactivate(PhoenixView& view)
{
    if (_turning)
        view.endInteraction();
    _pressed = false;
    _turning = false;
    view.update();
}

// ---------------------------------------------------------------- time scrub

namespace {

/// How far the cursor goes for each frame, in screen pixels.
constexpr double kPixelsPerFrame = 8.0;

} // namespace

bool TimeScrubTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    if (event->button() != Qt::LeftButton || !view.player())
        return false;
    _scrubbing = true;
    _startX = event->position().x();
    _startFrame = view.player()->currentFrame();
    _cursor = event->position();
    view.update();
    return true;
}

bool TimeScrubTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    _cursor = event->position();
    if (!_scrubbing || !view.player())
        return false;
    const int last = std::max(0, view.frameCount() - 1);
    const int steps = static_cast<int>(std::lround((event->position().x() - _startX) / kPixelsPerFrame));
    const int frame = std::max(0, std::min(last, _startFrame + steps));
    if (frame != view.player()->currentFrame())
        view.player()->setCurrentFrame(frame);
    view.update();
    return true;
}

bool TimeScrubTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;
    if (event->button() != Qt::LeftButton || !_scrubbing)
        return false;
    _scrubbing = false;
    view.update();
    return true;
}

void TimeScrubTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)scale;
    if (!_scrubbing || !view.player())
        return;
    // The frame under the hand, numbered from one as the timeline does.
    painter.save();
    painter.resetTransform();
    const QString text = QString("Frame %1").arg(view.player()->currentFrame() + 1);
    QFont font = painter.font();
    font.setPointSizeF(9.0);
    painter.setFont(font);
    const QRectF box(_cursor + QPointF(14.0, -26.0), QSizeF(72.0, 18.0));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(41, 128, 238, 230));
    painter.drawRoundedRect(box, 3.0, 3.0);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
    painter.restore();
}

void TimeScrubTool::deactivate(PhoenixView& view)
{
    _scrubbing = false;
    view.update();
}
