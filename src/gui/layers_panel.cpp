#include "layers_panel.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QColor>
#include <QPixmap>
#include <QVBoxLayout>
#include <QIcon>
#include <QSize>
#include <QPalette>

namespace {

/// A drawn glyph, so the panel does not depend on an emoji font.
QIcon glyph(void (*draw)(QPainter&))
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    draw(painter);
    return QIcon(pixmap);
}

void drawEye(QPainter& painter)
{
    painter.setPen(QPen(QColor(220, 220, 220), 1.2));
    painter.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(2.0, 8.0);
    eye.quadTo(8.0, 1.5, 14.0, 8.0);
    eye.quadTo(8.0, 14.5, 2.0, 8.0);
    painter.drawPath(eye);
    painter.setBrush(QColor(220, 220, 220));
    painter.drawEllipse(QPointF(8.0, 8.0), 2.2, 2.2);
}

void drawHiddenCross(QPainter& painter)
{
    // Animate marks a hidden layer with a red cross in the eye column.
    painter.setPen(QPen(QColor(230, 70, 70), 2.0));
    painter.drawLine(QPointF(4.0, 4.0), QPointF(12.0, 12.0));
    painter.drawLine(QPointF(12.0, 4.0), QPointF(4.0, 12.0));
}

void drawUnlockedDot(QPainter& painter)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(150, 150, 150));
    painter.drawEllipse(QPointF(8.0, 8.0), 1.8, 1.8);
}

void drawPadlock(QPainter& painter)
{
    painter.setPen(QPen(QColor(220, 220, 220), 1.4));
    painter.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    shackle.moveTo(5.0, 8.0);
    shackle.lineTo(5.0, 5.5);
    shackle.quadTo(5.0, 2.5, 8.0, 2.5);
    shackle.quadTo(11.0, 2.5, 11.0, 5.5);
    shackle.lineTo(11.0, 8.0);
    painter.drawPath(shackle);
    painter.setBrush(QColor(220, 220, 220));
    painter.drawRoundedRect(QRectF(3.5, 7.5, 9.0, 6.5), 1.0, 1.0);
}

/// The layer's outline colour as a small swatch.
QIcon swatchIcon(const uint8_t rgba[4])
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QPen(QColor(20, 20, 20), 1.0));
    painter.setBrush(QColor(rgba[0], rgba[1], rgba[2]));
    painter.drawRect(QRectF(3.5, 3.5, 9.0, 9.0));
    return QIcon(pixmap);
}

} // namespace

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _layerIcon(createLayerIcon())
    , _folderIcon(createFolderIcon())
    , _openFolderIcon(createOpenFolderIcon())
    , _visibleIcon(glyph(drawEye))
    , _hiddenIcon(glyph(drawHiddenCross))
    , _unlockedIcon(glyph(drawUnlockedDot))
    , _lockedIcon(glyph(drawPadlock))
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
    _treeWidget->setColumnCount(ColumnCount);
    _treeWidget->header()->setStretchLastSection(false);
    _treeWidget->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    for (int column = VisibleColumn; column < ColumnCount; ++column)
    {
        _treeWidget->header()->setSectionResizeMode(column, QHeaderView::Fixed);
        _treeWidget->header()->resizeSection(column, 24);
    }
    _treeWidget->header()->setHidden(true);
    _treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(_treeWidget, 1);

    connect(_treeWidget, &QTreeWidget::itemExpanded, this, &LayersPanel::onItemExpanded);
    connect(_treeWidget, &QTreeWidget::itemCollapsed, this, &LayersPanel::onItemCollapsed);
    connect(_treeWidget, &QTreeWidget::itemClicked, this, &LayersPanel::onItemClicked);
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
    _timeline = nullptr;
    refreshLayers();
}

void LayersPanel::setTimeline(fla::Timeline* timeline)
{
    _timeline = timeline;
    refreshLayers();
}

std::vector<fla::Timeline*> LayersPanel::shownTimelines() const
{
    std::vector<fla::Timeline*> timelines;
    if (_timeline)
    {
        timelines.push_back(_timeline);
        return timelines;
    }
    if (_flaDocument && _flaDocument->document)
    {
        for (fla::Timeline* timeline : _flaDocument->document->timelines)
            timelines.push_back(timeline);
    }
    return timelines;
}

