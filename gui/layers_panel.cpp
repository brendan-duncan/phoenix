#include "layers_panel.h"

#include <QVBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QSize>

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
        QMap<int, QTreeWidgetItem*> folderIndexMap;
        folderIndexMap[0] = nullptr;

        int folderIndex = 1;

        for (const fla::Layer* layer : timeline->layers)
        {
            QString layerName = QString::fromStdString(layer->name);
            if (layerName.isEmpty())
            {
                if (layer->layerType == fla::Layer::Type::Folder)
                {
                    layerName = QString("Folder %1").arg(folderIndex);
                }
                else
                {
                    layerName = "Layer";
                }
            }

            if (!layer->visible)
                layerName += " (Hidden)";

            QTreeWidgetItem* item = new QTreeWidgetItem();
            item->setSizeHint(0, QSize(100, 30));

            if (layer->layerType == fla::Layer::Type::Folder)
            {
                item->setText(0, layerName);
                item->setIcon(0, _openFolderIcon);
                folderIndexMap[folderIndex] = item;
                folderIndex++;

                if (layer->parentLayerIndex == -1)
                {
                    _treeWidget->addTopLevelItem(item);
                }
                else
                {
                    QTreeWidgetItem* parent = folderIndexMap.value(layer->parentLayerIndex, nullptr);
                    if (parent)
                    {
                        parent->addChild(item);
                    }
                    else
                    {
                        _treeWidget->addTopLevelItem(item);
                    }
                }
                item->setExpanded(true);
            }
            else
            {
                item->setText(0, layerName);
                item->setIcon(0, _layerIcon);

                if (layer->parentLayerIndex == -1)
                {
                    _treeWidget->addTopLevelItem(item);
                }
                else
                {
                    QTreeWidgetItem* parent = folderIndexMap.value(layer->parentLayerIndex, nullptr);
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
}

void LayersPanel::onItemExpanded(QTreeWidgetItem* item)
{
    item->setIcon(0, _openFolderIcon);
}

void LayersPanel::onItemCollapsed(QTreeWidgetItem* item)
{
    item->setIcon(0, _folderIcon);
}
