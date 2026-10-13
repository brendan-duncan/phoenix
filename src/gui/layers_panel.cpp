#include "layers_panel.h"
#include "../edit/layer_effects.h"

#include "theme.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QSpinBox>
#include <cstdlib>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QScrollBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <functional>

namespace {

/// The layer a layer follows by layer parenting, from its first keyframe:
/// the frame names the parent by its rigging index.
const fla::Layer* parentLayerOf(const fla::Layer& layer)
{
    const fla::Timeline* timeline = dynamic_cast<const fla::Timeline*>(layer.parent);
    if (!timeline || layer.frames.empty() || !layer.frames.front())
        return nullptr;
    int rig = -1;
    for (const auto& attribute : layer.frames.front()->extraAttributes)
        if (attribute.first == "parentLayerIndex")
            rig = std::atoi(attribute.second.c_str());
    if (rig < 0)
        return nullptr;
    for (const fla::Layer* candidate : timeline->layers)
    {
        if (!candidate || candidate == &layer)
            continue;
        for (const auto& attribute : candidate->extraAttributes)
            if (attribute.first == "layerRiggingIndex" && std::atoi(attribute.second.c_str()) == rig)
                return candidate;
    }
    return nullptr;
}

} // namespace

namespace {

/// The layers list, with its viewport margins open to the panel: a bottom
/// margin lets it scroll as far as the frame grid does.
/// Where a dragged row was let go, relative to the row under it.
enum DropAt
{
    DropOnto,
    DropAbove,
    DropBelow,
    DropOnEmpty
};

class LayerTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

    void setBottomMargin(int margin) { setViewportMargins(0, 0, 0, margin); }

    /// Drawn over the rows once they are painted.
    std::function<void(QPainter&)> overlay;

    /// Called with the row dragged, the row it was dropped at (null for
    /// empty space) and where on it (a DropAt). The list does
    /// not move the rows itself: the drop becomes an edit of the document,
    /// and the list is rebuilt from that.
    std::function<void(QTreeWidgetItem*, QTreeWidgetItem*, int)> dropped;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QTreeWidget::paintEvent(event);
        if (overlay)
        {
            QPainter painter(viewport());
            overlay(painter);
        }
    }

    void dropEvent(QDropEvent* event) override
    {
        QTreeWidgetItem* dragged = currentItem();
        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        int position = DropOnEmpty;
        switch (dropIndicatorPosition())
        {
        case QAbstractItemView::OnItem: position = DropOnto; break;
        case QAbstractItemView::AboveItem: position = DropAbove; break;
        case QAbstractItemView::BelowItem: position = DropBelow; break;
        case QAbstractItemView::OnViewport: position = DropOnEmpty; break;
        }
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (dragged && dropped)
            dropped(dragged, target, position);
    }
};

/// The glyphs' ink, from the theme.
QColor ink()
{
    return theme::color("icon");
}

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
    painter.setPen(QPen(ink(), 1.2));
    painter.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(2.0, 8.0);
    eye.quadTo(8.0, 1.5, 14.0, 8.0);
    eye.quadTo(8.0, 14.5, 2.0, 8.0);
    painter.drawPath(eye);
    painter.setBrush(ink());
    painter.drawEllipse(QPointF(8.0, 8.0), 2.2, 2.2);
}

void drawFadedEye(QPainter& painter)
{
    // A transparent layer: the eye, half there.
    painter.setOpacity(0.45);
    drawEye(painter);
    painter.setOpacity(1.0);
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
    painter.setPen(QPen(ink(), 1.4));
    painter.setBrush(Qt::NoBrush);
    QPainterPath shackle;
    shackle.moveTo(5.0, 8.0);
    shackle.lineTo(5.0, 5.5);
    shackle.quadTo(5.0, 2.5, 8.0, 2.5);
    shackle.quadTo(11.0, 2.5, 11.0, 5.5);
    shackle.lineTo(11.0, 8.0);
    painter.drawPath(shackle);
    painter.setBrush(ink());
    painter.drawRoundedRect(QRectF(3.5, 7.5, 9.0, 6.5), 1.0, 1.0);
}