fla::Layer* LayersPanel::layerFor(const QTreeWidgetItem* item) const
{
    if (!item || !_flaDocument || !_flaDocument->document)
        return nullptr;

    const int timelineIndex = item->data(NameColumn, Qt::UserRole + 1).toInt();
    const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();

    const auto& timelines = shownTimelines();
    if (timelineIndex < 0 || timelineIndex >= static_cast<int>(timelines.size()))
        return nullptr;

    const fla::Timeline* timeline = timelines[timelineIndex];
    if (!timeline || layerIndex < 0 || layerIndex >= static_cast<int>(timeline->layers.size()))
        return nullptr;

    return timeline->layers[layerIndex];
}

void LayersPanel::refreshLayers()
{
    _treeWidget->clear();

    if (!_flaDocument || !_flaDocument->document)
        return;

    int timelineIndex = 0;
    for (const fla::Timeline* timeline : shownTimelines())
    {
        QList<QTreeWidgetItem*> layerIndexMap;
        QTreeWidgetItem* currentItem = nullptr;

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

            QTreeWidgetItem* item = new QTreeWidgetItem();
            item->setSizeHint(0, QSize(100, 30));

            layerIndexMap.push_back(item);

            item->setText(NameColumn, layerName);
            item->setExpanded(true);
            switch (layer->layerType)
            {
                case fla::Layer::Type::Normal:
                    item->setIcon(NameColumn, _layerIcon);
                    break;
                case fla::Layer::Type::Folder:
                    item->setIcon(NameColumn, _openFolderIcon);
                    break;
                case fla::Layer::Type::Mask:
                    item->setIcon(NameColumn, _layerIcon);
                    break;
                case fla::Layer::Type::Masked:
                    item->setIcon(NameColumn, _layerIcon);
                    break;
                case fla::Layer::Type::Guide:
                    item->setIcon(NameColumn, _layerIcon);
                    break;
            }

            item->setIcon(VisibleColumn, layer->visible ? _visibleIcon : _hiddenIcon);
            item->setIcon(LockColumn, layer->locked ? _lockedIcon : _unlockedIcon);
            item->setIcon(ColourColumn, swatchIcon(layer->color));
            item->setToolTip(VisibleColumn, "Show or hide the layer");
            item->setToolTip(LockColumn, "Lock or unlock the layer");
            item->setToolTip(ColourColumn, "Outline colour; selections on this layer are highlighted in it");

            item->setData(NameColumn, Qt::UserRole, QVariant::fromValue(layerIndexMap.size() - 1));
            item->setData(NameColumn, Qt::UserRole + 1, timelineIndex);

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

            if (layer->selected && !currentItem)
                currentItem = item;
        }

        if (currentItem)
            _treeWidget->setCurrentItem(currentItem);

        ++timelineIndex;
    }
}

void LayersPanel::onItemExpanded(QTreeWidgetItem* item)
{
    item->setIcon(NameColumn, _openFolderIcon);
}

void LayersPanel::onItemCollapsed(QTreeWidgetItem* item)
{
    item->setIcon(NameColumn, _folderIcon);
}

void LayersPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    fla::Layer* layer = layerFor(item);
    if (!layer)
        return;

    const int timelineIndex = item->data(NameColumn, Qt::UserRole + 1).toInt();
    const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();
    fla::Timeline* timeline = shownTimelines()[timelineIndex];
    const QString timelineName = QString::fromStdString(timeline->name);

    switch (column)
    {
    case VisibleColumn:
        layer->visible = !layer->visible;
        item->setIcon(VisibleColumn, layer->visible ? _visibleIcon : _hiddenIcon);
        emit layerVisibilityChanged(timelineName, layerIndex, layer->visible);
        break;

    case LockColumn:
        layer->locked = !layer->locked;
        item->setIcon(LockColumn, layer->locked ? _lockedIcon : _unlockedIcon);
        emit layerLockChanged(timelineName, layerIndex, layer->locked);
        break;

    default:
        // Clicking the name makes this the current layer, the one new drawings
        // go into. Only one layer in a timeline is current.
        for (fla::Layer* other : timeline->layers)
        {
            if (other)
                other->selected = other == layer;
        }
        _treeWidget->setCurrentItem(item);
        emit currentLayerChanged(timelineName, layerIndex);
        break;
    }
}
