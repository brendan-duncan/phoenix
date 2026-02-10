#include "document_view.h"
#include "data/bitmap_instance.h"
#include "data/group.h"
#include "data/symbol_instance.h"
#include "data/shape.h"

#include <QHeaderView>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>

DocumentView::DocumentView(QWidget *parent)
    : QTreeWidget(parent)
    , _flaDocument(nullptr)
{
    // Set up the tree widget
    setColumnCount(1);
    setHeaderLabels(QStringList() << "Name");

    // Configure appearance
    setAlternatingRowColors(true);
    setRootIsDecorated(true);
    setSortingEnabled(false);
    setSelectionMode(QAbstractItemView::SingleSelection);

    // Connect lazy loading signal
    connect(this, &QTreeWidget::itemExpanded, this, &DocumentView::onItemExpanded);

    // Visible icon
    {
        QString iconText = "👁";
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setFont(QFont("Segoe UI Emoji", 10));
        painter.setPen(Qt::black);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, iconText);
        _visibleIcon = QIcon(pixmap);
    }
    // Hidden icon
    {
        // Use Unicode characters for visibility icons
        QString iconText = "⚠";
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setFont(QFont("Segoe UI Emoji", 10));
        painter.setPen(Qt::gray);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, iconText);
        _hiddenIcon = QIcon(pixmap);
    }
}

DocumentView::~DocumentView()
{
}

void DocumentView::setDocument(const fla::FLADocument* document)
{
    _flaDocument = document;
    refreshTree();
}

void DocumentView::refreshTree()
{
    clearTree();

    if (!_flaDocument || !_flaDocument->document)
        return;

    buildDocumentTree();
    // Don't auto-expand - let user expand as needed for lazy loading
}

void DocumentView::clearTree()
{
    clear();
    _itemToElement.clear();
    _elementToItem.clear();
    _populatedItems.clear();
}

void DocumentView::setItemTypeData(QTreeWidgetItem* item, ItemType type, void* data)
{
    item->setData(0, ItemTypeRole, QVariant::fromValue(static_cast<int>(type)));
    item->setData(0, ItemDataRole, QVariant::fromValue(reinterpret_cast<quintptr>(data)));
}

DocumentView::ItemType DocumentView::getItemType(QTreeWidgetItem* item) const
{
    return static_cast<ItemType>(item->data(0, ItemTypeRole).toInt());
}

void* DocumentView::getItemData(QTreeWidgetItem* item) const
{
    return reinterpret_cast<void*>(item->data(0, ItemDataRole).value<quintptr>());
}

void DocumentView::onItemExpanded(QTreeWidgetItem* item)
{
    // If this item hasn't been populated yet, populate it now
    if (!_populatedItems.contains(item))
    {
        populateItemChildren(item);
        _populatedItems.insert(item);
    }
}

