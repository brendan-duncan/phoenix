#include "layers_panel.h"

#include <QVBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QColor>

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _layerIcon(createLayerIcon())
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* header = new QWidget(this);
    header->setAutoFillBackground(true);
    header->setFixedHeight(20);
    layout->addWidget(header);

    _listWidget = new QListWidget(this);    
    layout->addWidget(_listWidget, 1);
}

QIcon LayersPanel::createLayerIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(QFont("Segoe UI Emoji", 10));
    painter.setPen(QColor(150, 150, 50));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "≣");
    return QIcon(pixmap);
}

void LayersPanel::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    refreshLayers();
}

void LayersPanel::refreshLayers()
{
    _listWidget->clear();

    if (!_flaDocument || !_flaDocument->document)
        return;

    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            QString layerName = QString::fromStdString(layer->name);
            if (layerName.isEmpty())
                layerName = QString("Layer %1").arg(_listWidget->count() + 1);

            if (!layer->visible)
                layerName += " (Hidden)";

            QListWidgetItem* item = new QListWidgetItem(layerName);
            item->setIcon(_layerIcon);
            _listWidget->addItem(item);
        }
    }
}
