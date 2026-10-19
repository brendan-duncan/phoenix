#include "timelines_panel.h"
#include <QTimer>
#include <QDragEnterEvent>
#include <QMimeData>
#include "library_panel.h"
#include "player.h"
#include "../edit/frame_actions.h"
#include "../edit/motion_object.h"
#include "../edit/timeline_edit.h"

namespace {

/// The ruler's height over the frames.
constexpr int kRulerHeight = 25;

} // namespace
#include <algorithm>
#include <cmath>
#include <map>

#include <QElapsedTimer>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QContextMenuEvent>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QWheelEvent>
#include <cstdlib>

#include "theme.h"

enum class TransportIcon { First, Prev, Play, Stop, Next };

/// Columns drawn past the last frame, room to add frames into.
static const int kSpareColumns = 10;

static QIcon onionIcon(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter icon(&pixmap);
    icon.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = theme::color("icon");
    icon.setPen(QPen(ink, 1.4));
    icon.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 60));
    icon.drawRoundedRect(QRectF(3.5, 5.5, 9, 11), 1.5, 1.5);
    icon.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 140));
    icon.drawRoundedRect(QRectF(9.5, 5.5, 9, 11), 1.5, 1.5);
    return QIcon(pixmap);
}

static QIcon loopIcon(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter icon(&pixmap);
    icon.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = theme::color("icon");
    icon.setPen(QPen(ink, 1.6));
    icon.setBrush(Qt::NoBrush);
    // Two arrows chasing each other round a loop.
    icon.drawArc(QRectF(4.5, 6.5, 13, 9), 30 * 16, 150 * 16);
    icon.drawArc(QRectF(4.5, 6.5, 13, 9), 210 * 16, 150 * 16);
    icon.setBrush(ink);
    icon.setPen(Qt::NoPen);
    icon.drawPolygon(QPolygonF({QPointF(17.5, 6.0), QPointF(17.5, 11.0), QPointF(13.5, 8.5)}));
    icon.drawPolygon(QPolygonF({QPointF(4.5, 16.0), QPointF(4.5, 11.0), QPointF(8.5, 13.5)}));
    return QIcon(pixmap);
}

static QIcon makeTransportIcon(TransportIcon icon, int size = 24)
{
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(0);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::color("icon"));

    const float pad = size * 0.2f;
    const float h = size - 2 * pad;
    const float w = h * 0.6f;
    const float cx = size / 2.0f;
    const float cy = size / 2.0f;
    const float barWidth = size * 0.12f;  // narrow vertical bar

    auto leftTriangle = [&](float centerX, float centerY) {
        QPolygonF tri;
        tri << QPointF(centerX + w/2, centerY - h/2)
            << QPointF(centerX + w/2, centerY + h/2)
            << QPointF(centerX - w/2, centerY);
        p.drawPolygon(tri);
    };
    auto rightTriangle = [&](float centerX, float centerY) {
        QPolygonF tri;
        tri << QPointF(centerX - w/2, centerY - h/2)
            << QPointF(centerX - w/2, centerY + h/2)
            << QPointF(centerX + w/2, centerY);
        p.drawPolygon(tri);
    };
    auto narrowRect = [&](float left, float top, float width, float height) {
        p.drawRoundedRect(QRectF(left, top, width, height), 1, 1);
    };

    switch (icon)
    {
    case TransportIcon::First:
        leftTriangle(cx - w - 0.5f, cy);
        leftTriangle(cx, cy);
        break;
    case TransportIcon::Prev:
        leftTriangle(cx - w/2 - barWidth/2 - 1, cy);
        narrowRect(cx - barWidth/2, cy - h/2, barWidth, h);
        break;
    case TransportIcon::Play:
        rightTriangle(cx + 1, cy);
        break;
    case TransportIcon::Stop:
        p.drawRoundedRect(QRectF(pad, pad, size - 2*pad, size - 2*pad), 2, 2);
        break;
    case TransportIcon::Next:
        narrowRect(cx - barWidth/2, cy - h/2, barWidth, h);
        rightTriangle(cx + w/2 + barWidth/2 + 1, cy);
        break;
    }
    p.end();
    return QIcon(QPixmap::fromImage(img));
}

