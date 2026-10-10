#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QIcon>

#include <vector>

/// The layer list beside the timeline, laid out the way Animate's is: the
/// layer name, then an eye column, a lock column and the layer's outline
/// color. Clicking a name makes that layer the current one, which is where
/// new drawings go.
/// One row of the layers list as shown: which layer of which timeline.
/// The timeline grid draws exactly these rows, so a collapsed folder hides
/// its layers' lanes and every label lines up with its track.
struct LayerRow
{
    const fla::Timeline* timeline = nullptr;
    int layerIndex = -1;
    const fla::Layer* layer = nullptr;
};

class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    /// Shows one timeline's layers only, as when a symbol is being edited;
    /// null shows the document's.
    void setTimeline(fla::Timeline* timeline);

    /// Makes \a layerIndex of \a timeline the current layer, as a click on
    /// its name would, and says so.
    void setCurrentLayer(const fla::Timeline* timeline, int layerIndex);

    /// The rows on show, top to bottom: the layers inside a collapsed
    /// folder are left out.
    std::vector<LayerRow> visibleRows() const;

signals:
    void layerVisibilityChanged(const QString& timelineName, int layerIndex, bool visible);

    /// Locking changes what can be picked and drawn on, so the stage hears
    /// about it the same way it hears about visibility.
    void layerLockChanged(const QString& timelineName, int layerIndex, bool locked);

    /// The current layer changed, so the stage draws into a different frame.
    void currentLayerChanged(const QString& timelineName, int layerIndex);

    /// The rows on show changed: a folder opened or closed, or the list was
    /// rebuilt.
    void rowsChanged();

private:
    enum Column
    {
        NameColumn = 0,
        VisibleColumn,
        LockColumn,
        ColorColumn,
        ColumnCount
    };

    const fla::FLADocument* _flaDocument;
    fla::Timeline* _timeline = nullptr;
    QTreeWidget* _treeWidget;

    std::vector<fla::Timeline*> shownTimelines() const;
    QIcon _layerIcon;
    QIcon _folderIcon;
    QIcon _openFolderIcon;
    QIcon _visibleIcon;
    QIcon _hiddenIcon;
    QIcon _unlockedIcon;
    QIcon _lockedIcon;

    void refreshLayers();

    QIcon createLayerIcon();

    QIcon createFolderIcon();

    QIcon createOpenFolderIcon();

    /// The layer an item stands for, or null.
    fla::Layer* layerFor(const QTreeWidgetItem* item) const;

private slots:
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);
    void onItemClicked(QTreeWidgetItem* item, int column);
};
