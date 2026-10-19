#include "canvas_only.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

namespace {

/// Whether Tab belongs to \a widget, to move on to the next field, rather
/// than to Canvas Only.
bool takesTab(const QWidget* widget)
{
    if (!widget)
        return false;
    if (const QComboBox* combo = qobject_cast<const QComboBox*>(widget))
        return combo->isEditable();
    return qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget) ||
           qobject_cast<const QTextEdit*>(widget) || qobject_cast<const QPlainTextEdit*>(widget);
}

} // namespace

CanvasOnly::CanvasOnly(QMainWindow* window, const QList<QWidget*>& extras, int layoutVersion)
    : QObject(window)
    , _window(window)
    , _layoutVersion(layoutVersion)
{
    for (QWidget* extra : extras)
        _extras << extra;
    _action = new QAction("&Canvas Only", this);
    _action->setObjectName("viewCanvasOnly");
    _action->setCheckable(true);
    _action->setShortcut(QKeySequence(Qt::Key_Tab));
    _action->setStatusTip("Put away every panel and toolbar to see only the stage; Tab again brings them back");
    connect(_action, &QAction::triggered, this, [this]() { toggle(); });
    _window->addAction(_action);
}

void CanvasOnly::toggle()
{
    // In a field, Tab moves on to the next field as it always has.
    QWidget* focus = QApplication::focusWidget();
    if (takesTab(focus) && !isOn())
    {
        const QSignalBlocker quiet(_action);
        _action->setChecked(false);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(focus, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(focus, &release);
        return;
    }
    if (isOn())
        leave();
    else
        enter();
}

void CanvasOnly::enter()
{
    if (isOn())
        return;
    // The window's whole layout, as a workspace keeps it, to put back
    // exactly.
    _state = _window->saveState(_layoutVersion);
    _on = true;
    QList<QWidget*> bars;
    for (QDockWidget* dock : _window->findChildren<QDockWidget*>())
        if (dock->parentWidget() == _window || dock->isFloating())
            bars << dock;
    for (QToolBar* toolbar : _window->findChildren<QToolBar*>())
        if (toolbar->parentWidget() == _window)
            bars << toolbar;
    for (const QPointer<QWidget>& extra : _extras)
        if (extra)
            bars << extra.data();
    for (QWidget* bar : bars)
    {
        if (bar->isVisible())
        {
            _hidden << bar;
            bar->hide();
        }
    }
    {
        const QSignalBlocker quiet(_action);
        _action->setChecked(true);
    }
    if (_window->statusBar())
        _window->statusBar()->showMessage("Canvas Only: press Tab to bring back the panels", 4000);
}

void CanvasOnly::leave()
{
    if (!isOn())
        return;
    for (const QPointer<QWidget>& bar : _hidden)
        if (bar)
            bar->show();
    _hidden.clear();
    _on = false;
    // Sizes and places as they were.
    if (!_state.isEmpty())
        _window->restoreState(_state, _layoutVersion);
    _state.clear();
    const QSignalBlocker quiet(_action);
    _action->setChecked(false);
}
