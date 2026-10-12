#pragma once

#include "../data/fla_document.h"
#include "layers_panel.h"

#include <QWidget>
#include <QScrollArea>

#include "audio_engine.h"

#include <algorithm>
#include <functional>
#include <map>
#include <vector>
#include <QPushButton>
#include <QTimer>
#include <QImage>

class Player;

/// A block of selected frames in the timeline: the layers of the rows it
/// spans, top to bottom, and the frames from \a first to \a last.
struct FrameSelection
{
    std::vector<const fla::Layer*> layers;
    int first = 0;
    int last = 0;
    /// Frames added one at a time with Ctrl-click, beside the block.
    std::vector<std::pair<const fla::Layer*, int>> cells;
    bool isEmpty() const { return layers.empty() && cells.empty(); }
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

    /// Every frame of every layer shown (Select All Frames).
    void selectAll();

    /// How many frames the shown timeline has: where its last frame ends.
    int frameCount() const;
    void clearSelection();

    /// Shows the onion skin range in the ruler.
    void setOnionSkin(bool on, int before, int after);

    /// The loop range playback keeps to while it is on, shown in the ruler
    /// with handles to drag its ends by.
    void setLoop(bool on, int first, int last);
    bool loopOn() const { return _loopOn; }
    int loopFirst() const { return _loopFirst; }
    int loopLast() const { return _loopLast; }

    /// Frame view: how wide a frame's cell is (zoom), how tall a row, the
    /// ruler counting seconds rather than frames, and keyframe thumbnails
    /// (0 none, 1 each keyframe's art, 2 the whole stage).
    struct View
    {
        int frameWidth = 20;
        int rowHeight = 22;
        bool seconds = false;
        int preview = 0;
        /// A click selects the whole span it lands in, as a unit to drag.
        bool spanSelection = false;
    };
    void setView(const View& view);
    const View& view() const { return _view; }
    /// A frame's left edge.
    int cellStride() const { return _view.frameWidth + 1; }

    /// Draws a layer's keyframe for the thumbnails: the art alone or in
    /// context, at a size.
    using ThumbnailProvider = std::function<QImage(const fla::Layer*, int frame, QSize size, bool inContext)>;
    void setThumbnailProvider(ThumbnailProvider provider) { _thumbnails = std::move(provider); }
    /// Forgets the thumbnails drawn, after an edit.
    void clearThumbnails() { _thumbnailCache.clear(); }

    /// Where the grid finds a frame's sound and the point in it the frame
    /// starts from, to draw its waveform along the span.
    using SoundProvider = std::function<std::shared_ptr<const PcmBuffer>(const fla::Frame*, size_t& in)>;
    void setSoundProvider(SoundProvider provider) { _soundProvider = std::move(provider); update(); }

public slots:
    void onPlayerFrameChanged(int frame) { update(); }

signals:
    /// Ctrl+wheel zoomed the frames.
    void viewChanged();
    /// A frame cell was pressed on this layer, which makes the layer
    /// current as well as moving the playhead.
    void layerPressed(const fla::Timeline* timeline, int layerIndex);

    /// A keyframe was dragged from one frame to another.
    /// A single frame was clicked: what is on it is the stage's selection.
    void framePicked(const fla::Layer* layer, int frame);
    void keyframeMoveRequested(const fla::Layer* layer, int from, int to);
    /// A library sound was dropped on a frame.
    void soundDropped(const fla::Layer* layer, int frame, const QString& name);

    /// The selected block of frames was dragged: frames \a first to \a last
    /// of each source layer go to frame \a to of its target, copied with
    /// Alt held.
    void framesMoveRequested(const std::vector<const fla::Layer*>& sources, const std::vector<const fla::Layer*>& targets, int first, int last, int to, bool copy);

    /// The frames were right-clicked; the menu goes at \a globalPos.
    void contextMenuRequested(const QPoint& globalPos);
    /// The span starting at \a keyframe was stretched or shortened to
    /// \a duration frames by Ctrl-dragging its end.
    void spanResizeRequested(const fla::Layer* layer, int keyframe, int duration);
    /// The onion skin's range was dragged by its markers in the ruler.
    void onionRangeChanged(int before, int after);

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    View _view;
    ThumbnailProvider _thumbnails;
    mutable std::map<std::pair<const fla::Frame*, int>, QImage> _thumbnailCache;
    /// A keyframe's thumbnail, drawn once and kept.
    QImage thumbnailOf(const fla::Layer* layer, const fla::Frame* frame, QSize size) const;
    const fla::FLADocument* _flaDocument;
    Player* _player;
    bool _isDragging = false;
    const fla::Timeline* _timeline = nullptr;
    std::vector<LayerRow> _rows;

    SoundProvider _soundProvider;

    bool _onionSkin = false;
    int _onionBefore = 2;
    int _onionAfter = 2;

    bool _loopOn = false;
    int _loopFirst = 0;
    int _loopLast = 0;
    /// The end of the loop range being dragged: 1 its start, 2 its end.
    int _draggingLoop = 0;
    /// The onion skin marker being dragged: 1 the one before, 2 after.
    int _draggingOnion = 0;

