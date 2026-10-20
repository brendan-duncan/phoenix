#include "library_panel.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QDateTime>
#include "../edit/font_items.h"
#include "modal.h"
#include "theme.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/folder.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../edit/library_commands.h"
#include "../edit/linked_assets.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int KindRole = Qt::UserRole;
constexpr int NameRole = Qt::UserRole + 1;

/// The library's columns.
enum Column
{
    NameColumn,
    TypeColumn,
    UseColumn,
    LinkageColumn,
    DateColumn,
    ColumnCount
};

/// Sorts the Use Count column by number, the others as text.
class LibraryItem : public QTreeWidgetItem
{
public:
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (column == UseColumn)
            return data(UseColumn, Qt::UserRole).toInt() < other.data(UseColumn, Qt::UserRole).toInt();
        return text(column).compare(other.text(column), Qt::CaseInsensitive) < 0;
    }
};

/// The glyphs' ink, from the theme.
QColor ink()
{
    return theme::color("icon");
}

QIcon glyph(const std::function<void(QPainter&)>& draw)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    draw(painter);
    return QIcon(pixmap);
}

QIcon folderIcon()
{
    return glyph([](QPainter& painter) {
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
    });
}

QIcon symbolIcon(fla::SymbolType type)
{
    return glyph([type](QPainter& painter) {
        // A movie clip is marked with a reel, a graphic with shapes and a
        // button with a pointer; a simple glyph for each.
        painter.setPen(QPen(ink(), 1.2));
        painter.setBrush(Qt::NoBrush);
        switch (type)
        {
        case fla::SymbolType::MovieClip:
            painter.drawEllipse(QRectF(2.5, 2.5, 11, 11));
            painter.setBrush(ink());
            painter.drawEllipse(QRectF(6.5, 6.5, 3, 3));
            break;
        case fla::SymbolType::Button:
            painter.drawRoundedRect(QRectF(2.5, 4.5, 11, 7), 2, 2);
            painter.setBrush(ink());
            painter.drawEllipse(QRectF(6.5, 6.5, 3, 3));
            break;
        case fla::SymbolType::Graphic:
            painter.setBrush(theme::withAlpha(ink(), 70));
            painter.drawEllipse(QRectF(2.5, 2.5, 7, 7));
            painter.drawRect(QRectF(6.5, 6.5, 7, 7));
            break;
        }
    });
}

QIcon bitmapIcon()
{
    return glyph([](QPainter& painter) {
        painter.setPen(QPen(ink(), 1.1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(2.5, 3.5, 11, 9));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(120, 200, 120));
        QPainterPath hill;
        hill.moveTo(3.0, 12.0);
        hill.lineTo(7.0, 7.0);
        hill.lineTo(10.0, 10.0);
        hill.lineTo(13.0, 8.0);
        hill.lineTo(13.0, 12.0);
        hill.closeSubpath();
        painter.drawPath(hill);
        painter.setBrush(QColor(240, 210, 90));
        painter.drawEllipse(QRectF(9.5, 4.5, 2.5, 2.5));
    });
}

QIcon videoIcon()
{
    // A film frame with its sprocket holes, which marks a video.
    return glyph([](QPainter& painter) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink());
        painter.drawRect(QRectF(2.0, 3.0, 12.0, 10.0));
        painter.setBrush(QColor(255, 255, 255));
        for (double y = 4.0; y < 12.0; y += 3.0)
        {
            painter.drawRect(QRectF(3.0, y, 1.5, 1.5));
            painter.drawRect(QRectF(11.5, y, 1.5, 1.5));
        }
        painter.drawRect(QRectF(5.5, 5.0, 5.0, 6.0));
    });
}

QIcon fontIcon()
{
    // A capital A, as the library marks a font.
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font("Times New Roman");
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(theme::color("icon"));
    painter.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, "A");
    return QIcon(pixmap);
}

QIcon soundIcon()
{
    // A speaker with two waves, which marks a sound.
    return glyph([](QPainter& painter) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink());
        QPainterPath speaker;
        speaker.moveTo(2.5, 6.0);
        speaker.lineTo(5.0, 6.0);
        speaker.lineTo(8.5, 3.0);
        speaker.lineTo(8.5, 13.0);
        speaker.lineTo(5.0, 10.0);
        speaker.lineTo(2.5, 10.0);
        speaker.closeSubpath();
        painter.drawPath(speaker);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ink(), 1.2));
        painter.drawArc(QRectF(7.0, 5.0, 5.0, 6.0), -60 * 16, 120 * 16);
        painter.drawArc(QRectF(7.0, 2.5, 8.0, 11.0), -60 * 16, 120 * 16);
    });
}

