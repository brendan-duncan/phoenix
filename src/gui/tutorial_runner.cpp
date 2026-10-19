#include "tutorial_runner.h"
#include "modal.h"

#include "behavior_library.h"

#include "main_window.h"
#include "phoenix_view.h"
#include "player.h"
#include "timeline_view.h"
#include "timelines_panel.h"
#include "toolbox.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

/// Whether Phoenix is the app in front. Windows is asked who owns the
/// foreground window, which nothing inside the app can make look so.
bool phoenixInFront()
{
#ifdef Q_OS_WIN
    DWORD owner = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &owner);
    return owner == GetCurrentProcessId();
#else
    return QGuiApplication::applicationState() == Qt::ApplicationActive;
#endif
}

/// A menu or label text as it reads: without its mnemonic, its trailing
/// colon and its "..." (which lessons may leave off).
QString plain(QString text)
{
    text.remove('&');
    text = text.trimmed();
    if (text.endsWith(':'))
        text.chop(1);
    if (text.endsWith("..."))
        text.chop(3);
    return text.trimmed();
}

QDockWidget* findDock(MainWindow& window, const QString& title)
{
    for (QDockWidget* dock : window.findChildren<QDockWidget*>())
    {
        if (plain(dock->windowTitle()).compare(plain(title), Qt::CaseInsensitive) == 0)
            return dock;
    }
    return nullptr;
}

/// The tab of a panel tabbed with others in the main window, where it is
/// not the one in front.
bool dockTab(MainWindow& window, QDockWidget* dock, TutorialTarget& target)
{
    for (QTabBar* bar : window.findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (!bar->isVisible())
            continue;
        for (int i = 0; i < bar->count(); ++i)
        {
            if (plain(bar->tabText(i)) == plain(dock->windowTitle()))
            {
                target.widget = bar;
                target.rect = bar->tabRect(i);
                return true;
            }
        }
    }
    return false;
}

/// The frames a "frame:" target names: "<layer>:<n>" or "<layer>:<n>-<m>".
bool frameRange(const QString& argument, QString& layer, int& first, int& last)
{
    const int colon = static_cast<int>(argument.lastIndexOf(':'));
    if (colon < 0)
        return false;
    layer = argument.left(colon);
    const QStringList numbers = argument.mid(colon + 1).split('-');
    bool ok = false;
    first = numbers.value(0).toInt(&ok);
    if (!ok || first < 1)
        return false;
    last = numbers.size() > 1 ? numbers.value(1).toInt(&ok) : first;
    return ok && last >= first;
}

/// Keys written <kbd>X</kbd> as key caps, in the window's colors.
QString withKeyCaps(QString text, const QPalette& palette)
{
    const QString cap = QString("<span style=\"background-color:%1; color:%2;\">&nbsp;")
                            .arg(palette.color(QPalette::Button).name(), palette.color(QPalette::ButtonText).name());
    text.replace("<kbd>", cap);
    text.replace("</kbd>", "&nbsp;</span>");
    return text;
}

/// Copies the folder (or file) \a from, in the app's resources or on disk,
/// to \a to.
bool copyTree(const QString& from, const QString& to)
{
    const QFileInfo info(from);
    if (info.isFile())
    {
        QDir().mkpath(QFileInfo(to).path());
        QFile::remove(to);
        return QFile::copy(from, to);
    }
    if (!info.isDir())
        return false;
    QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
    bool copied = false;
    while (files.hasNext())
    {
        const QString file = files.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(file));
        QDir().mkpath(QFileInfo(target).path());
        QFile::remove(target);
        if (!QFile::copy(file, target))
            return false;
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        copied = true;
    }
    return copied;
}

} // namespace

QRect TutorialTarget::globalRect() const
{
    if (!widget)
        return QRect();
    QRect shown = rect;
    // Only the part that can be seen, as of a frame scrolled half out of view.
    const QRect visible = widget->visibleRegion().boundingRect();
    if (!visible.isEmpty() && shown.intersects(visible))
        shown = shown.intersected(visible);
    return QRect(widget->mapToGlobal(shown.topLeft()), shown.size());
}

namespace tutorials {

QAction* findMenuAction(MainWindow& window, const QString& path, std::vector<QPointer<QMenu>>* menus,
    std::vector<QPointer<QAction>>* actions)
{
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QList<QAction*> level = window.menuBar()->actions();
    QAction* found = nullptr;
    for (int i = 0; i < parts.size(); ++i)
    {
        found = nullptr;
        for (QAction* action : level)
        {
            if (!action->isSeparator() && plain(action->text()).compare(plain(parts[i]), Qt::CaseInsensitive) == 0)
            {
                found = action;
                break;
            }
        }
        if (!found)
            return nullptr;
        if (actions)
            actions->push_back(found);
        if (i + 1 < parts.size())
        {
            QMenu* menu = found->menu();
            if (!menu)
                return nullptr;
            if (menus)
                menus->push_back(menu);
            // A menu filled as it opens is filled now, so its items are there.
            emit menu->aboutToShow();
            level = menu->actions();
        }
    }
    return found;
}

TutorialTarget resolve(MainWindow& window, const QString& spec, bool reveal)
{
    TutorialTarget target;
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty())
        return target;
    const int colon = static_cast<int>(trimmed.indexOf(':'));
    const QString kind = colon < 0 ? trimmed : trimmed.left(colon);
    const QString argument = colon < 0 ? QString() : trimmed.mid(colon + 1);

