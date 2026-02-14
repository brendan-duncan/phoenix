#pragma once

#include "../data/fla_document.h"

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QIcon>

class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(const fla::FLADocument* document);

private:
    const fla::FLADocument* _flaDocument;
    QListWidget* _listWidget;
    QIcon _layerIcon;

    void refreshLayers();
    QIcon createLayerIcon();
};
