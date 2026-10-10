#pragma once

#include "../data/fla_document.h"
#include "layers_panel.h"

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

    /// The rows to draw, as the layers list shows them.
    void setRows(const std::vector<LayerRow>& rows);

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
    std::vector<LayerRow> _rows;

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

    /// The rows the layers list shows, which the grid draws lane for lane.
    void setRows(const std::vector<LayerRow>& rows);

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
    /// Animate's timeline status: the frame shown, the frame rate and the
    /// time into the animation.
    class QLabel* _frameLabel = nullptr;
    class QLabel* _fpsLabel = nullptr;
    class QLabel* _timeLabel = nullptr;
    void updateStatus();
    QTimer* _playTimer;
    bool _isPlaying = false;
};