    if (kind == "tool")
    {
        Toolbox* toolbox = window.toolbox();
        if (!toolbox || !toolbox->tool(argument))
        {
            target.problem = QString("Phoenix has no tool called %1.").arg(argument);
            return target;
        }
        QToolButton* button = toolbox->buttonFor(argument);
        if (!button)
        {
            target.problem = QString("The %1 tool is not in the toolbox. Edit Toolbar in the toolbox's menu puts it back.")
                                 .arg(argument);
            return target;
        }
        if (!button->isVisible())
            target.problem = "Show the toolbox with Window > Tools.";
        target.widget = button;
        target.rect = button->rect();
        return target;
    }
    if (kind == "menu")
    {
        target.action = findMenuAction(window, argument, &target.menus, &target.path);
        if (!target.action || target.path.empty())
        {
            target.problem = QString("There is no %1 command.").arg(QString(argument).replace('/', " > "));
            return target;
        }
        // The deepest menu open on the way, or the menu bar's title.
        for (int i = static_cast<int>(target.menus.size()) - 1; i >= 0; --i)
        {
            QMenu* menu = target.menus[static_cast<size_t>(i)];
            QAction* item = target.path[static_cast<size_t>(i) + 1];
            if (menu && item && menu->isVisible())
            {
                target.widget = menu;
                target.rect = menu->actionGeometry(item);
                return target;
            }
        }
        target.widget = window.menuBar();
        target.rect = window.menuBar()->actionGeometry(target.path.front());
        return target;
    }
    if (kind == "dock" || kind == "tab" || kind == "label")
    {
        const QString dockTitle = kind == "dock" ? argument : argument.section('/', 0, 0);
        QDockWidget* dock = findDock(window, dockTitle);
        if (!dock)
        {
            target.problem = QString("There is no %1 panel.").arg(dockTitle);
            return target;
        }
        if (dock->isHidden())
        {
            if (!reveal)
            {
                target.problem = QString("Open the %1 panel from the Window menu.").arg(dockTitle);
                return target;
            }
            dock->show();
            dock->raise();
        }
        if (dock->visibleRegion().isEmpty() && dockTab(window, dock, target))
        {
            // Behind another panel: point at its tab, to bring it forward.
            return target;
        }
        if (kind == "dock")
        {
            target.widget = dock;
            target.rect = dock->rect();
            return target;
        }
        const QString name = plain(argument.section('/', 1));
        if (kind == "tab")
        {
            for (QTabBar* bar : dock->findChildren<QTabBar*>())
            {
                for (int i = 0; i < bar->count(); ++i)
                {
                    if (plain(bar->tabText(i)).compare(name, Qt::CaseInsensitive) == 0)
                    {
                        target.widget = bar;
                        target.rect = bar->tabRect(i);
                        return target;
                    }
                }
            }
            // Tabs drawn as buttons, as the Properties panel's are.
            for (QAbstractButton* button : dock->findChildren<QAbstractButton*>())
            {
                if (button->isVisible() && plain(button->text()).compare(name, Qt::CaseInsensitive) == 0)
                {
                    target.widget = button;
                    target.rect = button->rect();
                    return target;
                }
            }
            target.problem = QString("The %1 panel has no %2 tab.").arg(dockTitle, name);
            return target;
        }
        for (QLabel* label : dock->findChildren<QLabel*>())
        {
            if (!label->isVisible() || plain(label->text()).compare(name, Qt::CaseInsensitive) != 0)
                continue;
            // The label's row, across the panel.
            QWidget* row = label->parentWidget();
            QRect rect = label->geometry().adjusted(-4, -3, 0, 3);
            rect.setRight(row->width() - 4);
            target.widget = row;
            target.rect = rect;
            return target;
        }
        target.problem = QString("%1 is not showing in the %2 panel.").arg(name, dockTitle);
        return target;
    }
    if (kind == "layer")
    {
        // A layer's row in the list beside the timeline, by its name.
        TimelineView* timeline = window.timelineView();
        for (QTreeWidget* tree : timeline ? timeline->findChildren<QTreeWidget*>() : QList<QTreeWidget*>())
        {
            if (!tree->isVisible())
                continue;
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
            {
                for (int column = 0; column < tree->columnCount(); ++column)
                {
                    if ((*it)->text(column) == argument)
                    {
                        target.widget = tree->viewport();
                        target.rect = tree->visualItemRect(*it);
                        return target;
                    }
                }
            }
        }
        target.problem = timeline && timeline->isVisible() ? QString("There is no layer called %1.").arg(argument)
                                                           : QString("Open the timeline with Window > Timeline.");
        return target;
    }
    if (kind == "frame" || kind == "ruler")
    {
        TimelineView* timeline = window.timelineView();
        TimelineGrid* grid = timeline && timeline->timelines() ? timeline->timelines()->grid() : nullptr;
        if (!grid || !grid->isVisible())
        {
            target.problem = "Open the timeline with Window > Timeline.";
            return target;
        }
        QString layer;
        int first = 0;
        int last = 0;
        if (kind == "ruler")
        {
            bool ok = false;
            first = last = argument.toInt(&ok);
            if (!ok || first < 1)
            {
                target.problem = QString("%1 is not a frame number.").arg(argument);
                return target;
            }
        }
        else if (!frameRange(argument, layer, first, last))
        {
            target.problem = QString("%1 is not a layer and frames.").arg(argument);
            return target;
        }
        const QRect rect = grid->cellsRect(layer, first - 1, last - 1, kind == "ruler");
        if (rect.isEmpty())
        {
            target.problem = QString("There is no layer called %1.").arg(layer);
            return target;
        }
        target.widget = grid;
        target.rect = rect;
        return target;
    }
    if (kind == "stage")
    {
        PhoenixView* view = window.phoenixView();
        if (!view)
        {
            target.problem = "There is no stage.";
            return target;
        }
        target.widget = view;
        target.rect = view->rect();
        if (!argument.isEmpty())
        {
            const QStringList numbers = argument.split(',');
            if (numbers.size() != 4)
            {
                target.problem = QString("%1 is not x, y, width and height.").arg(argument);
                return target;
            }
            const double x = numbers[0].toDouble();
            const double y = numbers[1].toDouble();
            const QPointF a = view->mapToWidget(QPointF(x, y));
            const QPointF b = view->mapToWidget(QPointF(x + numbers[2].toDouble(), y + numbers[3].toDouble()));
            target.rect = QRectF(a, b).normalized().toAlignedRect().intersected(view->rect());
        }
        return target;
    }
    if (kind == "widget")
    {
        QWidget* widget = window.findChild<QWidget*>(argument);
        if (!widget)
        {
            target.problem = QString("There is no control called %1.").arg(argument);
            return target;
        }
        target.widget = widget;
        target.rect = widget->rect();
        if (!widget->isVisible())
            target.problem = "That control is hidden just now.";
        return target;
    }
    target.problem = QString("%1 is not a kind of target.").arg(kind);
    return target;
}

