#pragma once

#include <QTreeWidget>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;

namespace fla {
class FLADocument;
}

/// The tree under the Library panel, which starts a drag of the item under
/// the cursor with what the stage needs to place it, and takes drops of its
/// own items onto folders to move them there.
class LibraryTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit LibraryTree(QWidget* parent = nullptr);

    /// The MIME type a library drag carries; its data is "symbol:Name",
    /// "bitmap:Name", "sound:Name" or "folder:Name". The stage places the
    /// first two.
    static const char* mimeType();

signals:
    /// An item or folder was dropped into the folder \a folder (empty for
    /// the top level).
    void moveRequested(const QString& name, const QString& folder);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    /// The folder a drop at \a position goes into: the folder under it, the
    /// folder holding the item under it, or the top level.
    QString folderAt(const QPoint& position) const;
};

/// Animate's Library panel: the document's symbols, bitmaps and sounds in
/// their folders, with a count, the kind of each, how many times it is
/// used and the class a symbol is exported as. A search field above the
/// list narrows it by name. Dragging an item to
/// the stage places an instance of it there; dragging it onto a folder in
/// the list moves it there; double-clicking a symbol edits it, and
/// double-clicking a name (or Rename) renames it.
class LibraryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryPanel(QWidget* parent = nullptr);

    void setDocument(const fla::FLADocument* document);

    /// What draws the preview of an item: its kind ("symbol" or "bitmap"),
    /// its name and the size to fill.
    void setPreviewProvider(std::function<QImage(const QString&, const QString&, const QSize&)> provider);

    /// Rebuilds the list, keeping which folders are open.
    void refresh();

    /// Rebuilds the list only when the items no longer match the document,
    /// and brings the use counts up to date.
    void refreshIfStale();

    /// Selects every item the document does not need (see
    /// fla::library::usedItems), as Animate's Select Unused Items does.
    void selectUnused();

signals:
    /// A symbol was double-clicked, to be edited on its own.
    void editSymbolRequested(const QString& name);

    /// The item menu's commands, for the window to carry out undoably.
    /// Any item or folder, by its full name, to a new full name in the
    /// same folder.
    void renameRequested(const QString& name, const QString& newName);
    /// An item or folder dropped into another folder.
    void moveRequested(const QString& name, const QString& folder);
    void duplicateSymbolRequested(const QString& name);
    /// The symbols and bitmaps selected, to be deleted.
    void deleteItemsRequested(const QStringList& names);
    /// A symbol's linkage, to be set.
    void linkageRequested(const QString& name);
    /// A new folder inside \a parentPath (empty for the top level).
    void newFolderRequested(const QString& parentPath);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Kind
    {
        Folder,
        Symbol,
        Bitmap,
        Sound
    };

    QTreeWidgetItem* folderItem(const QString& path);
    QStringList snapshot() const;
    /// The Use Count column, from the document as it is now.
    void updateCounts();
    /// Shows only the items whose names hold the search text, and the
    /// folders they are in.
    void applyFilter();
    /// The full names of the symbols and bitmaps selected.
    QStringList selectedItems() const;

    const fla::FLADocument* _document = nullptr;
    void updatePreview();

    LibraryTree* _tree;
    QLabel* _preview;
    QLabel* _count;
    QLineEdit* _search;
    std::function<QImage(const QString&, const QString&, const QSize&)> _previewProvider;
    QStringList _built;
    QStringList _openFolders;
    bool _refreshing = false;
};
