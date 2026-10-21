// The phone layout of the web editor (View > Phone Layout): the stage alone,
// two rows of large buttons along the bottom (the common tools with undo
// and redo; playing, looping, onion skin, a time slider and a menu holding
// the other panels and the menu bar's menus), the timeline as a drawer
// that comes up from a bar over them, Properties as one from a bar at the
// side, and the other panels one at a time as a sheet over the bottom of
// the stage. A press held still on the stage opens the pop-up palette, and
// a turned stage has a button to set it straight. Phones get it the first
// time; the menu switches it, and the choice is kept. Compiled only into
// the web build (CMakeLists.txt).

#include "main_window.h"
#include "phoenix_view.h"
#include "timeline_view.h"
#include "timelines_panel.h"
#include "player.h"
#include "theme.h"
#include "web_page.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTouchEvent>
#include <QVBoxLayout>

#include <memory>
#include <vector>

namespace {

constexpr int kButtonSize = 40;
constexpr const char* kSetting = "web/phoneLayout";
/// A press held this long on the stage, moving no farther than kHoldSlop,
/// opens the pop-up palette.
constexpr int kHoldTime = 500;
constexpr int kHoldSlop = 10;

enum class PhoneIcon { Undo, Redo, Back, Play, Stop, Forward, Panels, Loop, Onion, Straighten, Up, Left };

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
    case PhoneIcon::Stop:
        painter.setBrush(ink);
        painter.drawRect(QRectF(19, 19, 26, 26));
        break;
    case PhoneIcon::Forward:
        triangle(true, 14);
        painter.drawLine(48, 16, 48, 48);
        break;
    case PhoneIcon::Panels:
        for (int y : {18, 32, 46})
            painter.drawLine(14, y, 50, y);
        break;
    case PhoneIcon::Loop:
    case PhoneIcon::Straighten:
    {
        // Round the circle, ending in an arrowhead pointing on round.
        painter.drawArc(QRectF(13, 13, 38, 38), 70 * 16, 280 * 16);
        QPainterPath head;
        head.moveTo(50, 25);
        head.lineTo(43, 37);
        head.lineTo(57, 37);
        head.closeSubpath();
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        painter.drawPath(head);
        // Straighten: the stage square inside it.
        if (kind == PhoneIcon::Straighten)
        {
            painter.setPen(QPen(ink, 4));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(25, 25, 14, 14));
        }
        break;
    }
    case PhoneIcon::Onion:
    {
        // The frame shown, and fainter ones either side of it.
        QColor faint = ink;
        faint.setAlphaF(0.45);
        painter.setPen(QPen(faint, 5));
        painter.drawEllipse(QPointF(18, 32), 11, 11);
        painter.drawEllipse(QPointF(46, 32), 11, 11);
        painter.setPen(QPen(ink, 6));
        painter.drawEllipse(QPointF(32, 32), 12, 12);
        break;
    }
    case PhoneIcon::Up:
        painter.drawPolyline(QPolygonF({QPointF(18, 40), QPointF(32, 26), QPointF(46, 40)}));
        break;
    case PhoneIcon::Left:
        painter.drawPolyline(QPolygonF({QPointF(40, 18), QPointF(26, 32), QPointF(40, 46)}));
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

/// A panel over the stage along its bottom or right edge. It is a window of
/// its own: on a page the stage's canvas covers anything the main window
/// draws over it.
class PhoneDrawer : public QWidget
{
public:
    PhoneDrawer(QWidget* window, Qt::Edge edge)
        : QWidget(window, Qt::Tool | Qt::FramelessWindowHint)
        , _edge(edge)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAutoFillBackground(true);
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    /// Over \a stage, in screen coordinates: a little under half of it at
    /// the bottom, most of it at the side.
    void place(const QRect& stage)
    {
        if (_edge == Qt::BottomEdge)
        {
            const int height = std::min(stage.height(), std::max(200, stage.height() * 9 / 20));
            setGeometry(stage.left(), stage.bottom() + 1 - height, stage.width(), height);
        }
        else
        {
            const int width = std::min(stage.width(), std::clamp(stage.width() * 17 / 20, 240, 340));
            setGeometry(stage.right() + 1 - width, stage.top(), width, stage.height());
        }
    }

private:
    Qt::Edge _edge;
};

} // namespace

