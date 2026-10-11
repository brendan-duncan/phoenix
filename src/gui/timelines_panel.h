#pragma once

#include "../data/fla_document.h"
#include "layers_panel.h"

#include <QWidget>
#include <QScrollArea>

#include "audio_engine.h"

#include <algorithm>
#include <functional>
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

    /// Empty space under the last row, so the grid scrolls exactly as far as
    /// the layers list beside it.
    void setBottomPadding(int padding);

    /// Where the ruler sits: at the top of the visible part of the grid, so
    /// it stays in view however far down the rows are scrolled.
    int rulerTop() const { return std::max(0, -y()); }

    /// The selected frames, or an empty selection.
    FrameSelection selection() const;
    void clearSelection();

    /// Shows the onion skin range in the ruler.
    void setOnionSkin(bool on, int before, int after);

    /// Where the grid finds a frame's sound and the point in it the frame
    /// starts from, to draw its waveform along the span.
    using SoundProvider = std::function<std::shared_ptr<const PcmBuffer>(const fla::Frame*, size_t& in)>;
    void setSoundProvider(SoundProvider provider) { _soundProvider = std::move(provider); update(); }

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

    SoundProvider _soundProvider;

    bool _onionSkin = false;
    int _onionBefore = 2;
    int _onionAfter = 2;

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
    int _bottomPadding = 0;

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

    /// The scroll bar the rows scroll by, which the layers list follows.
    class QScrollBar* verticalScrollBar() const;

    /// The height the rows are shown in, under the ruler.
    int rowsViewportHeight() const;

    void setBottomPadding(int padding) { _gridContainer->setBottomPadding(padding); }

    /// Reflects the onion skin state set from elsewhere (the View menu).
    void setOnionSkin(bool on, int before, int after);

    void setSoundProvider(TimelineGrid::SoundProvider provider) { _gridContainer->setSoundProvider(std::move(provider)); }

signals:
    /// The onion skin button was toggled.
    void onionSkinToggled(bool on);

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
    QPushButton* _onionButton = nullptr;
    /// Animate's timeline status: the frame shown, the frame rate and the
    /// time into the animation.
    class QLabel* _frameLabel = nullptr;
    class QLabel* _fpsLabel = nullptr;
    class QLabel* _timeLabel = nullptr;
    void updateStatus();
    QTimer* _playTimer;
    bool _isPlaying = false;
    /// Playback runs by the clock, so the frame shown is the one that is
    /// due even when a frame takes longer to draw than it lasts, and sound
    /// stays in step: the frame and time it started from.
    class QElapsedTimer* _playClock = nullptr;
    int _playStartFrame = 0;
    double frameRate() const;
    void stopPlaying();
};
