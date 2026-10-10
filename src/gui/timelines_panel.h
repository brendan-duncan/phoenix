#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QScrollArea>

#include <vector>
#include <QPushButton>
#include <QTimer>

class Player;

class TimelineGrid : public QWidget
{
    Q_OBJECT
public:
    TimelineGrid(Player* player, QWidget* parent = nullptr);

    void setDocument(const fla::FLADocument* document)
    {
        _flaDocument = document;
    }

    void setPlayer(Player* player) { _player = player; }

    /// Shows one timeline only, as when a symbol is being edited; null shows
    /// the document's.
    void setTimeline(const fla::Timeline* timeline) { _timeline = timeline; }

    void updateSize();

public slots:
    void onPlayerFrameChanged(int frame) { update(); }

signals:
    /// A frame cell was pressed on this layer, which makes the layer
    /// current as well as moving the playhead.
    void layerPressed(const fla::Timeline* timeline, int layerIndex);

protected:
    void paintEvent(QPaintEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    const fla::FLADocument* _flaDocument;
    Player* _player;
    bool _isDragging = false;
    const fla::Timeline* _timeline = nullptr;

    std::vector<const fla::Timeline*> shownTimelines() const;

    void updatePlayerFromMouse(int x);
};

class TimelinesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TimelinesPanel(Player* player, QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    void setTimeline(const fla::Timeline* timeline);

signals:
    void layerPressed(const fla::Timeline* timeline, int layerIndex);

private slots:
    void onGotoFirstFrame();

    void onStepBack();

    void onPlayStop();

    void onStepForward();

    void onTimerTick();

    void onPlayerFrameChanged(int frame);

private:
    const fla::FLADocument* _flaDocument;
    QScrollArea* _scrollArea;
    QWidget* _header;
    TimelineGrid* _gridContainer;
    Player* _player;
    QPushButton* _playButton;
    QTimer* _playTimer;
    bool _isPlaying = false;
};
