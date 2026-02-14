#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QIcon>

class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

private:
    const fla::FLADocument* _flaDocument;
    QTreeWidget* _treeWidget;
    QIcon _layerIcon;
    QIcon _folderIcon;
    QIcon _openFolderIcon;

    void refreshLayers();

    QIcon createLayerIcon();

    QIcon createFolderIcon();

    QIcon createOpenFolderIcon();

private slots:
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);
};
