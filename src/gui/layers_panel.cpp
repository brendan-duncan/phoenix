#include "layers_panel.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSize>
#include <QToolButton>
#include <QScrollBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <cstdint>
#include <functional>

namespace {

/// The layers list, with its viewport margins open to the panel: a bottom
/// margin lets it scroll as far as the frame grid does.
class LayerTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

    void setBottomMargin(int margin) { setViewportMargins(0, 0, 0, margin); }
};

const QColor kInk(220, 220, 220);

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
    painter.setPen(QPen(kInk, 1.2));
    painter.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(2.0, 8.0);
    eye.quadTo(8.0, 1.5, 14.0, 8.0);
    eye.quadTo(8.0, 14.5, 2.0, 8.0);
    painter.drawPath(eye);
    painter.setBrush(kInk);
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
    painter.setPen(QPen(kInk, 1.4));
    painter.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    shackle.moveTo(5.0, 8.0);
    shackle.lineTo(5.0, 5.5);
    shackle.quadTo(5.0, 2.5, 8.0, 2.5);
    shackle.quadTo(11.0, 2.5, 11.0, 5.5);
    shackle.lineTo(11.0, 8.0);
    painter.drawPath(shackle);
    painter.setBrush(kInk);
    painter.drawRoundedRect(QRectF(3.5, 7.5, 9.0, 6.5), 1.0, 1.0);
}

