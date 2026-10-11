#include "library_panel.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/folder.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"

#include <QDrag>
#include <QHeaderView>
#include <QLabel>
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

const QColor kInk(220, 220, 220);

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
    });
}

QIcon symbolIcon(fla::SymbolType type)
{
    return glyph([type](QPainter& painter) {
        // Animate marks a movie clip with a reel, a graphic with shapes and a
        // button with a pointer; a simple glyph for each.
        painter.setPen(QPen(kInk, 1.2));
        painter.setBrush(Qt::NoBrush);
        switch (type)
        {
        case fla::SymbolType::MovieClip:
            painter.drawEllipse(QRectF(2.5, 2.5, 11, 11));
            painter.setBrush(kInk);
            painter.drawEllipse(QRectF(6.5, 6.5, 3, 3));
            break;
        case fla::SymbolType::Button:
            painter.drawRoundedRect(QRectF(2.5, 4.5, 11, 7), 2, 2);
            painter.setBrush(kInk);
            painter.drawEllipse(QRectF(6.5, 6.5, 3, 3));
            break;
        case fla::SymbolType::Graphic:
            painter.setBrush(QColor(kInk.red(), kInk.green(), kInk.blue(), 70));
            painter.drawEllipse(QRectF(2.5, 2.5, 7, 7));
            painter.drawRect(QRectF(6.5, 6.5, 7, 7));
            break;
        }
    });
}

QIcon bitmapIcon()
{
    return glyph([](QPainter& painter) {
        painter.setPen(QPen(kInk, 1.1));
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
    setDragDropMode(QAbstractItemView::DragOnly);
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
    if (kind == 0)
        return;
    const QString name = item->data(0, NameRole).toString();
    QMimeData* data = new QMimeData;
    data->setData(mimeType(), (QString(kind == 1 ? "symbol:" : "bitmap:") + name).toUtf8());
    QDrag* drag = new QDrag(this);
    drag->setMimeData(data);
    drag->setPixmap(item->icon(0).pixmap(16, 16));
    drag->exec(Qt::CopyAction);
}

LibraryPanel::LibraryPanel(QWidget* parent)
    : QWidget(parent)
    , _tree(new LibraryTree(this))
    , _preview(new QLabel(this))
    , _count(new QLabel(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The preview of the selected item, at the top as in Animate.
    _preview->setFixedHeight(150);
    _preview->setAlignment(Qt::AlignCenter);
    _preview->setStyleSheet("QLabel { background: #1e1e1e; border-bottom: 1px solid #3a3a3a; }");
    layout->addWidget(_preview);

    _count->setContentsMargins(10, 6, 10, 6);
    layout->addWidget(_count);

    _tree->setColumnCount(2);
    _tree->setHeaderLabels({"Name", "Type"});
    _tree->header()->setStretchLastSection(false);
    _tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _tree->setIndentation(14);
    _tree->setUniformRowHeights(true);
    _tree->setSortingEnabled(true);
    _tree->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(_tree, 1);

    connect(_tree, &QTreeWidget::currentItemChanged, this, [this]() { updatePreview(); });

    // The item menu: Rename, Duplicate, Delete, New Folder, as Animate's
    // library offers them.
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    _tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QTreeWidgetItem* item = _tree->itemAt(at);
        const int kind = item ? item->data(0, KindRole).toInt() : -1;
        const QString name = item ? item->data(0, NameRole).toString() : QString();
        QMenu menu(this);
        QAction* rename = nullptr;
        QAction* duplicate = nullptr;
        QAction* remove = nullptr;
        if (kind == static_cast<int>(Kind::Symbol))
        {
            rename = menu.addAction("Rename");
            duplicate = menu.addAction("Duplicate");
            remove = menu.addAction("Delete");
            menu.addSeparator();
        }
        QAction* folder = menu.addAction("New Folder");
        QAction* chosen = menu.exec(_tree->viewport()->mapToGlobal(at));
        if (!chosen)
            return;
        if (chosen == rename)
        {
            _tree->editItem(item, 0);
        }
        else if (chosen == duplicate)
        {
            emit duplicateSymbolRequested(name);
        }
        else if (chosen == remove)
        {
            emit deleteSymbolRequested(name);
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
    connect(_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (_refreshing || column != 0 || item->data(0, KindRole).toInt() != static_cast<int>(Kind::Symbol))
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
        QTimer::singleShot(0, this, [this, name, newName]() { emit renameSymbolRequested(name, newName); });
    });
    connect(_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item && item->data(0, KindRole).toInt() == static_cast<int>(Kind::Symbol))
            emit editSymbolRequested(item->data(0, NameRole).toString());
    });
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
    if (!item || kind == static_cast<int>(Kind::Folder) || !_previewProvider || !_document)
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
            names << "s:" + QString::fromStdString(symbol->name);
    for (const fla::Resource* resource : doc->resources)
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
            names << "b:" + QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name);
    return names;
}

void LibraryPanel::refreshIfStale()
{
    if (snapshot() != _built)
        refresh();
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
            found = new QTreeWidgetItem();
            found->setText(0, part);
            found->setIcon(0, folderIcon());
            found->setText(1, "Folder");
            found->setData(0, KindRole, static_cast<int>(Kind::Folder));
            found->setData(0, NameRole, sofar);
            found->setFlags(found->flags() & ~Qt::ItemIsDragEnabled);
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
    const auto add = [&](const QString& path, const QIcon& icon, const QString& type, Kind kind) {
        const int slash = path.lastIndexOf('/');
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, slash < 0 ? path : path.mid(slash + 1));
        item->setIcon(0, icon);
        item->setText(1, type);
        item->setData(0, KindRole, static_cast<int>(kind));
        item->setData(0, NameRole, path);
        item->setToolTip(0, path);
        if (kind == Kind::Symbol)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
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
                add(QString::fromStdString(symbol->name), symbolIcon(symbol->symbolType),
                    typeName(symbol->symbolType), Kind::Symbol);
        }
    }
    for (const fla::Resource* resource : doc->resources)
    {
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
            add(QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name), bitmapIcon(),
                "Bitmap", Kind::Bitmap);
    }

    _count->setText(QString("%1 item%2").arg(items).arg(items == 1 ? "" : "s"));
    _tree->setSortingEnabled(true);
    _refreshing = false;
    updatePreview();
}
