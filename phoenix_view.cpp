#include "phoenix_view.h"
#include "data/group.h"
#include "data/linear_gradient.h"
#include "data/solid_color.h"
#include "data/symbol_instance.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QDebug>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

PhoenixView::PhoenixView(QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _zoom(1.0)
    , _panX(0)
    , _panY(0)
    , _isDragging(false)
{
    // Set widget properties
    setMinimumSize(400, 300);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    
    // Enable mouse tracking for smooth panning
    setMouseTracking(true);
}

PhoenixView::~PhoenixView()
{
}

void PhoenixView::setDocument(const FLADocument* document)
{
    _flaDocument = document;
    update(); // Trigger repaint
}

void PhoenixView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Set default background
    painter.fillRect(rect(), Qt::white);

    if (!_flaDocument || !_flaDocument->document)
    {
        // Draw placeholder text when no document is available
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "PhoenixView\n(No document loaded)");
        return;
    }

    // Get document dimensions
    double docWidth = _flaDocument->document->width;
    double docHeight = _flaDocument->document->height;

    if (docWidth <= 0 || docHeight <= 0)
    {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "PhoenixView\n(Invalid document dimensions)");
        return;
    }

    // Calculate initial scale to fit document in widget (if zoom is 1.0)
    QRectF widgetRect = rect();
    double scaleX = (widgetRect.width() - 20) / docWidth;
    double scaleY = (widgetRect.height() - 20) / docHeight;
    double defaultScale = qMin(scaleX, scaleY);

    // Use zoom level if not default (1.0), otherwise fit to widget
    double scale = (_zoom != 1.0) ? _zoom : defaultScale;
    
    // Calculate center offset for initial fit
    double centerX = (_zoom == 1.0) ? (widgetRect.width() - docWidth * scale) / 2.0 : 0;
    double centerY = (_zoom == 1.0) ? (widgetRect.height() - docHeight * scale) / 2.0 : 0;

    // Apply transformations: pan + zoom + center offset
    painter.translate(_panX + centerX, _panY + centerY);
    painter.scale(scale, scale);

    // Draw document content
    drawDocument(painter, _flaDocument->document);
}

void PhoenixView::drawDocument(QPainter& painter, const Document* document)
{
    // Check document visibility
    if (!document->visible)
        return;
        
    for (const Timeline* timeline : document->timelines)
    {
        drawTimeline(painter, timeline);
    }
}

void PhoenixView::drawTimeline(QPainter& painter, const Timeline* timeline)
{
    // Check timeline visibility
    if (!timeline->visible)
        return;
        
    for (int i = timeline->layers.size() - 1; i >= 0; --i)
    {
        const Layer* layer = timeline->layers[i];
        if (layer->visible)
            drawLayer(painter, layer);
    }
}

void PhoenixView::drawLayer(QPainter& painter, const Layer* layer)
{
    // Check layer visibility (already checked in drawTimeline, but keeping for safety)
    if (!layer->visible)
        return;
        
    QColor color;
    color.setRgb(layer->color[0], layer->color[1], layer->color[2], layer->color[3]);
    painter.setPen(QPen(color, 1.0));

    for (const Frame* frame : layer->frames)
    {
        drawFrame(painter, frame);
    }
}

void PhoenixView::drawFrame(QPainter& painter, const Frame* frame)
{
    // Check frame visibility
    if (!frame->visible)
        return;
        
    for (const Element* element : frame->elements)
    {
        drawElement(painter, element);
    }
}

static Symbol* findSymbolByName(const Document* document, const std::string& name)
{
    auto it = document->symbolMap.find(name);
    if (it != document->symbolMap.end())
        return it->second;
    return nullptr;
}

void PhoenixView::drawElement(QPainter& painter, const Element* element)
{
    // Check element visibility
    if (!element->visible)
        return;

    Element::Type type = element->elementType();
    painter.save();
    QTransform transform(element->transform.m11, element->transform.m12, element->transform.m21, element->transform.m22,
                         element->transform.tx, element->transform.ty);
    painter.setTransform(transform, true);
    //painter.translate(instance->transformationPoint);

    if (type == Element::Type::Shape)
    {
        drawShape(painter, static_cast<const Shape*>(element));
    }
    else if (type == Element::Type::SymbolInstance)
    {
        const SymbolInstance* instance = static_cast<const SymbolInstance*>(element);   
        const Symbol* symbol = findSymbolByName(_flaDocument->document, instance->libraryItemName);
        if (symbol)
        {
            for (const Timeline* timeline : symbol->timelines)
            {
                drawTimeline(painter, timeline);
            }
        }
    }
    else if (type == Element::Type::Group)
    {
        const Group* group = static_cast<const Group*>(element);
        for (const Element* member : group->members)
        {
            drawElement(painter, member);
        }
    }

    painter.restore();
}

static bool pathToPainterPath(const Edge* edge, QPainterPath& painterPath)
{
    for (const PathSegment& segment : edge->segments)
    {
        for (const PathSection& section : segment.sections)
        {
            if (section.command == PathSection::Command::MoveTo)
            {
                painterPath.moveTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == PathSection::Command::LineTo)
            {
                painterPath.lineTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == PathSection::Command::QuadTo)
            {
                painterPath.quadTo(section.points[0].x, section.points[0].y,
                                   section.points[1].x, section.points[1].y);
            }
        }
    }

    return true;
}