    /// The frames a motion tween span keys, from its motion, kept by the
    /// span and the motion they were read from.
    struct MotionKeys
    {
        size_t motionHash = 0;
        int duration = 0;
        std::vector<int> frames;
    };
    mutable std::map<const fla::Frame*, MotionKeys> _motionKeys;
    const std::vector<int>& motionKeysOf(const fla::Frame* frame) const;

    /// The selected block, in rows and frames, while there is one.
    bool _hasSelection = false;
    /// Single frames Ctrl-clicked into the selection, as rows and frames.
    std::vector<std::pair<int, int>> _cells;
    void toggleCell(int row, int frame);
    int _anchorRow = 0;
    int _anchorFrame = 0;
    int _focusRow = 0;
    int _focusFrame = 0;
    bool _selecting = false;
    /// The press was a plain click that may pick a frame's contents.
    bool _picked = false;

    /// A span's end being dragged: its row, its keyframe and its length.
    bool _resizingSpan = false;
    int _resizeRow = 0;
    int _resizeKeyframe = 0;
    int _resizeDuration = 0;

    /// A keyframe being dragged: its row, where it started and where it is.
    bool _movingKeyframe = false;
    int _moveRow = 0;
    int _moveFrom = 0;
    int _moveTo = 0;

    /// The selected block being dragged: the cell it was picked up by and
    /// how far it has gone, in rows and frames.
    bool _movingBlock = false;
    int _blockRow = 0;
    int _blockFrame = 0;
    int _blockRows = 0;
    int _blockFrames = 0;

    int rowAt(int y) const;
    int frameAt(int x) const;
    /// Lets go of the dragged block: asks for the move, or the copy.
    void dropBlock(bool copy);
    int _bottomPadding = 0;

    std::vector<const fla::Timeline*> shownTimelines() const;

    void updatePlayerFromMouse(int x);
};

class TimelinesPanel : public QWidget
{
    Q_OBJECT
public:
    /// Play or stop (Enter), the first and last frames, a frame either way,
    /// as the Control menu and the transport buttons do them.
    void playStop() { onPlayStop(); }
    void goToFirst() { onGotoFirstFrame(); }
    void goToLast();
    void stepBack() { onStepBack(); }
    void stepForward() { onStepForward(); }

    /// Loop Playback: on, playback keeps to the loop range, starting as the
    /// selected frames or, with one frame or none selected, the whole
    /// timeline.
    void setLoopPlayback(bool on);
    bool loopPlayback() const;

    explicit TimelinesPanel(Player* player, QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    void setTimeline(const fla::Timeline* timeline);

    /// The rows the layers list shows, which the grid draws lane for lane.
    void setRows(const std::vector<LayerRow>& rows);

    FrameSelection selection() const { return _gridContainer->selection(); }
    void clearSelection() { _gridContainer->clearSelection(); }
    void selectAll() { _gridContainer->selectAll(); }

    /// Redraws the grid after frames were edited.
    void refreshFrames();

    /// The scroll bar the rows scroll by, which the layers list follows.
    class QScrollBar* verticalScrollBar() const;

    /// The height the rows are shown in, under the ruler.
    int rowsViewportHeight() const;

    void setBottomPadding(int padding) { _gridContainer->setBottomPadding(padding); }

    /// The frame view, kept between sessions.
    TimelineGrid* grid() const { return _gridContainer; }

signals:
    /// The frame view was changed from its menu or by zooming.
    void frameViewChanged();

public:

    /// Reflects the onion skin state set from elsewhere (the View menu).
    void setOnionSkin(bool on, int before, int after);

    void setSoundProvider(TimelineGrid::SoundProvider provider) { _gridContainer->setSoundProvider(std::move(provider)); }

signals:
    /// The onion skin button was toggled.
    void onionSkinToggled(bool on);

    void layerPressed(const fla::Timeline* timeline, int layerIndex);
    void framePicked(const fla::Layer* layer, int frame);
    void soundDropped(const fla::Layer* layer, int frame, const QString& name);
    void keyframeMoveRequested(const fla::Layer* layer, int from, int to);
    void framesMoveRequested(const std::vector<const fla::Layer*>& sources, const std::vector<const fla::Layer*>& targets, int first, int last, int to, bool copy);
    void contextMenuRequested(const QPoint& globalPos);
    void spanResizeRequested(const fla::Layer* layer, int keyframe, int duration);
    void onionRangeChanged(int before, int after);
    /// The onion skin button was right-clicked: its settings menu goes here.
    void onionMenuRequested(const QPoint& globalPos);

private slots:
    void onGotoFirstFrame();

    void onStepBack();

    void onPlayStop();

    void onStepForward();

    void onTimerTick();

    void onPlayerFrameChanged(int frame);

private:
    /// The header and its icons in the current theme's colors.
    void applyTheme();
    QString _transportStyle;
    const fla::FLADocument* _flaDocument;
    QScrollArea* _scrollArea;
    QWidget* _header;
    TimelineGrid* _gridContainer;
    Player* _player;
    QPushButton* _playButton;
    QPushButton* _onionButton = nullptr;
    QPushButton* _loopButton = nullptr;
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
