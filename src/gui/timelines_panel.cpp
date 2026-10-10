#include "timelines_panel.h"
#include "player.h"

namespace {

/// Animate's timeline rows are 22 px tall, the frame cells filling them
/// but for a one pixel rule. The layers list uses the same height so the
/// two stay in step.
constexpr int kLayerRowHeight = 22;
constexpr int kFrameHeight = 21;

} // namespace
#include <algorithm>
#include <map>

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QIcon>
#include <QPixmap>
#include <QImage>

enum class TransportIcon { First, Prev, Play, Stop, Next };

static int getMaxFrames(const fla::FLADocument* doc)
{
    if (!doc || !doc->document)
        return 0;
    int maxFrame = 0;
    for (const fla::Timeline* timeline : doc->document->timelines)
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            for (const fla::Frame* frame : layer->frames)
            {
                int endFrame = frame->index + frame->duration;
                if (endFrame > maxFrame)
                    maxFrame = endFrame;
            }
        }
    }
    return maxFrame + 10;
}

static QIcon makeTransportIcon(TransportIcon icon, int size = 24)
{
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(0);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);

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
    QPalette headerPalette = _header->palette();
    headerPalette.setColor(QPalette::Window, QColor("#2d2d2d"));
    _header->setPalette(headerPalette);
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
        "QPushButton:hover { background-color: rgba(255,255,255,0.08); border-radius: 4px; }"
        "QPushButton:pressed { background-color: rgba(255,255,255,0.15); border-radius: 4px; }";
    const int btnSize = 28;
    const int iconSize = 22;

    // Frame, rate and time at the left, as Animate's timeline footer has.
    const QString styleStatus = "QLabel { color: rgb(190, 190, 190); padding: 0 6px; }";
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
    gotoFirstBtn->setFixedSize(btnSize, btnSize);
    gotoFirstBtn->setIcon(makeTransportIcon(TransportIcon::First, iconSize));
    gotoFirstBtn->setIconSize(QSize(iconSize, iconSize));
    gotoFirstBtn->setToolTip("Go to first frame");
    gotoFirstBtn->setStyleSheet(styleTransport);
    connect(gotoFirstBtn, &QPushButton::clicked, this, &TimelinesPanel::onGotoFirstFrame);
    headerLayout->addWidget(gotoFirstBtn);

    QPushButton* stepBackBtn = new QPushButton(_header);
    stepBackBtn->setFixedSize(btnSize, btnSize);
    stepBackBtn->setIcon(makeTransportIcon(TransportIcon::Prev, iconSize));
    stepBackBtn->setIconSize(QSize(iconSize, iconSize));
    stepBackBtn->setToolTip("Step back");
    stepBackBtn->setStyleSheet(styleTransport);
    connect(stepBackBtn, &QPushButton::clicked, this, &TimelinesPanel::onStepBack);
    headerLayout->addWidget(stepBackBtn);

    _playButton = new QPushButton(_header);
    _playButton->setFixedSize(btnSize, btnSize);
    _playButton->setIcon(makeTransportIcon(TransportIcon::Play, iconSize));
    _playButton->setIconSize(QSize(iconSize, iconSize));
    _playButton->setToolTip("Play/Stop");
    _playButton->setStyleSheet(styleTransport);
    connect(_playButton, &QPushButton::clicked, this, &TimelinesPanel::onPlayStop);
    headerLayout->addWidget(_playButton);

    QPushButton* stepForwardBtn = new QPushButton(_header);
    stepForwardBtn->setFixedSize(btnSize, btnSize);
    stepForwardBtn->setIcon(makeTransportIcon(TransportIcon::Next, iconSize));
    stepForwardBtn->setIconSize(QSize(iconSize, iconSize));
    stepForwardBtn->setToolTip("Step forward");
    stepForwardBtn->setStyleSheet(styleTransport);
    connect(stepForwardBtn, &QPushButton::clicked, this, &TimelinesPanel::onStepForward);
    headerLayout->addWidget(stepForwardBtn);

    headerLayout->addStretch();
    // Balance the status labels so the transport stays centered.
    QWidget* balance = new QWidget(_header);
    balance->setFixedWidth(3 * 60 + 2 * 2);
    headerLayout->addWidget(balance);

    _gridContainer = new TimelineGrid(_player, this);
    _scrollArea->setWidget(_gridContainer);

    connect(_player, &Player::currentFrameChanged, _gridContainer, &TimelineGrid::onPlayerFrameChanged);
    connect(_player, &Player::currentFrameChanged, this, &TimelinesPanel::onPlayerFrameChanged);
    connect(_gridContainer, &TimelineGrid::layerPressed, this, &TimelinesPanel::layerPressed);
    connect(_playTimer, &QTimer::timeout, this, &TimelinesPanel::onTimerTick);
}

