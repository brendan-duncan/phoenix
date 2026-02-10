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
    expandToDepth(2); // Expand first few levels by default
}

void DocumentView::clearTree()
{
    clear();
    _itemToElement.clear();
    _elementToItem.clear();
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

    // Add main timeline
    if (!_flaDocument->document->timelines.empty())
    {
        const Timeline* mainTimeline = _flaDocument->document->timelines[0];
        buildTimelineTree(docItem, mainTimeline);
    }

    // Add symbols
    QTreeWidgetItem* symbolsItem = createTreeItem("Symbols", nullptr, docItem);
    for (const auto& symbolPair : _flaDocument->document->symbolMap)
    {
        const Symbol* symbol = symbolPair.second;
        buildSymbolTree(symbolsItem, symbol);
    }
}

void DocumentView::buildTimelineTree(QTreeWidgetItem* parentItem, const Timeline* timeline)
{
    QString timelineName = timeline->name.empty() ? "Timeline" : QString::fromStdString(timeline->name);
    QTreeWidgetItem* timelineItem = createTreeItem(timelineName, (DOMElement*)timeline, parentItem);

    // Add layers (in reverse order for proper visual stacking)
    for (const Layer* layer : timeline->layers)
    {
        buildLayerTree(timelineItem, layer);
    }
}

void DocumentView::buildLayerTree(QTreeWidgetItem* parentItem, const Layer* layer)
{
    QString layerName = QString("Layer %1 %2")
        .arg(layer->name.empty() ? "Layer" : QString::fromStdString(layer->name))
        .arg(layer->visible ? "" : "(Hidden)");

    QTreeWidgetItem* layerItem = createTreeItem(layerName, (DOMElement*)layer, parentItem);
    updateItemVisibility(layerItem);

    // Add frames
    for (const Frame* frame : layer->frames)
    {
        buildFrameTree(layerItem, frame);
    }
}

void DocumentView::buildFrameTree(QTreeWidgetItem* parentItem, const Frame* frame)
{
    QTreeWidgetItem* frameItem = createTreeItem(
        QString("Frame %1").arg(frame->index),
        (DOMElement*)frame,
        parentItem
    );

    // Add elements
    for (const Element* element : frame->elements)
    {
        buildElementTree(frameItem, element);
    }
}

void DocumentView::buildElementTree(QTreeWidgetItem* parentItem, const Element* element)
{
    QString elementName;
    switch (element->elementType())
    {
        case Element::Type::Shape:
            elementName = "Shape";
            break;
        case Element::Type::SymbolInstance:
            {
                const SymbolInstance* instance = static_cast<const SymbolInstance*>(element);
                elementName = QString("Symbol: %1").arg(QString::fromStdString(instance->libraryItemName));
            }
            break;
        case Element::Type::Group:
            elementName = "Group";
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
    updateItemVisibility(elementItem);

    // If it's a shape, add its edges
    if (element->elementType() == Element::Type::Shape)
    {
        const ::Shape* shape = static_cast<const ::Shape*>(element);
        int edgeIndex = 0;
        for (const Edge* edge : shape->edges)
        {
            QString edgeName = QString("Edge %1").arg(edgeIndex++);
            QTreeWidgetItem* edgeItem = createTreeItem(edgeName, (DOMElement*)edge, elementItem);

            // Add segments as children of the edge
            int segmentIndex = 0;
            for (const PathSegment& segment : edge->segments)
            {
                QString segmentName = QString("Segment %1").arg(segmentIndex++);
                QTreeWidgetItem* segmentItem = createTreeItem(segmentName, (DOMElement*)&segment, edgeItem);
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
                    createTreeItem(sectionName, nullptr, segmentItem);
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
            buildElementTree(elementItem, member);
        }
    }
}

void DocumentView::buildSymbolTree(QTreeWidgetItem* parentItem, const Symbol* symbol)
{
    QTreeWidgetItem* symbolItem = createTreeItem(
        QString("Symbol: %1").arg(QString::fromStdString(symbol->name)),
        (DOMElement*)symbol,
        parentItem
    );

    // Add symbol's timelines
    for (const Timeline* timeline : symbol->timelines)
    {
        buildTimelineTree(symbolItem, timeline);
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
    //item->setText(1, QString::fromStdString(domElement ? domElement->domTypeName() : ""));
    item->setIcon(0, getVisibilityIcon(domElement ? domElement->visible : true));

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
    item->setIcon(0, getVisibilityIcon(element->visible));

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

QIcon DocumentView::getVisibilityIcon(bool visible) const
{
    // Use Unicode characters for visibility icons
    QString iconText = visible ? "👁" : "⚠";
    
    // Create a simple text-based icon
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setFont(QFont("Segoe UI Emoji", 10));
    painter.setPen(visible ? Qt::black : Qt::gray);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, iconText);
    
    return QIcon(pixmap);
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
