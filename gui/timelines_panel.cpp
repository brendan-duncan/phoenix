#include "timelines_panel.h"
#include "player.h"
#include <map>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QIcon>
#include <QPixmap>
#include <QImage>

static const int MAX_FRAMES = 600;

enum class TransportIcon { First, Prev, Play, Stop, Next };

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

    _gridContainer = new TimelineGrid(_player, this);
    _scrollArea->setWidget(_gridContainer);

    connect(_player, &Player::currentFrameChanged, _gridContainer, &TimelineGrid::onPlayerFrameChanged);
    connect(_player, &Player::currentFrameChanged, this, &TimelinesPanel::onPlayerFrameChanged);
    connect(_playTimer, &QTimer::timeout, this, &TimelinesPanel::onTimerTick);
}

void TimelinesPanel::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    _gridContainer->setDocument(document);
    _gridContainer->updateSize();
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
    const int frameHeight = 20;
    const int layerRowHeight = 30;
    const int headerHeight = 25;

    int numRows = 0;
    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        numRows += static_cast<int>(timeline->layers.size());
    }

    int totalWidth = headerHeight + MAX_FRAMES * (frameWidth + 2);
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

    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
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
    const int frameHeight = 20;
    const int layerRowHeight = 30;
    const int headerHeight = 25;
    const int margin = 1;

    QColor darkColor(40, 40, 40);
    QColor lightColor(70, 70, 70);
    QColor textColor(135, 135, 135);

    painter.fillRect(event->rect(), QColor(30, 30, 30));

    int y = headerHeight;

    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            std::map<int, const fla::Frame*> frameMap;
            int durationCount = 0;

            for (const fla::Frame* frame : layer->frames)
            {
                frameMap[frame->index] = frame;
            }

            for (int i = 0; i < MAX_FRAMES; ++i)
            {
                int x = i * (frameWidth + margin);
                int yOffset = y;

                QRect frameRect(x, yOffset, frameWidth, frameHeight);

                QColor bgColor;
                if ((i + 1) % 5 == 0)
                    bgColor = lightColor;
                else
                    bgColor = darkColor;

                painter.fillRect(frameRect, bgColor);

                auto frameIt = frameMap.find(i);

                if (frameIt != frameMap.end())
                {
                    const fla::Frame* frame = (*frameIt).second;
                    durationCount = frame->duration;
                    painter.fillRect(frameRect, QColor(60, 110, 60));
                }
                else if (durationCount > 0)
                {
                    durationCount--;
                    painter.fillRect(frameRect, QColor(40, 60, 40));
                }
            }

            y += layerRowHeight;
        }
    }

    if (_player)
    {
        const int playHeadY = 5;
        int currentFrame = _player->currentFrame();
        int playheadX = currentFrame * (frameWidth + margin) + frameWidth / 2;

        painter.setPen(QColor(50, 150, 255));
        painter.drawLine(playheadX, playHeadY, playheadX, height());

        QPolygon triangle;
        triangle.append(QPoint(playheadX - 6, playHeadY));
        triangle.append(QPoint(playheadX + 6, playHeadY));
        triangle.append(QPoint(playheadX, playHeadY + 10));
        painter.setBrush(QColor(50, 150, 255));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(triangle);
    }

    painter.setPen(textColor);
    QFont frameFont = painter.font();
    frameFont.setPointSize(8);
    painter.setFont(frameFont);
    for (int i = 0; i < MAX_FRAMES; i += 5)
    {
        int x = i * (frameWidth + margin);
        QString label = QString::number(i);
        painter.drawText(x + 2, 20, label);
    }
}