bool showFinal(MainWindow& window, const TutorialLesson& lesson, QString& error)
{
    if (lesson.finalDocument.isEmpty())
    {
        error = "this lesson has no final to show";
        return false;
    }
    // A copy, out of the app, in a folder of its own, as a lesson's start is.
    const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("phoenix-lessons");
    const QString folder = QDir(root).filePath(QString("%1-result-%2").arg(lesson.id).arg(QDateTime::currentMSecsSinceEpoch()));
    const QFileInfo source(lesson.finalDocument);
    const QString copy = QDir(folder).filePath(source.fileName());
    if (!copyTree(lesson.finalDocument, copy))
    {
        error = QString("its final %1 could not be copied").arg(lesson.finalDocument);
        return false;
    }
    // Behaviors of one's own the lesson writes live beside the result, in
    // Behaviors; they go where Phoenix finds them, never over one already
    // there.
    const QDir behaviors(QDir(lesson.finalDocument).filePath("Behaviors"));
    if (source.isDir() && behaviors.exists())
        for (const QFileInfo& script : behaviors.entryInfoList({"*.js"}, QDir::Files))
        {
            const QString target = QDir(BehaviorLibrary::folder()).filePath(script.fileName());
            if (!QFileInfo::exists(target) && QFile::copy(script.absoluteFilePath(), target))
                QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner);
        }
    window.openLessonFinal(source.isDir() ? QDir(copy).filePath("DOMDocument.xml") : copy, lesson.title + " (final)");
    return true;
}

QStringList checkLessons(MainWindow& window)
{
    QStringList problems;
    QStringList unreadable;
    const std::vector<TutorialLesson> lessons = allLessons(&unreadable);
    problems << unreadable;
    for (const TutorialLesson& lesson : lessons)
    {
        if (lesson.startKind == "document" && !QFileInfo::exists(lesson.startDocument))
            problems << QString("%1: its document %2 is missing").arg(lesson.id, lesson.startDocument);
        if (!lesson.finalDocument.isEmpty() && !QFileInfo::exists(lesson.finalDocument))
            problems << QString("%1: its final document %2 is missing").arg(lesson.id, lesson.finalDocument);
        for (size_t i = 0; i < lesson.steps.size(); ++i)
        {
            const TutorialStep& step = lesson.steps[i];
            const QString where = QString("%1, step %2").arg(lesson.id).arg(i + 1);
            const QString kind = step.target.section(':', 0, 0);
            // What depends on the lesson's own document as it will be by
            // then (its layers, the selection) is checked as it runs;
            // the controls must be there from the start.
            if (kind == "tool" || kind == "menu" || kind == "dock" || kind == "widget" || kind == "tab")
            {
                const TutorialTarget target = resolve(window, step.target, true);
                if (!target.widget || (!target.problem.isEmpty() && kind != "widget"))
                    problems << QString("%1: %2: %3").arg(where, step.target, target.problem);
            }
            else if (!step.target.isEmpty() && kind != "frame" && kind != "ruler" && kind != "stage" &&
                     kind != "label" && kind != "layer")
                problems << QString("%1: %2 is not a kind of target").arg(where, step.target);
            if (step.waitKind == "tool" && !(window.toolbox() && window.toolbox()->tool(step.waitValue)))
                problems << QString("%1: waits for a tool called %2, which there is not").arg(where, step.waitValue);
            if (step.waitKind == "action" && !findMenuAction(window, step.waitValue))
                problems << QString("%1: waits for %2, which is not a menu command").arg(where, step.waitValue);
            const QStringList kinds = {"", "tool", "command", "action", "frame", "selection", "scope", "playing"};
            if (!kinds.contains(step.waitKind))
                problems << QString("%1: %2 is not a kind of wait").arg(where, step.waitKind);
        }
    }
    return problems;
}

} // namespace tutorials

// The ring ---------------------------------------------------------------

TutorialRing::TutorialRing()
    : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                           Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    _pulse = new QTimer(this);
    _pulse->setInterval(40);
    connect(_pulse, &QTimer::timeout, this, [this]() {
        _phase = (_phase + 1) % 50;
        update();
    });
}

