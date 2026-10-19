#pragma once

#include <QObject>
#include <QPointF>

#include <functional>

class PhoenixView;
class QGestureEvent;
class QNativeGestureEvent;

/// Touch gestures on the stage: two fingers pinch to zoom and twist to turn
/// the view (on a touch screen, or a trackpad that reports them), and a tap
/// of two fingers undoes. The mouse and the pen are left as they are: one
/// finger still works as the mouse does.
class StageGestures : public QObject
{
    Q_OBJECT

public:
    /// \a touchStarted is told as fingers come down and \a gesture when
    /// they turn out to be a pinch or a two-finger tap (\a tap), so whatever
    /// the first finger began as a mouse press can be taken back.
    StageGestures(PhoenixView* view, std::function<void()> touchStarted, std::function<void(bool tap)> gesture);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool gestureEvent(QGestureEvent* event);
    bool nativeGesture(QNativeGestureEvent* event);
    /// Zooms by \a factor and turns by \a degrees about \a widgetPos.
    void zoomAndTurn(double factor, double degrees, const QPointF& widgetPos);

    PhoenixView* _view;
    std::function<void()> _touchStarted;
    std::function<void(bool)> _gesture;
    /// A twist turns the view only once it is clearly meant, so a pinch to
    /// zoom does not tilt it too.
    double _twist = 0.0;
    bool _turning = false;
    bool _pinching = false;
};
