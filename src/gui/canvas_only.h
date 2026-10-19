#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>

class QAction;
class QMainWindow;
class QWidget;

/// Canvas Only (Tab): every panel and toolbar put away to leave the stage
/// alone in the window, and the same key brings them all back as they were,
/// sizes and places too. The layout is the window's own state, as a
/// workspace keeps it, so nothing about the panels is lost.
class CanvasOnly : public QObject
{
    Q_OBJECT

public:
    /// \a extras are other bars to put away with the panels (the edit bar);
    /// \a layoutVersion is the one the window saves its state with.
    CanvasOnly(QMainWindow* window, const QList<QWidget*>& extras, int layoutVersion);

    /// View > Canvas Only, checked while it is on.
    QAction* action() const { return _action; }
    bool isOn() const { return _on; }
    /// While it is on, the window's layout as it will come back.
    QByteArray savedState() const { return _state; }

    void enter();
    /// Brings everything back; nothing happens when it is off.
    void leave();

private:
    void toggle();

    QMainWindow* _window;
    QList<QPointer<QWidget>> _extras;
    int _layoutVersion;
    QAction* _action = nullptr;
    QByteArray _state;
    QList<QPointer<QWidget>> _hidden;
    bool _on = false;
};