void drawOutlineSquare(QPainter& painter)
{
    painter.setPen(QPen(ink(), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(3.5, 3.5, 9.0, 9.0));
}

void drawPencil(QPainter& painter)
{
    // Animate's current-layer marker: a pencil leaning to the right.
    painter.setPen(QPen(ink(), 1.2));
    painter.setBrush(theme::withAlpha(ink(), 90));
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
    painter.setPen(QPen(ink(), 1.1));
    painter.setBrush(theme::withAlpha(ink(), 50));
    painter.drawRect(QRectF(3.5, 2.5, 9.0, 11.0));
    painter.setPen(QPen(ink(), 0.9));
    painter.drawLine(QPointF(5.5, 6.0), QPointF(10.5, 6.0));
    painter.drawLine(QPointF(5.5, 8.5), QPointF(10.5, 8.5));
    painter.drawLine(QPointF(5.5, 11.0), QPointF(8.5, 11.0));
}

void drawFolderClosed(QPainter& painter)
{
    painter.setPen(QPen(theme::color("folder"), 1.1));
    painter.setBrush(theme::withAlpha(theme::color("folder"), 120));
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
    painter.setPen(QPen(theme::color("folder"), 1.1));
    painter.setBrush(theme::withAlpha(theme::color("folder"), 120));
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

void drawParentLink(QPainter& painter)
{
    // Two nodes joined: a parent and the layer that follows it.
    painter.setPen(QPen(ink(), 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(4.5, 4.5), 2.5, 2.5);
    painter.drawEllipse(QPointF(11.5, 11.5), 2.5, 2.5);
    painter.drawLine(QPointF(4.5, 7.0), QPointF(4.5, 11.5));
    painter.drawLine(QPointF(4.5, 11.5), QPointF(9.0, 11.5));
}

void drawTrash(QPainter& painter)
{
    painter.setPen(QPen(ink(), 1.2));
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
    painter.setPen(QPen(outline ? color : theme::withAlpha(ink(), 140), outline ? 1.6 : 1.0));
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
    , _fadedIcon(glyph(drawFadedEye))
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
    _header = header;
    // The buttons sit over their columns (see alignHeader).
    _allVisible = smallButton(header, _visibleIcon, "Show or hide all layers");
    _allLocked = smallButton(header, _lockedIcon, "Lock or unlock all layers");
    _allOutline = smallButton(header, glyph(drawOutlineSquare), "Show all layers as outlines");
    theme::themedIcon(_allVisible, []() { return glyph(drawEye); });
    theme::themedIcon(_allLocked, []() { return glyph(drawPadlock); });
    theme::themedIcon(_allOutline, []() { return glyph(drawOutlineSquare); });
    _parenting = smallButton(header, glyph(drawParentLink), "Show the parenting view: drag from a layer's handle to the layer it should follow");
    _parenting->setCheckable(true);
    theme::themedIcon(_parenting, []() { return glyph(drawParentLink); });
    layout->addWidget(header);
    connect(_parenting, &QToolButton::toggled, this, [this](bool on) {
        _treeWidget->setColumnHidden(ParentColumn, !on);
        _treeWidget->viewport()->update();
        QTimer::singleShot(0, this, [this]() { alignHeader(); });
    });

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
        _treeWidget->header()->resizeSection(column, column == CurrentColumn ? 20 : column == ParentColumn ? 32 : 24);
    }
    _treeWidget->header()->setHidden(true);
    _treeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _treeWidget->viewport()->installEventFilter(this);
    _treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Rows are dragged to reorder layers and to move them into and out of
    // folders, masks and guides; a right-click offers the layer commands.
    _treeWidget->setDragEnabled(true);
    _treeWidget->setAcceptDrops(true);
    _treeWidget->setDropIndicatorShown(true);
    _treeWidget->setDragDropMode(QAbstractItemView::InternalMove);
    _treeWidget->setColumnHidden(ParentColumn, true);
    static_cast<LayerTree*>(_treeWidget)->overlay = [this](QPainter& painter) { drawParenting(painter); };
    static_cast<LayerTree*>(_treeWidget)->dropped = [this](QTreeWidgetItem* dragged, QTreeWidgetItem* target, int position) {
        dropLayer(dragged, target, position);
    };
    _treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_treeWidget, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (QTreeWidgetItem* item = _treeWidget->itemAt(pos))
            showLayerMenu(item, _treeWidget->viewport()->mapToGlobal(pos));
    });
    layout->addWidget(_treeWidget, 1);

    // Under the list: New Layer, New Folder, Delete.
    QWidget* buttons = new QWidget(this);
    buttons->setAutoFillBackground(true);
    _buttonRow = buttons;
    buttons->setFixedHeight(26);
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(4, 2, 4, 2);
    buttonLayout->setSpacing(2);
    QToolButton* newLayer = smallButton(buttons, glyph(drawNewLayer), "New Layer");
    QToolButton* newFolder = smallButton(buttons, glyph(drawNewFolder), "New Folder");
    QToolButton* remove = smallButton(buttons, glyph(drawTrash), "Delete the current layer");
    newLayer->setObjectName("layerNew");
    newFolder->setObjectName("layerNewFolder");
    remove->setObjectName("layerDelete");
    theme::themedIcon(newLayer, []() { return glyph(drawNewLayer); });
    theme::themedIcon(newFolder, []() { return glyph(drawNewFolder); });
    theme::themedIcon(remove, []() { return glyph(drawTrash); });
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

    applyTheme();
    connect(theme::notifier(), &theme::Notifier::changed, this, [this]() {
        applyTheme();
        refresh();
    });
}