QString typeName(fla::SymbolType type)
{
    switch (type)
    {
    case fla::SymbolType::MovieClip: return "Movie Clip";
    case fla::SymbolType::Button: return "Button";
    case fla::SymbolType::Graphic: break;
    }
    return "Graphic";
}

} // namespace

LibraryTree::LibraryTree(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
}

QString LibraryTree::folderAt(const QPoint& position) const
{
    const QTreeWidgetItem* item = itemAt(position);
    if (!item)
        return QString();
    const QString name = item->data(0, NameRole).toString();
    if (item->data(0, KindRole).toInt() == 0)
        return name;
    const int slash = name.lastIndexOf('/');
    return slash < 0 ? QString() : name.left(slash);
}

namespace {

/// Whether \a source, a drop's, is a library list: this one, moving an item
/// into a folder, or another (another window's, an external library),
/// whose item is copied in. Folders do not come from another library.
bool takesDrop(const QObject* source, const QObject* self, const QMimeData* data)
{
    if (!data->hasFormat(LibraryTree::mimeType()))
        return false;
    if (source == self)
        return true;
    return qobject_cast<const LibraryTree*>(source) && !QString::fromUtf8(data->data(LibraryTree::mimeType())).startsWith("folder:");
}

} // namespace

void LibraryTree::dragEnterEvent(QDragEnterEvent* event)
{
    if (takesDrop(event->source(), this, event->mimeData()))
    {
        if (event->source() != this)
            event->setDropAction(Qt::CopyAction);
        event->accept();
    }
    else
        event->ignore();
}

void LibraryTree::dragMoveEvent(QDragMoveEvent* event)
{
    if (takesDrop(event->source(), this, event->mimeData()))
    {
        if (event->source() != this)
            event->setDropAction(Qt::CopyAction);
        event->accept();
    }
    else
        event->ignore();
}

void LibraryTree::dropEvent(QDropEvent* event)
{
    if (!takesDrop(event->source(), this, event->mimeData()))
    {
        event->ignore();
        return;
    }
    const QString text = QString::fromUtf8(event->mimeData()->data(mimeType()));
    const int colon = text.indexOf(':');
    if (colon < 0)
        return;
    const QString name = text.mid(colon + 1);
    if (event->source() != this)
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        // After the drag has finished: the copy may ask about clashes.
        QTimer::singleShot(0, this, [this, name]() { emit foreignItemDropped(name); });
        return;
    }
    const QString folder = folderAt(event->position().toPoint());
    event->acceptProposedAction();
    // After the drag has finished: the move rebuilds the list.
    QTimer::singleShot(0, this, [this, name, folder]() { emit moveRequested(name, folder); });
}

const char* LibraryTree::mimeType()
{
    return "application/x-phoenix-library-item";
}

void LibraryTree::startDrag(Qt::DropActions)
{
    QTreeWidgetItem* item = currentItem();
    if (!item)
        return;
    const int kind = item->data(0, KindRole).toInt();
    static const char* const prefixes[] = {"folder:", "symbol:", "bitmap:", "sound:"};
    if (kind < 0 || kind > 3)
        return;
    const QString name = item->data(0, NameRole).toString();
    QMimeData* data = new QMimeData;
    data->setData(mimeType(), (QString(prefixes[kind]) + name).toUtf8());
    QDrag* drag = new QDrag(this);
    drag->setMimeData(data);
    drag->setPixmap(item->icon(0).pixmap(16, 16));
    emit dragStarting(QString(prefixes[kind]).chopped(1), name);
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
    emit dragEnded();
}