/// The phone layout's drawers: each takes its dock's panel while the
/// layout is on and gives it back after. A drawer's bar opens and closes
/// it; a press anywhere else in the main window closes it, and one on the
/// stage does nothing more, so closing a drawer never draws.
class PhoneDrawers : public QObject
{
public:
    PhoneDrawers(QWidget* window, QWidget* stage)
        : QObject(window)
        , _window(window)
        , _stage(stage)
    {
    }

    void add(QDockWidget* dock, Qt::Edge edge, QToolButton* bar)
    {
        if (!dock)
            return;
        const size_t index = _drawers.size();
        _drawers.push_back({dock, nullptr, new PhoneDrawer(_window, edge), bar});
        connect(bar, &QToolButton::clicked, this, [this, index]() { setOpen(index, !_drawers[index].drawer->isVisible()); });
    }

    void setActive(bool on)
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
                each.bar->setChecked(false);
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

    void setOpen(size_t index, bool open)
    {
        for (size_t i = 0; i < _drawers.size(); ++i)
        {
            PhoneDrawer* drawer = _drawers[i].drawer;
            if (i == index && open && _drawers[i].panel)
            {
                drawer->place(stageOnScreen());
                drawer->show();
                drawer->raise();
            }
            else
                drawer->hide();
            _drawers[i].bar->setChecked(drawer->isVisible());
        }
    }

    void closeAll() { setOpen(_drawers.size(), false); }

    /// Called when the stage moves or changes size, after the drawers.
    std::function<void()> stageMoved;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
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
            // Its bar closes it as a button.
            if (pressed == open->bar || open->bar->isAncestorOf(pressed))
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

private:
    struct Drawer
    {
        QPointer<QDockWidget> dock;
        QPointer<QWidget> panel;
        PhoneDrawer* drawer;
        QToolButton* bar;
    };

    QRect stageOnScreen() const { return QRect(_stage->mapToGlobal(QPoint(0, 0)), _stage->size()); }

    QWidget* _window;
    QWidget* _stage;
    std::vector<Drawer> _drawers;
    bool _active = false;
    /// A press on the stage closed a drawer: the rest of it goes nowhere.
    bool _swallowing = false;
};

/// A press held still on the stage, which the phone layout gives the pop-up
/// palette as a right-click would. The press is held back until it moves,
/// is let go or is held long enough, so a held one never starts a stroke;
/// one that moves or is let go goes on to the stage as it was.
class StageHold : public QObject
{
public:
    StageHold(QWidget* stage, std::function<void(const QPoint& globalPos)> held)
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