void TutorialRing::pointAt(const QRect& global)
{
    if (global.isEmpty())
    {
        hide();
        _pulse->stop();
        return;
    }
    const QRect area = global.adjusted(-10, -10, 10, 10);
    if (geometry() != area)
        setGeometry(area);
    if (!isVisible())
    {
        show();
        _pulse->start();
    }
}

void TutorialRing::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A ring that breathes in and out, wide and faint to narrow and strong.
    const double t = (1.0 + std::sin(_phase * 2.0 * 3.14159265358979 / 50.0)) / 2.0;
    const QColor accent(255, 140, 40);
    QColor glow = accent;
    glow.setAlphaF(0.25 + 0.25 * (1.0 - t));
    painter.setPen(QPen(glow, 6.0));
    painter.setBrush(Qt::NoBrush);
    const double inset = 3.0 + 3.0 * t;
    painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), 6, 6);
    painter.setPen(QPen(accent, 2.5));
    painter.drawRoundedRect(QRectF(rect()).adjusted(7, 7, -7, -7), 4, 4);
}

// The shade --------------------------------------------------------------

TutorialShade::TutorialShade(QWidget* window)
    : QWidget(window)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void TutorialShade::setHole(const QRect& hole)
{
    QWidget* window = parentWidget();
    if (hole.isEmpty() || !window)
    {
        hide();
        return;
    }
    if (geometry() != window->rect())
        setGeometry(window->rect());
    if (_hole != hole)
    {
        _hole = hole;
        update();
    }
    raise();
    show();
}

void TutorialShade::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath everything;
    everything.addRect(rect());
    QPainterPath hole;
    hole.addRoundedRect(QRectF(_hole.adjusted(-8, -8, 8, 8)), 6, 6);
    painter.fillPath(everything.subtracted(hole), QColor(0, 0, 0, 70));
}

// The step window --------------------------------------------------------

TutorialStepWindow::TutorialStepWindow(QWidget* window)
    : QWidget(window, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    QWidget* card = this;
    // Secondary text: the text color, faded, which reads on light and dark
    // themes alike.
    QColor faded = palette().color(QPalette::WindowText);
    faded.setAlpha(150);
    const QString fadedStyle = QString("color: rgba(%1, %2, %3, %4);")
                                   .arg(faded.red()).arg(faded.green()).arg(faded.blue()).arg(faded.alpha());

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 12);
    layout->setSpacing(8);

    QHBoxLayout* top = new QHBoxLayout;
    _lesson = new QLabel(card);
    _lesson->setStyleSheet(fadedStyle);
    top->addWidget(_lesson, 1);
    QToolButton* close = new QToolButton(card);
    close->setText(QString::fromUtf8("✕"));
    close->setAutoRaise(true);
    close->setToolTip("Close the lesson (Esc). Help > Tutorials starts it again.");
    connect(close, &QToolButton::clicked, this, &TutorialStepWindow::closed);
    top->addWidget(close);
    layout->addLayout(top);

    _title = new QLabel(card);
    QFont titleFont = _title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    _title->setFont(titleFont);
    _title->setWordWrap(true);
    layout->addWidget(_title);

    _text = new QLabel(card);
    _text->setWordWrap(true);
    _text->setTextFormat(Qt::RichText);
    _text->setOpenExternalLinks(true);
    _text->setMinimumWidth(300);
    _text->setMaximumWidth(360);
    layout->addWidget(_text);

    _image = new QLabel(card);
    _image->setAlignment(Qt::AlignCenter);
    _image->hide();
    layout->addWidget(_image);

    _problem = new QLabel(card);
    _problem->setWordWrap(true);
    _problem->setStyleSheet("color: rgb(255, 160, 60);");
    _problem->hide();
    layout->addWidget(_problem);

    QHBoxLayout* bottom = new QHBoxLayout;
    _count = new QLabel(card);
    _count->setStyleSheet(fadedStyle);
    bottom->addWidget(_count, 1);
    _previous = new QPushButton("Previous", card);
    _previous->setToolTip("Back a step, undoing what that step did");
    connect(_previous, &QPushButton::clicked, this, &TutorialStepWindow::previous);
    bottom->addWidget(_previous);
    _next = new QPushButton("Next", card);
    connect(_next, &QPushButton::clicked, this, &TutorialStepWindow::next);
    bottom->addWidget(_next);
    _final = new QPushButton("Show Final", card);
    _final->setToolTip("Open how the lesson turns out, in a tab of its own, to compare with yours");
    connect(_final, &QPushButton::clicked, this, &TutorialStepWindow::finalAsked);
    _final->hide();
    layout->addWidget(_final, 0, Qt::AlignLeft);
    layout->addLayout(bottom);

    const QVariant saved = QSettings().value("tutorials/stepWindow");
    if (saved.isValid())
    {
        move(saved.toPoint());
        _placedByHand = true;
    }
}