LibraryPanel::LibraryPanel(QWidget* parent)
    : QWidget(parent)
    , _tree(new LibraryTree(this))
    , _preview(new QLabel(this))
    , _count(new QLabel(this))
    , _search(new QLineEdit(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The preview of the selected item, at the top.
    _preview->setFixedHeight(150);
    _preview->setAlignment(Qt::AlignCenter);
    _preview->setStyleSheet("QLabel { background: palette(base); border-bottom: 1px solid palette(mid); }");
    layout->addWidget(_preview);
    // Play: a symbol's frames run in the preview.
    _play = new QToolButton(_preview);
    _play->setText(QString(QChar(0x25B6)));
    _play->setCheckable(true);
    _play->setAutoRaise(true);
    _play->setToolTip("Play the symbol in the preview");
    _play->move(6, 150 - 30);
    _play->hide();
    _playTimer = new QTimer(this);
    _playTimer->setInterval(1000 / 24);
    connect(_play, &QToolButton::toggled, this, &LibraryPanel::playPreview);
    connect(_playTimer, &QTimer::timeout, this, [this]() {
        QTreeWidgetItem* item = _tree->currentItem();
        if (!item || !_framePreview || !_frameCount)
            return;
        const QString name = item->data(0, NameRole).toString();
        const int frames = std::max(1, _frameCount(name));
        _playFrame = (_playFrame + 1) % frames;
        const qreal ratio = devicePixelRatioF();
        QImage image = _framePreview(name, _playFrame, QSize(std::max(_preview->width(), 1), _preview->height()) * ratio);
        image.setDevicePixelRatio(ratio);
        _preview->setPixmap(QPixmap::fromImage(image));
    });

    // The item count, and the panel menu.
    QWidget* countRow = new QWidget(this);
    QHBoxLayout* countLayout = new QHBoxLayout(countRow);
    countLayout->setContentsMargins(10, 4, 6, 4);
    countLayout->addWidget(_count, 1);
    QToolButton* panelMenu = new QToolButton(countRow);
    panelMenu->setText(QString(QChar(0x2630)));
    panelMenu->setAutoRaise(true);
    panelMenu->setToolTip("Library menu");
    connect(panelMenu, &QToolButton::clicked, this, [this, panelMenu]() {
        showItemMenu(_tree->currentItem(), panelMenu->mapToGlobal(QPoint(0, panelMenu->height())));
    });
    countLayout->addWidget(panelMenu);
    layout->addWidget(countRow);

    _search->setPlaceholderText("Search");
    _search->setClearButtonEnabled(true);
    _search->setContentsMargins(6, 0, 6, 6);
    connect(_search, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    layout->addWidget(_search);

    _tree->setColumnCount(ColumnCount);
    _tree->setHeaderLabels({"Name", "Type", "Use Count", "Linkage", "Date Modified"});
    // The name keeps room to read; the rest scrolls into view when the
    // panel is narrow.
    _tree->header()->setStretchLastSection(true);
    _tree->header()->setSectionResizeMode(NameColumn, QHeaderView::Interactive);
    _tree->header()->resizeSection(NameColumn, 170);
    _tree->header()->setSectionResizeMode(TypeColumn, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(UseColumn, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(LinkageColumn, QHeaderView::Interactive);
    _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tree->setIndentation(14);
    _tree->setUniformRowHeights(true);
    _tree->setSortingEnabled(true);
    _tree->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(_tree, 1);

    connect(_tree, &QTreeWidget::currentItemChanged, this, [this]() { updatePreview(); });

    // The item menu: Rename, Duplicate, Delete, New Folder.
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    _tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        showItemMenu(_tree->itemAt(at), _tree->viewport()->mapToGlobal(at));
    });
    connect(_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (_refreshing || column != 0)
            return;
        const QString name = item->data(0, NameRole).toString();
        const QString typed = item->text(0).trimmed();
        const int slash = name.lastIndexOf('/');
        const QString folder = slash < 0 ? QString() : name.left(slash + 1);
        const QString leaf = slash < 0 ? name : name.mid(slash + 1);
        if (typed.isEmpty() || typed == leaf || typed.contains('/'))
        {
            _refreshing = true;
            item->setText(0, leaf);
            _refreshing = false;
            return;
        }
        // After the edit has closed: the rename rebuilds the list, which
        // must not delete the item while its editor is still finishing.
        const QString newName = folder + typed;
        QTimer::singleShot(0, this, [this, name, newName]() { emit renameRequested(name, newName); });
    });
    connect(_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item)
            return;
        const int kind = item->data(0, KindRole).toInt();
        const QString name = item->data(0, NameRole).toString();
        if (kind == static_cast<int>(Kind::Symbol))
            emit editSymbolRequested(name);
        else if (kind == static_cast<int>(Kind::Bitmap))
            emit bitmapPropertiesRequested(name);
        else if (kind == static_cast<int>(Kind::Sound))
            emit soundPropertiesRequested(name);
        else if (kind == static_cast<int>(Kind::Video))
            emit videoPropertiesRequested(name);
    });
    connect(_tree, &LibraryTree::moveRequested, this, &LibraryPanel::moveRequested);
    connect(_tree, &LibraryTree::dragStarting, this, [this](const QString& kind, const QString& name) {
        if (_external)
            emit externalDragStarted(kind, name);
        else
            emit itemDragStarted(kind, name);
    });
    connect(_tree, &LibraryTree::dragEnded, this, [this]() {
        if (_external)
            emit externalDragEnded();
        else
            emit itemDragEnded();
    });
    connect(_tree, &LibraryTree::foreignItemDropped, this, &LibraryPanel::foreignItemDropped);
    // The icons are drawn in the theme's colors.
    connect(theme::notifier(), &theme::Notifier::changed, this, &LibraryPanel::refresh);

    // Delete in the list deletes what is selected there.
    _tree->installEventFilter(this);
}