    void setEnabled(bool on)
    {
        _on = on;
        _timer.stop();
        _press.reset();
        _swallowing = false;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
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

private:
    void replay()
    {
        _timer.stop();
        const std::unique_ptr<QMouseEvent> press = std::move(_press);
        _replaying = true;
        QCoreApplication::sendEvent(_stage, press.get());
        _replaying = false;
    }

    QWidget* _stage;
    std::function<void(const QPoint&)> _held;
    QTimer _timer;
    std::unique_ptr<QMouseEvent> _press;
    bool _on = false;
    bool _replaying = false;
    bool _swallowing = false;
};

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
        _phoneDrawers->setActive(true);
        _stageHold->setEnabled(true);
        // The stage's least size is wider than a phone; kept, it pushes the
        // side bar and the ends of the rows off the screen. The page fits
        // the window to it again once that comes down (web_page.cpp).
        _phoenixView->setMinimumSize(100, 100);
        _phoneFitPending = true;
        _phoenixView->zoomToFit();
        _timelineView->setPhoneView(true);
        placePhoneStraighten();
    }
    else
    {
        _phoneLayout = false;
        _phoneSheet = nullptr;
        for (QToolBar* bar : _phoneBars)
            bar->hide();
        // The panels back in their docks before the docks come back.
        _phoneDrawers->setActive(false);
        _stageHold->setEnabled(false);
        _phoenixView->setMinimumSize(400, 300);
        _timelineView->setPhoneView(false);
        _phoneStraighten->hide();
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
        // Nothing round the buttons: a row of them only just fits a phone.
        bar->setStyleSheet("QToolBar { spacing: 0px; padding: 0px; }");
        bar->layout()->setContentsMargins(0, 0, 0, 0);
        _phoneBars << bar;
        return bar;
    };
    // The tools along the bottom, playing over them (and the timeline's
    // bar over that, below).
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
    QAction* play = _phoneBar->addAction(phoneIcon(PhoneIcon::Play), "Play", this, [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->playStop();
    });
    connect(_player, &Player::playingChanged, play, [play](bool playing) {
        play->setIcon(phoneIcon(playing ? PhoneIcon::Stop : PhoneIcon::Play));
        play->setText(playing ? "Stop" : "Play");
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

    // Looping and the onion skin, as the timeline's own buttons are.
    QAction* loop = _phoneBar->addAction(phoneIcon(PhoneIcon::Loop), "Loop Playback");
    loop->setCheckable(true);
    if (TimelinesPanel* panel = _timelineView->timelines())
    {
        loop->setChecked(panel->loopPlayback());
        connect(loop, &QAction::toggled, panel, &TimelinesPanel::setLoopPlayback);
        connect(panel, &TimelinesPanel::loopPlaybackChanged, loop, &QAction::setChecked);
    }
    // Its own action, so the View menu's gets no icon.
    QAction* onion = _phoneBar->addAction(phoneIcon(PhoneIcon::Onion), "Onion Skin");
    onion->setCheckable(true);
    onion->setChecked(_onionSkinAction->isChecked());
    connect(onion, &QAction::toggled, _onionSkinAction, &QAction::setChecked);
    connect(_onionSkinAction, &QAction::toggled, onion, &QAction::setChecked);

    // A slider through the timeline's frames, in the room left.
    QSlider* scrub = new QSlider(Qt::Horizontal, _phoneBar);
    scrub->setObjectName("phoneTimeSlider");
    scrub->setToolTip("Time");
    scrub->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    scrub->setMinimumSize(60, kButtonSize);
    scrub->setPageStep(1);
    // A handle large enough for a finger.
    scrub->setStyleSheet("QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: palette(mid); }"
                         "QSlider::handle:horizontal { width: 20px; margin: -8px 0; border-radius: 10px;"
                         " background: palette(highlight); }");
    // Its end follows the frames, measured again after each edit (a slider
    // with no range takes no presses) and as the frame shown changes.
    const auto showTime = [this, scrub]() {
        const QSignalBlocker blocker(scrub);
        const TimelinesPanel* panel = _timelineView->timelines();
        const int frames = panel ? panel->grid()->frameCount() : 1;
        scrub->setRange(0, std::max({1, frames, _player->currentFrame() + 1}) - 1);
        scrub->setValue(_player->currentFrame());
    };
    showTime();
    connect(_player, &Player::currentFrameChanged, scrub, showTime);
    connect(_timelineView, &TimelineView::currentLayerChanged, scrub, showTime);
    connect(this, &MainWindow::editStateChanged, scrub, showTime);
    connect(scrub, &QSlider::valueChanged, this, [this](int frame) {
        if (frame != _player->currentFrame())
            _player->setCurrentFrame(frame);
    });
    _phoneBar->addWidget(scrub);

    // The other panels, one at a time.
    QToolButton* panels = new QToolButton(_phoneBar);
    panels->setIcon(phoneIcon(PhoneIcon::Panels));
    panels->setToolTip("Panels");
    panels->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(panels);
    for (const auto& [name, title] : std::initializer_list<std::pair<const char*, const char*>>{
             {"colorDock", "Color"}, {"swatchesDock", "Swatches"}, {"libraryDock", "Library"}})
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

    _phoneDrawers = new PhoneDrawers(this, _phoenixView);

    // The timeline's bar, over the rows, naming the layer drawn on.
    addToolBarBreak(Qt::BottomToolBarArea);
    QToolBar* timelineBar = row("phoneTimeline");
    addToolBar(Qt::BottomToolBarArea, timelineBar);
    QToolButton* timeline = new QToolButton(timelineBar);
    timeline->setObjectName("phoneTimelineBar");
    timeline->setCheckable(true);
    timeline->setIcon(phoneIcon(PhoneIcon::Up));
    timeline->setIconSize(QSize(18, 18));
    timeline->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    timeline->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    timeline->setMinimumHeight(30);
    timeline->setToolTip("Timeline");
    timelineBar->addWidget(timeline);
    const auto showLayer = [this, timeline]() {
        fla::Timeline* shown = nullptr;
        int index = -1;
        QString text = "Timeline";
        if (_timelineView->currentLayer(shown, index) && shown && index >= 0 &&
            index < static_cast<int>(shown->layers.size()) && shown->layers[static_cast<size_t>(index)])
            text += QString::fromUtf8("  \u00b7  ") + QString::fromStdString(shown->layers[static_cast<size_t>(index)]->name);
        timeline->setText(text);
    };
    showLayer();
    connect(_timelineView, &TimelineView::currentLayerChanged, timeline, showLayer);
    _phoneDrawers->add(_timelineDock, Qt::BottomEdge, timeline);

    // Properties' bar, down the right of the stage.
    QToolBar* propertiesBar = row("phoneProperties");
    addToolBar(Qt::RightToolBarArea, propertiesBar);
    const auto stretch = [](QToolBar* bar) {
        QWidget* space = new QWidget(bar);
        space->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        bar->addWidget(space);
    };
    stretch(propertiesBar);
    QToolButton* properties = new QToolButton(propertiesBar);
    properties->setObjectName("phonePropertiesBar");
    properties->setCheckable(true);
    properties->setIcon(phoneIcon(PhoneIcon::Left));
    properties->setIconSize(QSize(18, 18));
    properties->setMinimumSize(26, 72);
    properties->setToolTip("Properties");
    propertiesBar->addWidget(properties);
    stretch(propertiesBar);
    _phoneDrawers->add(findChild<QDockWidget*>("propertiesDock"), Qt::RightEdge, properties);

    // A turned stage's way back, over its corner (a window, so it shows
    // over the stage's canvas).
    QToolButton* straighten = new QToolButton(this);
    straighten->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    straighten->setAttribute(Qt::WA_ShowWithoutActivating);
    straighten->setObjectName("phoneStraighten");
    straighten->setIcon(phoneIcon(PhoneIcon::Straighten));
    straighten->setIconSize(QSize(kButtonSize - 12, kButtonSize - 12));
    straighten->setFixedSize(kButtonSize, kButtonSize);
    straighten->setToolTip("Reset Rotation");
    connect(straighten, &QToolButton::clicked, this, [this]() { _phoenixView->setRotation(0.0); });
    _phoneStraighten = straighten;
    connect(_phoenixView, &PhoenixView::rotationChanged, this, [this]() { placePhoneStraighten(); });
    _phoneDrawers->stageMoved = [this]() {
        placePhoneStraighten();
        if (!_phoneFitPending)
            return;
        _phoenixView->zoomToFit();
        if (QScreen* screen = this->screen(); screen && geometry() == screen->geometry())
            _phoneFitPending = false;
    };

    _stageHold = new StageHold(_phoenixView, [this](const QPoint& at) { showPopupPalette(at); });
}

void MainWindow::placePhoneStraighten()
{
    if (!_phoneStraighten)
        return;
    if (!_phoneLayout || _phoenixView->rotation() == 0.0)
    {
        _phoneStraighten->hide();
        return;
    }
    const QPoint corner = _phoenixView->mapToGlobal(QPoint(_phoenixView->width(), 0));
    _phoneStraighten->move(corner + QPoint(-_phoneStraighten->width() - 8, 8));
    _phoneStraighten->show();
    _phoneStraighten->raise();
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