void drawOutlineSquare(QPainter& painter)
{
    painter.setPen(QPen(kInk, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(3.5, 3.5, 9.0, 9.0));
}

void drawPencil(QPainter& painter)
{
    // Animate's current-layer marker: a pencil leaning to the right.
    painter.setPen(QPen(kInk, 1.2));
    painter.setBrush(QColor(kInk.red(), kInk.green(), kInk.blue(), 90));
    QPainterPath body;
    body.moveTo(3.0, 13.0);
    body.lineTo(4.0, 9.5);
    body.lineTo(10.5, 3.0);
    body.lineTo(13.0, 5.5);
    body.lineTo(6.5, 12.0);
    body.closeSubpath();
    painter.drawPath(body);
    painter.drawLine(QPointF(4.0, 9.5), QPointF(6.5, 12.0));
}

void drawLayerSheet(QPainter& painter)
{
    painter.setPen(QPen(kInk, 1.1));
    painter.setBrush(QColor(kInk.red(), kInk.green(), kInk.blue(), 50));
    painter.drawRect(QRectF(3.5, 2.5, 9.0, 11.0));
    painter.setPen(QPen(kInk, 0.9));
    painter.drawLine(QPointF(5.5, 6.0), QPointF(10.5, 6.0));
    painter.drawLine(QPointF(5.5, 8.5), QPointF(10.5, 8.5));
    painter.drawLine(QPointF(5.5, 11.0), QPointF(8.5, 11.0));
}

void drawFolderClosed(QPainter& painter)
{
    painter.setPen(QPen(QColor(230, 200, 100), 1.1));
    painter.setBrush(QColor(230, 200, 100, 120));
    QPainterPath folder;
    folder.moveTo(2.0, 4.5);
    folder.lineTo(6.0, 4.5);
    folder.lineTo(7.5, 6.0);
    folder.lineTo(14.0, 6.0);
    folder.lineTo(14.0, 13.0);
    folder.lineTo(2.0, 13.0);
    folder.closeSubpath();
    painter.drawPath(folder);
}

void drawFolderOpen(QPainter& painter)
{
    painter.setPen(QPen(QColor(230, 200, 100), 1.1));
    painter.setBrush(QColor(230, 200, 100, 120));
    QPainterPath back;
    back.moveTo(2.0, 4.5);
    back.lineTo(6.0, 4.5);
    back.lineTo(7.5, 6.0);
    back.lineTo(13.0, 6.0);
    back.lineTo(13.0, 8.0);
    back.lineTo(2.0, 8.0);
    back.closeSubpath();
    painter.drawPath(back);
    QPainterPath front;
    front.moveTo(1.0, 8.0);
    front.lineTo(14.5, 8.0);
    front.lineTo(13.0, 13.0);
    front.lineTo(2.0, 13.0);
    front.closeSubpath();
    painter.drawPath(front);
}

void drawNewLayer(QPainter& painter)
{
    drawLayerSheet(painter);
    painter.setPen(QPen(QColor(120, 220, 120), 1.6));
    painter.drawLine(QPointF(12.0, 10.0), QPointF(12.0, 15.0));
    painter.drawLine(QPointF(9.5, 12.5), QPointF(14.5, 12.5));
}

void drawNewFolder(QPainter& painter)
{
    drawFolderClosed(painter);
    painter.setPen(QPen(QColor(120, 220, 120), 1.6));
    painter.drawLine(QPointF(12.0, 10.0), QPointF(12.0, 15.0));
    painter.drawLine(QPointF(9.5, 12.5), QPointF(14.5, 12.5));
}

void drawTrash(QPainter& painter)
{
    painter.setPen(QPen(kInk, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(3.0, 4.5), QPointF(13.0, 4.5));
    painter.drawLine(QPointF(6.5, 2.5), QPointF(9.5, 2.5));
    painter.drawRect(QRectF(4.5, 4.5, 7.0, 9.0));
    painter.drawLine(QPointF(7.0, 7.0), QPointF(7.0, 11.5));
    painter.drawLine(QPointF(9.0, 7.0), QPointF(9.0, 11.5));
}

/// The layer's outline color: a filled square in the normal view, a hollow
/// one when the layer is in outline view, as Animate shows it.
QIcon swatchIcon(const uint8_t rgba[4], bool outline)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const QColor color(rgba[0], rgba[1], rgba[2]);
    painter.setPen(QPen(outline ? color : QColor(20, 20, 20), outline ? 1.6 : 1.0));
    painter.setBrush(outline ? QBrush(Qt::NoBrush) : QBrush(color));
    painter.drawRect(QRectF(3.5, 3.5, 9.0, 9.0));
    return QIcon(pixmap);
}

QToolButton* smallButton(QWidget* parent, const QIcon& icon, const QString& tip)
{
    QToolButton* button = new QToolButton(parent);
    button->setIcon(icon);
    button->setIconSize(QSize(16, 16));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setFixedSize(24, 22);
    return button;
}

} // namespace

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _layerIcon(glyph(drawLayerSheet))
    , _folderIcon(glyph(drawFolderClosed))
    , _openFolderIcon(glyph(drawFolderOpen))
    , _visibleIcon(glyph(drawEye))
    , _hiddenIcon(glyph(drawHiddenCross))
    , _unlockedIcon(glyph(drawUnlockedDot))
    , _lockedIcon(glyph(drawPadlock))
    , _pencilIcon(glyph(drawPencil))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The header: the three column toggles, over their columns, that act on
    // every layer at once. Its height matches the timeline's ruler strip.
    QWidget* header = new QWidget(this);
    header->setAutoFillBackground(true);
    header->setFixedHeight(50);
    QPalette headerPalette = header->palette();
    headerPalette.setColor(QPalette::Window, QColor("#2d2d2d"));
    header->setPalette(headerPalette);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 2);
    headerLayout->setSpacing(0);
    headerLayout->setAlignment(Qt::AlignBottom);
    headerLayout->addStretch();
    _allVisible = smallButton(header, _visibleIcon, "Show or hide all layers");
    _allLocked = smallButton(header, _lockedIcon, "Lock or unlock all layers");
    _allOutline = smallButton(header, glyph(drawOutlineSquare), "Show all layers as outlines");
    headerLayout->addWidget(_allVisible);
    headerLayout->addWidget(_allLocked);
    headerLayout->addWidget(_allOutline);
    layout->addWidget(header);

    connect(_allVisible, &QToolButton::clicked, this, [this]() {
        // Hide everything if anything shows, else show everything.
        bool anyVisible = false;
        for (const fla::Timeline* timeline : shownTimelines())
            for (const fla::Layer* layer : timeline->layers)
                anyVisible = anyVisible || (layer && layer->visible);
        for (fla::Timeline* timeline : shownTimelines())
            for (fla::Layer* layer : timeline->layers)
                if (layer)
                    layer->visible = !anyVisible;
        for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
            refreshRow(*it);
        emit layersChanged();
    });
    connect(_allLocked, &QToolButton::clicked, this, [this]() {
        bool anyUnlocked = false;
        for (const fla::Timeline* timeline : shownTimelines())
            for (const fla::Layer* layer : timeline->layers)
                anyUnlocked = anyUnlocked || (layer && !layer->locked);
        for (fla::Timeline* timeline : shownTimelines())
            for (fla::Layer* layer : timeline->layers)
                if (layer)
                    layer->locked = anyUnlocked;
        for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
            refreshRow(*it);
        emit layersChanged();
    });
    connect(_allOutline, &QToolButton::clicked, this, [this]() {
        bool anyFilled = false;
        for (const fla::Timeline* timeline : shownTimelines())
            for (const fla::Layer* layer : timeline->layers)
                anyFilled = anyFilled || (layer && !layer->outline);
        for (fla::Timeline* timeline : shownTimelines())
            for (fla::Layer* layer : timeline->layers)
                if (layer)
                    layer->outline = anyFilled;
        for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
            refreshRow(*it);
        emit layersChanged();
    });

    _treeWidget = new LayerTree(this);
    _treeWidget->setHeaderHidden(true);
    _treeWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    _treeWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _treeWidget->setIndentation(15);
    _treeWidget->setUniformRowHeights(true);
    _treeWidget->setColumnCount(ColumnCount);
    _treeWidget->header()->setStretchLastSection(false);
    _treeWidget->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    for (int column = CurrentColumn; column < ColumnCount; ++column)
    {
        _treeWidget->header()->setSectionResizeMode(column, QHeaderView::Fixed);
        _treeWidget->header()->resizeSection(column, column == CurrentColumn ? 20 : 24);
    }
    _treeWidget->header()->setHidden(true);
    _treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    _treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(_treeWidget, 1);

    // Under the list: New Layer, New Folder, Delete.
    QWidget* buttons = new QWidget(this);
    buttons->setAutoFillBackground(true);
    buttons->setPalette(headerPalette);
    buttons->setFixedHeight(26);
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(4, 2, 4, 2);
    buttonLayout->setSpacing(2);
    QToolButton* newLayer = smallButton(buttons, glyph(drawNewLayer), "New Layer");
    QToolButton* newFolder = smallButton(buttons, glyph(drawNewFolder), "New Folder");
    QToolButton* remove = smallButton(buttons, glyph(drawTrash), "Delete the current layer");
    connect(newLayer, &QToolButton::clicked, this, [this]() { emit addLayerRequested(false); });
    connect(newFolder, &QToolButton::clicked, this, [this]() { emit addLayerRequested(true); });
    connect(remove, &QToolButton::clicked, this, [this]() { emit deleteLayerRequested(); });
    buttonLayout->addWidget(newLayer);
    buttonLayout->addWidget(newFolder);
    buttonLayout->addStretch();
    buttonLayout->addWidget(remove);
    layout->addWidget(buttons);

    connect(_treeWidget, &QTreeWidget::itemExpanded, this, &LayersPanel::onItemExpanded);
    connect(_treeWidget, &QTreeWidget::itemCollapsed, this, &LayersPanel::onItemCollapsed);
    connect(_treeWidget, &QTreeWidget::itemClicked, this, &LayersPanel::onItemClicked);
    connect(_treeWidget, &QTreeWidget::itemDoubleClicked, this, &LayersPanel::onItemDoubleClicked);
    connect(_treeWidget, &QTreeWidget::itemChanged, this, &LayersPanel::onItemChanged);
}

