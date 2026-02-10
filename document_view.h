#pragma once

#include "data/fla_document.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>

class DocumentView : public QTreeWidget
{
    Q_OBJECT
public:
    DocumentView(QWidget *parent = nullptr);

    ~DocumentView();

    void setDocument(const fla::FLADocument* document);

    void refreshTree();

signals:
    void visibilityChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onItemExpanded(QTreeWidgetItem* item);

private:
    enum class ItemType {
        Document,
        Timeline,
        Layer,
        Frame,
        Element,
        Symbol,
        SymbolList,
        Other
    };

    const fla::FLADocument* _flaDocument;

    void clearTree();

    void buildDocumentTree();

    void buildTimelineTree(QTreeWidgetItem* parentItem, const fla::Timeline* timeline, bool lazy = true);

    void buildLayerTree(QTreeWidgetItem* parentItem, const fla::Layer* layer, bool lazy = true);

    void buildFrameTree(QTreeWidgetItem* parentItem, const fla::Frame* frame, bool lazy = true);

    void buildElementTree(QTreeWidgetItem* parentItem, const fla::Element* element, bool lazy = true);

    void buildSymbolTree(QTreeWidgetItem* parentItem, const fla::Symbol* symbol, bool lazy = true);

    void populateItemChildren(QTreeWidgetItem* item);

    QTreeWidgetItem* createTreeItem(const QString& text, fla::DOMElement* domElement, QTreeWidgetItem* parent = nullptr);

    void updateItemVisibility(QTreeWidgetItem* item);

    void setItemTypeData(QTreeWidgetItem* item, ItemType type, void* data = nullptr);
    ItemType getItemType(QTreeWidgetItem* item) const;
    void* getItemData(QTreeWidgetItem* item) const;

    // Map items to their DOMElement pointers for quick access
    QMap<QTreeWidgetItem*, fla::DOMElement*> _itemToElement;
    QMap<fla::DOMElement*, QTreeWidgetItem*> _elementToItem;

    // Track which items have been populated
    QSet<QTreeWidgetItem*> _populatedItems;

    // Custom roles for storing item metadata
    static constexpr int ItemTypeRole = Qt::UserRole + 1;
    static constexpr int ItemDataRole = Qt::UserRole + 2;

    QIcon _visibleIcon;
    QIcon _hiddenIcon;
};