TimelinesPanel::TimelinesPanel(Player* player, QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
    , _playTimer(new QTimer(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _header = new QWidget(this);
    _header->setAutoFillBackground(true);
    _header->setFixedHeight(50);
    _header->setFixedHeight(25);
    QHBoxLayout* headerLayout = new QHBoxLayout(_header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(2);
    layout->addWidget(_header);

    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    layout->addWidget(_scrollArea);

    // Transport icons: drawn white on transparent (no font/symbol coloring)
    const QString styleTransport =
        "QPushButton { background-color: transparent; border: none; }"
        "QPushButton:hover { background-color: rgba(128,128,128,0.2); border-radius: 4px; }"
        "QPushButton:pressed { background-color: rgba(128,128,128,0.35); border-radius: 4px; }";
    _transportStyle = styleTransport;
    const int btnSize = 28;
    const int iconSize = 22;

    // Frame, rate and time at the left of the timeline footer.
    const QString styleStatus = "QLabel { padding: 0 6px; }";
    _frameLabel = new QLabel("1", _header);
    _fpsLabel = new QLabel("24.00 fps", _header);
    _timeLabel = new QLabel("0.00 s", _header);
    for (QLabel* label : {_frameLabel, _fpsLabel, _timeLabel})
    {
        label->setStyleSheet(styleStatus);
        label->setMinimumWidth(60);
        label->setAlignment(Qt::AlignCenter);
        headerLayout->addWidget(label);
    }
    _frameLabel->setToolTip("Current frame");
    _fpsLabel->setToolTip("Frame rate");
    _timeLabel->setToolTip("Time into the animation");

    headerLayout->addStretch();

    QPushButton* gotoFirstBtn = new QPushButton(_header);
    gotoFirstBtn->setObjectName("timelineFirstFrame");
    gotoFirstBtn->setFixedSize(btnSize, btnSize);
    gotoFirstBtn->setProperty("transport", static_cast<int>(TransportIcon::First));
    gotoFirstBtn->setIconSize(QSize(iconSize, iconSize));
    gotoFirstBtn->setToolTip("Go to first frame");
    gotoFirstBtn->setStyleSheet(styleTransport);
    connect(gotoFirstBtn, &QPushButton::clicked, this, &TimelinesPanel::onGotoFirstFrame);
    headerLayout->addWidget(gotoFirstBtn);

    QPushButton* stepBackBtn = new QPushButton(_header);
    stepBackBtn->setObjectName("timelineStepBack");
    stepBackBtn->setFixedSize(btnSize, btnSize);
    stepBackBtn->setProperty("transport", static_cast<int>(TransportIcon::Prev));
    stepBackBtn->setIconSize(QSize(iconSize, iconSize));
    stepBackBtn->setToolTip("Step back");
    stepBackBtn->setStyleSheet(styleTransport);
    connect(stepBackBtn, &QPushButton::clicked, this, &TimelinesPanel::onStepBack);
    headerLayout->addWidget(stepBackBtn);

    _playButton = new QPushButton(_header);
    _playButton->setObjectName("timelinePlay");
    _playButton->setFixedSize(btnSize, btnSize);
    _playButton->setIconSize(QSize(iconSize, iconSize));
    _playButton->setToolTip("Play/Stop");
    _playButton->setStyleSheet(styleTransport);
    connect(_playButton, &QPushButton::clicked, this, &TimelinesPanel::onPlayStop);
    headerLayout->addWidget(_playButton);

    QPushButton* stepForwardBtn = new QPushButton(_header);
    stepForwardBtn->setObjectName("timelineStepForward");
    stepForwardBtn->setFixedSize(btnSize, btnSize);
    stepForwardBtn->setProperty("transport", static_cast<int>(TransportIcon::Next));
    stepForwardBtn->setIconSize(QSize(iconSize, iconSize));
    stepForwardBtn->setToolTip("Step forward");
    stepForwardBtn->setStyleSheet(styleTransport);
    connect(stepForwardBtn, &QPushButton::clicked, this, &TimelinesPanel::onStepForward);
    headerLayout->addWidget(stepForwardBtn);

    // Onion skin, beside the transport.
    headerLayout->addSpacing(16);
    _onionButton = new QPushButton(_header);
    _onionButton->setObjectName("timelineOnionSkin");
    _onionButton->setFixedSize(btnSize, btnSize);
    _onionButton->setCheckable(true);
    _onionButton->setToolTip("Onion skin (Alt+Shift+O); right-click for its settings, drag its markers in the ruler");
    _onionButton->setIconSize(QSize(iconSize, iconSize));
    _onionButton->setStyleSheet(styleTransport +
        "QPushButton:checked { background-color: rgba(41,128,238,0.45); border-radius: 4px; }");
    _onionButton->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_onionButton, &QPushButton::customContextMenuRequested, this,
        [this](const QPoint& at) { emit onionMenuRequested(_onionButton->mapToGlobal(at)); });
    connect(_onionButton, &QPushButton::toggled, this, [this](bool on) {
        _gridContainer->setOnionSkin(on, 2, 2);
        emit onionSkinToggled(on);
    });
    headerLayout->addWidget(_onionButton);

    // Loop Playback, beside the onion skin.
    _loopButton = new QPushButton(_header);
    _loopButton->setObjectName("timelineLoop");
    _loopButton->setFixedSize(btnSize, btnSize);
    _loopButton->setCheckable(true);
    _loopButton->setToolTip("Loop playback (Alt+Shift+L)");
    _loopButton->setIconSize(QSize(iconSize, iconSize));
    _loopButton->setStyleSheet(styleTransport +
        "QPushButton:checked { background-color: rgba(41,128,238,0.45); border-radius: 4px; }");
    connect(_loopButton, &QPushButton::toggled, this, [this](bool on) { setLoopPlayback(on); });
    headerLayout->addWidget(_loopButton);

    // The frame view: frame widths, row heights, thumbnails and seconds.
    QPushButton* viewButton = new QPushButton(_header);
    viewButton->setObjectName("timelineFrameView");
    viewButton->setFixedSize(btnSize, btnSize);
    viewButton->setText(QString::fromUtf8("\u2630"));
    viewButton->setToolTip("Frame view: frame size, row height, thumbnails, time in seconds (Ctrl+wheel zooms)");
    viewButton->setStyleSheet(styleTransport);
    connect(viewButton, &QPushButton::clicked, this, [this, viewButton]() {
        QMenu menu(this);
        const TimelineGrid::View now = _gridContainer->view();
        const auto choose = [&](const QString& label, bool checked, std::function<void(TimelineGrid::View&)> change) {
            QAction* action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(checked);
            connect(action, &QAction::triggered, this, [this, change]() {
                TimelineGrid::View view = _gridContainer->view();
                change(view);
                _gridContainer->setView(view);
                emit frameViewChanged();
            });
        };
        const std::pair<const char*, int> widths[] = {{"Tiny", 5}, {"Small", 10}, {"Normal", 20}, {"Medium", 30}, {"Large", 44}};
        for (const auto& [label, width] : widths)
            choose(label, now.frameWidth == width, [width = width](TimelineGrid::View& view) { view.frameWidth = width; });
        menu.addSeparator();
        const std::pair<const char*, int> heights[] = {{"Short Rows", 15}, {"Normal Rows", 22}, {"Tall Rows", 44}};
        for (const auto& [label, height] : heights)
            choose(label, now.rowHeight == height, [height = height](TimelineGrid::View& view) { view.rowHeight = height; });
        menu.addSeparator();
        choose("Preview", now.preview == 1, [](TimelineGrid::View& view) {
            view.preview = view.preview == 1 ? 0 : 1;
            if (view.preview)
                view.rowHeight = std::max(view.rowHeight, 44);
        });
        choose("Preview in Context", now.preview == 2, [](TimelineGrid::View& view) {
            view.preview = view.preview == 2 ? 0 : 2;
            if (view.preview)
                view.rowHeight = std::max(view.rowHeight, 44);
        });
        menu.addSeparator();
        choose("Time in Seconds", now.seconds, [](TimelineGrid::View& view) { view.seconds = !view.seconds; });
        choose("Span Based Selection", now.spanSelection, [](TimelineGrid::View& view) { view.spanSelection = !view.spanSelection; });
        menu.exec(viewButton->mapToGlobal(QPoint(0, viewButton->height())));
    });
    headerLayout->addWidget(viewButton);

    headerLayout->addStretch();
    // Balance the status labels so the transport stays centered.
    QWidget* balance = new QWidget(_header);
    balance->setFixedWidth(3 * 60 + 2 * 2 - 2 * btnSize - 4);
    headerLayout->addWidget(balance);

    _gridContainer = new TimelineGrid(_player, this);
    _scrollArea->setWidget(_gridContainer);
    connect(_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, _gridContainer,
        [this]() { _gridContainer->update(); });

    connect(_player, &Player::currentFrameChanged, _gridContainer, &TimelineGrid::onPlayerFrameChanged);
    connect(_player, &Player::currentFrameChanged, this, &TimelinesPanel::onPlayerFrameChanged);
    connect(_gridContainer, &TimelineGrid::layerPressed, this, &TimelinesPanel::layerPressed);
    connect(_gridContainer, &TimelineGrid::keyframeMoveRequested, this, &TimelinesPanel::keyframeMoveRequested);
    connect(_gridContainer, &TimelineGrid::framePicked, this, &TimelinesPanel::framePicked);
    connect(_gridContainer, &TimelineGrid::soundDropped, this, &TimelinesPanel::soundDropped);
    connect(_gridContainer, &TimelineGrid::framesMoveRequested, this, &TimelinesPanel::framesMoveRequested);
    connect(_gridContainer, &TimelineGrid::contextMenuRequested, this, &TimelinesPanel::contextMenuRequested);
    connect(_gridContainer, &TimelineGrid::spanResizeRequested, this, &TimelinesPanel::spanResizeRequested);
    connect(_gridContainer, &TimelineGrid::onionRangeChanged, this, &TimelinesPanel::onionRangeChanged);
    connect(_gridContainer, &TimelineGrid::viewChanged, this, &TimelinesPanel::frameViewChanged);
    connect(_playTimer, &QTimer::timeout, this, &TimelinesPanel::onTimerTick);

    applyTheme();
    connect(theme::notifier(), &theme::Notifier::changed, this, &TimelinesPanel::applyTheme);
}

void TimelinesPanel::applyTheme()
{
    QPalette headerPalette = _header->palette();
    headerPalette.setColor(QPalette::Window, theme::color("panelHeader"));
    _header->setPalette(headerPalette);
    for (QPushButton* button : _header->findChildren<QPushButton*>())
    {
        const QVariant kind = button->property("transport");
        if (kind.isValid())
            button->setIcon(makeTransportIcon(static_cast<TransportIcon>(kind.toInt()), 22));
    }
    _playButton->setIcon(makeTransportIcon(_playTimer->isActive() ? TransportIcon::Stop : TransportIcon::Play, 22));
    _onionButton->setIcon(onionIcon(22));
    _loopButton->setIcon(loopIcon(22));
    const QColor accent = theme::color("playhead");
    const QString checked = QString("QPushButton:checked { background-color: rgba(%1,%2,%3,0.45); border-radius: 4px; }")
                                .arg(accent.red()).arg(accent.green()).arg(accent.blue());
    _onionButton->setStyleSheet(_transportStyle + checked);
    _loopButton->setStyleSheet(_transportStyle + checked);
    _gridContainer->update();
}

void TimelinesPanel::setDocument(const fla::FLADocument* document)
{
    stopPlaying();
    _flaDocument = document;
    _gridContainer->setDocument(document);
    _gridContainer->setTimeline(nullptr);
    // The rows belonged to the document before; the list sends new ones.
    _gridContainer->setRows({});
    updateStatus();
}

void TimelinesPanel::updateStatus()
{
    if (!_frameLabel)
        return;
    const int frame = _player ? _player->currentFrame() : 0;
    const double fps = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
    _frameLabel->setText(QString::number(frame + 1));
    _fpsLabel->setText(QString("%1 fps").arg(fps, 0, 'f', 2));
    _timeLabel->setText(QString("%1 s").arg(frame / fps, 0, 'f', 2));
}

void TimelinesPanel::setRows(const std::vector<LayerRow>& rows)
{
    _gridContainer->setRows(rows);
}

void TimelinesPanel::setOnionSkin(bool on, int before, int after)
{
    if (_onionButton && _onionButton->isChecked() != on)
    {
        const QSignalBlocker blocker(_onionButton);
        _onionButton->setChecked(on);
    }
    _gridContainer->setOnionSkin(on, before, after);
}

QScrollBar* TimelinesPanel::verticalScrollBar() const
{
    return _scrollArea->verticalScrollBar();
}

int TimelinesPanel::rowsViewportHeight() const
{
    return std::max(0, _scrollArea->viewport()->height() - 25);
}

void TimelinesPanel::refreshFrames()
{
    _gridContainer->clearThumbnails();
    _gridContainer->updateSize();
    _gridContainer->update();
    updateStatus();
}

void TimelineGrid::setRows(const std::vector<LayerRow>& rows)
{
    _rows = rows;
    updateSize();
    update();
}

void TimelinesPanel::setTimeline(const fla::Timeline* timeline)
{
    _gridContainer->setTimeline(timeline);
    _gridContainer->updateSize();
    _gridContainer->update();
}

int TimelineGrid::frameCount() const
{
    int count = 0;
    for (const fla::Timeline* timeline : shownTimelines())
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            for (const fla::Frame* frame : layer->frames)
                count = std::max(count, frame->index + frame->duration);
        }
    }
    return count;
}