void TutorialStepWindow::show(const QString& lesson, const TutorialStep& step, int index, int count, bool waits)
{
    _lesson->setText(lesson);
    _title->setText(step.title);
    QString text = withKeyCaps(step.text, palette());
    if (waits)
    {
        QColor faded = palette().color(QPalette::WindowText);
        faded.setAlpha(150);
        text += QString("<p style=\"color:%1;\"><i>Do it in Phoenix and the lesson moves on, or click Next.</i></p>")
                    .arg(faded.name(QColor::HexArgb));
    }
    _text->setText(text);
    if (QMovie* old = _image->movie())
    {
        _image->setMovie(nullptr);
        old->deleteLater();
    }
    _image->clear();
    _image->hide();
    if (!step.image.isEmpty() && QFileInfo::exists(step.image))
    {
        const QString suffix = QFileInfo(step.image).suffix().toLower();
        if (suffix == "gif" || suffix == "webp")
        {
            QMovie* movie = new QMovie(step.image, QByteArray(), _image);
            movie->jumpToFrame(0);
            const QSize size = movie->currentImage().size();
            if (size.width() > 340)
                movie->setScaledSize(size.scaled(340, 1000, Qt::KeepAspectRatio));
            _image->setMovie(movie);
            movie->start();
        }
        else
        {
            const QPixmap picture(step.image);
            _image->setPixmap(picture.width() > 340 ? picture.scaledToWidth(340, Qt::SmoothTransformation) : picture);
        }
        _image->show();
    }
    _count->setText(QString("Step %1 of %2").arg(index + 1).arg(count));
    _previous->setEnabled(index > 0);
    _next->setText(index + 1 == count ? "Finish" : "Next");
    setProblem(QString());
    adjustSize();
    QWidget::show();
    raise();
}

void TutorialStepWindow::paintEvent(QPaintEvent*)
{
    // The window's own color, edged in the ring's orange so the two read
    // as one.
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.setPen(QPen(QColor(255, 140, 40), 2.0));
    painter.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
}

void TutorialStepWindow::setFinalShown(bool shown)
{
    _final->setVisible(shown);
    adjustSize();
}

void TutorialStepWindow::setProblem(const QString& problem)
{
    if (_problem->text() == problem && _problem->isVisible() == !problem.isEmpty())
        return;
    _problem->setText(problem);
    _problem->setVisible(!problem.isEmpty());
    adjustSize();
}

void TutorialStepWindow::placeBeside(const QRect& target)
{
    if (_placedByHand)
    {
        // Kept where it was put, as long as that is on a screen.
        if (QGuiApplication::screenAt(geometry().center()))
            return;
        _placedByHand = false;
    }
    adjustSize();
    const QSize size = frameGeometry().size();
    QWidget* window = parentWidget();
    const QRect area = window ? window->frameGeometry() : QRect();
    QScreen* screen = QGuiApplication::screenAt(target.isEmpty() ? area.center() : target.center());
    if (!screen && window)
        screen = window->screen();
    const QRect bounds = screen ? screen->availableGeometry() : area;

    if (target.isEmpty())
    {
        // Nothing to point at: the lower right of the window.
        move(QPoint(area.right() - size.width() - 40, area.bottom() - size.height() - 60));
        return;
    }
    // Already clear of it: stay, rather than jump about.
    if (isVisible() && !geometry().intersects(target.adjusted(-20, -20, 20, 20)) && bounds.contains(geometry()))
        return;
    const int gap = 28;
    const QList<QPoint> places = {
        QPoint(target.right() + gap, target.top()),
        QPoint(target.left() - gap - size.width(), target.top()),
        QPoint(target.left(), target.bottom() + gap),
        QPoint(target.left(), target.top() - gap - size.height()),
    };
    for (const QPoint& place : places)
    {
        QRect where(place, size);
        // Slid up or across to fit the screen, if that keeps it clear.
        if (where.bottom() > bounds.bottom())
            where.moveBottom(bounds.bottom());
        if (where.right() > bounds.right())
            where.moveRight(bounds.right());
        if (where.top() < bounds.top())
            where.moveTop(bounds.top());
        if (where.left() < bounds.left())
            where.moveLeft(bounds.left());
        if (!where.intersects(target.adjusted(-12, -12, 12, 12)))
        {
            move(where.topLeft());
            return;
        }
    }
    move(QPoint(bounds.right() - size.width() - 20, bounds.bottom() - size.height() - 20));
}

void TutorialStepWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        _dragging = true;
        _dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void TutorialStepWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (_dragging)
        move(event->globalPosition().toPoint() - _dragOffset);
    QWidget::mouseMoveEvent(event);
}

void TutorialStepWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (_dragging)
    {
        _dragging = false;
        _placedByHand = true;
        QSettings().setValue("tutorials/stepWindow", pos());
    }
    QWidget::mouseReleaseEvent(event);
}

void TutorialStepWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        emit closed();
        return;
    }
    QWidget::keyPressEvent(event);
}

// The runner -------------------------------------------------------------

TutorialRunner::TutorialRunner(MainWindow* window)
    : QObject(window)
    , _window(window)
{
    _tracker = new QTimer(this);
    _tracker->setInterval(150);
    connect(_tracker, &QTimer::timeout, this, &TutorialRunner::track);
    _ring = new TutorialRing;
    _shade = new TutorialShade(window);
    _stepWindow = new TutorialStepWindow(window);
    _stepWindow->hide();
    connect(_stepWindow, &TutorialStepWindow::next, this, &TutorialRunner::next);
    connect(_stepWindow, &TutorialStepWindow::previous, this, &TutorialRunner::previous);
    connect(_stepWindow, &TutorialStepWindow::finalAsked, this, [this]() {
        QString error;
        if (!tutorials::showFinal(*_window, _lesson, error))
            _stepWindow->setProblem(QString("The final could not be shown: %1").arg(error));
    });
    connect(_stepWindow, &TutorialStepWindow::closed, this, [this]() {
        stop();
        _window->statusBar()->showMessage("Lesson closed. Help > Tutorials starts it again.", 5000);
    });
}

TutorialRunner::~TutorialRunner()
{
    unwatch();
    delete _ring;
}

