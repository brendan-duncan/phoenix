#pragma once

#include "tutorial_lesson.h"

#include <QDialog>
#include <QPointer>
#include <QRect>
#include <QWidget>

#include <vector>

class MainWindow;
class QAction;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QTimer;

/// What a lesson step points at, found in the window as it is now.
///
/// Targets are written as text in a lesson's steps:
///
/// - tool:<name>: the toolbox button holding that tool ("tool:Oval")
/// - menu:<path>: a menu command ("menu:Insert/Timeline/Keyframe"); the
///   menu bar's title until the menu is open, then the item
/// - dock:<title>: a panel ("dock:Library"), shown and brought forward
/// - tab:<dock>/<tab>: a tab in a panel ("tab:Properties/Frame")
/// - label:<dock>/<text>: the row of a panel's field by its label
///   ("label:Properties/Tween")
/// - frame:<layer>:<n> or frame:<layer>:<n>-<m>: timeline cells, frames
///   counted from 1; an empty layer is the top row ("frame::24")
/// - ruler:<n>: the timeline ruler over frame n
/// - layer:<name>: a layer's row in the list beside the timeline
/// - stage, or stage:<x>,<y>,<w>,<h>: the stage, or a part of it in the
///   document's pixels
/// - widget:<objectName>: any control by its object name ("widget:stageZoom")
struct TutorialTarget
{
    QPointer<QWidget> widget;
    /// The part of the widget meant, in its coordinates.
    QRect rect;
    /// For a menu target: the command, and the menus down to it.
    QPointer<QAction> action;
    std::vector<QPointer<QMenu>> menus;
    std::vector<QPointer<QAction>> path;
    /// Why the target is not there, when it is not.
    QString problem;

    bool found() const { return widget && problem.isEmpty(); }
    /// Where it is on the screen now.
    QRect globalRect() const;
};

namespace tutorials {

/// Finds \a spec in \a window. With \a reveal, a hidden panel it names is
/// shown first.
TutorialTarget resolve(MainWindow& window, const QString& spec, bool reveal);

/// Finds the menu command \a path ("Insert/Timeline/Keyframe") in the menu
/// bar; null when there is none.
QAction* findMenuAction(MainWindow& window, const QString& path, std::vector<QPointer<QMenu>>* menus = nullptr,
    std::vector<QPointer<QAction>>* actions = nullptr);

/// Every built-in lesson's targets that cannot be found in \a window, as
/// "lesson, step n: target: problem"; empty when all are found.
QStringList checkLessons(MainWindow& window);

/// Opens a copy of \a lesson's final document in a tab of its own, to see
/// how the lesson is meant to turn out. False, with \a error saying why,
/// when it has none or it cannot be opened.
bool showFinal(MainWindow& window, const TutorialLesson& lesson, QString& error);

} // namespace tutorials

/// The pulsing ring round a lesson's target: a window of its own above
/// everything (menus included) that lets every click through.
class TutorialRing : public QWidget
{
public:
    TutorialRing();
    void pointAt(const QRect& global);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* _pulse = nullptr;
    int _phase = 0;
};

/// The light shade over the main window, with a hole where the target is.
/// Clicks pass through it.
class TutorialShade : public QWidget
{
public:
    explicit TutorialShade(QWidget* window);
    /// The hole, in the window's coordinates; none dims nothing.
    void setHole(const QRect& hole);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect _hole;
};

/// The small window that tells a lesson's step: its title and text, a
/// picture when it has one, how far through the lesson it is, and
/// Previous, Next and Close. It can be dragged anywhere and stays there.
class TutorialStepWindow : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialStepWindow(QWidget* window);

    void show(const QString& lesson, const TutorialStep& step, int index, int count, bool waits);
    /// A line under the text, for when the target cannot be found.
    void setProblem(const QString& problem);
    /// Whether Show Final is offered: the lesson has a final document.
    void setFinalShown(bool shown);
    /// Puts the window beside \a target (global), clear of it, unless it was
    /// dragged somewhere of the reader's choosing.
    void placeBeside(const QRect& target);

signals:
    void previous();
    void next();
    void closed();
    void finalAsked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* _lesson = nullptr;
    QLabel* _title = nullptr;
    QLabel* _text = nullptr;
    QLabel* _image = nullptr;
    QLabel* _problem = nullptr;
    QLabel* _count = nullptr;
    QPushButton* _previous = nullptr;
    QPushButton* _next = nullptr;
    QPushButton* _final = nullptr;
    QPoint _dragOffset;
    bool _dragging = false;
    bool _placedByHand = false;
};

/// Runs a lesson in the main window: opens its document, shows each step
/// and points at its target, and moves on when the reader does what the
/// step asks.
class TutorialRunner : public QObject
{
    Q_OBJECT

public:
    explicit TutorialRunner(MainWindow* window);
    ~TutorialRunner() override;

    /// Starts \a lesson on its own document (a new one, or a copy of its
    /// start), or with \a onShownDocument on the document already open:
    /// one a host page made from the lesson's start.
    bool start(const TutorialLesson& lesson, QString& error, bool onShownDocument = false);
    void stop();
    bool running() const { return _running; }

private:
    void showStep(int index);
    void next();
    void previous();
    /// Finds the step's target again and moves the ring and shade to it.
    void track();
    /// Moves on, a moment after the reader did what the step asked.
    void satisfied();
    void watch(const TutorialStep& step);
    void unwatch();
    void finish();

    MainWindow* _window;
    TutorialLesson _lesson;
    int _index = 0;
    bool _running = false;
    bool _advancing = false;
    const void* _document = nullptr;
    /// Where the history stood as each step began, to undo back to.
    std::vector<size_t> _stepStart;
    std::vector<QMetaObject::Connection> _watching;
    QTimer* _tracker = nullptr;
    TutorialRing* _ring = nullptr;
    TutorialShade* _shade = nullptr;
    TutorialStepWindow* _stepWindow = nullptr;
    QString _tempFolder;
};

/// Help > Tutorials: the lessons, each with its length and how far the
/// reader got, to start or start again.
class TutorialsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TutorialsDialog(QWidget* parent = nullptr);
    /// The lesson chosen, by id.
    QString chosen() const { return _chosen; }
    /// Whether its final was asked for rather than the lesson.
    bool finalChosen() const { return _finalChosen; }

private:
    /// Only the lessons with \a tag show; all of them for an empty one.
    void filterBy(const QString& tag);
    /// The lesson picked, its picture playing beside the list.
    void preview(QListWidgetItem* item);

    QListWidget* _list = nullptr;
    QLabel* _picture = nullptr;
    QLabel* _title = nullptr;
    QLabel* _summary = nullptr;
    class QMovie* _movie = nullptr;
    QList<class QToolButton*> _tagButtons;
    QString _chosen;
    bool _finalChosen = false;
};