std::vector<const fla::Timeline*> TimelineGrid::shownTimelines() const
{
    std::vector<const fla::Timeline*> timelines;
    if (_timeline)
    {
        timelines.push_back(_timeline);
        return timelines;
    }
    if (_flaDocument && _flaDocument->document)
    {
        if (const fla::Timeline* scene = _flaDocument->document->currentScene())
            timelines.push_back(scene);
    }
    return timelines;
}

void TimelinesPanel::onGotoFirstFrame()
{
    if (_player)
    {
        _player->setCurrentFrame(0);
    }
}

void TimelinesPanel::onStepBack()
{
    if (_player)
    {
        _player->setCurrentFrame(_player->currentFrame() - 1);
    }
}

double TimelinesPanel::frameRate() const
{
    return _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
}

void TimelinesPanel::stopPlaying()
{
    if (!_isPlaying)
        return;
    _playTimer->stop();
    _playButton->setIcon(makeTransportIcon(TransportIcon::Play, 22));
    _isPlaying = false;
    if (_player)
        _player->setPlaying(false);
}

void TimelinesPanel::onPlayStop()
{
    if (_isPlaying)
    {
        stopPlaying();
        return;
    }
    if (!_player)
        return;
    // From the end, play starts over from the first frame;
    // looping, from outside the range, at its start.
    const int last = _gridContainer->frameCount() - 1;
    if (_gridContainer->loopOn())
    {
        if (_player->currentFrame() < _gridContainer->loopFirst() || _player->currentFrame() > _gridContainer->loopLast())
            _player->setCurrentFrame(_gridContainer->loopFirst());
    }
    else if (last > 0 && _player->currentFrame() >= last)
    {
        _player->setCurrentFrame(0);
    }
    if (!_playClock)
        _playClock = new QElapsedTimer();
    _playClock->start();
    _playStartFrame = _player->currentFrame();
    _frameActions.reset();
    if (_frameActionsOn)
    {
        if (const fla::Timeline* timeline = _gridContainer->shownTimeline())
        {
            auto actions = std::make_shared<fla::frameactions::TimelineActions>(*timeline);
            if (!actions->empty())
                _frameActions = std::move(actions);
        }
    }
    _playShown = _playStartFrame;
    _playSteps = 0;
    // Ticks a few times a frame; which frame shows is worked out from the
    // clock.
    _playTimer->start(std::max(1, static_cast<int>(1000.0 / frameRate() / 2.0)));
    _playButton->setIcon(makeTransportIcon(TransportIcon::Stop, 22));
    _isPlaying = true;
    _player->setPlaying(true);
}

void TimelinesPanel::goToLast()
{
    if (_player)
        _player->setCurrentFrame(std::max(0, _gridContainer->frameCount() - 1));
}

void TimelinesPanel::setLoopPlayback(bool on)
{
    if (_loopButton && _loopButton->isChecked() != on)
    {
        const QSignalBlocker blocker(_loopButton);
        _loopButton->setChecked(on);
    }
    if (on == _gridContainer->loopOn())
        return;
    if (!on)
    {
        _gridContainer->setLoop(false, _gridContainer->loopFirst(), _gridContainer->loopLast());
        return;
    }
    const FrameSelection selected = _gridContainer->selection();
    if (!selected.isEmpty() && selected.last > selected.first)
        _gridContainer->setLoop(true, selected.first, selected.last);
    else
        _gridContainer->setLoop(true, 0, std::max(0, _gridContainer->frameCount() - 1));
}

bool TimelinesPanel::loopPlayback() const
{
    return _gridContainer->loopOn();
}

void TimelineGrid::setLoop(bool on, int first, int last)
{
    _loopOn = on;
    _loopFirst = std::max(0, std::min(first, last));
    _loopLast = std::max(_loopFirst, std::max(first, last));
    update();
}

const std::vector<int>& TimelineGrid::motionKeysOf(const fla::Frame* frame) const
{
    static const std::vector<int> none;
    const std::string* xml = fla::timeline::motionOf(const_cast<fla::Frame&>(*frame));
    if (!xml)
        return none;
    const size_t hash = std::hash<std::string>()(*xml);
    MotionKeys& keys = _motionKeys[frame];
    if (keys.motionHash == hash && keys.duration == frame->duration)
        return keys.frames;
    keys.motionHash = hash;
    keys.duration = frame->duration;
    keys.frames.clear();
    fla::MotionObject motion;
    if (motion.parse(*xml) && motion.duration() > 0.0)
    {
        for (const double time : motion.keyTimes())
        {
            const int at = static_cast<int>(std::lround(time / motion.duration() * frame->duration));
            if (at > 0 && at < frame->duration && (keys.frames.empty() || keys.frames.back() != at))
                keys.frames.push_back(at);
        }
    }
    return keys.frames;
}

void TimelinesPanel::onStepForward()
{
    if (_player)
    {
        _player->setCurrentFrame(_player->currentFrame() + 1);
    }
}

void TimelinesPanel::onTimerTick()
{
    if (!_player || !_playClock)
        return;
    if (_frameActions)
    {
        playFrameActions();
        return;
    }
    const int due = _playStartFrame + static_cast<int>(_playClock->elapsed() * frameRate() / 1000.0);
    // Past a scene's end, on into the next scene, as a published movie
    // plays them (round to the first again when the whole scene loops).
    if (due > std::max(0, _gridContainer->frameCount() - 1) && playIntoNextScene())
        return;
    if (_gridContainer->loopOn())
    {
        // Round the range again and again, by the clock.
        const int first = _gridContainer->loopFirst();
        const int length = std::max(1, _gridContainer->loopLast() - first + 1);
        _player->setCurrentFrame(due <= _gridContainer->loopLast() ? due : first + (due - first) % length);
        return;
    }
    const int last = std::max(0, _gridContainer->frameCount() - 1);
    // Playback stops on the last frame when it is not looping,
    // once that frame has had its time.
    if (due > last)
    {
        _player->setCurrentFrame(last);
        stopPlaying();
        return;
    }
    _player->setCurrentFrame(due);
}