bool TutorialRunner::start(const TutorialLesson& lesson, QString& error)
{
    stop();
    _lesson = lesson;
    if (lesson.startKind == "new")
    {
        _window->newLessonDocument(lesson.startWidth, lesson.startHeight, lesson.startFps);
    }
    else if (lesson.startKind == "document")
    {
        // A copy to work on, out of the app (or the lesson folder), in a
        // folder of its own: the document may read its media from it later.
        const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("phoenix-lessons");
        const QDateTime old = QDateTime::currentDateTime().addDays(-2);
        for (const QFileInfo& stale : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (stale.lastModified() < old)
                QDir(stale.absoluteFilePath()).removeRecursively();
        }
        _tempFolder = QDir(root).filePath(QString("%1-%2").arg(lesson.id).arg(QDateTime::currentMSecsSinceEpoch()));
        const QFileInfo source(lesson.startDocument);
        const QString copy = QDir(_tempFolder).filePath(source.fileName());
        if (!copyTree(lesson.startDocument, copy))
        {
            error = QString("its document %1 could not be copied").arg(lesson.startDocument);
            return false;
        }
        _window->openLessonDocument(source.isDir() ? QDir(copy).filePath("DOMDocument.xml") : copy);
    }
    else if (!_window->shownDocument())
    {
        _window->newLessonDocument(1280, 720, 24.0);
    }
    _document = _window->shownDocument();
    if (!_document)
    {
        error = "its document could not be opened";
        return false;
    }
    _stepStart.assign(_lesson.steps.size(), 0);
    _stepWindow->setFinalShown(!_lesson.finalDocument.isEmpty());
    _running = true;
    _tracker->start();
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &TutorialRunner::track, Qt::UniqueConnection);
    showStep(0);
    return true;
}

void TutorialRunner::stop()
{
    unwatch();
    _running = false;
    _advancing = false;
    if (_tracker)
        _tracker->stop();
    if (_ring)
        _ring->pointAt(QRect());
    if (_shade)
        _shade->setHole(QRect());
    if (_stepWindow)
        _stepWindow->hide();
}

void TutorialRunner::showStep(int index)
{
    if (index < 0 || index >= static_cast<int>(_lesson.steps.size()))
        return;
    unwatch();
    _index = index;
    _advancing = false;
    _stepStart[static_cast<size_t>(index)] = _window->commandStack().index();
    const TutorialStep& step = _lesson.steps[static_cast<size_t>(index)];
    // A panel the step needs is opened for it.
    const TutorialTarget target = tutorials::resolve(*_window, step.target, true);
    _stepWindow->show(_lesson.title, step, index, static_cast<int>(_lesson.steps.size()), !step.waitKind.isEmpty());
    _stepWindow->placeBeside(target.found() ? target.globalRect() : QRect());
    tutorials::recordProgress(_lesson.id, index, false);
    watch(step);
    track();
}

void TutorialRunner::next()
{
    if (!_running)
        return;
    if (_index + 1 >= static_cast<int>(_lesson.steps.size()))
    {
        finish();
        return;
    }
    showStep(_index + 1);
}

void TutorialRunner::previous()
{
    if (!_running || _index == 0)
        return;
    // Back to where the step before began: what it and this one did, undone.
    const size_t back = _stepStart[static_cast<size_t>(_index - 1)];
    QAction* undo = _window->undoAction();
    for (int guard = 0; undo && _window->commandStack().index() > back && guard < 200; ++guard)
    {
        if (!undo->isEnabled())
            break;
        undo->trigger();
    }
    showStep(_index - 1);
}

void TutorialRunner::finish()
{
    tutorials::recordProgress(_lesson.id, static_cast<int>(_lesson.steps.size()) - 1, true);
    const QString title = _lesson.title;
    stop();
    modal::information(_window, "Tutorials",
        QString("You finished <b>%1</b>.<br><br>Help &gt; Tutorials has more lessons. The document you made "
                "stays open; save it if you want to keep it.")
            .arg(title.toHtmlEscaped()));
}

void TutorialRunner::track()
{
    if (!_running)
        return;
    // The lesson's document closed: the lesson ends with it.
    if (!_window->hasDocument(static_cast<const fla::FLADocument*>(_document)))
    {
        stop();
        _window->statusBar()->showMessage("The lesson ended because its document was closed.", 6000);
        return;
    }
    // The ring floats above every window, so it shows only while Phoenix is
    // the app in front.
    const bool active = phoenixInFront();
    // For screenshots of a lesson taken from outside: the shade shows
    // without Phoenix in front, the ring (which floats over everything) never.
    const bool capture = qEnvironmentVariableIsSet("PHOENIX_TUTORIAL_CAPTURE");
    if (_window->shownDocument() != _document)
    {
        _ring->pointAt(QRect());
        _shade->setHole(QRect());
        _stepWindow->setProblem("Go back to the lesson's tab to carry on.");
        return;
    }
    const TutorialStep& step = _lesson.steps[static_cast<size_t>(_index)];
    const TutorialTarget target = tutorials::resolve(*_window, step.target, false);
    _stepWindow->setProblem(target.problem);
    if (!target.found() || !(active || capture) || _window->isMinimized())
    {
        _ring->pointAt(QRect());
        _shade->setHole(QRect());
        return;
    }
    const QRect global = target.globalRect();
    _ring->pointAt(active ? global : QRect());
    // The shade dims the main window round a target inside it; a target in
    // an open menu or a floating panel is left undimmed.
    const QRect local(_window->mapFromGlobal(global.topLeft()), global.size());
    _shade->setHole(_window->rect().contains(local.center()) && !qobject_cast<QMenu*>(target.widget.data())
                        ? local
                        : QRect());
}

