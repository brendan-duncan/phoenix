#include "stage_gestures.h"

#include "phoenix_view.h"

#include <QDateTime>
#include <QGesture>
#include <QGestureEvent>
#include <QGestureRecognizer>
#include <QLineF>
#include <QNativeGestureEvent>
#include <QTouchEvent>

#include <algorithm>
#include <cmath>

namespace {

/// How far a twist has to go, in degrees, before it turns the view.
constexpr double kTwistStart = 12.0;

/// Two fingers down and up again quickly, without moving: Qt has no such
/// gesture of its own.
class TwoFingerTapRecognizer : public QGestureRecognizer
{
public:
    QGesture* create(QObject* target) override
    {
        (void)target;
        return new QGesture;
    }

    Result recognize(QGesture* state, QObject* watched, QEvent* event) override
    {
        (void)watched;
        constexpr qint64 kLongest = 350;
        constexpr double kFarthest = 16.0;
        switch (event->type())
        {
        case QEvent::TouchBegin:
            state->setProperty("began", QDateTime::currentMSecsSinceEpoch());
            state->setProperty("fingers", static_cast<int>(static_cast<QTouchEvent*>(event)->points().size()));
            return MayBeGesture;
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        {
            const QTouchEvent* touch = static_cast<QTouchEvent*>(event);
            const int fingers = std::max(state->property("fingers").toInt(), static_cast<int>(touch->points().size()));
            state->setProperty("fingers", fingers);
            if (fingers > 2 || QDateTime::currentMSecsSinceEpoch() - state->property("began").toLongLong() > kLongest)
                return CancelGesture;
            for (const QEventPoint& point : touch->points())
                if (QLineF(point.pressPosition(), point.position()).length() > kFarthest)
                    return CancelGesture;
            if (event->type() == QEvent::TouchUpdate)
                return MayBeGesture;
            return fingers == 2 ? FinishGesture : CancelGesture;
        }
        default:
            break;
        }
        return Ignore;
    }

    void reset(QGesture* state) override
    {
        state->setProperty("began", QVariant());
        state->setProperty("fingers", QVariant());
        QGestureRecognizer::reset(state);
    }
};

Qt::GestureType twoFingerTap()
{
    static const Qt::GestureType type = QGestureRecognizer::registerRecognizer(new TwoFingerTapRecognizer);
    return type;
}

} // namespace

StageGestures::StageGestures(PhoenixView* view, std::function<void()> touchStarted, std::function<void(bool)> gesture)
    : QObject(view)
    , _view(view)
    , _touchStarted(std::move(touchStarted))
    , _gesture(std::move(gesture))
{
    // Touches the view does not take are still turned into mouse events by
    // Qt, so one finger works as it always has.
    _view->setAttribute(Qt::WA_AcceptTouchEvents, true);
    _view->grabGesture(Qt::PinchGesture);
    _view->grabGesture(twoFingerTap());
    _view->installEventFilter(this);
}

bool StageGestures::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _view)
    {
        switch (event->type())
        {
        case QEvent::TouchBegin:
            if (_touchStarted)
                _touchStarted();
            break;
        case QEvent::Gesture:
            return gestureEvent(static_cast<QGestureEvent*>(event));
        case QEvent::NativeGesture:
            return nativeGesture(static_cast<QNativeGestureEvent*>(event));
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool StageGestures::gestureEvent(QGestureEvent* event)
{
    if (QGesture* tap = event->gesture(twoFingerTap()))
    {
        event->accept(tap);
        if (tap->state() == Qt::GestureFinished && !_pinching && _gesture)
            _gesture(true);
    }
    if (QPinchGesture* pinch = static_cast<QPinchGesture*>(event->gesture(Qt::PinchGesture)))
    {
        event->accept(pinch);
        if (pinch->state() == Qt::GestureStarted)
        {
            _twist = 0.0;
            _turning = false;
            _pinching = true;
        }
        const QPointF center = _view->mapFromGlobal(pinch->centerPoint());
        double degrees = 0.0;
        if (pinch->changeFlags() & QPinchGesture::RotationAngleChanged)
        {
            const double step = pinch->rotationAngle() - pinch->lastRotationAngle();
            _twist += step;
            if (_turning)
                degrees = step;
            else if (std::fabs(_twist) >= kTwistStart)
            {
                _turning = true;
                degrees = _twist;
            }
        }
        const double factor = pinch->changeFlags() & QPinchGesture::ScaleFactorChanged ? pinch->scaleFactor() : 1.0;
        zoomAndTurn(factor, degrees, center);
        // Over: whatever the first finger drew as it came down is taken back.
        if (pinch->state() == Qt::GestureFinished || pinch->state() == Qt::GestureCanceled)
        {
            _pinching = false;
            if (_gesture)
                _gesture(false);
        }
    }
    return true;
}

bool StageGestures::nativeGesture(QNativeGestureEvent* event)
{
    // A trackpad's own pinch and twist, where the system reports them.
    switch (event->gestureType())
    {
    case Qt::ZoomNativeGesture:
        zoomAndTurn(1.0 + event->value(), 0.0, event->position());
        return true;
    case Qt::RotateNativeGesture:
        zoomAndTurn(1.0, event->value(), event->position());
        return true;
    case Qt::SmartZoomNativeGesture:
        _view->zoomToFit();
        return true;
    default:
        break;
    }
    return false;
}

void StageGestures::zoomAndTurn(double factor, double degrees, const QPointF& widgetPos)
{
    if (std::isfinite(degrees) && degrees != 0.0)
    {
        // About the fingers: the pivot moves there without moving the view.
        _view->setRotationPivot(_view->widgetToStage(widgetPos));
        _view->setRotation(_view->rotation() + degrees);
    }
    if (std::isfinite(factor) && factor > 0.0 && factor != 1.0)
        _view->zoomTo(_view->zoom() * factor, widgetPos);
}