void TimelinesPanel::playFrameActions()
{
    // Each frame that is due is entered in turn, so its actions run even
    // when drawing falls behind the clock.
    const int due = static_cast<int>(_playClock->elapsed() * frameRate() / 1000.0);
    const int last = std::max(0, _gridContainer->frameCount() - 1);
    const bool looping = _gridContainer->loopOn();
    int frame = _playShown;
    while (_playSteps < due)
    {
        // From a scene's last frame, on into the next scene.
        if (frame >= last && playIntoNextScene())
            return;
        ++_playSteps;
        int next = frame + 1;
        if (looping && (frame == _gridContainer->loopLast() || next > last))
            next = _gridContainer->loopFirst();
        const fla::frameactions::State state = _frameActions->enter(std::min(next, last));
        frame = state.frame;
        // A stop, or the last frame without looping, ends playback there.
        if (!state.playing || (!looping && frame >= last))
        {
            _playShown = frame;
            _player->setCurrentFrame(frame);
            stopPlaying();
            return;
        }
    }
    if (frame != _playShown)
    {
        _playShown = frame;
        _player->setCurrentFrame(frame);
    }
}

bool TimelinesPanel::playIntoNextScene()
{
    if (!_sceneAdvance || !_flaDocument || !_flaDocument->document)
        return false;
    // Only the scenes themselves play on; a symbol's timeline stays.
    if (_gridContainer->shownTimeline() != _flaDocument->document->currentScene())
        return false;
    // A loop over part of the scene keeps playing that part.
    const int last = std::max(0, _gridContainer->frameCount() - 1);
    const bool loopingAll = _gridContainer->loopOn() && _gridContainer->loopFirst() == 0 && _gridContainer->loopLast() >= last;
    if (_gridContainer->loopOn() && !loopingAll)
        return false;
    if (!_sceneAdvance(loopingAll))
        return false;
    // The new scene's first frame, playing on.
    if (!_isPlaying)
        onPlayStop();
    return true;
}

void TimelinesPanel::onPlayerFrameChanged(int frame)
{
    updateStatus();
    const int playheadX = frame * _gridContainer->cellStride() + _gridContainer->view().frameWidth / 2;
    _scrollArea->ensureVisible(playheadX, 0, 50, 0);
}