void TutorialRunner::satisfied()
{
    if (_advancing || !_running)
        return;
    _advancing = true;
    const int at = _index;
    // A moment to see what was done before the next step.
    QTimer::singleShot(650, this, [this, at]() {
        if (_running && _index == at)
            next();
        _advancing = false;
    });
}

void TutorialRunner::watch(const TutorialStep& step)
{
    const QString kind = step.waitKind;
    const QString value = step.waitValue;
    PhoenixView* view = _window->phoenixView();
    Player* player = view ? view->player() : nullptr;
    if (kind == "tool" && view)
    {
        _watching.push_back(connect(view, &PhoenixView::activeToolChanged, this, [this, value](Tool* tool) {
            if (_window->toolName(tool).compare(value, Qt::CaseInsensitive) == 0)
                satisfied();
        }));
    }
    else if (kind == "command")
    {
        _watching.push_back(connect(_window, &MainWindow::editStateChanged, this, [this, value]() {
            const fla::CommandStack& stack = _window->commandStack();
            const size_t index = stack.index();
            if (index > _stepStart[static_cast<size_t>(_index)] &&
                QString::fromStdString(stack.nameAt(index - 1)).startsWith(value, Qt::CaseInsensitive))
                satisfied();
        }));
    }
    else if (kind == "action")
    {
        if (QAction* action = tutorials::findMenuAction(*_window, value))
            _watching.push_back(connect(action, &QAction::triggered, this, [this]() { satisfied(); }));
    }
    else if (kind == "frame" && player)
    {
        const int wanted = value.toInt();
        _watching.push_back(connect(player, &Player::currentFrameChanged, this, [this, wanted](int frame) {
            if (frame + 1 == wanted)
                satisfied();
        }));
    }
    else if (kind == "selection")
    {
        _watching.push_back(connect(_window, &MainWindow::selectionStateChanged, this, [this, value]() {
            const QString selected = _window->selectionKind();
            if (value == "any" ? selected != "none" : selected == value)
                satisfied();
        }));
    }
    else if (kind == "scope" && view)
    {
        _watching.push_back(connect(view, &PhoenixView::scopeChanged, this, [this, value, view]() {
            if ((value == "symbol") == (view->scopeDepth() > 0))
                satisfied();
        }));
    }
    else if (kind == "playing" && player)
    {
        _watching.push_back(connect(player, &Player::playingChanged, this, [this](bool playing) {
            if (playing)
                satisfied();
        }));
    }
}

void TutorialRunner::unwatch()
{
    for (const QMetaObject::Connection& connection : _watching)
        disconnect(connection);
    _watching.clear();
}

// Help > Tutorials -------------------------------------------------------

