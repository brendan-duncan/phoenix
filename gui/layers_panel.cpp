#include "layers_panel.h"

#include <QVBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QSize>
#include <QPalette>

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _layerIcon(createLayerIcon())
    , _folderIcon(createFolderIcon())
    , _openFolderIcon(createOpenFolderIcon())
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* header = new QWidget(this);
    header->setAutoFillBackground(true);
    header->setFixedHeight(50);
    QPalette headerPalette = header->palette();
    headerPalette.setColor(QPalette::Window, QColor("#2d2d2d"));
    header->setPalette(headerPalette);
    layout->addWidget(header);

    _treeWidget = new QTreeWidget(this);
    _treeWidget->setHeaderHidden(true);
    _treeWidget->setIndentation(15);
    _treeWidget->setUniformRowHeights(true);
    layout->addWidget(_treeWidget, 1);

    connect(_treeWidget, &QTreeWidget::itemExpanded, this, &LayersPanel::onItemExpanded);
    connect(_treeWidget, &QTreeWidget::itemCollapsed, this, &LayersPanel::onItemCollapsed);
}

QIcon LayersPanel::createLayerIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(QFont("Segoe UI Emoji", 10));
    painter.setPen(QColor(150, 150, 50));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "📃");
    return QIcon(pixmap);
}

QIcon LayersPanel::createFolderIcon()
{
    QPixmap pixmap(10, 10);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(QFont("Segoe UI Emoji", 10));
    painter.setPen(QColor(100, 150, 200));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "📁");
    return QIcon(pixmap);
}

QIcon LayersPanel::createOpenFolderIcon()
{
    QPixmap pixmap(10, 10);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(QFont("Segoe UI Emoji", 10));
    painter.setPen(QColor(100, 150, 200));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "📂");
    return QIcon(pixmap);
}

void LayersPanel::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    refreshLayers();
}

void LayersPanel::refreshLayers()
{
    _treeWidget->clear();

    if (!_flaDocument || !_flaDocument->document)
        return;

    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        QList<QTreeWidgetItem*> layerIndexMap;

        for (const fla::Layer* layer : timeline->layers)
        {
            QString layerName = QString::fromStdString(layer->name);
            if (layerName.isEmpty())
            {
                if (layer->layerType == fla::Layer::Type::Folder)
                {
                    layerName = QString("Folder %1").arg(layerIndexMap.size());
                }
                else
                {
                    layerName = QString("Layer %1").arg(layerIndexMap.size());
                }
            }

            if (!layer->visible)
                layerName += " (Hidden)";

            QTreeWidgetItem* item = new QTreeWidgetItem();
            item->setSizeHint(0, QSize(100, 30));

            layerIndexMap.push_back(item);

            item->setText(0, layerName);
            item->setExpanded(true);
            switch (layer->layerType)
            {
                case fla::Layer::Type::Normal:
                    item->setIcon(0, _layerIcon);
                    break;
                case fla::Layer::Type::Folder:
                    item->setIcon(0, _openFolderIcon);
                    break;
                case fla::Layer::Type::Mask:
                    item->setIcon(0, _layerIcon);
                    break;
                case fla::Layer::Type::Masked:
                    item->setIcon(0, _layerIcon);
                    break;
                case fla::Layer::Type::Guide:
                    item->setIcon(0, _layerIcon);
                    break;
            }

            if (layer->parentLayerIndex == -1)
            {
                _treeWidget->addTopLevelItem(item);
            }
            else
            {
                QTreeWidgetItem* parent = layerIndexMap.value(layer->parentLayerIndex, nullptr);
                if (parent)
                {
                    parent->addChild(item);
                }
                else
                {
                    _treeWidget->addTopLevelItem(item);
                }
            }
        }
    }
}

void LayersPanel::onItemExpanded(QTreeWidgetItem* item)
{
    item->setIcon(0, _openFolderIcon);
}

void LayersPanel::onItemCollapsed(QTreeWidgetItem* item)
{
    item->setIcon(0, _folderIcon);
}