TimelineGrid::TimelineGrid(Player* player, QWidget* parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
{
    // Sounds dragged from the library land on frames.
    setAcceptDrops(true);
}

namespace {

/// The library sound a drag carries, or empty.
QString draggedSound(const QMimeData* data)
{
    if (!data->hasFormat(LibraryTree::mimeType()))
        return QString();
    const QString text = QString::fromUtf8(data->data(LibraryTree::mimeType()));
    return text.startsWith("sound:") ? text.mid(6) : QString();
}

} // namespace

void TimelineGrid::dragEnterEvent(QDragEnterEvent* event)
{
    if (!draggedSound(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void TimelineGrid::dragMoveEvent(QDragMoveEvent* event)
{
    const int row = rowAt(event->position().toPoint().y());
    if (!draggedSound(event->mimeData()).isEmpty() && row >= 0 && row < static_cast<int>(_rows.size()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void TimelineGrid::dropEvent(QDropEvent* event)
{
    const QString name = draggedSound(event->mimeData());
    const int row = rowAt(event->position().toPoint().y());
    const int frame = frameAt(event->position().toPoint().x());
    if (name.isEmpty() || row < 0 || row >= static_cast<int>(_rows.size()) || frame < 0)
        return;
    event->acceptProposedAction();
    const fla::Layer* layer = _rows[static_cast<size_t>(row)].layer;
    // After the drag has finished.
    QTimer::singleShot(0, this, [this, layer, frame, name]() { emit soundDropped(layer, frame, name); });
}

void TimelineGrid::updateSize()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        setMinimumSize(0, 0);
        return;
    }

    const int layerRowHeight = _view.rowHeight;
    const int headerHeight = kRulerHeight;

    const int numRows = static_cast<int>(_rows.size());

    int maxFrames = frameCount() + kSpareColumns;
    int totalWidth = headerHeight + maxFrames * (cellStride() + 1);
    int totalHeight = headerHeight + numRows * layerRowHeight + _bottomPadding;

    setMinimumSize(totalWidth, totalHeight);
}

void TimelineGrid::setBottomPadding(int padding)
{
    if (padding == _bottomPadding)
        return;
    _bottomPadding = padding;
    updateSize();
}

void TimelineGrid::updatePlayerFromMouse(int x)
{
    if (!_player || !_flaDocument || !_flaDocument->document)
        return;

    int frameX = x;
    if (frameX < 0)
        frameX = 0;

    int frame = frameX / cellStride();
    if (frame < 0)
        frame = 0;

    for (const fla::Timeline* timeline : shownTimelines())
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            int layerEndFrame = 0;
            for (const fla::Frame* f : layer->frames)
            {
                int endFrame = f->index + f->duration;
                if (endFrame > layerEndFrame)
                    layerEndFrame = endFrame;
            }
        }
    }

    _player->setCurrentFrame(frame);
}

int TimelineGrid::rowAt(int y) const
{
    const int headerHeight = kRulerHeight;
    // The pinned ruler covers the rows scrolled under it.
    if (y < rulerTop() + headerHeight)
        return -1;
    const int row = (y - headerHeight) / _view.rowHeight;
    return row < static_cast<int>(_rows.size()) ? row : -1;
}

QRect TimelineGrid::cellsRect(const QString& layer, int first, int last, bool ruler) const
{
    first = std::max(0, first);
    last = std::max(first, last);
    const int x = first * cellStride();
    const int width = (last - first + 1) * cellStride();
    if (ruler)
        return QRect(x, rulerTop(), width, kRulerHeight);
    for (size_t row = 0; row < _rows.size(); ++row)
    {
        const fla::Layer* shown = _rows[row].layer;
        if (layer.isEmpty() || (shown && QString::fromStdString(shown->name) == layer))
            return QRect(x, kRulerHeight + static_cast<int>(row) * _view.rowHeight, width, _view.rowHeight);
    }
    return QRect();
}

int TimelineGrid::frameAt(int x) const
{
    return std::max(0, x / cellStride());
}

void TimelineGrid::dropBlock(bool copy)
{
    // Each layer of the block goes to the row as far down or up; every one
    // of those has to be a layer of the same timeline, or the block stays.
    const int top = std::min(_anchorRow, _focusRow);
    const int bottom = std::max(_anchorRow, _focusRow);
    std::vector<const fla::Layer*> sources;
    std::vector<const fla::Layer*> targets;
    for (int row = top; row <= bottom; ++row)
    {
        const int to = row + _blockRows;
        if (row < 0 || to < 0 || row >= static_cast<int>(_rows.size()) || to >= static_cast<int>(_rows.size()))
            return;
        const LayerRow& source = _rows[row];
        const LayerRow& target = _rows[to];
        const bool sourceIsLayer = source.layer && source.layer->layerType != fla::Layer::Type::Folder;
        const bool targetIsLayer = target.layer && target.layer->layerType != fla::Layer::Type::Folder;
        if (!sourceIsLayer)
            continue;
        if (!targetIsLayer || target.timeline != source.timeline)
        {
            update();
            return;
        }
        sources.push_back(source.layer);
        targets.push_back(target.layer);
    }
    if (sources.empty())
        return;
    const int first = std::min(_anchorFrame, _focusFrame);
    const int last = std::max(_anchorFrame, _focusFrame);
    // The selection goes with the block.
    _anchorRow += _blockRows;
    _focusRow += _blockRows;
    _anchorFrame += _blockFrames;
    _focusFrame += _blockFrames;
    const int to = first + _blockFrames;
    _blockRows = 0;
    _blockFrames = 0;
    update();
    emit framesMoveRequested(sources, targets, first, last, to, copy);
}

FrameSelection TimelineGrid::selection() const
{
    FrameSelection selection;
    if (!_hasSelection)
        return selection;
    const int top = std::min(_anchorRow, _focusRow);
    const int bottom = std::max(_anchorRow, _focusRow);
    for (int row = top; row <= bottom && row < static_cast<int>(_rows.size()); ++row)
    {
        const fla::Layer* layer = _rows[row].layer;
        if (layer && layer->layerType != fla::Layer::Type::Folder)
            selection.layers.push_back(layer);
    }
    selection.first = std::min(_anchorFrame, _focusFrame);
    selection.last = std::max(_anchorFrame, _focusFrame);
    for (const auto& cell : _cells)
    {
        const fla::Layer* layer = cell.first < static_cast<int>(_rows.size()) ? _rows[cell.first].layer : nullptr;
        if (layer && layer->layerType != fla::Layer::Type::Folder)
            selection.cells.push_back({layer, cell.second});
    }
    return selection;
}

void TimelineGrid::selectAll()
{
    if (_rows.empty())
        return;
    int end = 1;
    for (const LayerRow& row : _rows)
    {
        if (!row.layer)
            continue;
        for (const fla::Frame* frame : row.layer->frames)
            if (frame)
                end = std::max(end, frame->index + frame->duration);
    }
    _hasSelection = true;
    _cells.clear();
    _anchorRow = 0;
    _focusRow = static_cast<int>(_rows.size()) - 1;
    _anchorFrame = 0;
    _focusFrame = end - 1;
    update();
}

void TimelineGrid::setSelection(const FrameSelection& selection)
{
    const auto rowOf = [this](const fla::Layer* layer) {
        for (size_t i = 0; i < _rows.size(); ++i)
            if (_rows[i].layer == layer)
                return static_cast<int>(i);
        return -1;
    };
    _hasSelection = false;
    _cells.clear();
    int top = -1;
    int bottom = -1;
    for (const fla::Layer* layer : selection.layers)
    {
        const int row = rowOf(layer);
        if (row < 0)
            continue;
        top = top < 0 ? row : std::min(top, row);
        bottom = std::max(bottom, row);
    }
    if (top >= 0)
    {
        _hasSelection = true;
        _anchorRow = top;
        _focusRow = bottom;
        _anchorFrame = selection.first;
        _focusFrame = selection.last;
    }
    for (const auto& [layer, frame] : selection.cells)
    {
        const int row = rowOf(layer);
        if (row < 0)
            continue;
        if (!_hasSelection)
        {
            _hasSelection = true;
            _anchorRow = _focusRow = row;
            _anchorFrame = _focusFrame = frame;
        }
        else
            _cells.push_back({row, frame});
    }
    update();
}

void TimelineGrid::toggleCell(int row, int frame)
{
    const std::pair<int, int> cell(row, frame);
    const auto found = std::find(_cells.begin(), _cells.end(), cell);
    if (found != _cells.end())
        _cells.erase(found);
    else if (!_hasSelection)
    {
        _hasSelection = true;
        _anchorRow = _focusRow = row;
        _anchorFrame = _focusFrame = frame;
    }
    else
        _cells.push_back(cell);
    update();
}

void TimelineGrid::setOnionSkin(bool on, int before, int after)
{
    _onionSkin = on;
    _onionBefore = before;
    _onionAfter = after;
    update();
}

void TimelineGrid::clearSelection()
{
    _hasSelection = false;
    _cells.clear();
    update();
}

void TimelineGrid::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }

    const int row = rowAt(event->pos().y());
    const int frame = frameAt(event->pos().x());

    if (row < 0)
    {
        // The loop range's ends are dragged by their handles; elsewhere the
        // ruler scrubs the playhead.
        // The onion skin's markers drag its range.
        if (event->button() == Qt::LeftButton && _onionSkin && _player && !_loopOn)
        {
            const int stride = cellStride();
            const int current = _player->currentFrame();
            const int x = event->pos().x();
            const int left = std::max(0, current - _onionBefore) * stride + 1;
            const int right = (current + _onionAfter + 1) * stride - 2;
            if (std::abs(x - left) <= 4 && _onionBefore > 0)
                _draggingOnion = 1;
            else if (std::abs(x - right) <= 4)
                _draggingOnion = 2;
            if (_draggingOnion)
                return;
        }
        if (event->button() == Qt::LeftButton && _loopOn)
        {
            const int stride = cellStride();
            const int x = event->pos().x();
            if (std::abs(x - _loopFirst * stride) <= 4)
                _draggingLoop = 1;
            else if (std::abs(x - ((_loopLast + 1) * stride - 1)) <= 4)
                _draggingLoop = 2;
            if (_draggingLoop)
                return;
        }
        if (event->button() == Qt::LeftButton)
        {
            _isDragging = true;
            updatePlayerFromMouse(event->pos().x());
        }
        QWidget::mousePressEvent(event);
        return;
    }

    // The row under the press is a layer, which becomes current. Rows are
    // the layers list's, lane for lane.
    emit layerPressed(_rows[row].timeline, _rows[row].layerIndex);

    // A right-click inside the selection keeps it for the menu.
    const FrameSelection current = selection();
    const bool insideSelection = _hasSelection &&
        row >= std::min(_anchorRow, _focusRow) && row <= std::max(_anchorRow, _focusRow) &&
        frame >= current.first && frame <= current.last;
    if (event->button() == Qt::RightButton && insideSelection)
        return;

    const fla::Layer* layer = _rows[row].layer;

    // Ctrl on a span's last frame takes hold of its end: dragging stretches
    // or shortens the span.
    if (layer && event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier))
    {
        for (const fla::Frame* span : layer->frames)
        {
            if (span && span->index + span->duration - 1 == frame)
            {
                _resizingSpan = true;
                _resizeRow = row;
                _resizeKeyframe = span->index;
                _resizeDuration = span->duration;
                update();
                return;
            }
        }
        // Anywhere else Ctrl adds the frame to the selection, or takes it out.
        toggleCell(row, frame);
        return;
    }

    // Pressing a keyframe that is already selected on its own picks it up
    // to drag.
    bool onKeyframe = false;
    if (layer && event->button() == Qt::LeftButton)
    {
        for (const fla::Frame* candidate : layer->frames)
            onKeyframe = onKeyframe || (candidate && candidate->index == frame);
    }
    const bool singleSelected = _hasSelection && _anchorRow == row && _focusRow == row &&
        _anchorFrame == frame && _focusFrame == frame;
    if (onKeyframe && singleSelected)
    {
        _movingKeyframe = true;
        _moveRow = row;
        _moveFrom = frame;
        _moveTo = frame;
        update();
        return;
    }

    // Pressing inside the selection picks the block up to drag. Let go
    // where it was pressed and it was only a click.
    if (event->button() == Qt::LeftButton && insideSelection && !(event->modifiers() & Qt::ShiftModifier))
    {
        _movingBlock = true;
        _blockRow = row;
        _blockFrame = frame;
        _blockRows = 0;
        _blockFrames = 0;
        return;
    }

    if ((event->modifiers() & Qt::ShiftModifier) && _hasSelection)
    {
        _focusRow = row;
        _focusFrame = frame;
    }
    else
    {
        _hasSelection = true;
        _cells.clear();
        _anchorRow = _focusRow = row;
        _anchorFrame = _focusFrame = frame;
        // Span based: the whole span the click lands in.
        if (_view.spanSelection && layer)
            for (const fla::Frame* span : layer->frames)
                if (span && frame >= span->index && frame < span->index + std::max(span->duration, 1))
                {
                    _anchorFrame = span->index;
                    _focusFrame = span->index + std::max(span->duration, 1) - 1;
                }
    }
    _selecting = event->button() == Qt::LeftButton;
    _picked = event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier);
    if (_player)
        _player->setCurrentFrame(frame);
    update();
}

