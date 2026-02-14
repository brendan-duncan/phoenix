#include "timelines_panel.h"
#include "player.h"
#include <map>

#include <QVBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>

static const int MAX_FRAMES = 600;

TimelinesPanel::TimelinesPanel(Player* player, QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    QWidget* container = new QWidget();
    _scrollArea->setWidget(container);
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    _header = new QWidget(this);
    _header->setFixedHeight(25);
    containerLayout->addWidget(_header);

    _gridContainer = new TimelineGrid(_player, this);
    containerLayout->addWidget(_gridContainer);

    connect(_player, &Player::currentFrameChanged, _gridContainer, &TimelineGrid::onPlayerFrameChanged);

    layout->addWidget(_scrollArea);
}

void TimelinesPanel::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    _gridContainer->setDocument(document);
    _gridContainer->updateSize();
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