void DocumentView::populateItemChildren(QTreeWidgetItem* item)
{
    // Remove the dummy child if present
    if (item->childCount() == 1 && item->child(0)->text(0) == "Loading...")
    {
        delete item->takeChild(0);
    }

    ItemType itemType = getItemType(item);
    void* data = getItemData(item);

    switch (itemType)
    {
        case ItemType::Timeline:
            if (data)
            {
                const fla::Timeline* timeline = static_cast<const fla::Timeline*>(data);
                for (const fla::Layer* layer : timeline->layers)
                {
                    buildLayerTree(item, layer, true);
                }
            }
            break;

        case ItemType::Layer:
            if (data)
            {
                const fla::Layer* layer = static_cast<const fla::Layer*>(data);
                for (const fla::Frame* frame : layer->frames)
                {
                    buildFrameTree(item, frame, true);
                }
            }
            break;

        case ItemType::Frame:
            if (data)
            {
                const fla::Frame* frame = static_cast<const fla::Frame*>(data);
                for (const fla::Element* element : frame->elements)
                {
                    buildElementTree(item, element, true);
                }
            }
            break;

        case ItemType::Element:
            if (data)
            {
                const fla::Element* element = static_cast<const fla::Element*>(data);

                // If it's a shape, add its edges
                if (element->elementType() == fla::Element::Type::Shape)
                {
                    const fla::Shape* shape = static_cast<const fla::Shape*>(element);
                    int edgeIndex = 0;
                    for (const fla::Edge* edge : shape->edges)
                    {
                        QString edgeName = QString("Edge %1").arg(edgeIndex++);
                        QTreeWidgetItem* edgeItem = createTreeItem(edgeName, (fla::DOMElement*)edge, item);
                        setItemTypeData(edgeItem, ItemType::Other, nullptr);

                        // Add segments as children of the edge
                        int segmentIndex = 0;
                        for (const fla::PathSegment& segment : edge->segments)
                        {
                            QString segmentName = QString("Segment %1").arg(segmentIndex++);
                            QTreeWidgetItem* segmentItem = createTreeItem(segmentName, (fla::DOMElement*)&segment, edgeItem);
                            setItemTypeData(segmentItem, ItemType::Other, nullptr);

                            // Add sections as children of the segment
                            for (const fla::PathSection& section : segment.sections)
                            {
                                QString sectionName;
                                switch (section.command)
                                {
                                    case fla::PathSection::Command::Move:
                                        sectionName = "Move To";
                                        break;
                                    case fla::PathSection::Command::Line:
                                        sectionName = "Line To";
                                        break;
                                    case fla::PathSection::Command::Quad:
                                        sectionName = "Quadratic Curve To";
                                        break;
                                    case fla::PathSection::Command::Cubic:
                                        sectionName = "Cubic Curve To";
                                        break;
                                    case fla::PathSection::Command::Close:
                                        sectionName = "Close Path";
                                        break;
                                }
                                for (size_t i = 0; i < section.points.size(); ++i)
                                {
                                    const fla::Point& pt = section.points[i];
                                    sectionName += QString(" (x: %1, y: %2)").arg(pt.x).arg(pt.y);
                                }
                                QTreeWidgetItem* sectionItem = createTreeItem(sectionName, nullptr, segmentItem);
                                setItemTypeData(sectionItem, ItemType::Other, nullptr);
                            }
                        }
                    }
                }

                // If it's a group, add its children
                if (element->elementType() == fla::Element::Type::Group)
                {
                    const fla::Group* group = static_cast<const fla::Group*>(element);
                    for (const fla::Element* member : group->members)
                    {
                        buildElementTree(item, member, true);
                    }
                }
            }
            break;

        case ItemType::Symbol:
            if (data)
            {
                const fla::Symbol* symbol = static_cast<const fla::Symbol*>(data);
                for (const fla::Timeline* timeline : symbol->timelines)
                {
                    buildTimelineTree(item, timeline, true);
                }
            }
            break;

        case ItemType::SymbolList:
            if (_flaDocument && _flaDocument->document)
            {
                for (const auto& symbolPair : _flaDocument->document->symbolMap)
                {
                    const fla::Symbol* symbol = symbolPair.second;
                    buildSymbolTree(item, symbol, true);
                }
            }
            break;

        default:
            break;
    }
}

void DocumentView::buildDocumentTree()
{
    if (!_flaDocument->document)
        return;

    // Root document item
    QTreeWidgetItem* docItem = createTreeItem(
        QString("Document (%1x%2)").arg(_flaDocument->document->width).arg(_flaDocument->document->height),
        _flaDocument->document
    );
    addTopLevelItem(docItem);
    setItemTypeData(docItem, ItemType::Document, _flaDocument->document);

    // Add main timeline
    if (!_flaDocument->document->timelines.empty())
    {
        const fla::Timeline* mainTimeline = _flaDocument->document->timelines[0];
        buildTimelineTree(docItem, mainTimeline, true);
    }

    // Add symbols container
    QTreeWidgetItem* symbolsItem = createTreeItem("Symbols", nullptr, docItem);
    setItemTypeData(symbolsItem, ItemType::SymbolList, nullptr);

    // Add dummy child to enable expansion if there are symbols
    if (!_flaDocument->document->symbolMap.empty())
    {
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(symbolsItem);
        dummyItem->setText(0, "Loading...");
    }

    // Expand document by default
    docItem->setExpanded(true);
}