void TimelineGrid::mouseMoveEvent(QMouseEvent* event)
{
    if (_draggingOnion && _player)
    {
        const int at = std::max(0, frameAt(event->pos().x()));
        const int current = _player->currentFrame();
        if (_draggingOnion == 1)
            _onionBefore = std::clamp(current - at, 0, 99);
        else
            _onionAfter = std::clamp(at - current, 0, 99);
        update();
    }
    else if (_resizingSpan)
    {
        const int duration = std::max(1, frameAt(event->pos().x()) - _resizeKeyframe + 1);
        if (duration != _resizeDuration)
        {
            _resizeDuration = duration;
            update();
        }
    }
    else if (_draggingLoop)
    {
        const int at = std::max(0, frameAt(event->pos().x()));
        if (_draggingLoop == 1)
            _loopFirst = std::min(at, _loopLast);
        else
            _loopLast = std::max(at, _loopFirst);
        update();
    }
    else if (_isDragging)
    {
        updatePlayerFromMouse(event->pos().x());
    }
    else if (_movingBlock)
    {
        // The block keeps to the rows there are and does not go before the
        // first frame.
        const int top = std::min(_anchorRow, _focusRow);
        const int bottom = std::max(_anchorRow, _focusRow);
        const int first = std::min(_anchorFrame, _focusFrame);
        const int frames = std::max(frameAt(event->pos().x()) - _blockFrame, -first);
        const int lastRow = static_cast<int>(_rows.size()) - 1;
        const int under = std::max(0, std::min(lastRow, (event->pos().y() - kRulerHeight) / _view.rowHeight));
        const int rows = std::max(-top, std::min(lastRow - bottom, under - _blockRow));
        if (frames != _blockFrames || rows != _blockRows)
        {
            _blockFrames = frames;
            _blockRows = rows;
            update();
        }
    }
    else if (_movingKeyframe)
    {
        const int to = frameAt(event->pos().x());
        if (to != _moveTo)
        {
            _moveTo = to;
            update();
        }
    }
    else if (_selecting)
    {
        const int row = rowAt(std::max(event->pos().y(), rulerTop() + 25));
        const int frame = frameAt(event->pos().x());
        if (row >= 0 && (row != _focusRow || frame != _focusFrame))
        {
            _focusRow = row;
            _focusFrame = frame;
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TimelineGrid::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        _isDragging = false;
        _draggingLoop = 0;
        if (_draggingOnion)
        {
            _draggingOnion = 0;
            emit onionRangeChanged(_onionBefore, _onionAfter);
        }
        if (_resizingSpan)
        {
            _resizingSpan = false;
            const fla::Layer* layer = _resizeRow < static_cast<int>(_rows.size()) ? _rows[_resizeRow].layer : nullptr;
            const fla::Frame* span = nullptr;
            for (const fla::Frame* candidate : layer ? layer->frames : std::vector<fla::Frame*>())
                if (candidate && candidate->index == _resizeKeyframe)
                    span = candidate;
            // A Ctrl-click that stretched nothing adds the frame instead.
            if (span && span->duration == _resizeDuration)
                toggleCell(_resizeRow, _resizeKeyframe + _resizeDuration - 1);
            else if (layer)
                emit spanResizeRequested(layer, _resizeKeyframe, _resizeDuration);
            update();
        }
        // A click on one frame picks what is on it.
        if (_selecting && _picked && _hasSelection && _anchorRow == _focusRow && _anchorFrame == _focusFrame &&
            _anchorRow < static_cast<int>(_rows.size()) && _rows[_anchorRow].layer)
            emit framePicked(_rows[_anchorRow].layer, _anchorFrame);
        _picked = false;
        _selecting = false;
        if (_movingBlock)
        {
            _movingBlock = false;
            if (_blockRows == 0 && _blockFrames == 0)
            {
                // Only a click: the cell pressed becomes the selection.
                _anchorRow = _focusRow = _blockRow;
                _anchorFrame = _focusFrame = _blockFrame;
                _cells.clear();
                if (_player)
                    _player->setCurrentFrame(_blockFrame);
                update();
                if (_blockRow < static_cast<int>(_rows.size()) && _rows[_blockRow].layer)
                    emit framePicked(_rows[_blockRow].layer, _blockFrame);
            }
            else
            {
                dropBlock((event->modifiers() & Qt::AltModifier) != 0);
            }
        }
        if (_movingKeyframe)
        {
            _movingKeyframe = false;
            if (_moveTo != _moveFrom && _moveRow < static_cast<int>(_rows.size()))
            {
                // The selection follows the keyframe to where it lands; with
                // Alt the keyframe is copied there.
                _anchorFrame = _focusFrame = _moveTo;
                if (event->modifiers() & Qt::AltModifier)
                {
                    const fla::Layer* layer = _rows[_moveRow].layer;
                    emit framesMoveRequested({layer}, {layer}, _moveFrom, _moveFrom, _moveTo, true);
                }
                else
                    emit keyframeMoveRequested(_rows[_moveRow].layer, _moveFrom, _moveTo);
            }
            update();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineGrid::contextMenuEvent(QContextMenuEvent* event)
{
    if (rowAt(event->pos().y()) >= 0)
        emit contextMenuRequested(event->globalPos());
}

void TimelineGrid::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    if (!_flaDocument || !_flaDocument->document)
        return;

    const int frameWidth = _view.frameWidth;
    const int frameHeight = _view.rowHeight - 1;
    const int layerRowHeight = _view.rowHeight;
    const int headerHeight = kRulerHeight;
    const int margin = 1;
    const int cellStride = frameWidth + margin;

    // Frame colors: an empty keyframe span is white, a span with
    // content gray, a classic tween lilac and a shape tween green.
    const QColor background = theme::color("timeline.background");
    const QColor cellDark = theme::color("timeline.cell");
    const QColor cellLight = theme::color("timeline.cellFifth");
    const QColor textColor = theme::color("timeline.text");
    const QColor emptySpan = theme::color("frame.empty");
    const QColor staticSpan = theme::color("frame.static");
    const QColor motionSpan = theme::color("frame.classicTween");
    const QColor shapeSpan = theme::color("frame.shapeTween");
    const QColor motionObjectSpan = theme::color("frame.motionTween");
    const QColor poseSpan = theme::color("frame.pose");
    const QColor mark = theme::color("frame.mark");
    // The time head is blue.
    const QColor playheadColor = theme::color("playhead");
    const auto faded = [&playheadColor](int alpha) {
        return QColor(playheadColor.red(), playheadColor.green(), playheadColor.blue(), alpha);
    };

    painter.fillRect(event->rect(), background);

    const int maxFrames = frameCount() + kSpareColumns;
    int y = headerHeight;

    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const LayerRow& rowEntry : _rows)
    {
        const fla::Layer* layer = rowEntry.layer;
        {
            // The empty cells, with every fifth column picked out.
            for (int i = 0; i < maxFrames; ++i)
            {
                const QRect cell(i * cellStride, y, frameWidth, frameHeight);
                painter.fillRect(cell, (i + 1) % 5 == 0 && frameWidth >= 8 ? cellLight : cellDark);
            }

            if (layer->layerType == fla::Layer::Type::Folder)
            {
                y += layerRowHeight;
                continue;
            }

            for (const fla::Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;

                const int duration = std::max(frame->duration, 1);
                const int x0 = frame->index * cellStride;
                const int x1 = (frame->index + duration) * cellStride - margin;
                const QRect span(x0, y, x1 - x0, frameHeight);

                const bool hasContent = !frame->elements.empty();
                QColor spanColor = hasContent ? staticSpan : emptySpan;
                const bool motionObject = frame->otherTweenType == "motion object";
                const bool pose = frame->otherTweenType == "IK pose";
                if (frame->tweenType == fla::TweenType::Motion)
                    spanColor = motionSpan;
                else if (frame->tweenType == fla::TweenType::Shape)
                    spanColor = shapeSpan;
                else if (motionObject)
                    spanColor = motionObjectSpan;
                else if (pose)
                    spanColor = poseSpan;
                painter.fillRect(span, spanColor);

                // A sound draws its waveform along the span, from where the
                // frame starts it.
                size_t soundIn = 0;
                std::shared_ptr<const PcmBuffer> sound = _soundProvider ? _soundProvider(frame, soundIn) : nullptr;
                if (sound && sound->frames() > soundIn)
                {
                    const double fps = _flaDocument->document->frameRate > 0.0 ? _flaDocument->document->frameRate : 24.0;
                    const double perPixel = PcmBuffer::kRate / fps / cellStride;
                    const int left = std::max(x0, event->rect().left());
                    const int right = std::min(x1, event->rect().right() + 1);
                    const double half = frameHeight / 2.0 - 2.0;
                    const int centerY = y + frameHeight / 2;
                    painter.setRenderHint(QPainter::Antialiasing, false);
                    painter.setPen(QPen(theme::color("frame.waveform"), 1.0));
                    for (int px = left; px < right; ++px)
                    {
                        const size_t from = soundIn + static_cast<size_t>((px - x0) * perPixel);
                        if (from >= sound->frames())
                            break;
                        const size_t to = std::min(sound->frames(), soundIn + static_cast<size_t>((px - x0 + 1) * perPixel));
                        // The loudest sample in the column, looking at no
                        // more than about 64 of them.
                        const size_t step = std::max<size_t>(1, (to - from) / 64);
                        float peak = 0.0f;
                        for (size_t s = from; s < to; s += step)
                            peak = std::max({peak, std::fabs(sound->samples[s * 2]), std::fabs(sound->samples[s * 2 + 1])});
                        const int extent = static_cast<int>(std::lround(std::min(1.0f, peak) * half));
                        if (extent > 0)
                            painter.drawLine(px, centerY - extent, px, centerY + extent);
                    }
                    painter.setRenderHint(QPainter::Antialiasing, true);
                }

                const double midY = y + frameHeight / 2.0;
                const double dotX = x0 + frameWidth / 2.0;

                // Frame view thumbnails: the keyframe's art along its span.
                if (_view.preview && hasContent && x1 - x0 > 8)
                {
                    const QRect room(x0 + 1, y + 1, x1 - x0 - 2, frameHeight - 2);
                    const QImage thumbnail = thumbnailOf(layer, frame, room.size());
                    if (!thumbnail.isNull())
                    {
                        painter.save();
                        painter.setClipRect(room);
                        painter.drawImage(QPoint(room.left(), room.top() + (room.height() - thumbnail.height()) / 2), thumbnail);
                        painter.restore();
                    }
                }

                // The keyframe dot: filled when the frame holds something,
                // hollow when it is an empty keyframe. Narrow frames get a
                // smaller one, at the bottom when thumbnails fill the row.
                const double dotRadius = std::min(3.2, frameWidth / 2.0 - 0.5);
                const double dotY = _view.preview ? y + frameHeight - dotRadius - 2.0 : midY;
                painter.setPen(QPen(mark, 1.0));
                painter.setBrush(hasContent ? mark : QColor(255, 255, 255));
                painter.drawEllipse(QPointF(dotX, dotY), dotRadius, dotRadius);

                // A script on the keyframe is marked with an "a" over its dot.
                if (frame->actionScript && !frame->actionScript->code.empty())
                {
                    painter.save();
                    QFont small = painter.font();
                    small.setPointSize(7);
                    small.setBold(true);
                    painter.setFont(small);
                    painter.setPen(mark);
                    painter.drawText(QRectF(x0, y - 1, frameWidth, frameHeight / 2.0), Qt::AlignCenter, "a");
                    painter.restore();
                }

                // A label follows the keyframe along its span: a red flag for a
                // name, green slashes for a comment, a gold anchor for an
                // anchor, then the text where there is room.
                if (!frame->name.empty())
                {
                    painter.save();
                    painter.setClipRect(span);
                    const double gx = duration > 1 ? x0 + frameWidth + 1.0 : x0 + 2.0;
                    const double top = y + 3.0;
                    QColor glyphColor(210, 40, 40);
                    QString text = QString::fromStdString(frame->name);
                    if (frame->labelType == "comment")
                    {
                        glyphColor = QColor(40, 150, 60);
                        painter.setPen(QPen(glyphColor, 1.4));
                        painter.drawLine(QPointF(gx + 3.0, top), QPointF(gx, top + 8.0));
                        painter.drawLine(QPointF(gx + 6.0, top), QPointF(gx + 3.0, top + 8.0));
                    }
                    else if (frame->labelType == "anchor")
                    {
                        glyphColor = QColor(200, 150, 20);
                        painter.setPen(QPen(glyphColor, 1.4));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawEllipse(QPointF(gx + 3.0, top + 1.0), 1.5, 1.5);
                        painter.drawLine(QPointF(gx + 3.0, top + 2.5), QPointF(gx + 3.0, top + 9.0));
                        painter.drawArc(QRectF(gx, top + 4.0, 6.0, 5.0), 180 * 16, 180 * 16);
                    }
                    else
                    {
                        painter.setPen(QPen(mark, 1.0));
                        painter.drawLine(QPointF(gx + 0.5, top), QPointF(gx + 0.5, top + 10.0));
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(glyphColor);
                        painter.drawPolygon(QPolygonF({QPointF(gx + 1.0, top), QPointF(gx + 7.0, top + 2.5),
                            QPointF(gx + 1.0, top + 5.0)}));
                    }
                    if (duration > 1)
                    {
                        QFont labelFont = painter.font();
                        labelFont.setPointSize(8);
                        labelFont.setBold(false);
                        painter.setFont(labelFont);
                        painter.setPen(mark);
                        painter.drawText(QRectF(gx + 9.0, y, x1 - gx - 9.0, frameHeight),
                            Qt::AlignLeft | Qt::AlignVCenter, text);
                    }
                    painter.restore();
                }

                if (motionObject || pose)
                {
                    // A motion tween or a pose span has no arrow; its property
                    // keyframes are small diamonds along it, and its last frame a
                    // hollow one.
                    painter.setPen(QPen(mark, 1.0));
                    painter.setBrush(mark);
                    const auto diamond = [&](double cx, bool filled) {
                        painter.setBrush(filled ? mark : QColor(Qt::transparent));
                        painter.drawPolygon(QPolygonF({QPointF(cx, midY - 3.5), QPointF(cx + 3.5, midY),
                            QPointF(cx, midY + 3.5), QPointF(cx - 3.5, midY)}));
                    };
                    if (motionObject)
                    {
                        for (const int at : motionKeysOf(frame))
                            diamond((frame->index + at) * cellStride + frameWidth / 2.0, true);
                    }
                    if (duration > 1)
                    {
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(QRectF(x1 - frameWidth / 2.0 - 2.5, midY - 4.0, 5.0, 8.0));
                    }
                }
                else if (duration > 1 && frame->tweenType != fla::TweenType::None)
                {
                    // A tween runs an arrow from the keyframe to the end of
                    // its span.
                    painter.setPen(QPen(mark, 1.0));
                    const double arrowStart = dotX + 6.0;
                    const double arrowEnd = x1 - 5.0;
                    painter.drawLine(QPointF(arrowStart, midY), QPointF(arrowEnd, midY));
                    painter.setBrush(mark);
                    painter.drawPolygon(QPolygonF({
                        QPointF(arrowEnd, midY), QPointF(arrowEnd - 5.0, midY - 3.0),
                        QPointF(arrowEnd - 5.0, midY + 3.0)
                    }));
                }
                else if (duration > 1)
                {
                    // A static span ends in a small hollow rectangle.
                    painter.setPen(QPen(mark, 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(QRectF(x1 - frameWidth / 2.0 - 2.5, midY - 4.0, 5.0, 8.0));
                }
            }

            y += layerRowHeight;
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, false);

    // The selected frames, tinted.
    if (_hasSelection && !_rows.empty())
    {
        const int top = std::min(_anchorRow, _focusRow);
        const int bottom = std::min(std::max(_anchorRow, _focusRow), static_cast<int>(_rows.size()) - 1);
        const int first = std::min(_anchorFrame, _focusFrame);
        const int last = std::max(_anchorFrame, _focusFrame);
        const QRect block(first * cellStride, headerHeight + top * layerRowHeight,
            (last - first + 1) * cellStride - margin, (bottom - top) * layerRowHeight + frameHeight);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(block, faded(110));
        painter.setPen(QPen(playheadColor, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(block.adjusted(0, 0, -1, -1));
    }

    // Frames Ctrl-clicked in, one by one.
    for (const auto& cell : _cells)
    {
        if (cell.first >= static_cast<int>(_rows.size()))
            continue;
        const QRect box(cell.second * cellStride, headerHeight + cell.first * layerRowHeight, frameWidth, frameHeight);
        painter.fillRect(box, faded(110));
        painter.setPen(QPen(playheadColor, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box.adjusted(0, 0, -1, -1));
    }

    // A block being dragged shows where it would land.
    if (_movingBlock && (_blockRows != 0 || _blockFrames != 0) && _hasSelection)
    {
        const int top = std::min(_anchorRow, _focusRow) + _blockRows;
        const int bottom = std::max(_anchorRow, _focusRow) + _blockRows;
        const int first = std::min(_anchorFrame, _focusFrame) + _blockFrames;
        const int last = std::max(_anchorFrame, _focusFrame) + _blockFrames;
        const QRect ghost(first * cellStride, headerHeight + top * layerRowHeight,
            (last - first + 1) * cellStride - margin, (bottom - top) * layerRowHeight + frameHeight);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(playheadColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(ghost.adjusted(1, 1, -1, -1));
    }

    // A span being stretched shows its new length.
    if (_resizingSpan)
    {
        const QRect ghost(_resizeKeyframe * cellStride, headerHeight + _resizeRow * layerRowHeight,
            _resizeDuration * cellStride - margin, frameHeight);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(playheadColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(ghost.adjusted(1, 1, -1, -1));
    }

    // A keyframe being dragged shows where it would land.
    if (_movingKeyframe && _moveTo != _moveFrom)
    {
        const QRect ghost(_moveTo * cellStride, headerHeight + _moveRow * layerRowHeight, frameWidth, frameHeight);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(playheadColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(ghost.adjusted(1, 1, -1, -1));
    }

    // The ruler, pinned to the top of what shows: frames are numbered
    // from one and every fifth is labeled.
    const int rulerY = rulerTop();
    painter.save();
    painter.translate(0, rulerY);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(QRect(0, 0, width(), headerHeight), background);
    painter.setPen(textColor);
    QFont frameFont = painter.font();
    frameFont.setPointSize(8);
    painter.setFont(frameFont);
    if (_view.seconds)
    {
        // Seconds, as far apart as there is room for.
        const double fps = _flaDocument->document->frameRate > 0.0 ? _flaDocument->document->frameRate : 24.0;
        double step = 1.0;
        for (const double candidate : {0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0})
        {
            step = candidate;
            if (candidate * fps * cellStride >= 34.0)
                break;
        }
        for (int tick = 0;; ++tick)
        {
            const double seconds = tick * step;
            const int x = static_cast<int>(std::lround(seconds * fps * cellStride));
            if (x > maxFrames * cellStride)
                break;
            painter.setPen(cellLight.lighter(130));
            painter.drawLine(x, headerHeight - 6, x, headerHeight - 1);
            painter.setPen(textColor);
            painter.drawText(QRect(x + 2, 2, 60, headerHeight - 8), Qt::AlignLeft | Qt::AlignVCenter,
                QString::number(seconds) + "s");
        }
    }
    else
    {
        // Frame numbers from one, every fifth or as far apart as there is
        // room for.
        int step = 5;
        for (const int candidate : {5, 10, 20, 50, 100, 200, 500})
        {
            step = candidate;
            if (candidate * cellStride >= 28)
                break;
        }
        for (int i = 0; i < maxFrames; ++i)
        {
            const int number = i + 1;
            const int x = i * cellStride;
            if (number == 1 || number % step == 0)
            {
                painter.setPen(cellLight.lighter(130));
                painter.drawLine(x, headerHeight - 6, x, headerHeight - 1);
                painter.setPen(textColor);
                painter.drawText(QRect(x, 2, std::max(cellStride * 3, 40), headerHeight - 8),
                    Qt::AlignLeft | Qt::AlignVCenter, QString::number(number));
            }
        }
    }

    // Onion skin markers: brackets in the ruler at either end of the range,
    // with the range shaded between them.
    if (_onionSkin && _player)
    {
        const int current = _player->currentFrame();
        const int first = std::max(0, current - _onionBefore);
        const int last = current + _onionAfter;
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRect range(first * cellStride, 2, (last - first + 1) * cellStride - margin, headerHeight - 12);
        painter.fillRect(range, faded(45));
        painter.setPen(QPen(playheadColor, 2.0));
        const int left = first * cellStride + 1;
        const int right = (last + 1) * cellStride - margin - 1;
        painter.drawLine(left, 2, left, headerHeight - 10);
        painter.drawLine(left, 2, left + 4, 2);
        painter.drawLine(right, 2, right, headerHeight - 10);
        painter.drawLine(right, 2, right - 4, 2);
    }

    // The loop range: a band in the ruler with a handle at either end.
    if (_loopOn)
    {
        painter.setRenderHint(QPainter::Antialiasing, false);
        const int left = _loopFirst * cellStride;
        const int right = (_loopLast + 1) * cellStride - margin;
        const QColor loopColor(150, 150, 150);
        painter.fillRect(QRect(left, headerHeight - 8, right - left, 4), QColor(150, 150, 150, 160));
        painter.fillRect(QRect(left - 1, 2, 3, headerHeight - 4), loopColor);
        painter.fillRect(QRect(right - 2, 2, 3, headerHeight - 4), loopColor);
    }

    if (_player)
    {
        // The playhead: a blue marker in the ruler with a blue line down the
        // frames.
        const int currentFrame = _player->currentFrame();
        const int x = currentFrame * cellStride;

        // A narrow rounded head in the ruler, tapering to a point, over a
        // hairline down through the frames.
        const double center = x + frameWidth / 2.0;
        const double halfWidth = 4.5;
        painter.setPen(QPen(playheadColor, 1.0));
        painter.drawLine(QPointF(center, headerHeight - 1), QPointF(center, height() - rulerY));

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(playheadColor);
        QPainterPath head;
        head.addRoundedRect(QRectF(center - halfWidth, 2, 2 * halfWidth, headerHeight - 10), 2.0, 2.0);
        head.moveTo(center - halfWidth, headerHeight - 9);
        head.lineTo(center + halfWidth, headerHeight - 9);
        head.lineTo(center, headerHeight - 3);
        head.closeSubpath();
        head.setFillRule(Qt::WindingFill);
        painter.drawPath(head);
    }
    painter.restore();
}

void TimelineGrid::setView(const View& view)
{
    _view = view;
    _view.frameWidth = std::clamp(_view.frameWidth, 3, 80);
    _view.rowHeight = std::clamp(_view.rowHeight, 12, 120);
    _thumbnailCache.clear();
    updateSize();
    update();
}

void TimelineGrid::wheelEvent(QWheelEvent* event)
{
    // Ctrl+wheel zooms the frames about the pointer.
    if (!(event->modifiers() & Qt::ControlModifier))
    {
        QWidget::wheelEvent(event);
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    View view = _view;
    view.frameWidth = std::clamp(_view.frameWidth + steps * (_view.frameWidth >= 20 ? 4 : _view.frameWidth >= 8 ? 2 : 1), 3, 80);
    if (view.frameWidth == _view.frameWidth)
        return;
    setView(view);
    emit viewChanged();
    event->accept();
}

QImage TimelineGrid::thumbnailOf(const fla::Layer* layer, const fla::Frame* frame, QSize size) const
{
    if (!_thumbnails || size.isEmpty())
        return QImage();
    const auto key = std::make_pair(frame, size.height() * 4 + _view.preview);
    const auto found = _thumbnailCache.find(key);
    if (found != _thumbnailCache.end())
        return found->second;
    QImage image = _thumbnails(layer, frame->index, size, _view.preview == 2);
    _thumbnailCache[key] = image;
    return image;
}
