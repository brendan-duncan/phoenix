// Dragging inside Phoenix in a browser.
//
// QDrag::exec() waits for the drop in an event loop of its own, which a
// browser page cannot run. The web build links with QDrag::exec wrapped
// (-Wl,--wrap in CMakeLists.txt), so every drag, Phoenix's own and those
// Qt's item views start, comes here instead: the drag follows the pointer
// as a picture, and the widgets under it get the drag enter, move, leave
// and drop events they would get from a real drag. exec() itself returns
// at once with no action, as for a drag canceled; what the drop does, the
// widget dropped on does, as it always has. Drags to and from other
// programs are not offered.

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QWidget>

#include <QtGui/private/qdnd_p.h>

namespace {

// QDropEvent::source() asks QDragManager for the drag going on, which only
// QDrag::exec() sets. Its member is private; this reaches it the way the
// language allows, through an explicit instantiation.
struct DragObject
{
    using type = QPointer<QDrag> QDragManager::*;
    friend type member(DragObject);
};

template <typename Tag, typename Tag::type Member>
struct Reach
{
    friend typename Tag::type member(Tag) { return Member; }
};

template struct Reach<DragObject, &QDragManager::m_object>;

void setCurrentDrag(QDrag* drag)
{
    if (QDragManager* manager = QDragManager::self())
        manager->*member(DragObject()) = drag;
}

/// The widget under \a global that takes drops, or nullptr.
QWidget* dropTargetAt(const QPoint& global)
{
    for (QWidget* widget = QApplication::widgetAt(global); widget; widget = widget->parentWidget())
    {
        if (widget->acceptDrops())
            return widget;
        if (widget->isWindow())
            break;
    }
    return nullptr;
}

class WebDrag : public QObject
{
public:
    WebDrag(QDrag* drag, Qt::DropActions actions, Qt::DropAction defaultAction)
        : _drag(drag)
        , _actions(actions)
        , _defaultAction(defaultAction)
    {
        setCurrentDrag(drag);
        if (!drag->pixmap().isNull())
        {
            _picture = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
            _picture->setAttribute(Qt::WA_TransparentForMouseEvents);
            _picture->setAttribute(Qt::WA_TranslucentBackground);
            _picture->setPixmap(drag->pixmap());
            _picture->resize(drag->pixmap().size() / drag->pixmap().devicePixelRatio());
        }
        qApp->installEventFilter(this);
        moveTo(QCursor::pos(), Qt::NoModifier);
    }

    ~WebDrag() override
    {
        qApp->removeEventFilter(this);
        delete _picture;
        setCurrentDrag(nullptr);
        if (_drag)
            _drag->deleteLater();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!watched->isWidgetType())
            return false;
        switch (event->type())
        {
        case QEvent::MouseMove:
        {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            moveTo(mouse->globalPosition().toPoint(), mouse->modifiers());
            return true;
        }
        case QEvent::MouseButtonRelease:
        {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            moveTo(mouse->globalPosition().toPoint(), mouse->modifiers());
            drop(mouse->modifiers());
            deleteLater();
            return true;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            return true;
        case QEvent::KeyPress:
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
            {
                leave();
                deleteLater();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

private:
    QMimeData* data() const { return _drag ? _drag->mimeData() : nullptr; }

    void moveTo(const QPoint& global, Qt::KeyboardModifiers modifiers)
    {
        if (_picture)
        {
            // Beside the pointer rather than under it, so it is never the
            // widget the drag is over.
            _picture->move(global + QPoint(12, 12));
            _picture->show();
        }
        if (!data())
            return;
        QWidget* target = dropTargetAt(global);
        if (target != _target)
        {
            leave();
            _target = target;
            if (_target)
            {
                QDragEnterEvent enter(_target->mapFromGlobal(global), _actions, data(), Qt::LeftButton, modifiers);
                enter.setDropAction(_defaultAction);
                QApplication::sendEvent(_target, &enter);
                _accepted = enter.isAccepted();
                _action = enter.dropAction();
            }
        }
        if (_target && _accepted)
        {
            QDragMoveEvent move(_target->mapFromGlobal(global), _actions, data(), Qt::LeftButton, modifiers);
            move.setDropAction(_action);
            move.accept();
            QApplication::sendEvent(_target, &move);
            _moveAccepted = move.isAccepted();
            _action = move.dropAction();
        }
        _at = global;
    }

    void leave()
    {
        if (_target && _accepted)
        {
            QDragLeaveEvent leave;
            QApplication::sendEvent(_target, &leave);
        }
        _target = nullptr;
        _accepted = false;
        _moveAccepted = false;
    }

    void drop(Qt::KeyboardModifiers modifiers)
    {
        if (!_target || !_accepted || !_moveAccepted || !data())
        {
            leave();
            return;
        }
        QDropEvent dropped(QPointF(_target->mapFromGlobal(_at)), _actions, data(), Qt::LeftButton, modifiers);
        dropped.setDropAction(_action);
        QApplication::sendEvent(_target, &dropped);
        _target = nullptr;
    }

    QPointer<QDrag> _drag;
    Qt::DropActions _actions;
    Qt::DropAction _defaultAction;
    QLabel* _picture = nullptr;
    QPointer<QWidget> _target;
    bool _accepted = false;
    bool _moveAccepted = false;
    Qt::DropAction _action = Qt::IgnoreAction;
    QPoint _at;
};

Qt::DropAction startDrag(QDrag* drag, Qt::DropActions actions, Qt::DropAction defaultAction)
{
    if (!drag || !drag->mimeData())
        return Qt::IgnoreAction;
    if (defaultAction == Qt::IgnoreAction || !(actions & defaultAction))
        defaultAction = (actions & Qt::MoveAction) ? Qt::MoveAction : (actions & Qt::CopyAction) ? Qt::CopyAction : Qt::LinkAction;
    new WebDrag(drag, actions, defaultAction);
    return Qt::IgnoreAction;
}

} // namespace

// QDrag::exec(Qt::DropActions) and QDrag::exec(Qt::DropActions, Qt::DropAction).
extern "C" Qt::DropAction __wrap__ZN5QDrag4execE6QFlagsIN2Qt10DropActionEE(QDrag* drag, Qt::DropActions actions)
{
    return startDrag(drag, actions, Qt::IgnoreAction);
}

extern "C" Qt::DropAction __wrap__ZN5QDrag4execE6QFlagsIN2Qt10DropActionEES2_(QDrag* drag, Qt::DropActions actions,
    Qt::DropAction defaultAction)
{
    return startDrag(drag, actions, defaultAction);
}