void LibraryPanel::setAllExpanded(bool open)
{
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
        if ((*it)->data(0, KindRole).toInt() == static_cast<int>(Kind::Folder))
            (*it)->setExpanded(open);
}

void LibraryPanel::selectItem(const QString& name)
{
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
    {
        if ((*it)->data(0, NameRole).toString() != name || (*it)->data(0, KindRole).toInt() == static_cast<int>(Kind::Folder))
            continue;
        for (QTreeWidgetItem* parent = (*it)->parent(); parent; parent = parent->parent())
            parent->setExpanded(true);
        _tree->setCurrentItem(*it);
        _tree->scrollToItem(*it);
        return;
    }
}

QString LibraryPanel::currentFolder() const
{
    const QTreeWidgetItem* item = _tree->currentItem();
    if (!item)
        return QString();
    const QString name = item->data(0, NameRole).toString();
    if (item->data(0, KindRole).toInt() == static_cast<int>(Kind::Folder))
        return name;
    const int slash = name.lastIndexOf('/');
    return slash < 0 ? QString() : name.left(slash);
}

void LibraryPanel::setAnimationProvider(std::function<int(const QString&)> frames,
    std::function<QImage(const QString&, int, const QSize&)> frame)
{
    _frameCount = std::move(frames);
    _framePreview = std::move(frame);
}

void LibraryPanel::playPreview(bool play)
{
    _playFrame = 0;
    if (play)
        _playTimer->start();
    else
    {
        _playTimer->stop();
        updatePreview();
    }
}

void LibraryPanel::setExternal(bool external)
{
    _external = external;
    // Nothing in another file's library is renamed or moved here.
    _tree->setEditTriggers(external ? QAbstractItemView::NoEditTriggers : QAbstractItemView::EditKeyPressed);
    _tree->setAcceptDrops(!external);
    _tree->viewport()->setAcceptDrops(!external);
}

