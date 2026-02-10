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

void DocumentView::setDocument(const FLADocument* document)
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
                const Timeline* timeline = static_cast<const Timeline*>(data);
                for (const Layer* layer : timeline->layers)
                {
                    buildLayerTree(item, layer, true);
                }
            }
            break;

        case ItemType::Layer:
            if (data)
            {
                const Layer* layer = static_cast<const Layer*>(data);
                for (const Frame* frame : layer->frames)
                {
                    buildFrameTree(item, frame, true);
                }
            }
            break;

        case ItemType::Frame:
            if (data)
            {
                const Frame* frame = static_cast<const Frame*>(data);
                for (const Element* element : frame->elements)
                {
                    buildElementTree(item, element, true);
                }
            }
            break;

        case ItemType::Element:
            if (data)
            {
                const Element* element = static_cast<const Element*>(data);

                // If it's a shape, add its edges
                if (element->elementType() == Element::Type::Shape)
                {
                    const ::Shape* shape = static_cast<const ::Shape*>(element);
                    int edgeIndex = 0;
                    for (const Edge* edge : shape->edges)
                    {
                        QString edgeName = QString("Edge %1").arg(edgeIndex++);
                        QTreeWidgetItem* edgeItem = createTreeItem(edgeName, (DOMElement*)edge, item);
                        setItemTypeData(edgeItem, ItemType::Other, nullptr);

                        // Add segments as children of the edge
                        int segmentIndex = 0;
                        for (const PathSegment& segment : edge->segments)
                        {
                            QString segmentName = QString("Segment %1").arg(segmentIndex++);
                            QTreeWidgetItem* segmentItem = createTreeItem(segmentName, (DOMElement*)&segment, edgeItem);
                            setItemTypeData(segmentItem, ItemType::Other, nullptr);

                            // Add sections as children of the segment
                            for (const PathSection& section : segment.sections)
                            {
                                QString sectionName;
                                switch (section.command)
                                {
                                    case PathSection::Command::Move:
                                        sectionName = "Move To";
                                        break;
                                    case PathSection::Command::Line:
                                        sectionName = "Line To";
                                        break;
                                    case PathSection::Command::Quad:
                                        sectionName = "Quadratic Curve To";
                                        break;
                                    case PathSection::Command::Cubic:
                                        sectionName = "Cubic Curve To";
                                        break;
                                    case PathSection::Command::Close:
                                        sectionName = "Close Path";
                                        break;
                                }
                                for (size_t i = 0; i < section.points.size(); ++i)
                                {
                                    const Point& pt = section.points[i];
                                    sectionName += QString(" (x: %1, y: %2)").arg(pt.x).arg(pt.y);
                                }
                                QTreeWidgetItem* sectionItem = createTreeItem(sectionName, nullptr, segmentItem);
                                setItemTypeData(sectionItem, ItemType::Other, nullptr);
                            }
                        }
                    }
                }

                // If it's a group, add its children
                if (element->elementType() == Element::Type::Group)
                {
                    const Group* group = static_cast<const Group*>(element);
                    for (const Element* member : group->members)
                    {
                        buildElementTree(item, member, true);
                    }
                }
            }
            break;

        case ItemType::Symbol:
            if (data)
            {
                const Symbol* symbol = static_cast<const Symbol*>(data);
                for (const Timeline* timeline : symbol->timelines)
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
                    const Symbol* symbol = symbolPair.second;
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
        const Timeline* mainTimeline = _flaDocument->document->timelines[0];
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

void DocumentView::buildTimelineTree(QTreeWidgetItem* parentItem, const Timeline* timeline, bool lazy)
{
    QString timelineName = timeline->name.empty() ? "Timeline" : QString::fromStdString(timeline->name);
    QTreeWidgetItem* timelineItem = createTreeItem(timelineName, (DOMElement*)timeline, parentItem);
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
        for (const Layer* layer : timeline->layers)
        {
            buildLayerTree(timelineItem, layer, lazy);
        }
        _populatedItems.insert(timelineItem);
    }
}

void DocumentView::buildLayerTree(QTreeWidgetItem* parentItem, const Layer* layer, bool lazy)
{
    QString layerName = QString("%1 %2")
        .arg(layer->name.empty() ? "Layer" : QString::fromStdString(layer->name))
        .arg(layer->visible ? "" : "(Hidden)");

    QTreeWidgetItem* layerItem = createTreeItem(layerName, (DOMElement*)layer, parentItem);
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
        for (const Frame* frame : layer->frames)
        {
            buildFrameTree(layerItem, frame, lazy);
        }
        _populatedItems.insert(layerItem);
    }
}

void DocumentView::buildFrameTree(QTreeWidgetItem* parentItem, const Frame* frame, bool lazy)
{
    QTreeWidgetItem* frameItem = createTreeItem(
        QString("Frame %1").arg(frame->index),
        (DOMElement*)frame,
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
        for (const Element* element : frame->elements)
        {
            buildElementTree(frameItem, element, lazy);
        }
        _populatedItems.insert(frameItem);
    }
}

void DocumentView::buildElementTree(QTreeWidgetItem* parentItem, const Element* element, bool lazy)
{
    QString elementName;
    bool hasChildren = false;

    switch (element->elementType())
    {
        case Element::Type::Shape:
            {
                const ::Shape* shape = static_cast<const ::Shape*>(element);
                elementName = "Shape";
                hasChildren = !shape->edges.empty();
            }
            break;
        case Element::Type::SymbolInstance:
            {
                const SymbolInstance* instance = static_cast<const SymbolInstance*>(element);
                elementName = QString("Symbol: %1").arg(QString::fromStdString(instance->libraryItemName));
            }
            break;
        case Element::Type::Group:
            {
                const Group* group = static_cast<const Group*>(element);
                elementName = "Group";
                hasChildren = !group->members.empty();
            }
            break;
        case Element::Type::StaticText:
            elementName = "StaticText";
            break;
        case Element::Type::BitmapInstance:
            {
                const BitmapInstance* instance = static_cast<const BitmapInstance*>(element);
                elementName = QString("Bitmap: %1").arg(QString::fromStdString(instance->libraryItemName));
            }
            break;
    }

    QTreeWidgetItem* elementItem = createTreeItem(elementName, (DOMElement*)element, parentItem);
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

void DocumentView::buildSymbolTree(QTreeWidgetItem* parentItem, const Symbol* symbol, bool lazy)
{
    QTreeWidgetItem* symbolItem = createTreeItem(
        QString("%1").arg(QString::fromStdString(symbol->name)),
        (DOMElement*)symbol,
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
        for (const Timeline* timeline : symbol->timelines)
        {
            buildTimelineTree(symbolItem, timeline, lazy);
        }
        _populatedItems.insert(symbolItem);
    }
}

QTreeWidgetItem* DocumentView::createTreeItem(const QString& text, DOMElement* domElement, QTreeWidgetItem* parent)
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

    DOMElement* element = _itemToElement[item];
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
            DOMElement* element = _itemToElement[item];
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