void PhoenixView::drawShape(QPainter& painter, const Shape* shape)
{
    // Draw edges
    for (const Edge* edge : shape->edges)
    {
        const FillStyle* fillStyle = shape->getFillStyleByIndex(edge->fillStyle0 != -1 ? edge->fillStyle0 : edge->fillStyle1);
        if (fillStyle)
        {
            if (fillStyle->type() == FillStyle::Type::SolidColor)
            {
                const SolidColor* solidFill = static_cast<const SolidColor*>(fillStyle);
                QColor color(solidFill->color[0], solidFill->color[1], solidFill->color[2], solidFill->color[3]);
                painter.setBrush(QBrush(color));
            }
            else if (fillStyle->type() == FillStyle::Type::LinearGradient)
            {
                const LinearGradient* linearFill = static_cast<const LinearGradient*>(fillStyle);
                QLinearGradient gradient(0, 0, 100, 100); // Placeholder gradient coordinates
                for (const GradientEntry& entry : linearFill->entries)
                {
                    QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
                    gradient.setColorAt(entry.ratio, color);
                }
                painter.setBrush(QBrush(gradient));
            }
        }
        else
        {
            painter.setBrush(Qt::NoBrush);
        }

        const StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(edge->strokeStyle);
        if (strokeStyle)
        {
            if (strokeStyle->stroke)
            {
                if (strokeStyle->stroke->fill)
                {
                    double weight = MAX(strokeStyle->stroke->weight, 0.5);
                    QPen pen(QColor(0, 0, 0, 255), weight);

                    if (strokeStyle->stroke->style == "DashedStroke")
                        pen.setStyle(Qt::DashLine);
                    else if (strokeStyle->stroke->style == "RaggedStroke")
                        pen.setStyle(Qt::DashDotLine);
                    else if (strokeStyle->stroke->style == "StippleStroke")
                        pen.setStyle(Qt::DashDotDotLine);

                    if (strokeStyle->stroke->fill->type() == FillStyle::Type::SolidColor)
                    {
                        const SolidColor* fill = static_cast<const SolidColor*>(strokeStyle->stroke->fill);
                        QColor color(fill->color[0], fill->color[1], fill->color[2], fill->color[3]);
                        pen.setColor(color);
                    }
                    else if (strokeStyle->stroke->fill->type() == FillStyle::Type::LinearGradient)
                    {
                        const LinearGradient* fill = static_cast<const LinearGradient*>(strokeStyle->stroke->fill);
                        QLinearGradient gradient(0, 0, 100, 100); // Placeholder gradient coordinates
                        for (const GradientEntry& entry : fill->entries)
                        {
                            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
                            gradient.setColorAt(entry.ratio, color);
                        }
                        pen.setBrush(QBrush(gradient));
                    }

                    painter.setPen(pen);
                }
                else
                {
                    painter.setPen(Qt::NoPen);
                }
            }
            else
            {
                painter.setPen(Qt::NoPen);
            }
        }
        else
        {
            painter.setPen(Qt::NoPen);
        }

        QPainterPath painterPath;
        pathToPainterPath(edge, painterPath);
        painter.drawPath(painterPath);
    }
}

void PhoenixView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        _isDragging = true;
        _lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void PhoenixView::mouseMoveEvent(QMouseEvent *event)
{
    if (_isDragging)
    {
        QPoint delta = event->pos() - _lastMousePos;
        _panX += delta.x();
        _panY += delta.y();
        _lastMousePos = event->pos();
        update();
    }
}

void PhoenixView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        _isDragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void PhoenixView::wheelEvent(QWheelEvent *event)
{
    // Zoom with mouse wheel
    double scaleFactor = 1.15;
    if (event->angleDelta().y() < 0)
        scaleFactor = 1.0 / scaleFactor;
    
    double oldZoom = _zoom;
    double newZoom = _zoom * scaleFactor;
    
    // Limit zoom range
    if (newZoom < 0.1 || newZoom > 50.0)
        return;
    
    // Get mouse position in screen coordinates
    QPointF mousePos = event->position();
    
    // Calculate what scene point is under the mouse before zoom
    QPointF scenePosBefore = (mousePos - QPointF(_panX, _panY)) / oldZoom;
    
    // Apply zoom
    _zoom = newZoom;
    
    // Calculate new pan to keep the same scene point under the mouse
    QPointF newScreenPos = scenePosBefore * newZoom + QPointF(_panX, _panY);
    QPointF delta = newScreenPos - mousePos;
    _panX -= delta.x();
    _panY -= delta.y();
    
    update();
}

void PhoenixView::resetView()
{
    _zoom = 1.0;
    _panX = 0;
    _panY = 0;
    update();
}

QPointF PhoenixView::screenToScene(const QPointF& screenPos) const
{
    return QPointF(
        (screenPos.x() - _panX) / _zoom,
        (screenPos.y() - _panY) / _zoom
    );
}

QPointF PhoenixView::sceneToScreen(const QPointF& scenePos) const
{
    return QPointF(
        scenePos.x() * _zoom + _panX,
        scenePos.y() * _zoom + _panY
    );
}