void LayersPanel::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    _timeline = nullptr;
    _expanded.clear();
    refresh();
}

void LayersPanel::setTimeline(fla::Timeline* timeline)
{
    _timeline = timeline;
    refresh();
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
        if (fla::Timeline* scene = _flaDocument->document->currentScene())
            timelines.push_back(scene);
    }
    return timelines;
}

fla::Timeline* LayersPanel::timelineFor(const QTreeWidgetItem* item) const
{
    if (!item || !_flaDocument || !_flaDocument->document)
        return nullptr;
    const int timelineIndex = item->data(NameColumn, Qt::UserRole + 1).toInt();
    const std::vector<fla::Timeline*> timelines = shownTimelines();
    if (timelineIndex < 0 || timelineIndex >= static_cast<int>(timelines.size()))
        return nullptr;
    return timelines[timelineIndex];
}

fla::Layer* LayersPanel::layerFor(const QTreeWidgetItem* item) const
{
    fla::Timeline* timeline = timelineFor(item);
    if (!timeline)
        return nullptr;
    const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();
    if (layerIndex < 0 || layerIndex >= static_cast<int>(timeline->layers.size()))
        return nullptr;
    return timeline->layers[layerIndex];
}

QScrollBar* LayersPanel::verticalScrollBar() const
{
    return _treeWidget->verticalScrollBar();
}

int LayersPanel::rowsViewportHeight() const
{
    return _treeWidget->viewport()->height() + _bottomMargin;
}

void LayersPanel::setBottomMargin(int margin)
{
    if (margin == _bottomMargin)
        return;
    _bottomMargin = margin;
    static_cast<LayerTree*>(_treeWidget)->setBottomMargin(margin);
}