TutorialsDialog::TutorialsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Tutorials");
    resize(860, 560);
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* intro = new QLabel("Each lesson walks through a task in Phoenix. A small window tells you what to do, "
                               "and the control to use is circled. Lessons work on a new document of their own.",
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QStringList problems;
    const std::vector<TutorialLesson> lessons = tutorials::allLessons(&problems);

    // The tags to show only some of them by, in the order lessons first
    // use them: the basics first.
    QStringList tags;
    for (const TutorialLesson& lesson : lessons)
        for (const QString& tag : lesson.tags)
            if (!tags.contains(tag))
                tags << tag;
    if (!tags.isEmpty())
    {
        QWidget* bar = new QWidget(this);
        QGridLayout* grid = new QGridLayout(bar);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(4);
        QButtonGroup* group = new QButtonGroup(this);
        group->setExclusive(true);
        QStringList choices = {QString()};
        choices << tags;
        const int across = 7;
        for (int i = 0; i < choices.size(); ++i)
        {
            QToolButton* button = new QToolButton(bar);
            button->setText(choices[i].isEmpty() ? QString("All") : choices[i]);
            button->setCheckable(true);
            button->setAutoRaise(true);
            button->setProperty("tag", choices[i]);
            button->setToolTip(choices[i].isEmpty() ? QString("Every lesson") : QString("Only the lessons about %1").arg(choices[i]));
            group->addButton(button);
            grid->addWidget(button, i / across, i % across);
            _tagButtons << button;
            connect(button, &QToolButton::clicked, this, [this, tag = choices[i]]() { filterBy(tag); });
        }
        grid->setColumnStretch(across, 1);
        layout->addWidget(bar);
    }

    QHBoxLayout* middle = new QHBoxLayout();
    _list = new QListWidget(this);
    _list->setIconSize(QSize(128, 72));
    _list->setSpacing(2);
    middle->addWidget(_list, 3);
    // The lesson picked: its picture playing, its name and what it does.
    QVBoxLayout* side = new QVBoxLayout();
    _picture = new QLabel(this);
    _picture->setFixedSize(320, 180);
    _picture->setAlignment(Qt::AlignCenter);
    _picture->setStyleSheet("QLabel { background: palette(base); border: 1px solid palette(mid); }");
    side->addWidget(_picture);
    _title = new QLabel(this);
    _title->setWordWrap(true);
    QFont bold = _title->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.15);
    _title->setFont(bold);
    side->addWidget(_title);
    _summary = new QLabel(this);
    _summary->setWordWrap(true);
    _summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _summary->setFixedWidth(320);
    side->addWidget(_summary);
    side->addStretch(1);
    middle->addLayout(side, 0);
    layout->addLayout(middle, 1);
    for (const TutorialLesson& lesson : lessons)
    {
        const bool done = tutorials::finished(lesson.id);
        const int reached = tutorials::stepReached(lesson.id);
        QString status = QString("%1 min").arg(lesson.minutes);
        if (done)
            status += QString::fromUtf8(" • finished");
        else if (reached > 0)
            status += QString::fromUtf8(" • reached step %1 of %2")
                          .arg(std::min<int>(reached + 1, static_cast<int>(lesson.steps.size())))
                          .arg(lesson.steps.size());
        if (!lesson.tags.isEmpty())
            status += QString::fromUtf8(" • ") + lesson.tags.join(", ");
        QListWidgetItem* item = new QListWidgetItem(QString("%1\n%2").arg(lesson.title, status), _list);
        item->setData(Qt::UserRole, lesson.id);
        item->setData(Qt::UserRole + 1, lesson.summary);
        item->setData(Qt::UserRole + 2, !lesson.finalDocument.isEmpty());
        item->setData(Qt::UserRole + 3, lesson.tags);
        item->setData(Qt::UserRole + 4, lesson.thumbnail);
        item->setData(Qt::UserRole + 5, lesson.title);
        // In the list, still: its middle frame, which shows more than the
        // first (a line not yet drawn on); the one picked plays.
        if (!lesson.thumbnail.isEmpty())
        {
            // Read through to it: a GIF's frames build on the ones before.
            QImageReader reader(lesson.thumbnail);
            const int middle = std::max(0, reader.imageCount() / 2);
            QImage still = reader.read();
            for (int frame = 1; frame <= middle && reader.canRead(); ++frame)
            {
                const QImage next = reader.read();
                if (next.isNull())
                    break;
                still = next;
            }
            if (!still.isNull())
                item->setIcon(QIcon(QPixmap::fromImage(still)));
        }
    }

    QLabel* own = new QLabel(QString("<small>Lessons of your own go in <a href=\"%1\">%2</a>, a folder each with its "
                                     "lesson.json.</small>")
                                 .arg(QUrl::fromLocalFile(tutorials::userFolder()).toString(),
                                     QDir::toNativeSeparators(tutorials::userFolder()).toHtmlEscaped()),
        this);
    own->setWordWrap(true);
    connect(own, &QLabel::linkActivated, this, [](const QString& link) {
        QDir().mkpath(QUrl(link).toLocalFile());
        QDesktopServices::openUrl(QUrl(link));
    });
    layout->addWidget(own);
    if (!problems.isEmpty())
    {
        QLabel* broken = new QLabel("<small>Not shown, as they could not be read: " +
                                        problems.join("; ").toHtmlEscaped() + "</small>",
            this);
        broken->setWordWrap(true);
        layout->addWidget(broken);
    }

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* start = buttons->addButton("Start", QDialogButtonBox::AcceptRole);
    start->setEnabled(false);
    QPushButton* result = buttons->addButton("Show Final", QDialogButtonBox::ActionRole);
    result->setToolTip("Open how the lesson turns out, without starting it");
    result->setEnabled(false);
    connect(result, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* item = _list->currentItem())
        {
            _chosen = item->data(Qt::UserRole).toString();
            _finalChosen = true;
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (QListWidgetItem* item = _list->currentItem())
        {
            _chosen = item->data(Qt::UserRole).toString();
            accept();
        }
    });
    connect(_list, &QListWidget::currentItemChanged, this, [this, start, result](QListWidgetItem* item) {
        start->setEnabled(item != nullptr);
        result->setEnabled(item && item->data(Qt::UserRole + 2).toBool());
        preview(item);
        if (item)
            start->setText(tutorials::finished(item->data(Qt::UserRole).toString()) ? "Start Again" : "Start");
    });
    connect(_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        _chosen = item->data(Qt::UserRole).toString();
        accept();
    });
    layout->addWidget(buttons);
    if (_list->count() > 0)
        _list->setCurrentRow(0);
    // The tag chosen last time.
    const QString remembered = QSettings().value("tutorialsTag").toString();
    filterBy(tags.contains(remembered) ? remembered : QString());
}

void TutorialsDialog::filterBy(const QString& tag)
{
    for (QToolButton* button : _tagButtons)
        button->setChecked(button->property("tag").toString() == tag);
    QListWidgetItem* first = nullptr;
    for (int row = 0; row < _list->count(); ++row)
    {
        QListWidgetItem* item = _list->item(row);
        const bool shown = tag.isEmpty() || item->data(Qt::UserRole + 3).toStringList().contains(tag);
        item->setHidden(!shown);
        if (shown && !first)
            first = item;
    }
    QSettings().setValue("tutorialsTag", tag);
    // The one picked stays picked if it still shows.
    if (!_list->currentItem() || _list->currentItem()->isHidden())
        _list->setCurrentItem(first);
    if (!first)
        preview(nullptr);
}

void TutorialsDialog::preview(QListWidgetItem* item)
{
    if (_movie)
    {
        _movie->stop();
        _movie->deleteLater();
        _movie = nullptr;
    }
    _picture->clear();
    _title->setText(item ? item->data(Qt::UserRole + 5).toString() : QString());
    _summary->setText(item ? item->data(Qt::UserRole + 1).toString() : QString());
    const QString thumbnail = item ? item->data(Qt::UserRole + 4).toString() : QString();
    if (thumbnail.isEmpty())
        return;
    if (thumbnail.endsWith(".gif", Qt::CaseInsensitive))
    {
        _movie = new QMovie(thumbnail, QByteArray(), this);
        _movie->setScaledSize(_picture->size() - QSize(2, 2));
        _picture->setMovie(_movie);
        _movie->start();
    }
    else
        _picture->setPixmap(QPixmap(thumbnail).scaled(_picture->size() - QSize(2, 2), Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}