void LibraryPanel::showItemMenu(QTreeWidgetItem* item, const QPoint& globalPos)
{
    const QStringList chosenNames = selectedItems();
    if (_external)
    {
        QMenu* menu = new QMenu(this);
        QAction* copyTo = menu->addAction("Copy to Document");
        copyTo->setEnabled(!chosenNames.isEmpty());
        QAction* copy = menu->addAction("Copy");
        copy->setEnabled(!chosenNames.isEmpty());
        menu->addSeparator();
        QAction* expandAll = menu->addAction("Expand All Folders");
        QAction* collapseAll = menu->addAction("Collapse All Folders");
        modal::popup(menu, globalPos, [=, this](QAction* chosen) {
            if (chosen == copyTo)
                emit copyToDocumentRequested(chosenNames);
            else if (chosen == copy)
                emit copyItemsRequested(chosenNames);
            else if (chosen == expandAll || chosen == collapseAll)
                setAllExpanded(chosen == expandAll);
        });
        return;
    }
    {
        const int kind = item ? item->data(0, KindRole).toInt() : -1;
        const QString name = item ? item->data(0, NameRole).toString() : QString();
        QMenu* menu = new QMenu(this);
        QAction* rename = nullptr;
        QAction* duplicate = nullptr;
        QAction* remove = nullptr;
        QAction* linkage = nullptr;
        QAction* symbolProperties = nullptr;
        QAction* linkMaster = nullptr;
        QAction* updateLinked = nullptr;
        QAction* unlink = nullptr;
        QAction* spriteSheet = nullptr;
        const QStringList chosenItems = selectedItems();
        bool linked = false;
        bool anyLinked = false;
        if (_document && _document->document)
        {
            const std::vector<std::string> names = fla::links::linkedSymbols(*_document->document);
            anyLinked = !names.empty();
            linked = std::find(names.begin(), names.end(), name.toStdString()) != names.end();
        }
        if (item)
            rename = menu->addAction("Rename");
        QAction* fontProperties = nullptr;
        QAction* useFont = nullptr;
        if (kind == static_cast<int>(Kind::Font))
        {
            fontProperties = menu->addAction("Font Properties...");
            useFont = menu->addAction("Use for Selected Text");
            menu->addSeparator();
        }
        if (kind == static_cast<int>(Kind::Symbol))
        {
            duplicate = menu->addAction("Duplicate");
            symbolProperties = menu->addAction("Properties...");
            linkage = menu->addAction("Linkage...");
            spriteSheet = menu->addAction("Generate Sprite Sheet...");
            menu->addSeparator();
            linkMaster = menu->addAction(linked ? "Change Master File..." : "Link to Master File...");
            if (linked)
            {
                updateLinked = menu->addAction("Update from Master");
                unlink = menu->addAction("Unlink from Master");
            }
            menu->addSeparator();
        }
        if (!chosenItems.isEmpty())
            remove = menu->addAction(chosenItems.size() == 1 ? QString("Delete") : QString("Delete %1 Items").arg(chosenItems.size()));
        if (item)
            menu->addSeparator();
        QAction* bitmapProperties = nullptr;
        QAction* updateBitmap = nullptr;
        QAction* editWith = nullptr;
        QAction* soundProperties = nullptr;
        if (kind == static_cast<int>(Kind::Bitmap))
        {
            bitmapProperties = menu->addAction("Properties...");
            updateBitmap = menu->addAction("Update");
            editWith = menu->addAction("Edit With...");
            menu->addSeparator();
        }
        if (kind == static_cast<int>(Kind::Sound))
        {
            soundProperties = menu->addAction("Properties...");
            menu->addSeparator();
        }
        QAction* copyItems = menu->addAction("Copy");
        copyItems->setEnabled(!chosenNames.isEmpty());
        QAction* pasteItems = menu->addAction("Paste");
        pasteItems->setEnabled(_canPaste && _canPaste());
        menu->addSeparator();
        QAction* newSymbol = menu->addAction("New Symbol...");
        QAction* folder = menu->addAction("New Folder");
        QAction* newFont = menu->addAction("New Font...");
        menu->addSeparator();
        QAction* expandAll = menu->addAction("Expand All Folders");
        QAction* collapseAll = menu->addAction("Collapse All Folders");
        QAction* unused = menu->addAction("Select Unused Items");
        QAction* updateAll = nullptr;
        if (anyLinked)
            updateAll = menu->addAction("Update All Linked Items");
        modal::popup(menu, globalPos, [=, this](QAction* chosen) {
            if (!chosen)
                return;
            if (chosen == expandAll || chosen == collapseAll)
            {
                setAllExpanded(chosen == expandAll);
            }
            else if (chosen == copyItems)
            {
                emit copyItemsRequested(chosenNames);
            }
            else if (chosen == pasteItems)
            {
                emit pasteItemsRequested();
            }
            else if (chosen == newSymbol)
            {
                emit newSymbolRequested();
            }
            else if (bitmapProperties && chosen == bitmapProperties)
            {
                emit bitmapPropertiesRequested(name);
            }
            else if (spriteSheet && chosen == spriteSheet)
            {
                emit spriteSheetRequested(name);
            }
            else if (updateBitmap && chosen == updateBitmap)
            {
                QStringList bitmaps;
                for (QTreeWidgetItem* selected : _tree->selectedItems())
                    if (selected->data(0, KindRole).toInt() == static_cast<int>(Kind::Bitmap))
                        bitmaps << selected->data(0, NameRole).toString();
                if (bitmaps.isEmpty())
                    bitmaps << name;
                emit updateBitmapsRequested(bitmaps);
            }
            else if (editWith && chosen == editWith)
            {
                emit editBitmapWithRequested(name);
            }
            else if (soundProperties && chosen == soundProperties)
            {
                emit soundPropertiesRequested(name);
            }
            else if (chosen == rename)
            {
                // The item, if the list still holds it once the menu is answered.
                for (QTreeWidgetItemIterator it(_tree); *it; ++it)
                {
                    if (*it == item)
                    {
                        _tree->editItem(item, 0);
                        break;
                    }
                }
            }
            else if (chosen == newFont)
            {
                emit newFontRequested();
            }
            else if (fontProperties && chosen == fontProperties)
            {
                emit fontPropertiesRequested(name);
            }
            else if (useFont && chosen == useFont)
            {
                emit useFontRequested(name);
            }
            else if (chosen == duplicate)
            {
                emit duplicateSymbolRequested(name);
            }
            else if (chosen == remove)
            {
                emit deleteItemsRequested(chosenItems);
            }
            else if (chosen == linkage)
            {
                emit linkageRequested(name);
            }
            else if (chosen == symbolProperties)
            {
                emit symbolPropertiesRequested(name);
            }
            else if (chosen == unused)
            {
                selectUnused();
            }
            else if (chosen == linkMaster)
            {
                emit linkToMasterRequested(name);
            }
            else if (chosen == updateLinked)
            {
                emit updateLinkedRequested({name});
            }
            else if (chosen == unlink)
            {
                emit unlinkRequested(name);
            }
            else if (chosen == updateAll)
            {
                emit updateLinkedRequested({});
            }
            else if (chosen == folder)
            {
                // Inside the folder clicked, or the one holding the item.
                QString parent;
                if (kind == static_cast<int>(Kind::Folder))
                    parent = name;
                else if (name.contains('/'))
                    parent = name.left(name.lastIndexOf('/'));
                emit newFolderRequested(parent);
            }
        });
    }
}