std::vector<LayerRow> LayersPanel::visibleRows() const
{
    std::vector<LayerRow> rows;
    const std::vector<fla::Timeline*> timelines = shownTimelines();

    // Walk the tree in display order, going into a folder only when it is
    // open, which is what the list shows.
    std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        const int timelineIndex = item->data(NameColumn, Qt::UserRole + 1).toInt();
        const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();
        if (timelineIndex >= 0 && timelineIndex < static_cast<int>(timelines.size()))
        {
            const fla::Timeline* timeline = timelines[timelineIndex];
            if (layerIndex >= 0 && layerIndex < static_cast<int>(timeline->layers.size()))
                rows.push_back({timeline, layerIndex, timeline->layers[layerIndex]});
        }
        if (!item->isExpanded())
            return;
        for (int i = 0; i < item->childCount(); ++i)
            visit(item->child(i));
    };
    for (int i = 0; i < _treeWidget->topLevelItemCount(); ++i)
        visit(_treeWidget->topLevelItem(i));
    return rows;
}

bool LayersPanel::currentLayer(fla::Timeline*& timeline, int& layerIndex) const
{
    for (fla::Timeline* candidate : shownTimelines())
    {
        for (size_t i = 0; i < candidate->layers.size(); ++i)
        {
            const fla::Layer* layer = candidate->layers[i];
            if (layer && layer->selected)
            {
                timeline = candidate;
                layerIndex = static_cast<int>(i);
                return true;
            }
        }
    }
    return false;
}

void LayersPanel::setCurrentLayer(const fla::Timeline* timeline, int layerIndex)
{
    const std::vector<fla::Timeline*> timelines = shownTimelines();
    int timelineIndex = -1;
    for (size_t i = 0; i < timelines.size(); ++i)
    {
        if (timelines[i] == timeline)
            timelineIndex = static_cast<int>(i);
    }
    if (timelineIndex < 0 || layerIndex < 0 || layerIndex >= static_cast<int>(timeline->layers.size()))
        return;

    fla::Timeline* target = timelines[timelineIndex];
    fla::Layer* layer = target->layers[layerIndex];
    if (!layer || layer->layerType == fla::Layer::Type::Folder)
        return;
    if (layer->selected)
        return;

    for (fla::Layer* other : target->layers)
    {
        if (other)
            other->selected = other == layer;
    }

    for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
    {
        QTreeWidgetItem* item = *it;
        if (item->data(NameColumn, Qt::UserRole).toInt() == layerIndex &&
            item->data(NameColumn, Qt::UserRole + 1).toInt() == timelineIndex)
        {
            _treeWidget->setCurrentItem(item);
            break;
        }
    }
    markCurrent();

    emit currentLayerChanged(QString::fromStdString(target->name), layerIndex);
}

void LayersPanel::markCurrent()
{
    for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
    {
        const fla::Layer* layer = layerFor(*it);
        (*it)->setIcon(CurrentColumn, layer && layer->selected ? _pencilIcon : QIcon());
    }
}

void LayersPanel::refreshRow(QTreeWidgetItem* item)
{
    const fla::Layer* layer = layerFor(item);
    if (!layer)
        return;
    item->setIcon(VisibleColumn, layer->visible ? _visibleIcon : _hiddenIcon);
    item->setIcon(LockColumn, layer->locked ? _lockedIcon : _unlockedIcon);
    item->setIcon(ColorColumn, swatchIcon(layer->color, layer->outline));
    item->setIcon(CurrentColumn, layer->selected ? _pencilIcon : QIcon());
}

std::vector<LayersPanel::Built> LayersPanel::snapshot() const
{
    std::vector<Built> built;
    for (const fla::Timeline* timeline : shownTimelines())
    {
        for (const fla::Layer* layer : timeline->layers)
            built.push_back({layer, layer ? layer->name : std::string(), layer && layer->selected});
    }
    return built;
}

void LayersPanel::refreshIfStale()
{
    const std::vector<Built> now = snapshot();
    bool same = now.size() == _built.size();
    for (size_t i = 0; same && i < now.size(); ++i)
    {
        same = now[i].layer == _built[i].layer && now[i].name == _built[i].name &&
            now[i].selected == _built[i].selected;
    }
    if (!same)
        refresh();
}

