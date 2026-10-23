#include "stage_drawers.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QToolButton>
#include <QTouchEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/// A press held this long on the stage, moving no farther than kHoldSlop,
/// opens the pop-up palette.
constexpr int kHoldTime = 500;
constexpr int kHoldSlop = 10;

} // namespace

StageDrawer::StageDrawer(QWidget* window, Qt::Edge edge, int size)
    : QWidget(window, Qt::Tool | Qt::FramelessWindowHint)
    , _edge(edge)
    , _size(size)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAutoFillBackground(true);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
}

void StageDrawer::place(const QRect& stage)
{
    // A little under half of the stage at the bottom, most of it at the
    // side, unless the drawer asked for a size; never more than the stage.
    if (_edge == Qt::BottomEdge)
    {
        const int wanted = _size > 0 ? _size : std::max(200, stage.height() * 9 / 20);
        const int height = std::min(stage.height(), wanted);
        setGeometry(stage.left(), stage.bottom() + 1 - height, stage.width(), height);
    }
    else
    {
        const int wanted = _size > 0 ? _size : std::clamp(stage.width() * 17 / 20, 240, 340);
        const int width = std::min(stage.width(), wanted);
        setGeometry(stage.right() + 1 - width, stage.top(), width, stage.height());
    }
}

StageDrawers::StageDrawers(QWidget* window, QWidget* stage)
    : QObject(window)
    , _window(window)
    , _stage(stage)
{
}

void StageDrawers::add(QDockWidget* dock, Qt::Edge edge, QToolButton* button, int size)
{
    if (!dock || !button)
        return;
    const size_t index = _drawers.size();
    StageDrawer* drawer = new StageDrawer(_window, edge, size);
    // Named after its button, for a check that draws it.
    if (!button->objectName().isEmpty())
        drawer->setObjectName(button->objectName() + "Drawer");
    _drawers.push_back({dock, nullptr, drawer, button, edge});
    connect(button, &QToolButton::clicked, this, [this, index]() {
        setOpen(index, !_drawers[index].drawer->isVisible());
    });
}

void StageDrawers::setActive(bool on)
{
    if (on == _active)
        return;
    _active = on;
    for (Drawer& each : _drawers)
    {
        if (on)
        {
            each.panel = each.dock ? each.dock->widget() : nullptr;
            if (!each.panel)
                continue;
            each.dock->setWidget(nullptr);
            each.drawer->layout()->addWidget(each.panel);
            each.panel->show();
        }
        else
        {
            each.drawer->hide();
            each.button->setChecked(false);
            if (each.panel && each.dock)
                each.dock->setWidget(each.panel);
            each.panel = nullptr;
        }
    }
    _swallowing = false;
    if (on)
        qApp->installEventFilter(this);
    else
        qApp->removeEventFilter(this);
    if (changed)
        changed();
}

void StageDrawers::setOpen(size_t index, bool open)
{
    for (size_t i = 0; i < _drawers.size(); ++i)
    {
        StageDrawer* drawer = _drawers[i].drawer;
        if (i == index && open && _drawers[i].panel)
        {
            drawer->place(stageOnScreen());
            drawer->show();
            drawer->raise();
        }
        else
            drawer->hide();
        _drawers[i].button->setChecked(drawer->isVisible());
    }
    if (changed)
        changed();
}

QRect StageDrawers::openDrawer(Qt::Edge edge) const
{
    for (const Drawer& each : _drawers)
        if (each.edge == edge && each.drawer->isVisible())
            return each.drawer->geometry();
    return QRect();
}

QRect StageDrawers::stageOnScreen() const
{
    return QRect(_stage->mapToGlobal(QPoint(0, 0)), _stage->size());
}

bool StageDrawers::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::Resize:
    case QEvent::Move:
        if (watched == _stage || watched == _window)
        {
            for (const Drawer& each : _drawers)
                if (each.drawer->isVisible())
                    each.drawer->place(stageOnScreen());
            // The window's own comes before it has laid the stage out.
            if (stageMoved && watched == _stage)
                stageMoved();
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    {
        // A press can come as a double click alone (soon after a tap).
        if (_swallowing && event->type() == QEvent::MouseButtonDblClick)
        {
            if (watched == _stage)
                return true;
            break;
        }
        // The first widget a press is offered to is the one pressed.
        QWidget* pressed = qobject_cast<QWidget*>(watched);
        const Drawer* open = nullptr;
        for (const Drawer& each : _drawers)
            if (each.drawer->isVisible())
                open = &each;
        // In the drawer, a menu or a dialog: it stays.
        if (!pressed || !open || pressed->window() != _window)
            break;
        // Its button closes it as a button.
        if (pressed == open->button || open->button->isAncestorOf(pressed))
            break;
        closeAll();
        if (pressed == _stage || _stage->isAncestorOf(pressed))
        {
            _swallowing = true;
            return true;
        }
        break;
    }
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
        if (_swallowing && watched == _stage)
        {
            if (event->type() == QEvent::MouseButtonRelease)
                _swallowing = false;
            return true;
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

StageHold::StageHold(QWidget* stage, std::function<void(const QPoint&)> held)
    : QObject(stage)
    , _stage(stage)
    , _held(std::move(held))
{
    _timer.setSingleShot(true);
    _timer.setInterval(kHoldTime);
    connect(&_timer, &QTimer::timeout, this, [this]() {
        if (!_press)
            return;
        const QPoint at = _press->globalPosition().toPoint();
        _press.reset();
        _swallowing = true;
        if (_held)
            _held(at);
    });
    stage->installEventFilter(this);
}

void StageHold::setEnabled(bool on)
{
    _on = on;
    _timer.stop();
    _press.reset();
    _swallowing = false;
}

bool StageHold::eventFilter(QObject* watched, QEvent* event)
{
    if (!_on || _replaying || watched != _stage)
        return QObject::eventFilter(watched, event);
    switch (event->type())
    {
    case QEvent::MouseButtonPress:
    {
        QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
        _swallowing = false;
        if (mouse->button() != Qt::LeftButton)
            break;
        _press.reset(static_cast<QMouseEvent*>(mouse->clone()));
        _timer.start();
        return true;
    }
    case QEvent::MouseButtonDblClick:
        // A press Qt takes for the second of a double click (one soon
        // after a tap on a button, say) comes as this alone.
        if (!_press && !_swallowing && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
        {
            _press.reset(static_cast<QMouseEvent*>(static_cast<QMouseEvent*>(event)->clone()));
            _timer.start();
            return true;
        }
        [[fallthrough]];
    case QEvent::MouseButtonRelease:
        if (_swallowing)
        {
            if (event->type() == QEvent::MouseButtonRelease)
                _swallowing = false;
            return true;
        }
        if (_press)
            replay();
        break;
    case QEvent::MouseMove:
        if (_swallowing)
            return true;
        if (_press)
        {
            const QPointF moved = static_cast<QMouseEvent*>(event)->globalPosition() - _press->globalPosition();
            if (moved.manhattanLength() <= kHoldSlop)
                return true;
            replay();
        }
        break;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        // A second finger makes it a gesture: the press goes nowhere.
        if (static_cast<QTouchEvent*>(event)->points().size() >= 2 && _press)
        {
            _timer.stop();
            _press.reset();
            _swallowing = true;
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void StageHold::replay()
{
    _timer.stop();
    const std::unique_ptr<QMouseEvent> press = std::move(_press);
    _replaying = true;
    QCoreApplication::sendEvent(_stage, press.get());
    _replaying = false;
}
