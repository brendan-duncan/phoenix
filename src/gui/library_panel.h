#pragma once

#include <QTreeWidget>
#include <QWidget>

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

    /// Rebuilds the list, keeping which folders are open.
    void refresh();

    /// Rebuilds the list only when the items no longer match the document.
    void refreshIfStale();

signals:
    /// A symbol was double-clicked, to be edited on its own.
    void editSymbolRequested(const QString& name);

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
    LibraryTree* _tree;
    QLabel* _count;
    QStringList _built;
    QStringList _openFolders;
};
