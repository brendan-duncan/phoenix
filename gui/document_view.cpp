#include "document_view.h"
#include "../data/bitmap_instance.h"
#include "../data/edge.h"
#include "../data/group.h"
#include "../data/symbol_instance.h"
#include "../data/shape.h"

#include <QHeaderView>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>

DocumentView::DocumentView(QWidget *parent)
    : QTreeWidget(parent)
    , _flaDocument(nullptr)
{
    // Set up the tree widget
    setColumnCount(2);
    setHeaderLabels(QStringList() << "Name" << "Type");

    // Configure appearance
    setAlternatingRowColors(true);
    setRootIsDecorated(true);
    setSortingEnabled(false);
    setSelectionMode(QAbstractItemView::SingleSelection);

    // Set column widths
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::Fixed);
    setColumnWidth(1, 24); // Type icon column width

    // Connect lazy loading signal
    connect(this, &QTreeWidget::itemExpanded, this, &DocumentView::onItemExpanded);
    connect(this, &QTreeWidget::itemSelectionChanged, this, &DocumentView::onSelectionChanged);

    // Create type icons
    createTypeIcons();

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

void DocumentView::createTypeIcons()
{
    // Helper lambda to create icon from text
    auto createIcon = [](const QString& text, const QColor& color) -> QIcon {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setFont(QFont("Segoe UI Emoji", 10));
        painter.setPen(color);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
        return QIcon(pixmap);
    };

    // Create type-specific icons with Unicode symbols
    _documentIcon = createIcon("📋", QColor(70, 130, 180));   // Clipboard for document
    _timelineIcon = createIcon("⏱", QColor(100, 100, 200));   // Clock for timeline
    _layerIcon = createIcon("≣", QColor(150, 150, 50));      // Document for layer
    _frameIcon = createIcon("🎞", QColor(200, 100, 100));      // Film for frame
    _shapeIcon = createIcon("▢", QColor(100, 200, 100));      // Square for shape
    _groupIcon = createIcon("📁", QColor(200, 150, 50));       // Folder for group
    _symbolIcon = createIcon("⭐", QColor(200, 200, 50));      // Star for symbol
    _bitmapIcon = createIcon("🖼", QColor(150, 100, 200));     // Picture for bitmap
    _textIcon = createIcon("A", QColor(255, 255, 255));          // A for text
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

void DocumentView::setItemData(QTreeWidgetItem* item, const fla::DOMElement* element)
{
    item->setData(0, ItemDataRole, QVariant::fromValue(reinterpret_cast<quintptr>(element)));

    // Set type icon and tooltip based on item type
    QIcon typeIcon;
    QString typeTooltip;

    switch (element->domType())
    {
        case fla::DOMElement::DOMType::Document:
            typeIcon = _documentIcon;
            typeTooltip = "Document";
            break;
        case fla::DOMElement::DOMType::Timeline:
            typeIcon = _timelineIcon;
            typeTooltip = "Timeline";
            break;
        case fla::DOMElement::DOMType::Layer:
            typeIcon = _layerIcon;
            typeTooltip = "Layer";
            break;
        case fla::DOMElement::DOMType::Frame:
            typeIcon = _frameIcon;
            typeTooltip = "Frame";
            break;
        case fla::DOMElement::DOMType::Symbol:
            typeIcon = _symbolIcon;
            typeTooltip = "Symbol";
            break;
        case fla::DOMElement::DOMType::SymbolList:
            typeIcon = _symbolIcon;
            typeTooltip = "Symbols Folder";
            break;
        case fla::DOMElement::DOMType::Shape:
            typeIcon = _shapeIcon;
            typeTooltip = "Shape";
            break;
        case fla::DOMElement::DOMType::Group:
            typeIcon = _groupIcon;
            typeTooltip = "Group";
            break;
        case fla::DOMElement::DOMType::SymbolInstance:
            typeIcon = _symbolIcon;
            typeTooltip = "Symbol Instance";
            break;
        case fla::DOMElement::DOMType::BitmapInstance:
            typeIcon = _bitmapIcon;
            typeTooltip = "Bitmap Instance";
            break;
        case fla::DOMElement::DOMType::StaticText:
            typeIcon = _textIcon;
            typeTooltip = "Static Text";
            break;
        default:
            typeTooltip = "Other";
            break;
    }

    if (!typeIcon.isNull())
    {
        item->setIcon(1, typeIcon);
    }

    if (!typeTooltip.isEmpty())
    {
        item->setToolTip(1, typeTooltip);
    }
}

const fla::DOMElement* DocumentView::getItemData(QTreeWidgetItem* item) const
{
    return reinterpret_cast<const fla::DOMElement*>(item->data(0, ItemDataRole).value<quintptr>());
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

    const fla::DOMElement* element = getItemData(item);

    switch (element->domType())
    {
        case fla::DOMElement::DOMType::Timeline:
        {
            const fla::Timeline* timeline = static_cast<const fla::Timeline*>(element);
            for (const fla::Layer* layer : timeline->layers)
            {
                buildLayerTree(item, layer, true);
            }
            break;
        }
        case fla::DOMElement::DOMType::Layer:
        {
            const fla::Layer* layer = static_cast<const fla::Layer*>(element);
            for (const fla::Frame* frame : layer->frames)
            {
                buildFrameTree(item, frame, true);
            }
            break;
        }
        case fla::DOMElement::DOMType::Frame:
        {
            const fla::Frame* frame = static_cast<const fla::Frame*>(element);
            for (const fla::Element* element : frame->elements)
            {
                buildElementTree(item, element, true);
            }
            break;
        }
        case fla::DOMElement::DOMType::Symbol:
        {
            const fla::Symbol* symbol = static_cast<const fla::Symbol*>(element);
            for (const fla::Timeline* timeline : symbol->timelines)
            {
                buildTimelineTree(item, timeline, true);
            }
            break;
        }
        case fla::DOMElement::DOMType::SymbolList:
        {
            if (_flaDocument && _flaDocument->document && _flaDocument->document->symbolList)
            {
                for (const auto& symbolPair : _flaDocument->document->symbolList->symbolMap)
                {
                    const fla::Symbol* symbol = symbolPair.second;
                    buildSymbolTree(item, symbol, true);
                }
            }
            break;
        }
        case fla::DOMElement::DOMType::Shape:
        {
            const fla::Shape* shape = static_cast<const fla::Shape*>(element);
            int edgeIndex = 0;
            for (const fla::Edge* edge : shape->edges)
            {
                QString edgeName = QString("Edge %1").arg(edgeIndex++);
                QTreeWidgetItem* edgeItem = createTreeItem(edgeName, edge, item);
                setItemData(edgeItem, edge);
                edgeItem->setIcon(1, _shapeIcon); // Use shape icon for edges
                edgeItem->setToolTip(1, "Edge");

                // Add segments as children of the edge
                int segmentIndex = 0;
                for (const fla::Path* path : edge->paths)
                {
                    QString segmentName = QString("Path %1").arg(segmentIndex++);
                    QTreeWidgetItem* pathItem = createTreeItem(segmentName, path, edgeItem);
                    setItemData(pathItem, path);
                    pathItem->setIcon(1, _shapeIcon);
                    pathItem->setToolTip(1, "Path");

                    // Add segments as children of the segment
                    for (const fla::PathSegment* segment : path->segments)
                    {
                        QString segmentName;
                        QString segmentType;
                        switch (segment->command)
                        {
                            case fla::PathSegment::Command::Move:
                                segmentName = "Move To";
                                segmentType = "Move Command";
                                break;
                            case fla::PathSegment::Command::Line:
                                segmentName = "Line To";
                                segmentType = "Line Command";
                                break;
                            case fla::PathSegment::Command::Quad:
                                segmentName = "Quadratic Curve To";
                                segmentType = "Quadratic Bezier";
                                break;
                            case fla::PathSegment::Command::Cubic:
                                segmentName = "Cubic Curve To";
                                segmentType = "Cubic Bezier";
                                break;
                            case fla::PathSegment::Command::Close:
                                segmentName = "Close Path";
                                segmentType = "Close Command";
                                break;
                        }
                        for (size_t i = 0; i < segment->points.size(); ++i)
                        {
                            const fla::Point& pt = segment->points[i];
                            segmentName += QString(" (x: %1, y: %2)").arg(pt.x).arg(pt.y);
                        }
                        // Store pointer to the segment for selection handling
                        const fla::PathSegment* segmentPtr = segment;
                        QTreeWidgetItem* segmentItem = createTreeItem(segmentName, nullptr, pathItem);
                        setItemData(segmentItem, segmentPtr);
                        segmentItem->setIcon(1, _shapeIcon);
                        segmentItem->setToolTip(1, segmentType);
                    }
                }
            }
            break;
        }
        case fla::DOMElement::DOMType::Group:
        {
            const fla::Group* group = static_cast<const fla::Group*>(element);
            for (const fla::Element* member : group->members)
            {
                buildElementTree(item, member, true);
            }
            break;
        }

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
    setItemData(docItem, _flaDocument->document);

    // Add main timeline
    if (!_flaDocument->document->timelines.empty())
    {
        const fla::Timeline* mainTimeline = _flaDocument->document->timelines[0];
        buildTimelineTree(docItem, mainTimeline, true);
    }

    // Add symbols container
    if (_flaDocument->document->symbolList)
    {
        QTreeWidgetItem* symbolsItem = createTreeItem("Symbols", _flaDocument->document->symbolList, docItem);
        setItemData(symbolsItem, _flaDocument->document->symbolList);

        // Add dummy child to enable expansion if there are symbols
        QTreeWidgetItem* dummyItem = new QTreeWidgetItem(symbolsItem);
        dummyItem->setText(0, "Loading...");
    }

    // Expand document by default
    docItem->setExpanded(true);
}

void DocumentView::buildTimelineTree(QTreeWidgetItem* parentItem, const fla::Timeline* timeline, bool lazy)
{
    QString timelineName = timeline->name.empty() ? "Timeline" : QString::fromStdString(timeline->name);
    QTreeWidgetItem* timelineItem = createTreeItem(timelineName, timeline, parentItem);
    setItemData(timelineItem, timeline);

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

    QTreeWidgetItem* layerItem = createTreeItem(layerName, layer, parentItem);
    setItemData(layerItem, layer);
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
    QTreeWidgetItem* frameItem = createTreeItem(QString("Frame %1").arg(frame->index), frame, parentItem);
    setItemData(frameItem, frame);

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
        case fla::Element::Type::Rectangle:
            elementName = "Rectangle";
            break;
        case fla::Element::Type::Oval:
            elementName = "Oval";
            break;
        default:
            elementName = "Unknown";
            break;
    }

    QTreeWidgetItem* elementItem = createTreeItem(elementName, element, parentItem);
    setItemData(elementItem, element);
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
    QTreeWidgetItem* symbolItem = createTreeItem(QString("%1").arg(QString::fromStdString(symbol->name)), symbol, parentItem);
    setItemData(symbolItem, symbol);

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

QTreeWidgetItem* DocumentView::createTreeItem(const QString& text, const fla::DOMElement* domElement, QTreeWidgetItem* parent)
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

    const fla::DOMElement* element = _itemToElement[item];
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
            const fla::DOMElement* element = _itemToElement[item];
            const_cast<fla::DOMElement*>(element)->visible = !element->visible;

            updateItemVisibility(item);
            emit visibilityChanged();

            // Don't propagate the click to the base class
            return;
        }
    }

    // For other clicks, use default behavior
    QTreeWidget::mousePressEvent(event);
}

void DocumentView::onSelectionChanged()
{
    QList<QTreeWidgetItem*> selected = selectedItems();
    if (selected.isEmpty())
    {
        emit elementSelected(nullptr);
        return;
    }

    QTreeWidgetItem* item = selected.first();
    if (!item)
    {
        emit elementSelected(nullptr);
        return;
    }

    const fla::DOMElement* element = getItemData(item);
    if (!element)
    {
        emit elementSelected(nullptr);
        return;
    }

    fla::DOMElement::DOMType type = element->domType();
    // Only emit for our specific types
    if (type == fla::DOMElement::DOMType::Symbol ||
        type == fla::DOMElement::DOMType::Shape ||
        type == fla::DOMElement::DOMType::Edge ||
        type == fla::DOMElement::DOMType::Path ||
        type == fla::DOMElement::DOMType::PathSegment)
    {
        emit elementSelected(element);
    }
    else
    {
        // For other types, emit nullptr to indicate no selection
        emit elementSelected(nullptr);
    }
}