bool LibraryPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _tree && event->type() == QEvent::KeyPress)
    {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && !_external)
        {
            const QStringList items = selectedItems();
            if (!items.isEmpty())
                emit deleteItemsRequested(items);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QStringList LibraryPanel::selectedItems() const
{
    QStringList names;
    for (const QTreeWidgetItem* item : _tree->selectedItems())
    {
        const int kind = item->data(0, KindRole).toInt();
        if (kind == static_cast<int>(Kind::Symbol) || kind == static_cast<int>(Kind::Bitmap))
            names << item->data(0, NameRole).toString();
    }
    return names;
}

void LibraryPanel::updateCounts()
{
    fla::Document* doc = _document ? _document->document : nullptr;
    if (!doc)
        return;
    const std::map<std::string, int> counts = fla::library::useCounts(*doc);
    const bool sorting = _tree->isSortingEnabled();
    _tree->setSortingEnabled(false);
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
    {
        const int kind = (*it)->data(0, KindRole).toInt();
        if (kind == static_cast<int>(Kind::Folder))
            continue;
        const auto found = counts.find((*it)->data(0, NameRole).toString().toStdString());
        const int uses = found == counts.end() ? 0 : found->second;
        if ((*it)->data(UseColumn, Qt::UserRole).toInt() == uses && !(*it)->text(UseColumn).isEmpty())
            continue;
        (*it)->setText(UseColumn, QString::number(uses));
        (*it)->setData(UseColumn, Qt::UserRole, uses);
        (*it)->setTextAlignment(UseColumn, Qt::AlignRight | Qt::AlignVCenter);
    }
    _tree->setSortingEnabled(sorting);
}

void LibraryPanel::selectUnused()
{
    fla::Document* doc = _document ? _document->document : nullptr;
    if (!doc)
        return;
    const std::set<std::string> used = fla::library::usedItems(*doc);
    _search->clear();
    _tree->clearSelection();
    int picked = 0;
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
    {
        const int kind = (*it)->data(0, KindRole).toInt();
        if (kind != static_cast<int>(Kind::Symbol) && kind != static_cast<int>(Kind::Bitmap))
            continue;
        if (used.count((*it)->data(0, NameRole).toString().toStdString()))
            continue;
        (*it)->setSelected(true);
        for (QTreeWidgetItem* parent = (*it)->parent(); parent; parent = parent->parent())
            parent->setExpanded(true);
        if (picked++ == 0)
            _tree->scrollToItem(*it);
    }
    _count->setText(picked ? QString("%1 unused item%2 selected").arg(picked).arg(picked == 1 ? "" : "s")
                           : QString("Every item is used"));
}

void LibraryPanel::applyFilter()
{
    const QString text = _search->text().trimmed();
    // A folder shows when it matches or holds something that does.
    std::function<bool(QTreeWidgetItem*)> show = [&](QTreeWidgetItem* item) {
        bool any = false;
        for (int i = 0; i < item->childCount(); ++i)
            any = show(item->child(i)) || any;
        const bool match = text.isEmpty() || item->text(NameColumn).contains(text, Qt::CaseInsensitive);
        const bool visible = match || any;
        item->setHidden(!visible);
        if (!text.isEmpty() && any)
            item->setExpanded(true);
        return visible;
    };
    for (int i = 0; i < _tree->topLevelItemCount(); ++i)
        show(_tree->topLevelItem(i));
}

void LibraryPanel::setPreviewProvider(std::function<QImage(const QString&, const QString&, const QSize&)> provider)
{
    _previewProvider = std::move(provider);
    updatePreview();
}

void LibraryPanel::updatePreview()
{
    QTreeWidgetItem* item = _tree->currentItem();
    const int kind = item ? item->data(0, KindRole).toInt() : 0;
    // Play shows for a symbol with frames to play.
    const bool animated = item && kind == static_cast<int>(Kind::Symbol) && _frameCount &&
        _frameCount(item->data(0, NameRole).toString()) > 1;
    if (!animated && _play->isChecked())
        _play->setChecked(false);
    _play->setVisible(animated);
    if (_playTimer->isActive())
        return;
    if (!item || kind == static_cast<int>(Kind::Folder) || kind == static_cast<int>(Kind::Sound) ||
        !_previewProvider || !_document)
    {
        _preview->setPixmap(QPixmap());
        return;
    }
    const qreal ratio = devicePixelRatioF();
    const QSize size(std::max(_preview->width(), 1), _preview->height());
    QImage image = _previewProvider(kind == static_cast<int>(Kind::Symbol) ? "symbol" : "bitmap",
        item->data(0, NameRole).toString(), size * ratio);
    image.setDevicePixelRatio(ratio);
    _preview->setPixmap(QPixmap::fromImage(image));
}

void LibraryPanel::setDocument(const fla::FLADocument* document)
{
    _document = document;
    _openFolders.clear();
    refresh();
}

QStringList LibraryPanel::snapshot() const
{
    QStringList names;
    const fla::Document* doc = _document ? _document->document : nullptr;
    if (!doc)
        return names;
    for (const fla::Folder* folder : doc->folders)
        names << "f:" + QString::fromStdString(folder->name);
    if (doc->symbolList)
        for (const fla::Symbol* symbol : doc->symbolList->symbols)
        {
            fla::links::Link link;
            names << "s:" + QString::fromStdString(symbol->name) + ":" +
                         QString::fromStdString(fla::library::linkageOf(*symbol)) +
                         (fla::links::linkOf(*symbol, link) ? QString(":linked") : QString());
        }
    for (const fla::Resource* resource : doc->resources)
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
            names << "b:" + QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name);
    for (const fla::Document::SoundInfo& sound : doc->sounds)
        names << "a:" + QString::fromStdString(sound.name);
    for (const fla::Document::VideoInfo& video : doc->videos)
        names << "v:" + QString::fromStdString(video.name);
    for (const fla::fonts::FontItem& font : fla::fonts::itemsOf(*doc))
        names << "t:" + QString::fromStdString(font.name) + ":" + QString::fromStdString(font.font);
    return names;
}

