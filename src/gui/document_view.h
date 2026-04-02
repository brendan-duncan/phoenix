#pragma once

#include "../data/fla_document.h"
#include "../data/edge.h"

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

    void elementSelected(const fla::DOMElement* element);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onItemExpanded(QTreeWidgetItem* item);

    void onSelectionChanged();

private:
    const fla::FLADocument* _flaDocument;

    void clearTree();

    void buildDocumentTree();

    void buildTimelineTree(QTreeWidgetItem* parentItem, const fla::Timeline* timeline, bool lazy = true);

    void buildLayerTree(QTreeWidgetItem* parentItem, const fla::Layer* layer, bool lazy = true);

    void buildFrameTree(QTreeWidgetItem* parentItem, const fla::Frame* frame, bool lazy = true);

    void buildElementTree(QTreeWidgetItem* parentItem, const fla::Element* element, bool lazy = true);

    void buildSymbolTree(QTreeWidgetItem* parentItem, const fla::Symbol* symbol, bool lazy = true);

    void populateItemChildren(QTreeWidgetItem* item);

    QTreeWidgetItem* createTreeItem(const QString& text, const fla::DOMElement* domElement, QTreeWidgetItem* parent = nullptr);

    void updateItemVisibility(QTreeWidgetItem* item);

    void setItemData(QTreeWidgetItem* item, const fla::DOMElement* element);

    const fla::DOMElement* getItemData(QTreeWidgetItem* item) const;

    // Map items to their DOMElement pointers for quick access
    QMap<QTreeWidgetItem*, const fla::DOMElement*> _itemToElement;
    QMap<const fla::DOMElement*, QTreeWidgetItem*> _elementToItem;

    // Track which items have been populated
    QSet<QTreeWidgetItem*> _populatedItems;

    // Custom roles for storing item metadata
    static constexpr int ItemTypeRole = Qt::UserRole + 1;
    static constexpr int ItemDataRole = Qt::UserRole + 2;

    QIcon _visibleIcon;
    QIcon _hiddenIcon;

    // Type icons
    QIcon _documentIcon;
    QIcon _timelineIcon;
    QIcon _layerIcon;
    QIcon _frameIcon;
    QIcon _shapeIcon;
    QIcon _groupIcon;
    QIcon _symbolIcon;
    QIcon _bitmapIcon;
    QIcon _textIcon;

    void createTypeIcons();
};