void TimelinesPanel::setDocument(const fla::FLADocument* document)
{
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
        for (const fla::Timeline* timeline : _flaDocument->document->timelines)
            timelines.push_back(timeline);
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

void TimelinesPanel::onPlayStop()
{
    if (_isPlaying)
    {
        _playTimer->stop();
        _playButton->setIcon(makeTransportIcon(TransportIcon::Play, 22));
    }
    else
    {
        _playTimer->start(1000 / 24); // Assuming 24 FPS
        _playButton->setIcon(makeTransportIcon(TransportIcon::Stop, 22));
    }
    _isPlaying = !_isPlaying;
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
    if (_player)
    {
        _player->setCurrentFrame(_player->currentFrame() + 1);
    }
}

void TimelinesPanel::onPlayerFrameChanged(int frame)
{
    updateStatus();
    const int frameWidth = 20;
    const int margin = 1;
    int playheadX = frame * (frameWidth + margin) + frameWidth / 2;
    _scrollArea->ensureVisible(playheadX, 0, 50, 0);
}

TimelineGrid::TimelineGrid(Player* player, QWidget* parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
{
}

void TimelineGrid::updateSize()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        setMinimumSize(0, 0);
        return;
    }

    const int frameWidth = 20;
    const int frameHeight = kFrameHeight;
    const int layerRowHeight = kLayerRowHeight;
    const int headerHeight = 25;

    const int numRows = static_cast<int>(_rows.size());

    int maxFrames = getMaxFrames(_flaDocument);
    int totalWidth = headerHeight + maxFrames * (frameWidth + 2);
    int totalHeight = headerHeight + numRows * layerRowHeight;

    setMinimumSize(totalWidth, totalHeight);
}

void TimelineGrid::updatePlayerFromMouse(int x)
{
    if (!_player || !_flaDocument || !_flaDocument->document)
        return;

    const int frameWidth = 20;
    const int margin = 1;

    int frameX = x;
    if (frameX < 0)
        frameX = 0;

    int frame = frameX / (frameWidth + margin);
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

void TimelineGrid::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        _isDragging = true;
        updatePlayerFromMouse(event->pos().x());

        // The row under the press is a layer, which becomes current as it
        // does in Animate. Rows are the layers list's, lane for lane.
        const int headerHeight = 25;
        const int layerRowHeight = kLayerRowHeight;
        const int row = (event->pos().y() - headerHeight) / layerRowHeight;
        if (event->pos().y() >= headerHeight && row >= 0 && row < static_cast<int>(_rows.size()))
            emit layerPressed(_rows[row].timeline, _rows[row].layerIndex);
    }
    QWidget::mousePressEvent(event);
}

void TimelineGrid::mouseMoveEvent(QMouseEvent* event)
{
    if (_isDragging)
    {
        updatePlayerFromMouse(event->pos().x());
    }
    QWidget::mouseMoveEvent(event);
}

void TimelineGrid::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        _isDragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineGrid::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    if (!_flaDocument || !_flaDocument->document)
        return;

    const int frameWidth = 20;
    const int frameHeight = kFrameHeight;
    const int layerRowHeight = kLayerRowHeight;
    const int headerHeight = 25;
    const int margin = 1;
    const int cellStride = frameWidth + margin;

    // Animate's frame colors: an empty keyframe span is white, a span with
    // content gray, a classic tween lilac and a shape tween green.
    const QColor background(30, 30, 30);
    const QColor cellDark(40, 40, 40);
    const QColor cellLight(55, 55, 55);
    const QColor textColor(170, 170, 170);
    const QColor emptySpan(236, 236, 236);
    const QColor staticSpan(196, 196, 196);
    const QColor motionSpan(197, 183, 232);
    const QColor shapeSpan(186, 224, 180);
    // Animate's time head is blue.
    const QColor playheadColor(41, 128, 238);

    painter.fillRect(event->rect(), background);

    const int maxFrames = getMaxFrames(_flaDocument);
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
                painter.fillRect(cell, (i + 1) % 5 == 0 ? cellLight : cellDark);
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
                if (frame->tweenType == fla::TweenType::Motion)
                    spanColor = motionSpan;
                else if (frame->tweenType == fla::TweenType::Shape)
                    spanColor = shapeSpan;
                painter.fillRect(span, spanColor);

                const double midY = y + frameHeight / 2.0;
                const double dotX = x0 + frameWidth / 2.0;

                // The keyframe dot: filled when the frame holds something,
                // hollow when it is an empty keyframe.
                painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                painter.setBrush(hasContent ? QColor(20, 20, 20) : QColor(255, 255, 255));
                painter.drawEllipse(QPointF(dotX, midY), 3.2, 3.2);

                if (duration > 1 && frame->tweenType != fla::TweenType::None)
                {
                    // A tween runs an arrow from the keyframe to the end of
                    // its span.
                    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                    const double arrowStart = dotX + 6.0;
                    const double arrowEnd = x1 - 5.0;
                    painter.drawLine(QPointF(arrowStart, midY), QPointF(arrowEnd, midY));
                    painter.setBrush(QColor(20, 20, 20));
                    painter.drawPolygon(QPolygonF({
                        QPointF(arrowEnd, midY), QPointF(arrowEnd - 5.0, midY - 3.0),
                        QPointF(arrowEnd - 5.0, midY + 3.0)
                    }));
                }
                else if (duration > 1)
                {
                    // A static span ends in a small hollow rectangle.
                    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(QRectF(x1 - frameWidth / 2.0 - 2.5, midY - 4.0, 5.0, 8.0));
                }
            }

            y += layerRowHeight;
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, false);

    // The ruler: Animate numbers frames from one and labels every fifth.
    painter.setPen(textColor);
    QFont frameFont = painter.font();
    frameFont.setPointSize(8);
    painter.setFont(frameFont);
    for (int i = 0; i < maxFrames; ++i)
    {
        const int number = i + 1;
        const int x = i * cellStride;
        if (number == 1 || number % 5 == 0)
        {
            painter.setPen(QColor(90, 90, 90));
            painter.drawLine(x, headerHeight - 6, x, headerHeight - 1);
            painter.setPen(textColor);
            painter.drawText(QRect(x, 2, cellStride * 3, headerHeight - 8),
                Qt::AlignLeft | Qt::AlignVCenter, QString::number(number));
        }
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
        painter.drawLine(QPointF(center, headerHeight - 1), QPointF(center, height()));

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
}
