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

    void setDocument(const FLADocument* document);

    void refreshTree();

signals:
    void visibilityChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    const FLADocument* _flaDocument;
    
    void clearTree();

    void buildDocumentTree();

    void buildTimelineTree(QTreeWidgetItem* parentItem, const Timeline* timeline);

    void buildLayerTree(QTreeWidgetItem* parentItem, const Layer* layer);

    void buildFrameTree(QTreeWidgetItem* parentItem, const Frame* frame);

    void buildElementTree(QTreeWidgetItem* parentItem, const Element* element);

    void buildSymbolTree(QTreeWidgetItem* parentItem, const Symbol* symbol);

    QTreeWidgetItem* createTreeItem(const QString& text, DOMElement* domElement, QTreeWidgetItem* parent = nullptr);

    void updateItemVisibility(QTreeWidgetItem* item);

    QIcon getVisibilityIcon(bool visible) const;

    // Map items to their DOMElement pointers for quick access
    QMap<QTreeWidgetItem*, DOMElement*> _itemToElement;
    QMap<DOMElement*, QTreeWidgetItem*> _elementToItem;
};
