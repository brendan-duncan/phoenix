#include "stage_drawers.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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
    _drawers.push_back({dock, nullptr, drawer, button});
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
