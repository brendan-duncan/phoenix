// The phone layout of the web editor (View > Phone Layout): the stage alone,
// two rows of large buttons along the bottom (the common tools with undo
// and redo; playing, and a menu holding the panels and the menu bar's
// menus), and the panels one at a time as a sheet over the bottom of the
// stage. Phones get it the first time; the menu switches it, and the choice
// is kept. Compiled only into the web build (CMakeLists.txt).

#include "main_window.h"
#include "timelines_panel.h"
#include "player.h"
#include "theme.h"
#include "web_page.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace {

constexpr int kButtonSize = 40;
constexpr const char* kSetting = "web/phoneLayout";

enum class PhoneIcon { Undo, Redo, Back, Play, Forward, Panels };

/// The bar's own pictures, in the theme's text color.
QIcon phoneIcon(PhoneIcon kind)
{
    const int size = 64;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = theme::isDark() ? QColor(225, 225, 225) : QColor(40, 40, 40);
    painter.setPen(QPen(ink, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const auto triangle = [&](bool right, double x) {
        QPainterPath path;
        if (right)
        {
            path.moveTo(x, 16);
            path.lineTo(x + 28, 32);
            path.lineTo(x, 48);
        }
        else
        {
            path.moveTo(x + 28, 16);
            path.lineTo(x, 32);
            path.lineTo(x + 28, 48);
        }
        path.closeSubpath();
        painter.setBrush(ink);
        painter.drawPath(path);
        painter.setBrush(Qt::NoBrush);
    };
    switch (kind)
    {
    case PhoneIcon::Undo:
    case PhoneIcon::Redo:
    {
        const bool redo = kind == PhoneIcon::Redo;
        QPainterPath arc;
        arc.moveTo(redo ? 16 : 48, 46);
        arc.cubicTo(redo ? 12 : 52, 22, redo ? 40 : 24, 14, redo ? 50 : 14, 26);
        painter.drawPath(arc);
        QPainterPath head;
        head.moveTo(redo ? 50 : 14, 12);
        head.lineTo(redo ? 50 : 14, 28);
        head.lineTo(redo ? 34 : 30, 28);
        painter.drawPath(head);
        break;
    }
    case PhoneIcon::Back:
        painter.drawLine(16, 16, 16, 48);
        triangle(false, 22);
        break;
    case PhoneIcon::Play:
        triangle(true, 20);
        break;
    case PhoneIcon::Forward:
        triangle(true, 14);
        painter.drawLine(48, 16, 48, 48);
        break;
    case PhoneIcon::Panels:
        for (int y : {18, 32, 46})
            painter.drawLine(14, y, 50, y);
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

} // namespace

bool MainWindow::wantsPhoneLayout()
{
    const QSettings settings;
    if (settings.contains(kSetting))
        return settings.value(kSetting).toBool();
    return web_page::isPhoneSized();
}

void MainWindow::setPhoneLayout(bool on)
{
    if (on == _phoneLayout || _secondary)
        return;
    if (on)
    {
        // The panels as they are now, to come back as they were.
        _phoneSavedWorkspace = currentWorkspace();
        _phoneLayout = true;
        for (QDockWidget* dock : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly))
            dock->hide();
        for (QToolBar* bar : findChildren<QToolBar*>(Qt::FindDirectChildrenOnly))
            if (!_phoneBars.contains(bar))
                bar->hide();
        if (_breadcrumbRow)
            _breadcrumbRow->hide();
        // Too wide for a phone: its menus are in the bar's menu.
        menuBar()->hide();
        statusBar()->hide();
        if (_phoneBars.isEmpty())
            buildPhoneBar();
        for (QToolBar* bar : _phoneBars)
            bar->show();
    }
    else
    {
        _phoneLayout = false;
        _phoneSheet = nullptr;
        for (QToolBar* bar : _phoneBars)
            bar->hide();
        applyWorkspace(_phoneSavedWorkspace);
        if (_breadcrumbRow)
            _breadcrumbRow->show();
        menuBar()->show();
        statusBar()->show();
    }
    if (_phoneLayoutAction && _phoneLayoutAction->isChecked() != on)
        _phoneLayoutAction->setChecked(on);
}

void MainWindow::buildPhoneBar()
{
    const auto row = [this](const char* name) {
        QToolBar* bar = new QToolBar(name, this);
        bar->setObjectName(name);
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setIconSize(QSize(kButtonSize - 12, kButtonSize - 12));
        bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        _phoneBars << bar;
        return bar;
    };
    // The tools on the row nearer the stage, playing under them.
    QToolBar* tools = row("phoneTools");
    addToolBar(Qt::BottomToolBarArea, tools);
    addToolBarBreak(Qt::BottomToolBarArea);
    QToolBar* playing = row("phonePlaying");
    addToolBar(Qt::BottomToolBarArea, playing);
    QToolBar* _phoneBar = tools;

    // The tools a phone draws with, as the toolbox has them.
    for (Tool* wanted : std::initializer_list<Tool*>{_selectionTool.get(), _brushTool.get(), _pencilTool.get(),
             _eraserTool.get(), _paintBucketTool.get(), _rectangleTool.get(), _ovalTool.get()})
    {
        for (const auto& [tool, action] : _toolActionList)
        {
            if (wanted && tool() == wanted)
            {
                _phoneBar->addAction(action);
                break;
            }
        }
    }
    _phoneBar->addSeparator();
    _phoneBar->addAction(phoneIcon(PhoneIcon::Undo), "Undo", _undoAction, &QAction::trigger);
    _phoneBar->addAction(phoneIcon(PhoneIcon::Redo), "Redo", _redoAction, &QAction::trigger);
    _phoneBar = playing;

    const auto timelines = [this]() { return findChild<TimelinesPanel*>(); };
    _phoneBar->addAction(phoneIcon(PhoneIcon::Back), "Step Back", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepBack();
    });
    _phoneBar->addAction(phoneIcon(PhoneIcon::Play), "Play", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->playStop();
    });
    _phoneBar->addAction(phoneIcon(PhoneIcon::Forward), "Step Forward", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepForward();
    });
    QLabel* frame = new QLabel(_phoneBar);
    frame->setMinimumWidth(36);
    frame->setAlignment(Qt::AlignCenter);
    const auto showFrame = [frame](int index) { frame->setText(QString::number(index + 1)); };
    showFrame(_player->currentFrame());
    connect(_player, &Player::currentFrameChanged, frame, showFrame);
    _phoneBar->addWidget(frame);

    // The panels, one at a time.
    QWidget* spacer = new QWidget(_phoneBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _phoneBar->addWidget(spacer);
    QToolButton* panels = new QToolButton(_phoneBar);
    panels->setIcon(phoneIcon(PhoneIcon::Panels));
    panels->setToolTip("Panels");
    panels->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(panels);
    for (const auto& [name, title] : std::initializer_list<std::pair<const char*, const char*>>{
             {"propertiesDock", "Properties"}, {"colorDock", "Color"}, {"swatchesDock", "Swatches"},
             {"libraryDock", "Library"}, {"timelineDock", "Timeline"}})
    {
        QDockWidget* dock = findChild<QDockWidget*>(name);
        if (!dock)
            continue;
        menu->addAction(title, this, [this, dock]() { showPhoneSheet(dock); });
    }
    // And the menu bar's menus, which the phone layout puts away.
    menu->addSeparator();
    for (QAction* top : menuBar()->actions())
        if (top->menu() && top->isVisible())
            menu->addAction(top);
    menu->addSeparator();
    menu->addAction("Full Layout", this, [this]() {
        QSettings().setValue(kSetting, false);
        setPhoneLayout(false);
    });
    panels->setMenu(menu);
    _phoneBar->addWidget(panels);

    // Large enough for a finger.
    for (QToolBar* bar : _phoneBars)
        for (QToolButton* button : bar->findChildren<QToolButton*>())
            button->setMinimumSize(kButtonSize, kButtonSize);
}

void MainWindow::showPhoneSheet(QDockWidget* dock)
{
    // One sheet at a time: the one up goes; asked for again, it closes.
    QDockWidget* previous = _phoneSheet;
    if (previous && previous != dock)
        previous->hide();
    if (previous == dock && dock->isVisible())
    {
        dock->hide();
        _phoneSheet = nullptr;
        return;
    }
    if (dock->isFloating())
        dock->setFloating(false);
    removeDockWidget(dock);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->show();
    dock->raise();
    resizeDocks({dock}, {std::max(160, height() * 2 / 5)}, Qt::Vertical);
    _phoneSheet = dock;
}
