#pragma once

#include "../data/fla_document.h"

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

signals:
    void layerVisibilityChanged(const QString& timelineName, int layerIndex, bool visible);

    void layerLockChanged(const QString& timelineName, int layerIndex, bool locked);

    void currentLayerChanged(const QString& timelineName, int layerIndex);

private:
    const fla::FLADocument* _flaDocument;
    QSplitter* _splitter;
    LayersPanel* _layersPanel;
    TimelinesPanel* _timelinesPanel;
    Player* _player;
};
