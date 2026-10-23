#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QWidget>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

class QDockWidget;
class QMouseEvent;
class QToolButton;

/// A panel over the stage along its bottom or right edge, for the Phone and
/// Artist layouts. It is a window of its own: on a page the stage's canvas
/// covers anything the main window draws over it.
class StageDrawer : public QWidget
{
public:
    /// \a size is how far it reaches over the stage (its height at the
    /// bottom, its width at the side); 0 for a share of the stage.
    StageDrawer(QWidget* window, Qt::Edge edge, int size = 0);

    /// Over \a stage, in screen coordinates.
    void place(const QRect& stage);

private:
    Qt::Edge _edge;
    int _size;
};

/// The layouts' drawers: each takes its dock's panel while the layout is on
/// and gives it back after. A drawer's button opens and closes it; a press
/// anywhere else in the main window closes it, and one on the stage does
/// nothing more, so closing a drawer never draws.
class StageDrawers : public QObject
{
public:
    StageDrawers(QWidget* window, QWidget* stage);

    /// \a button opens and closes the drawer holding \a dock's panel;
    /// \a size as StageDrawer takes it.
    void add(QDockWidget* dock, Qt::Edge edge, QToolButton* button, int size = 0);

    /// On, the panels move into their drawers (closed); off, they go back
    /// to their docks.
    void setActive(bool on);
    bool isActive() const { return _active; }

    /// Opens the drawer at \a index, closing the others, or closes it.
    void setOpen(size_t index, bool open);
    void closeAll() { setOpen(_drawers.size(), false); }

    /// Called when the stage moves or changes size, after the drawers.
    std::function<void()> stageMoved;
    /// Called after a drawer opens or closes, or the drawers go on or off.
    std::function<void()> changed;

    /// The open drawer at \a edge, in screen coordinates; empty when none is.
    QRect openDrawer(Qt::Edge edge) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Drawer
    {
        QPointer<QDockWidget> dock;
        QPointer<QWidget> panel;
        StageDrawer* drawer;
        QToolButton* button;
        Qt::Edge edge;
    };

    QRect stageOnScreen() const;

    QWidget* _window;
    QWidget* _stage;
    std::vector<Drawer> _drawers;
    bool _active = false;
    /// A press on the stage closed a drawer: the rest of it goes nowhere.
    bool _swallowing = false;
};

/// A press held still on the stage, which a touch layout gives the pop-up
/// palette as a right-click would. The press is held back until it moves,
/// is let go or is held long enough, so a held one never starts a stroke;
/// one that moves or is let go goes on to the stage as it was.
class StageHold : public QObject
{
public:
    StageHold(QWidget* stage, std::function<void(const QPoint& globalPos)> held);

    void setEnabled(bool on);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void replay();

    QWidget* _stage;
    std::function<void(const QPoint&)> _held;
    QTimer _timer;
    std::unique_ptr<QMouseEvent> _press;
    bool _on = false;
    bool _replaying = false;
    bool _swallowing = false;
};
