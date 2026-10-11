#pragma once

#include "../data/fla_document.h"
#include "timelines_panel.h"

#include <QWidget>
#include <QSplitter>

class Player;
class LayersPanel;
class TimelinesPanel;

class TimelineView : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineView(Player* player, QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    /// Follows the view's edit scope: one symbol's timeline, or null for
    /// the document's.
    void setTimeline(fla::Timeline* timeline);

    /// Rebuilds the layer list after layers were added, removed or renamed.
    void refresh();

    /// Rebuilds the layer list only if it no longer matches the document.
    void refreshIfStale();

    /// The current layer of the shown timeline. False when there is none.
    bool currentLayer(fla::Timeline*& timeline, int& layerIndex) const;

    /// The frames selected in the grid, or an empty selection.
    FrameSelection frameSelection() const;
    void clearFrameSelection();

    /// Redraws the grid after frames were edited.
    void refreshFrames();

    void setOnionSkin(bool on, int before, int after);

    /// Lets the frames draw the waveforms of their sounds.
    void setSoundProvider(std::function<std::shared_ptr<const PcmBuffer>(const fla::Frame*, size_t& in)> provider);

signals:
    void layerVisibilityChanged(const QString& timelineName, int layerIndex, bool visible);

    void layerLockChanged(const QString& timelineName, int layerIndex, bool locked);

    void currentLayerChanged(const QString& timelineName, int layerIndex);

    /// Outline view or a header toggle changed what the stage shows.
    void layersChanged();
    void addLayerRequested(bool folder);
    void deleteLayerRequested();
    void layerRenamed(const fla::Timeline* timeline, int layerIndex, const QString& name);
    void keyframeMoveRequested(const fla::Layer* layer, int from, int to);
    void framesContextMenuRequested(const QPoint& globalPos);
    void onionSkinToggled(bool on);

private:
    const fla::FLADocument* _flaDocument;
    QSplitter* _splitter;
    LayersPanel* _layersPanel;
    TimelinesPanel* _timelinesPanel;
    /// Set while one side's scroll is being copied to the other.
    bool _syncingScroll = false;
    Player* _player;
};