void DocumentView::buildTimelineTree(QTreeWidgetItem* parentItem, const fla::Timeline* timeline, bool lazy)
{
    QString timelineName = timeline->name.empty() ? "Timeline" : QString::fromStdString(timeline->name);
    QTreeWidgetItem* timelineItem = createTreeItem(timelineName, (fla::DOMElement*)timeline, parentItem);
    setItemTypeData(timelineItem, ItemType::Timeline, (void*)timeline);

    if (lazy && !timeline->layers.empty())
    {
        // Add dummy child to enable expansion
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(timelineItem);
        dummyItem->setText(0, "Loading...");
    }
    else
    {
        // Build immediately
        for (const fla::Layer* layer : timeline->layers)
        {
            buildLayerTree(timelineItem, layer, lazy);
        }
        _populatedItems.insert(timelineItem);
    }
}

void DocumentView::buildLayerTree(QTreeWidgetItem* parentItem, const fla::Layer* layer, bool lazy)
{
    QString layerName = QString("%1 %2")
        .arg(layer->name.empty() ? "Layer" : QString::fromStdString(layer->name))
        .arg(layer->visible ? "" : "(Hidden)");

    QTreeWidgetItem* layerItem = createTreeItem(layerName, (fla::DOMElement*)layer, parentItem);
    setItemTypeData(layerItem, ItemType::Layer, (void*)layer);
    updateItemVisibility(layerItem);

    if (lazy && !layer->frames.empty())
    {
        // Add dummy child to enable expansion
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(layerItem);
        dummyItem->setText(0, "Loading...");
    }
    else
    {
        // Build immediately
        for (const fla::Frame* frame : layer->frames)
        {
            buildFrameTree(layerItem, frame, lazy);
        }
        _populatedItems.insert(layerItem);
    }
}

void DocumentView::buildFrameTree(QTreeWidgetItem* parentItem, const fla::Frame* frame, bool lazy)
{
    QTreeWidgetItem* frameItem = createTreeItem(
        QString("Frame %1").arg(frame->index),
        (fla::DOMElement*)frame,
        parentItem
    );
    setItemTypeData(frameItem, ItemType::Frame, (void*)frame);

    if (lazy && !frame->elements.empty())
    {
        // Add dummy child to enable expansion
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(frameItem);
        dummyItem->setText(0, "Loading...");
    }
    else
    {
        // Build immediately
        for (const fla::Element* element : frame->elements)
        {
            buildElementTree(frameItem, element, lazy);
        }
        _populatedItems.insert(frameItem);
    }
}

