#include "timelines_panel.h"
#include "player.h"
#include <map>

#include <QVBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>

TimelineGrid::TimelineGrid(Player* player, QWidget* parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
{
}

void TimelineGrid::updatePlayerFromMouse(int x)
{
    if (!_player || !_flaDocument || !_flaDocument->document)
        return;

    const int frameWidth = 20;
    const int margin = 1;
    const int headerHeight = 25;

    int frameX = x - headerHeight;
    if (frameX < 0)
        frameX = 0;

    int frame = frameX / (frameWidth + margin);
    if (frame < 0)
        frame = 0;

    int maxFrame = 600;
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
            if (layerEndFrame > maxFrame)
                maxFrame = layerEndFrame;
        }
    }

    if (frame >= maxFrame)
        frame = maxFrame > 0 ? maxFrame - 1 : 0;

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

    _gridContainer = new TimelineGrid(_player, this);
    _scrollArea->setWidget(_gridContainer);

    connect(_player, &Player::currentFrameChanged, _gridContainer, &TimelineGrid::onPlayerFrameChanged);

    layout->addWidget(_scrollArea);
}

void TimelinesPanel::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    _gridContainer->setDocument(document);
    refreshTimelines();
}

void TimelinesPanel::refreshTimelines()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        _gridContainer->setMinimumSize(0, 0);
        return;
    }

    const int frameWidth = 20;
    const int frameHeight = 20;
    const int framesPerRow = 30;
    const int layerRowHeight = 30;
    const int headerHeight = 25;

    int maxFrames = 600;

    int numRows = 0;
    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        numRows += static_cast<int>(timeline->layers.size());
    }

    int totalWidth = headerHeight + maxFrames * (frameWidth + 2);
    int totalHeight = headerHeight + numRows * layerRowHeight;

    _gridContainer->setMinimumSize(totalWidth, totalHeight);
    _gridContainer->update();
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
    QColor textColor(200, 200, 200);

    painter.fillRect(event->rect(), QColor(30, 30, 30));

    painter.setPen(textColor);
    QFont frameFont = painter.font();
    frameFont.setPointSize(8);
    painter.setFont(frameFont);

    int maxFrame = 600;
    /*for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            for (const fla::Frame* f : layer->frames)
            {
                int endFrame = f->index + f->duration;
                if (endFrame > maxFrame)
                    maxFrame = endFrame;
            }
        }
    }*/

    for (int i = 0; i < maxFrame; i += 5)
    {
        int x = headerHeight + i * (frameWidth + margin);
        QString label = QString::number(i);
        painter.drawText(x + 2, 12, label);
    }

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

            int layerMaxFrame = 600;
            /*int layerMaxFrame = 0;
            for (const fla::Frame* f : layer->frames)
            {
                int endFrame = f->index + f->duration;
                if (endFrame > layerMaxFrame)
                    layerMaxFrame = endFrame;
            }*/

            for (int i = 0; i < layerMaxFrame; ++i)
            {
                int x = headerHeight + i * (frameWidth + margin);
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
        const int playHeadY = 15;
        int currentFrame = _player->currentFrame();
        int playheadX = headerHeight + currentFrame * (frameWidth + margin) + frameWidth / 2;
        
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
}
