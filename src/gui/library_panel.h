#pragma once

#include <QTreeWidget>
#include <QWidget>

#include <functional>

class QLabel;

namespace fla {
class FLADocument;
}

/// The tree under the Library panel, which starts a drag of the item under
/// the cursor with what the stage needs to place it.
class LibraryTree : public QTreeWidget
{
public:
    explicit LibraryTree(QWidget* parent = nullptr);

    /// The MIME type a library drag carries; its data is "symbol:Name" or
    /// "bitmap:Name".
    static const char* mimeType();

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

/// Animate's Library panel: the document's symbols and bitmaps in their
/// folders, with a count and the kind of each. Dragging an item to the
/// stage places an instance of it there; double-clicking a symbol edits it.
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

    /// Rebuilds the list only when the items no longer match the document.
    void refreshIfStale();

signals:
    /// A symbol was double-clicked, to be edited on its own.
    void editSymbolRequested(const QString& name);

    /// The item menu's commands, for the window to carry out undoably.
    void renameSymbolRequested(const QString& name, const QString& newName);
    void duplicateSymbolRequested(const QString& name);
    void deleteSymbolRequested(const QString& name);
    /// A new folder inside \a parentPath (empty for the top level).
    void newFolderRequested(const QString& parentPath);

private:
    enum class Kind
    {
        Folder,
        Symbol,
        Bitmap
    };

    QTreeWidgetItem* folderItem(const QString& path);
    QStringList snapshot() const;

    const fla::FLADocument* _document = nullptr;
    void updatePreview();

    LibraryTree* _tree;
    QLabel* _preview;
    QLabel* _count;
    std::function<QImage(const QString&, const QString&, const QSize&)> _previewProvider;
    QStringList _built;
    QStringList _openFolders;
    bool _refreshing = false;
};
