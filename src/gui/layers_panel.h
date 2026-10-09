#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QIcon>

/// The layer list beside the timeline, laid out the way Animate's is: the
/// layer name, then an eye column, a lock column and the layer's outline
/// colour. Clicking a name makes that layer the current one, which is where
/// new drawings go.
class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

signals:
    void layerVisibilityChanged(const QString& timelineName, int layerIndex, bool visible);

    /// Locking changes what can be picked and drawn on, so the stage hears
    /// about it the same way it hears about visibility.
    void layerLockChanged(const QString& timelineName, int layerIndex, bool locked);

    /// The current layer changed, so the stage draws into a different frame.
    void currentLayerChanged(const QString& timelineName, int layerIndex);

private:
    enum Column
    {
        NameColumn = 0,
        VisibleColumn,
        LockColumn,
        ColourColumn,
        ColumnCount
    };

    const fla::FLADocument* _flaDocument;
    QTreeWidget* _treeWidget;
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