void LibraryPanel::refreshIfStale()
{
    if (snapshot() != _built)
        refresh();
    else if (isVisible())
        updateCounts();
}

QTreeWidgetItem* LibraryPanel::folderItem(const QString& path)
{
    if (path.isEmpty())
        return nullptr;
    // An item for each folder on the path, made as needed.
    QTreeWidgetItem* parent = nullptr;
    QString sofar;
    for (const QString& part : path.split('/', Qt::SkipEmptyParts))
    {
        sofar = sofar.isEmpty() ? part : sofar + "/" + part;
        QTreeWidgetItem* found = nullptr;
        const int count = parent ? parent->childCount() : _tree->topLevelItemCount();
        for (int i = 0; i < count && !found; ++i)
        {
            QTreeWidgetItem* child = parent ? parent->child(i) : _tree->topLevelItem(i);
            if (child->data(0, KindRole).toInt() == static_cast<int>(Kind::Folder) &&
                child->data(0, NameRole).toString() == sofar)
                found = child;
        }
        if (!found)
        {
            found = new LibraryItem();
            found->setText(0, part);
            found->setIcon(0, folderIcon());
            found->setText(1, "Folder");
            found->setData(0, KindRole, static_cast<int>(Kind::Folder));
            found->setData(0, NameRole, sofar);
            found->setFlags(found->flags() | Qt::ItemIsEditable | Qt::ItemIsDropEnabled);
            if (parent)
                parent->addChild(found);
            else
                _tree->addTopLevelItem(found);
            found->setExpanded(_openFolders.contains(sofar));
        }
        parent = found;
    }
    return parent;
}