void DocumentView::buildElementTree(QTreeWidgetItem* parentItem, const fla::Element* element, bool lazy)
{
    QString elementName;
    bool hasChildren = false;

    switch (element->elementType())
    {
        case fla::Element::Type::Shape:
            {
                const fla::Shape* shape = static_cast<const fla::Shape*>(element);
                elementName = "Shape";
                hasChildren = !shape->edges.empty();
            }
            break;
        case fla::Element::Type::SymbolInstance:
            {
                const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
                elementName = QString("Symbol: %1").arg(QString::fromStdString(instance->libraryItemName));
            }
            break;
        case fla::Element::Type::Group:
            {
                const fla::Group* group = static_cast<const fla::Group*>(element);
                elementName = "Group";
                hasChildren = !group->members.empty();
            }
            break;
        case fla::Element::Type::StaticText:
            elementName = "StaticText";
            break;
        case fla::Element::Type::BitmapInstance:
            {
                const fla::BitmapInstance* instance = static_cast<const fla::BitmapInstance*>(element);
                elementName = QString("Bitmap: %1").arg(QString::fromStdString(instance->libraryItemName));
            }
            break;
    }

    QTreeWidgetItem* elementItem = createTreeItem(elementName, (fla::DOMElement*)element, parentItem);
    setItemTypeData(elementItem, ItemType::Element, (void*)element);
    updateItemVisibility(elementItem);

    if (lazy && hasChildren)
    {
        // Add dummy child to enable expansion
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(elementItem);
        dummyItem->setText(0, "Loading...");
    }
    else if (!lazy)
    {
        // Build children immediately (handled by populateItemChildren for consistency)
        populateItemChildren(elementItem);
        _populatedItems.insert(elementItem);
    }
}

void DocumentView::buildSymbolTree(QTreeWidgetItem* parentItem, const fla::Symbol* symbol, bool lazy)
{
    QTreeWidgetItem* symbolItem = createTreeItem(
        QString("%1").arg(QString::fromStdString(symbol->name)),
        (fla::DOMElement*)symbol,
        parentItem
    );
    setItemTypeData(symbolItem, ItemType::Symbol, (void*)symbol);

    if (lazy && !symbol->timelines.empty())
    {
        // Add dummy child to enable expansion
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(symbolItem);
        dummyItem->setText(0, "Loading...");
    }
    else
    {
        // Build immediately
        for (const fla::Timeline* timeline : symbol->timelines)
        {
            buildTimelineTree(symbolItem, timeline, lazy);
        }
        _populatedItems.insert(symbolItem);
    }
}

QTreeWidgetItem* DocumentView::createTreeItem(const QString& text, fla::DOMElement* domElement, QTreeWidgetItem* parent)
{
    QTreeWidgetItem* item;

    if (parent)
    {
        item = new QTreeWidgetItem(parent);
    }
    else
    {
        item = new QTreeWidgetItem(this);
    }

    item->setText(0, text);

    bool isVisible = domElement ? domElement->visible : true;
    item->setIcon(0, isVisible ? _visibleIcon : _hiddenIcon);

    // Store mapping for quick lookup
    if (domElement)
    {
        _itemToElement[item] = domElement;
        _elementToItem[domElement] = item;
    }

    return item;
}

void DocumentView::updateItemVisibility(QTreeWidgetItem* item)
{
    if (!_itemToElement.contains(item))
        return;

    fla::DOMElement* element = _itemToElement[item];
    item->setIcon(0, element->visible ? _visibleIcon : _hiddenIcon);

    // Update item text to reflect visibility
    QString text = item->text(0);
    bool isHidden = text.contains("(Hidden)");

    if (!element->visible && !isHidden)
    {
        item->setText(0, text + " (Hidden)");
    }
    else if (element->visible && isHidden)
    {
        item->setText(0, text.replace(" (Hidden)", ""));
    }
}

void DocumentView::mousePressEvent(QMouseEvent *event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    
    if (item && _itemToElement.contains(item) && event->button() == Qt::LeftButton)
    {
        // Get the visual rectangle of the item
        QRect itemRect = visualItemRect(item);
        
        // Calculate icon area (first ~20 pixels)
        int iconWidth = 25; // Approximate icon area width including indentation
        QRect iconRect = itemRect;
        iconRect.setWidth(iconWidth);
        
        // Check if click is within icon area
        if (iconRect.contains(event->pos()))
        {
            // Toggle visibility
            fla::DOMElement* element = _itemToElement[item];
            element->visible = !element->visible;
            
            updateItemVisibility(item);
            emit visibilityChanged();
            
            // Don't propagate the click to the base class
            return;
        }
    }
    
    // For other clicks, use default behavior
    QTreeWidget::mousePressEvent(event);
}