void LayersPanel::alignHeader()
{
    // Each header button over its column of toggles; the parenting button
    // over its column when shown, at the right edge when not.
    const int top = _header->height() - 24;
    // A row's icon sits at the left of its cell, past the item margin.
    const int iconCenter = _treeWidget->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, _treeWidget) + 1 + 8;
    const auto place = [this, top, iconCenter](QToolButton* button, int column) {
        const int x = _treeWidget->viewport()->mapTo(this, QPoint(_treeWidget->columnViewportPosition(column), 0)).x() - _header->x();
        const int center = column == ParentColumn ? _treeWidget->columnWidth(column) / 2 : iconCenter;
        button->move(x + center - button->width() / 2, top);
    };
    place(_allVisible, VisibleColumn);
    place(_allLocked, LockColumn);
    place(_allOutline, ColorColumn);
    if (_parenting->isChecked())
        place(_parenting, ParentColumn);
    else
        _parenting->move(_header->width() - _parenting->width() - 2, 2);
}

void LayersPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Once the list has its new width.
    QTimer::singleShot(0, this, [this]() { alignHeader(); });
}

void LayersPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() { alignHeader(); });
}

void LayersPanel::applyTheme()
{
    QPalette headerPalette = _header->palette();
    headerPalette.setColor(QPalette::Window, theme::color("panelHeader"));
    _header->setPalette(headerPalette);
    _buttonRow->setPalette(headerPalette);
    _layerIcon = glyph(drawLayerSheet);
    _folderIcon = glyph(drawFolderClosed);
    _openFolderIcon = glyph(drawFolderOpen);
    _visibleIcon = glyph(drawEye);
    _hiddenIcon = glyph(drawHiddenCross);
    _fadedIcon = glyph(drawFadedEye);
    _unlockedIcon = glyph(drawUnlockedDot);
    _lockedIcon = glyph(drawPadlock);
    _pencilIcon = glyph(drawPencil);
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

void LayersPanel::setRowHeight(int height)
{
    if (height == _rowHeight)
        return;
    _rowHeight = height;
    refresh();
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

std::vector<int> LayersPanel::selectedLayers(fla::Timeline*& timeline) const
{
    std::vector<int> indices;
    timeline = nullptr;
    for (QTreeWidgetItem* item : _treeWidget->selectedItems())
    {
        fla::Timeline* of = timelineFor(item);
        if (!of || !layerFor(item) || (timeline && of != timeline))
            continue;
        timeline = of;
        indices.push_back(item->data(NameColumn, Qt::UserRole).toInt());
    }
    int current = 0;
    fla::Timeline* currentTimeline = nullptr;
    if (indices.empty() && currentLayer(currentTimeline, current))
    {
        timeline = currentTimeline;
        indices.push_back(current);
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

bool LayersPanel::toggleOf(const fla::Layer& layer, int column)
{
    return column == VisibleColumn ? layer.visible : column == LockColumn ? layer.locked : layer.outline;
}

void LayersPanel::setToggle(QTreeWidgetItem* item, int column, bool value)
{
    fla::Layer* layer = layerFor(item);
    fla::Timeline* timeline = timelineFor(item);
    if (!layer || !timeline || toggleOf(*layer, column) == value)
        return;
    const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();
    const QString timelineName = QString::fromStdString(timeline->name);
    if (column == VisibleColumn)
    {
        layer->visible = value;
        refreshRow(item);
        emit layerVisibilityChanged(timelineName, layerIndex, value);
        // A folder's eye shows or hides everything in it.
        if (layer->layerType == fla::Layer::Type::Folder)
        {
            for (int i = 0; i < static_cast<int>(timeline->layers.size()); ++i)
            {
                fla::Layer* inside = timeline->layers[static_cast<size_t>(i)];
                bool within = false;
                int guard = 0;
                for (const fla::Layer* up = inside ? inside->parentLayer : nullptr; up && !within && guard < 256;
                     up = up->parentLayer, ++guard)
                    within = up == layer;
                if (!within || inside->visible == value)
                    continue;
                inside->visible = value;
                emit layerVisibilityChanged(timelineName, i, value);
            }
            for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
                refreshRow(*it);
        }
    }
    else if (column == LockColumn)
    {
        layer->locked = value;
        refreshRow(item);
        emit layerLockChanged(timelineName, layerIndex, value);
    }
    else
    {
        layer->outline = value;
        refreshRow(item);
        emit layersChanged();
    }
}

bool LayersPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != _treeWidget->viewport())
        return QWidget::eventFilter(watched, event);
    const auto* mouse = dynamic_cast<QMouseEvent*>(event);
    if (!mouse)
        return QWidget::eventFilter(watched, event);
    const QPoint at = mouse->position().toPoint();
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton)
    {
        QTreeWidgetItem* item = _treeWidget->itemAt(at);
        const int column = _treeWidget->columnAt(at.x());
        fla::Layer* layer = layerFor(item);
        if (layer && column == ParentColumn && layer->layerType != fla::Layer::Type::Folder)
        {
            // A drag from the handle to the layer to follow.
            _pressTaken = true;
            _parentDrag = item;
            _parentDragAt = at;
            _treeWidget->viewport()->update();
            return true;
        }
        if (!layer || (column != VisibleColumn && column != LockColumn && column != ColorColumn))
            return false;
        _pressTaken = true;
        fla::Timeline* timeline = timelineFor(item);
        if (column == VisibleColumn && (mouse->modifiers() & Qt::ShiftModifier))
        {
            // Shift on the eye: the layer shows faded, or in full again.
            layer->extraAttributes = fla::layers::withTransparency(*layer, fla::layers::transparencyOf(*layer) > 0 ? 0 : 50);
            layer->visible = true;
            refreshRow(item);
            emit layersChanged();
            return true;
        }
        if (mouse->modifiers() & Qt::AltModifier)
        {
            // Alt: every other layer, all one way. If any other is still
            // on, they all go off; otherwise they all come back.
            bool anyOn = false;
            for (const fla::Layer* other : timeline->layers)
                if (other && other != layer)
                    anyOn = anyOn || toggleOf(*other, column) == (column == VisibleColumn);
            const bool value = column == VisibleColumn ? !anyOn : anyOn;
            for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
                if (layerFor(*it) && layerFor(*it) != layer && timelineFor(*it) == timeline)
                    setToggle(*it, column, value);
            emit layersChanged();
            return true;
        }
        _dragColumn = column;
        _dragValue = !toggleOf(*layer, column);
        setToggle(item, column, _dragValue);
        return true;
    }
    if (event->type() == QEvent::MouseMove && _parentDrag)
    {
        _parentDragAt = at;
        _treeWidget->viewport()->update();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && _parentDrag)
    {
        QTreeWidgetItem* dragged = _parentDrag;
        _parentDrag = nullptr;
        _pressTaken = false;
        _treeWidget->viewport()->update();
        fla::Layer* layer = layerFor(dragged);
        fla::Timeline* timeline = timelineFor(dragged);
        QTreeWidgetItem* target = _treeWidget->itemAt(at);
        fla::Layer* parent = layerFor(target);
        if (!layer || !timeline || target == dragged)
            return true;
        if (!target)
        {
            // Let go below the rows: it follows nothing.
            if (parentLayerOf(*layer))
                emit parentLayerRequested(timeline, layer, nullptr);
            return true;
        }
        if (!parent || timelineFor(target) != timeline || parent->layerType == fla::Layer::Type::Folder ||
            parent->layerType == fla::Layer::Type::Guide)
            return true;
        // Not onto one that follows this one, which would go round in a circle.
        // A step limit too, as a damaged file can already hold a circle.
        int guard = 0;
        for (const fla::Layer* up = parent; up; up = parentLayerOf(*up))
            if (up == layer || ++guard > 256)
                return true;
        if (parentLayerOf(*layer) != parent)
            emit parentLayerRequested(timeline, layer, parent);
        return true;
    }
    if (event->type() == QEvent::MouseMove && _dragColumn >= 0)
    {
        if (QTreeWidgetItem* item = _treeWidget->itemAt(QPoint(_treeWidget->columnViewportPosition(_dragColumn) + 4, at.y())))
            setToggle(item, _dragColumn, _dragValue);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && _pressTaken)
    {
        _pressTaken = false;
        _dragColumn = -1;
        return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        const int column = _treeWidget->columnAt(at.x());
        if (column == VisibleColumn || column == LockColumn || column == ColorColumn || column == ParentColumn)
            return true;
    }
    return false;
}

void LayersPanel::drawParenting(QPainter& painter)
{
    if (!_parenting || !_parenting->isChecked())
        return;
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int columnX = _treeWidget->columnViewportPosition(ParentColumn);
    const int columnWidth = _treeWidget->columnWidth(ParentColumn);
    std::map<const fla::Layer*, QPoint> handles;
    for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
    {
        const fla::Layer* layer = layerFor(*it);
        const QRect row = _treeWidget->visualItemRect(*it);
        if (!layer || row.isEmpty() || layer->layerType == fla::Layer::Type::Folder)
            continue;
        handles[layer] = QPoint(columnX + columnWidth / 2, row.center().y());
    }
    // A line from each layer round to the layer it follows, in the
    // parent's color, then the handles over them.
    int lane = 0;
    for (const auto& [layer, at] : handles)
    {
        const fla::Layer* parent = parentLayerOf(*layer);
        const auto to = parent ? handles.find(parent) : handles.end();
        if (to == handles.end())
            continue;
        const QColor color(parent->color[0], parent->color[1], parent->color[2]);
        const int out = at.x() + 5 + (lane++ % 3) * 3;
        painter.setPen(QPen(color, 1.6));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path(at);
        path.lineTo(out, at.y());
        path.lineTo(out, to->second.y());
        path.lineTo(to->second);
        painter.drawPath(path);
    }
    for (const auto& [layer, at] : handles)
    {
        const QColor color(layer->color[0], layer->color[1], layer->color[2]);
        painter.setPen(QPen(theme::color("text"), 1.0));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(at), 4.0, 4.0);
    }
    if (_parentDrag)
    {
        const auto from = handles.find(layerFor(_parentDrag));
        if (from != handles.end())
        {
            painter.setPen(QPen(theme::color("playhead"), 1.6, Qt::DashLine));
            painter.drawLine(from->second, _parentDragAt);
        }
    }
}