void LibraryPanel::refresh()
{
    _refreshing = true;
    // Keep the open folders open across the rebuild.
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
    {
        if ((*it)->data(0, KindRole).toInt() != static_cast<int>(Kind::Folder))
            continue;
        const QString name = (*it)->data(0, NameRole).toString();
        if ((*it)->isExpanded() && !_openFolders.contains(name))
            _openFolders << name;
        else if (!(*it)->isExpanded())
            _openFolders.removeAll(name);
    }

    _tree->setSortingEnabled(false);
    _tree->clear();
    _built = snapshot();

    const fla::Document* doc = _document ? _document->document : nullptr;
    if (!doc)
    {
        _count->setText("No document");
        _tree->setSortingEnabled(true);
        _refreshing = false;
        return;
    }

    for (const fla::Folder* folder : doc->folders)
        folderItem(QString::fromStdString(folder->name));

    int items = 0;
    const auto add = [&](const QString& path, const QIcon& icon, const QString& type, Kind kind,
                         const QString& linkage = QString(), qint64 date = 0) {
        const int slash = path.lastIndexOf('/');
        QTreeWidgetItem* item = new LibraryItem();
        item->setText(LinkageColumn, linkage);
        item->setText(0, slash < 0 ? path : path.mid(slash + 1));
        item->setIcon(0, icon);
        item->setText(1, type);
        item->setData(0, KindRole, static_cast<int>(kind));
        item->setData(0, NameRole, path);
        item->setToolTip(0, path);
        item->setFlags((item->flags() | Qt::ItemIsEditable) & ~Qt::ItemIsDropEnabled);
        if (date > 0)
            item->setText(DateColumn, QDateTime::fromSecsSinceEpoch(date).toString("yyyy-MM-dd hh:mm"));
        if (QTreeWidgetItem* parent = folderItem(slash < 0 ? QString() : path.left(slash)))
            parent->addChild(item);
        else
            _tree->addTopLevelItem(item);
        ++items;
    };

    if (doc->symbolList)
    {
        for (const fla::Symbol* symbol : doc->symbolList->symbols)
        {
            if (symbol)
            {
                // A symbol kept in step with a master file says so.
                fla::links::Link link;
                const bool linked = fla::links::linkOf(*symbol, link);
                add(QString::fromStdString(symbol->name), symbolIcon(symbol->symbolType),
                    typeName(symbol->symbolType) + (linked ? QString(" (linked)") : QString()), Kind::Symbol,
                    QString::fromStdString(fla::library::linkageOf(*symbol)),
                    QString::fromStdString(symbol->lastModified).toLongLong());
            }
        }
    }
    for (const fla::Resource* resource : doc->resources)
    {
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
            add(QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name), bitmapIcon(),
                "Bitmap", Kind::Bitmap, QString(), static_cast<const fla::Bitmap*>(resource)->sourceLastImported);
    }
    for (const fla::Document::SoundInfo& sound : doc->sounds)
        add(QString::fromStdString(sound.name), soundIcon(), "Sound", Kind::Sound);
    for (const fla::Document::VideoInfo& video : doc->videos)
        add(QString::fromStdString(video.name), videoIcon(), video.embedded ? "Embedded Video" : "Video",
            Kind::Video);
    for (const fla::fonts::FontItem& font : fla::fonts::itemsOf(*doc))
        add(QString::fromStdString(font.name), fontIcon(), "Font", Kind::Font);

    _count->setText(QString("%1 item%2").arg(items).arg(items == 1 ? "" : "s"));
    _tree->setSortingEnabled(true);
    updateCounts();
    applyFilter();
    _refreshing = false;
    updatePreview();
}
