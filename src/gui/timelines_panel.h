#pragma once

#include "../data/fla_document.h"
#include "layers_panel.h"

#include <QWidget>
#include <QScrollArea>

#include <vector>
#include <QPushButton>
#include <QTimer>

class Player;

/// A block of selected frames in the timeline: the layers of the rows it
/// spans, top to bottom, and the frames from \a first to \a last.
struct FrameSelection
{
    std::vector<const fla::Layer*> layers;
    int first = 0;
    int last = 0;
    bool isEmpty() const { return layers.empty(); }
};

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

    /// The selected frames, or an empty selection.
    FrameSelection selection() const;
    void clearSelection();

public slots:
    void onPlayerFrameChanged(int frame) { update(); }

signals:
    /// A frame cell was pressed on this layer, which makes the layer
    /// current as well as moving the playhead.
    void layerPressed(const fla::Timeline* timeline, int layerIndex);

    /// A keyframe was dragged from one frame to another.
    void keyframeMoveRequested(const fla::Layer* layer, int from, int to);

    /// The frames were right-clicked; the menu goes at \a globalPos.
    void contextMenuRequested(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    const fla::FLADocument* _flaDocument;
    Player* _player;
    bool _isDragging = false;
    const fla::Timeline* _timeline = nullptr;
    std::vector<LayerRow> _rows;

    /// The selected block, in rows and frames, while there is one.
    bool _hasSelection = false;
    int _anchorRow = 0;
    int _anchorFrame = 0;
    int _focusRow = 0;
    int _focusFrame = 0;
    bool _selecting = false;

    /// A keyframe being dragged: its row, where it started and where it is.
    bool _movingKeyframe = false;
    int _moveRow = 0;
    int _moveFrom = 0;
    int _moveTo = 0;

    int rowAt(int y) const;
    int frameAt(int x) const;

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

    FrameSelection selection() const { return _gridContainer->selection(); }
    void clearSelection() { _gridContainer->clearSelection(); }

    /// Redraws the grid after frames were edited.
    void refreshFrames();

signals:
    void layerPressed(const fla::Timeline* timeline, int layerIndex);
    void keyframeMoveRequested(const fla::Layer* layer, int from, int to);
    void contextMenuRequested(const QPoint& globalPos);

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