void LayersPanel::refreshRow(QTreeWidgetItem* item)
{
    const fla::Layer* layer = layerFor(item);
    if (!layer)
        return;
    if (layer->visible && fla::layers::transparencyOf(*layer) > 0)
        item->setIcon(VisibleColumn, _fadedIcon);
    else
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
            built.push_back({layer, layer ? layer->name : std::string(), layer && layer->selected,
                layer ? layer->parentLayer : nullptr, layer ? layer->layerType : fla::Layer::Type::Normal});
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
            now[i].selected == _built[i].selected && now[i].parent == _built[i].parent &&
            now[i].type == _built[i].type;
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
            item->setSizeHint(0, QSize(100, _rowHeight));
            item->setFlags(item->flags() | Qt::ItemIsEditable);

            layerIndexMap.push_back(item);

            item->setText(NameColumn, layerName);
            if (const fla::Layer* parent = parentLayerOf(*layer))
                item->setToolTip(NameColumn, QString("Follows %1").arg(QString::fromStdString(parent->name)));
            const bool folder = layer->layerType == fla::Layer::Type::Folder;
            item->setIcon(NameColumn, folder ? _openFolderIcon : _layerIcon);
            item->setToolTip(VisibleColumn, "Show or hide the layer");
            item->setToolTip(LockColumn, "Lock or unlock the layer");
            item->setToolTip(ColorColumn, "Outline color; click to view the layer as outlines");
            item->setToolTip(CurrentColumn, "The current layer, where new drawings go");
            item->setToolTip(ParentColumn, "Drag to the layer this one should follow; let go below the layers for none");

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
    // After the tree is done with the item: the rename rebuilds the list,
    // which would delete the item while the tree is still in its handler.
    const int index = item->data(NameColumn, Qt::UserRole).toInt();
    QTimer::singleShot(0, this, [this, timeline, index, name]() { emit layerRenamed(timeline, index, name); });
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
    case LockColumn:
    case ColorColumn:
    case ParentColumn:
        // The toggles act on the press (see eventFilter).
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

fla::Layer* LayersPanel::layerAfterBlock(QTreeWidgetItem* item) const
{
    // The next row once the item's own rows are passed: its next sibling, or
    // its parent's, and so on up.
    for (QTreeWidgetItem* at = item; at; at = at->parent())
    {
        QTreeWidgetItem* parent = at->parent();
        const int index = parent ? parent->indexOfChild(at) : _treeWidget->indexOfTopLevelItem(at);
        const int count = parent ? parent->childCount() : _treeWidget->topLevelItemCount();
        if (index + 1 < count)
            return layerFor(parent ? parent->child(index + 1) : _treeWidget->topLevelItem(index + 1));
    }
    return nullptr;
}

void LayersPanel::dropLayer(QTreeWidgetItem* dragged, QTreeWidgetItem* target, int position)
{
    fla::Timeline* timeline = timelineFor(dragged);
    fla::Layer* layer = layerFor(dragged);
    if (!timeline || !layer || (target && timelineFor(target) != timeline))
        return;

    fla::LayerSnapshot after = fla::snapshotLayers(*timeline);
    fla::Layer* before = nullptr;
    fla::Layer* parent = nullptr;
    fla::Layer* onto = layerFor(target);
    const auto holds = [](const fla::Layer* candidate) {
        return candidate && (candidate->layerType == fla::Layer::Type::Folder ||
            candidate->layerType == fla::Layer::Type::Mask || candidate->layerType == fla::Layer::Type::Guide);
    };
    const auto firstInside = [&](const fla::Layer* owner) -> fla::Layer* {
        for (size_t i = 0; i + 1 < after.size(); ++i)
        {
            if (after[i].layer == owner)
                return after[i + 1].layer;
        }
        return nullptr;
    };
    switch (position)
    {
    case DropOnto:
        if (holds(onto))
        {
            // Into it, as its first layer.
            parent = onto;
            before = firstInside(onto);
            break;
        }
        [[fallthrough]];
    case DropAbove:
        before = onto;
        parent = onto ? onto->parentLayer : nullptr;
        break;
    case DropBelow:
        if (holds(onto) && target->isExpanded() && target->childCount() > 0)
        {
            parent = onto;
            before = firstInside(onto);
        }
        else
        {
            before = target ? layerAfterBlock(target) : nullptr;
            parent = onto ? onto->parentLayer : nullptr;
        }
        break;
    default:
        break;
    }
    if (before == layer)
        return;
    if (!fla::layers::moveLayer(after, layer, before, parent))
        return;
    emit layerEditRequested(timeline, after, "Move Layer");
}

void LayersPanel::showLayerMenu(QTreeWidgetItem* item, const QPoint& globalPos)
{
    fla::Timeline* timeline = timelineFor(item);
    fla::Layer* layer = layerFor(item);
    if (!timeline || !layer)
        return;
    const int layerIndex = item->data(NameColumn, Qt::UserRole).toInt();
    const bool folder = layer->layerType == fla::Layer::Type::Folder;
    const fla::LayerSnapshot now = fla::snapshotLayers(*timeline);
    const fla::layers::Kind kind = fla::layers::kindOf(now, layer);

    QMenu menu(this);
    // The commands that add or remove act at the row clicked, as Animate's do.
    const auto atRow = [this, timeline, layerIndex, folder]() {
        if (!folder)
            setCurrentLayer(timeline, layerIndex);
    };
    menu.addAction("Insert Layer", this, [=]() { atRow(); emit addLayerRequested(false); });
    menu.addAction("Insert Folder", this, [=]() { atRow(); emit addLayerRequested(true); });
    QAction* remove = menu.addAction("Delete Layer", this, [=]() { atRow(); emit deleteLayerRequested(); });
    remove->setEnabled(!folder || timeline->layers.size() > 1);
    menu.addSeparator();
    // On the layers picked, or this one.
    const auto onPicked = [this, item](int action) {
        if (!item->isSelected())
        {
            _treeWidget->clearSelection();
            item->setSelected(true);
        }
        emit layerClipboardRequested(action);
    };
    menu.addAction("Cut Layers", this, [=]() { onPicked(0); });
    menu.addAction("Copy Layers", this, [=]() { onPicked(1); });
    menu.addAction("Paste Layers", this, [=]() { atRow(); emit layerClipboardRequested(2); });
    menu.addAction("Duplicate Layers", this, [=]() { onPicked(3); });
    menu.addSeparator();

    if (!folder)
    {
        QAction* guide = menu.addAction("Guide", this, [=]() {
            fla::LayerSnapshot after = fla::snapshotLayers(*timeline);
            fla::layers::setKind(after, layer,
                kind == fla::layers::Kind::Guide ? fla::layers::Kind::Normal : fla::layers::Kind::Guide);
            emit layerEditRequested(timeline, after, "Guide");
        });
        guide->setCheckable(true);
        guide->setChecked(kind == fla::layers::Kind::Guide);

        QAction* mask = menu.addAction("Mask", this, [=]() {
            fla::LayerSnapshot after = fla::snapshotLayers(*timeline);
            if (kind == fla::layers::Kind::Mask)
            {
                fla::layers::setKind(after, layer, fla::layers::Kind::Normal);
            }
            else
            {
                // As Animate does: the layer masks the one just below it, and
                // both are locked so the mask shows.
                fla::layers::setKind(after, layer, fla::layers::Kind::Mask);
                fla::Layer* below = nullptr;
                for (size_t i = 0; i + 1 < after.size(); ++i)
                {
                    if (after[i].layer == layer)
                        below = after[i + 1].layer;
                }
                if (below && fla::layers::kindOf(after, below) == fla::layers::Kind::Normal &&
                    below->parentLayer == layer->parentLayer)
                {
                    fla::layers::setKind(after, below, fla::layers::Kind::Masked);
                    for (fla::LayerState& state : after)
                    {
                        if (state.layer == below)
                            state.locked = true;
                    }
                }
                for (fla::LayerState& state : after)
                {
                    if (state.layer == layer)
                        state.locked = true;
                }
            }
            emit layerEditRequested(timeline, after, "Mask");
        });
        mask->setCheckable(true);
        mask->setChecked(kind == fla::layers::Kind::Mask);

        if (fla::effects::isEffect(*layer))
            menu.addAction("Effect Settings...", this, [=]() { emit effectSettingsRequested(layer); });

        // Layer parenting: the layer follows another's object, as a forearm
        // its arm.
        QMenu* parentMenu = menu.addMenu("Parent Layer");
        const fla::Layer* current = parentLayerOf(*layer);
        QAction* none = parentMenu->addAction("None", this, [=]() { emit parentLayerRequested(timeline, layer, nullptr); });
        none->setCheckable(true);
        none->setChecked(!current);
        parentMenu->addSeparator();
        for (fla::Layer* other : timeline->layers)
        {
            if (!other || other == layer || other->layerType == fla::Layer::Type::Folder ||
                other->layerType == fla::Layer::Type::Guide)
                continue;
            // Not one that follows this one, which would go round in a circle.
            bool loops = false;
            for (const fla::Layer* up = other; up && !loops; up = parentLayerOf(*up))
                loops = up == layer;
            QAction* choice = parentMenu->addAction(QString::fromStdString(other->name), this,
                [=]() { emit parentLayerRequested(timeline, layer, other); });
            choice->setCheckable(true);
            choice->setChecked(other == current);
            choice->setEnabled(!loops);
        }
        menu.addSeparator();
    }

    // View state, as the column toggles are.
    const auto everyOther = [this, timeline, layer](const std::function<void(fla::Layer&, bool)>& set) {
        for (fla::Layer* other : timeline->layers)
        {
            if (other)
                set(*other, other == layer);
        }
        for (QTreeWidgetItemIterator it(_treeWidget); *it; ++it)
            refreshRow(*it);
        emit layersChanged();
    };
    menu.addAction("Show All", this, [=]() { everyOther([](fla::Layer& l, bool) { l.visible = true; }); });
    menu.addAction("Hide Others", this, [=]() { everyOther([](fla::Layer& l, bool self) { l.visible = self; }); });
    menu.addAction("Lock Others", this, [=]() { everyOther([](fla::Layer& l, bool self) { l.locked = !self; }); });
    menu.addSeparator();
    menu.addAction("Properties...", this, [=]() { showLayerProperties(item); });
    menu.exec(globalPos);
}

void LayersPanel::showLayerProperties(QTreeWidgetItem* item)
{
    fla::Timeline* timeline = timelineFor(item);
    fla::Layer* layer = layerFor(item);
    if (!timeline || !layer)
        return;
    const fla::LayerSnapshot now = fla::snapshotLayers(*timeline);
    const fla::layers::Kind kind = fla::layers::kindOf(now, layer);

    QDialog dialog(this);
    dialog.setWindowTitle("Layer Properties");
    QFormLayout* form = new QFormLayout(&dialog);
    QLineEdit* name = new QLineEdit(QString::fromStdString(layer->name), &dialog);
    form->addRow("Name:", name);
    QCheckBox* show = new QCheckBox("Show", &dialog);
    show->setChecked(layer->visible);
    QCheckBox* lock = new QCheckBox("Lock", &dialog);
    lock->setChecked(layer->locked);
    QHBoxLayout* flags = new QHBoxLayout();
    flags->addWidget(show);
    flags->addWidget(lock);
    flags->addStretch();
    form->addRow("", flags);
    // Transparent: shown faded while editing, so what is under shows.
    QHBoxLayout* fadeRow = new QHBoxLayout();
    QCheckBox* fade = new QCheckBox("Transparent, shown at", &dialog);
    QSpinBox* fadePercent = new QSpinBox(&dialog);
    fadePercent->setRange(5, 95);
    fadePercent->setSuffix("%");
    const int transparency = fla::layers::transparencyOf(*layer);
    fade->setChecked(transparency > 0);
    fadePercent->setValue(transparency > 0 ? transparency : 50);
    fadeRow->addWidget(fade);
    fadeRow->addWidget(fadePercent);
    fadeRow->addStretch();
    form->addRow("", fadeRow);

    // The kinds, those a folder cannot become (or a layer become a folder)
    // offered but not chosen.
    const std::pair<fla::layers::Kind, const char*> kinds[] = {
        {fla::layers::Kind::Normal, "Normal"}, {fla::layers::Kind::Guide, "Guide"},
        {fla::layers::Kind::Guided, "Guided"}, {fla::layers::Kind::Mask, "Mask"},
        {fla::layers::Kind::Masked, "Masked"}, {fla::layers::Kind::Folder, "Folder"}};
    std::vector<std::pair<fla::layers::Kind, QRadioButton*>> buttons;
    QVBoxLayout* typeLayout = new QVBoxLayout();
    for (const auto& [value, label] : kinds)
    {
        QRadioButton* button = new QRadioButton(label, &dialog);
        button->setChecked(value == kind);
        const bool folderEither = value == fla::layers::Kind::Folder || kind == fla::layers::Kind::Folder;
        button->setEnabled(value == kind || !folderEither);
        typeLayout->addWidget(button);
        buttons.emplace_back(value, button);
    }
    form->addRow("Type:", typeLayout);

    QColor color(layer->color[0], layer->color[1], layer->color[2]);
    QPushButton* colorButton = new QPushButton(&dialog);
    const auto showColor = [colorButton](const QColor& c) {
        colorButton->setStyleSheet(QString("background-color: %1; min-width: 48px;").arg(c.name()));
    };
    showColor(color);
    connect(colorButton, &QPushButton::clicked, &dialog, [&]() {
        const QColor chosen = QColorDialog::getColor(color, &dialog, "Outline Color");
        if (chosen.isValid())
        {
            color = chosen;
            showColor(color);
        }
    });
    form->addRow("Outline color:", colorButton);
    QCheckBox* outline = new QCheckBox("View layer as outlines", &dialog);
    outline->setChecked(layer->outline);
    form->addRow("", outline);

    // Layer parenting: the parts of the parent's transform this layer takes
    // on. Position and rotation always do.
    const fla::layers::ParentPropagation propagation = fla::layers::propagationOf(*layer);
    QCheckBox* parentScale = new QCheckBox("Scale", &dialog);
    parentScale->setChecked(propagation.scale);
    QCheckBox* parentSkew = new QCheckBox("Skew", &dialog);
    parentSkew->setChecked(propagation.skew);
    QCheckBox* parentFlip = new QCheckBox("Flip", &dialog);
    parentFlip->setChecked(propagation.flip);
    QHBoxLayout* propagationRow = new QHBoxLayout();
    for (QCheckBox* check : {parentScale, parentSkew, parentFlip})
    {
        check->setToolTip("Whether this layer follows its parent layer's " + check->text().toLower() +
            ". It always follows the parent's position and rotation.");
        propagationRow->addWidget(check);
    }
    propagationRow->addStretch();
    form->addRow("Follow parent's:", propagationRow);

    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(box);
    if (dialog.exec() != QDialog::Accepted)
        return;

    fla::LayerSnapshot after = now;
    fla::layers::Kind chosenKind = kind;
    for (const auto& [value, button] : buttons)
    {
        if (button->isChecked())
            chosenKind = value;
    }
    if (!fla::layers::setKind(after, layer, chosenKind))
    {
        QMessageBox::information(this, "Layer Properties",
            chosenKind == fla::layers::Kind::Masked ? "There is no mask layer above this one to mask it."
            : chosenKind == fla::layers::Kind::Guided ? "There is no guide layer above this one to guide it."
            : "A layer cannot become a folder, or a folder a layer.");
        return;
    }
    for (fla::LayerState& state : after)
    {
        if (state.layer != layer)
            continue;
        const std::string typed = name->text().trimmed().toStdString();
        if (!typed.empty() && typed != state.name)
        {
            state.name = typed;
            state.autoNamed = false;
        }
        state.visible = show->isChecked();
        state.locked = lock->isChecked();
        state.outline = outline->isChecked();
        state.color[0] = static_cast<uint8_t>(color.red());
        state.color[1] = static_cast<uint8_t>(color.green());
        state.color[2] = static_cast<uint8_t>(color.blue());
        fla::layers::ParentPropagation follows;
        follows.scale = parentScale->isChecked();
        follows.skew = parentSkew->isChecked();
        follows.flip = parentFlip->isChecked();
        state.extraAttributes = fla::layers::withPropagation(
            fla::layers::withTransparency(*layer, fade->isChecked() ? fadePercent->value() : 0), follows);
    }
    emit layerEditRequested(timeline, after, "Layer Properties");
}