void LayersPanel::refresh()
{
    _refreshing = true;
    _treeWidget->clear();

    if (!_flaDocument || !_flaDocument->document)
    {
        _built.clear();
        _refreshing = false;
        emit rowsChanged();
        return;
    }

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
                    layerName = QString("Folder %1").arg(layerIndexMap.size());
                else
                    layerName = QString("Layer %1").arg(layerIndexMap.size());
            }

            QTreeWidgetItem* item = new QTreeWidgetItem();
            // The same row height as the timeline grid's, so the rows line up.
            item->setSizeHint(0, QSize(100, 22));
            item->setFlags(item->flags() | Qt::ItemIsEditable);

            layerIndexMap.push_back(item);

            item->setText(NameColumn, layerName);
            const bool folder = layer->layerType == fla::Layer::Type::Folder;
            item->setIcon(NameColumn, folder ? _openFolderIcon : _layerIcon);
            item->setToolTip(VisibleColumn, "Show or hide the layer");
            item->setToolTip(LockColumn, "Lock or unlock the layer");
            item->setToolTip(ColorColumn, "Outline color; click to view the layer as outlines");
            item->setToolTip(CurrentColumn, "The current layer, where new drawings go");

            item->setData(NameColumn, Qt::UserRole, QVariant::fromValue(layerIndexMap.size() - 1));
            item->setData(NameColumn, Qt::UserRole + 1, timelineIndex);

            QTreeWidgetItem* parent = layer->parentLayerIndex == -1 ? nullptr
                : layerIndexMap.value(layer->parentLayerIndex, nullptr);
            if (parent)
                parent->addChild(item);
            else
                _treeWidget->addTopLevelItem(item);

            refreshRow(item);

            if (folder)
            {
                const auto known = _expanded.find(layer);
                const bool open = known == _expanded.end() ? true : known->second;
                item->setExpanded(open);
                item->setIcon(NameColumn, open ? _openFolderIcon : _folderIcon);
            }

            if (layer->selected && !currentItem)
                currentItem = item;
        }

        if (currentItem)
            _treeWidget->setCurrentItem(currentItem);

        ++timelineIndex;
    }

    _built = snapshot();
    _refreshing = false;
    emit rowsChanged();
}

void LayersPanel::onItemExpanded(QTreeWidgetItem* item)
{
    item->setIcon(NameColumn, _openFolderIcon);
    if (const fla::Layer* layer = layerFor(item))
        _expanded[layer] = true;
    if (!_refreshing)
        emit rowsChanged();
}

void LayersPanel::onItemCollapsed(QTreeWidgetItem* item)
{
    item->setIcon(NameColumn, _folderIcon);
    if (const fla::Layer* layer = layerFor(item))
        _expanded[layer] = false;
    if (!_refreshing)
        emit rowsChanged();
}

void LayersPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    // Only the name is typed into; the other columns are toggles.
    if (column == NameColumn && layerFor(item))
        _treeWidget->editItem(item, NameColumn);
}

void LayersPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (_refreshing || column != NameColumn)
        return;
    fla::Timeline* timeline = timelineFor(item);
    const fla::Layer* layer = layerFor(item);
    if (!timeline || !layer)
        return;
    const QString name = item->text(NameColumn).trimmed();
    if (name.isEmpty() || name == QString::fromStdString(layer->name))
    {
        item->setText(NameColumn, QString::fromStdString(layer->name));
        return;
    }
    emit layerRenamed(timeline, item->data(NameColumn, Qt::UserRole).toInt(), name);
}

void LayersPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    fla::Layer* layer = layerFor(item);
    fla::Timeline* timeline = timelineFor(item);
    if (!layer || !timeline)
        return;

    const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();
    const QString timelineName = QString::fromStdString(timeline->name);

    switch (column)
    {
    case VisibleColumn:
        layer->visible = !layer->visible;
        refreshRow(item);
        emit layerVisibilityChanged(timelineName, layerIndex, layer->visible);
        break;

    case LockColumn:
        layer->locked = !layer->locked;
        refreshRow(item);
        emit layerLockChanged(timelineName, layerIndex, layer->locked);
        break;

    case ColorColumn:
        // The square toggles outline view, as in Animate.
        layer->outline = !layer->outline;
        refreshRow(item);
        emit layersChanged();
        break;

    default:
        // Clicking the name makes this the current layer, the one new drawings
        // go into. Only one layer in a timeline is current.
        if (layer->layerType == fla::Layer::Type::Folder)
            break;
        for (fla::Layer* other : timeline->layers)
        {
            if (other)
                other->selected = other == layer;
        }
        _treeWidget->setCurrentItem(item);
        markCurrent();
        _built = snapshot();
        emit currentLayerChanged(timelineName, layerIndex);
        break;
    }
}
