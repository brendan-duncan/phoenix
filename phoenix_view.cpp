#include "phoenix_view.h"
#include "data/bitmap.h"
#include "data/bitmap_instance.h"
#include "data/group.h"
#include "data/linear_gradient.h"
#include "data/radial_gradient.h"
#include "data/solid_color.h"
#include "data/static_text.h"
#include "data/symbol_instance.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QDebug>
#include <map>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

uint32_t _skippedElement = 0;

PhoenixView::PhoenixView(QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _zoom(1.0)
    , _panX(0)
    , _panY(0)
    , _isDragging(false)
    , _showBounds(false)
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
    _bitmapCache.clear(); // Clear bitmap cache when loading new document
    _boundsCache.clear(); // Clear bounds cache for new document
    _pathCache.clear(); // Clear path cache for new document
    _flaDocument = document;
    update(); // Trigger repaint
}

void PhoenixView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Set default background
    painter.fillRect(rect(), QColor(37, 37, 37));

    if (!_flaDocument || !_flaDocument->document)
    {
        // Draw placeholder text when no document is available
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "PhoenixView\n(No document loaded)");
        return;
    }

    // Get document dimensions
    double docWidth = MAX(_flaDocument->document->width, 1024);
    double docHeight = MAX(_flaDocument->document->height, 768);

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

    // Calculate visible rect in document coordinates for culling
    double invScale = 1.0 / scale;
    _visibleRect = QRectF(
        (-_panX - centerX) * invScale,
        (-_panY - centerY) * invScale,
        widgetRect.width() * invScale,
        widgetRect.height() * invScale
    );

    // Apply transformations: pan + zoom + center offset
    painter.translate(_panX + centerX, _panY + centerY);
    painter.scale(scale, scale);

    // Draw white background for document area
    painter.fillRect(0, 0, docWidth, docHeight, QColor(255, 255, 255));

    // Draw visible rect if debug mode is enabled
    if (_showBounds)
    {
        painter.save();
        painter.setPen(QPen(QColor(0, 0, 255, 100), 2.0, Qt::DashLine)); // Blue dashed for visible area
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(_visibleRect);
        painter.restore();
    }

    // Draw document content
    drawDocument(painter, _flaDocument->document);
}

void PhoenixView::drawDocument(QPainter& painter, const Document* document)
{
    // Check document visibility
    if (!document->visible)
        return;

    _skippedElement = 0;

    for (const Timeline* timeline : document->timelines)
    {
        drawTimeline(painter, timeline);
    }

    qDebug() << "Skipped elements due to culling:" << _skippedElement;
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
        break;
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

    // Prepare transform data
    QTransform transform(element->transform.m11, element->transform.m12,
                       element->transform.m21, element->transform.m22,
                       element->transform.tx, element->transform.ty);
    QPointF instancePoint(element->transformationPoint.x, element->transformationPoint.y);

    // View frustum culling: Get element bounds and check if visible
    QRectF elementBounds = getElementBounds(element);

    bool culled = false;

    // Skip culling check if bounds are invalid (shouldn't happen, but be safe)
    if (!elementBounds.isNull() && elementBounds.isValid())
    {
        // Build full transformation matrix for bounds checking
        QTransform fullTransform;
        fullTransform.translate(instancePoint.x(), instancePoint.y());
        fullTransform = transform * fullTransform;
        fullTransform.translate(-instancePoint.x(), -instancePoint.y());

        QRectF transformedBounds = fullTransform.mapRect(elementBounds);

        // Cull if completely outside visible rect
        // Add margin to account for strokes and anti-aliasing
        QRectF expandedVisible = _visibleRect.adjusted(-100, -100, 100, 100);
        if (!transformedBounds.intersects(expandedVisible))
        {
            _skippedElement++;
            culled = true;

            // Draw culled bounds if debug mode is enabled
            if (_showBounds)
            {
                painter.save();
                painter.setPen(QPen(QColor(255, 0, 0, 100), 1.0)); // Red for culled
                painter.setBrush(Qt::NoBrush);
                painter.translate(instancePoint);
                painter.setTransform(transform, true);
                painter.translate(-instancePoint);
                painter.drawRect(elementBounds);
                painter.restore();
            }

            return; // Element is outside viewport, skip rendering
        }
    }

    painter.save();

    painter.translate(instancePoint);
    painter.setTransform(transform, true);
    painter.translate(-instancePoint);

    // Draw element bounds if debug mode is enabled (for rendered elements)
    if (_showBounds && !elementBounds.isNull() && elementBounds.isValid() && !culled)
    {
        painter.save();
        painter.setPen(QPen(QColor(0, 255, 0, 150), 1.0)); // Green for rendered
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(elementBounds);
        painter.restore();
    }

    Element::Type type = element->elementType();

    if (type == Element::Type::Shape)
    {
        drawShape(painter, static_cast<const Shape*>(element));
    }
    else if (type == Element::Type::SymbolInstance)
    {
        const SymbolInstance* instance = static_cast<const SymbolInstance*>(element);
        const Symbol* symbol = findSymbolByName(_flaDocument->document, instance->libraryItemName);
        if (symbol && symbol->visible)
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
    else if (type == Element::Type::StaticText)
    {
        StaticText* staticText = (StaticText*)element;
        for (const TextRun& run : staticText->runs)
        {
            painter.setPen(QPen(QColor(run.fillColor[0], run.fillColor[1], run.fillColor[2], run.fillColor[3]), run.size));
            painter.setFont(QFont(run.face.empty() ? "Arial" : QString::fromStdString(run.face), (int)run.size));
            painter.drawText(0, 0, QString::fromStdString(run.text));
        }
    }
    else if (type == Element::Type::BitmapInstance)
    {
        const BitmapInstance* instance = static_cast<const BitmapInstance*>(element);
        const Bitmap* bitmap = nullptr;
        for (Resource* resource : _flaDocument->document->resources)
        {
            if (resource->resourceType() == Resource::Type::Bitmap)
            {
                Bitmap* bmp = static_cast<Bitmap*>(resource);
                if (bmp->name == instance->libraryItemName)
                {
                    bitmap = bmp;
                    break;
                }
            }
        }

        if (bitmap && !bitmap->imageData.empty())
        {
            QString bitmapName = QString::fromStdString(bitmap->name);
            if (_bitmapCache.contains(bitmapName))
            {
                painter.drawPixmap(0, 0, _bitmapCache[bitmapName]);
            }
            else
            {
                 QPixmap pixmap;
                 QByteArray imgData(reinterpret_cast<const char*>(bitmap->imageData.data()), bitmap->imageData.size());
                 pixmap.loadFromData(imgData);
                 _bitmapCache[bitmapName] = pixmap;
                 painter.drawPixmap(0, 0, pixmap);
            }
        }
    }

    painter.restore();
}

static bool pathToPainterPath(const Edge* edge, QPainterPath& painterPath)
{
    for (const PathSegment& segment : edge->segments)
    {
        if (!segment.visible)
            continue;

        for (const PathSection& section : segment.sections)
        {
            if (section.command == PathSection::Command::Move)
            {
                painterPath.moveTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == PathSection::Command::Line)
            {
                painterPath.lineTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == PathSection::Command::Quad)
            {
                painterPath.quadTo(section.points[0].x, section.points[0].y,
                                   section.points[1].x, section.points[1].y);
            }
            else if (section.command == PathSection::Command::Cubic)
            {
                painterPath.cubicTo(section.points[0].x, section.points[0].y,
                                    section.points[1].x, section.points[1].y,
                                    section.points[2].x, section.points[2].y);
            }
            else if (section.command == PathSection::Command::Close)
            {
                painterPath.closeSubpath();
            }
        }
    }

    return true;
}

void PhoenixView::drawShape(QPainter& painter, const Shape* shape)
{
    if (_pathCache.contains(shape))
    {
        // Use cached paths
        for (const PathCacheEntry& entry : _pathCache[shape])
        {
            painter.setBrush(entry.fillBrush);
            painter.setPen(entry.pen);
            painter.drawPath(entry.painterPath);
        }
        return;
    }

    PathCacheList cacheEntries;

    // Helper lambda to build a path from segments
    auto buildPath = [](const PathSegment& segment) -> QPainterPath {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (const PathSection& section : segment.sections)
        {
            if (section.command == PathSection::Command::Move)
                path.moveTo(section.points[0].x, section.points[0].y);
            else if (section.command == PathSection::Command::Line)
                path.lineTo(section.points[0].x, section.points[0].y);
            else if (section.command == PathSection::Command::Quad)
                path.quadTo(section.points[0].x, section.points[0].y,
                           section.points[1].x, section.points[1].y);
            else if (section.command == PathSection::Command::Cubic)
                path.cubicTo(section.points[0].x, section.points[0].y,
                            section.points[1].x, section.points[1].y,
                            section.points[2].x, section.points[2].y);
            else if (section.command == PathSection::Command::Close)
                path.closeSubpath();
        }
        return path;
    };

    // Group segments by fill style
    std::map<int, QPainterPath> fillStylePaths;
    std::map<int, const FillStyle*> fillStyles;

    // Collect all segments and group by their fill styles
    for (const Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const PathSegment& segment : edge->segments)
        {
            if (!segment.visible || segment.sections.empty())
                continue;

            QPainterPath segmentPath = buildPath(segment);

            // Determine which fill styles this segment contributes to
            int fillStyleIdx1 = segment.fillStyleIndex != -1 ? segment.fillStyleIndex : edge->fillStyle1;
            int fillStyleIdx0 = edge->fillStyle0;

            // Add this path to fillStyle1 if present
            if (fillStyleIdx1 != -1)
            {
                fillStylePaths[fillStyleIdx1].addPath(segmentPath);
                if (fillStyles.find(fillStyleIdx1) == fillStyles.end())
                    fillStyles[fillStyleIdx1] = shape->getFillStyleByIndex(fillStyleIdx1);
            }

            // Add this path to fillStyle0 if present and different from fillStyle1
            if (fillStyleIdx0 != -1 && fillStyleIdx0 != fillStyleIdx1)
            {
                fillStylePaths[fillStyleIdx0].addPath(segmentPath);
                if (fillStyles.find(fillStyleIdx0) == fillStyles.end())
                    fillStyles[fillStyleIdx0] = shape->getFillStyleByIndex(fillStyleIdx0);
            }
        }
    }

    // Helper lambda to set fill brush from FillStyle
    auto setFillBrush = [&](const FillStyle* fill) {
        if (!fill)
        {
            painter.setBrush(Qt::NoBrush);
            return;
        }

        if (fill->type() == FillStyle::Type::SolidColor)
        {
            const SolidColor* solidFill = static_cast<const SolidColor*>(fill);
            QColor color(solidFill->color[0], solidFill->color[1], solidFill->color[2], solidFill->color[3]);
            painter.setBrush(QBrush(color));
        }
        else if (fill->type() == FillStyle::Type::LinearGradient)
        {
            const LinearGradient* linearFill = static_cast<const LinearGradient*>(fill);
            QLinearGradient gradient(0, 0, 100, 100);
            for (const GradientEntry& entry : linearFill->entries)
            {
                QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
                gradient.setColorAt(entry.ratio, color);
            }
            painter.setBrush(QBrush(gradient));
        }
        else if (fill->type() == FillStyle::Type::RadialGradient)
        {
            const RadialGradient* radialFill = static_cast<const RadialGradient*>(fill);
            QRadialGradient gradient(50, 50, 50);
            for (const RadialEntry& entry : radialFill->entries)
            {
                QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
                gradient.setColorAt(entry.ratio, color);
            }
            painter.setBrush(QBrush(gradient));
        }
        else
        {
            painter.setBrush(Qt::NoBrush);
        }
    };

    // Render each fill style with its compound path
    for (const auto& [fillIdx, compoundPath] : fillStylePaths)
    {
        const FillStyle* fillStyle = fillStyles[fillIdx];
        if (!fillStyle)
            continue;

        setFillBrush(fillStyle);
        painter.setPen(Qt::NoPen);
        painter.drawPath(compoundPath);

        cacheEntries.push_back({ painter.brush(), Qt::NoPen, compoundPath });
    }

    // Now render strokes separately (per segment)
    for (const Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const PathSegment& segment : edge->segments)
        {
            if (!segment.visible || segment.sections.empty())
                continue;

            int strokeStyleIdx = segment.lineStyleIndex != -1 ? segment.lineStyleIndex : edge->strokeStyle;
            if (strokeStyleIdx == -1)
                continue;

            const StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(strokeStyleIdx);
            if (!strokeStyle || !strokeStyle->stroke || !strokeStyle->stroke->fill)
                continue;

            QPainterPath strokePath = buildPath(segment);

            double weight = MAX(strokeStyle->stroke->weight, 0.5);
            QPen pen(QColor(0, 0, 0, 255), weight);

            if (strokeStyle->stroke->style == "DashedStroke")
                pen.setStyle(Qt::DashLine);
            else if (strokeStyle->stroke->style == "RaggedStroke")
                pen.setStyle(Qt::DashDotLine);
            else if (strokeStyle->stroke->style == "StippleStroke")
                pen.setStyle(Qt::DashDotDotLine);
            else if (strokeStyle->stroke->style == "DottedStroke")
                pen.setStyle(Qt::DotLine);

            if (strokeStyle->stroke->fill->type() == FillStyle::Type::SolidColor)
            {
                const SolidColor* fill = static_cast<const SolidColor*>(strokeStyle->stroke->fill);
                QColor color(fill->color[0], fill->color[1], fill->color[2], fill->color[3]);
                pen.setColor(color);
            }
            else if (strokeStyle->stroke->fill->type() == FillStyle::Type::LinearGradient)
            {
                const LinearGradient* fill = static_cast<const LinearGradient*>(strokeStyle->stroke->fill);
                QLinearGradient gradient(0, 0, 100, 100);
                for (const GradientEntry& entry : fill->entries)
                {
                    QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
                    gradient.setColorAt(entry.ratio, color);
                }
                pen.setBrush(QBrush(gradient));
            }
            else if (strokeStyle->stroke->fill->type() == FillStyle::Type::RadialGradient)
            {
                const RadialGradient* fill = static_cast<const RadialGradient*>(strokeStyle->stroke->fill);
                QRadialGradient gradient(50, 50, 50);
                for (const RadialEntry& entry : fill->entries)
                {
                    QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
                    gradient.setColorAt(entry.ratio, color);
                }
                pen.setBrush(QBrush(gradient));
            }

            painter.setBrush(Qt::NoBrush);
            painter.setPen(pen);
            painter.drawPath(strokePath);

            cacheEntries.push_back({ painter.brush(), pen, strokePath });
        }
    }

    _pathCache[shape] = cacheEntries;
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

QRectF PhoenixView::getElementBounds(const Element* element)
{
    // Check cache first
    auto it = _boundsCache.find(element);
    if (it != _boundsCache.end())
        return it.value();

    // Calculate bounds
    QRectF bounds = calculateElementBounds(element);
    _boundsCache[element] = bounds;
    return bounds;
}

QRectF PhoenixView::calculateElementBounds(const Element* element)
{
    Element::Type type = element->elementType();

    if (type == Element::Type::Shape)
    {
        return calculateShapeBounds(static_cast<const Shape*>(element));
    }
    else if (type == Element::Type::Group)
    {
        const Group* group = static_cast<const Group*>(element);
        QRectF bounds;
        for (const Element* member : group->members)
        {
            QRectF memberBounds = getElementBounds(member);

            // Apply member's transform
            QTransform transform(member->transform.m11, member->transform.m12,
                               member->transform.m21, member->transform.m22,
                               member->transform.tx, member->transform.ty);
            QRectF transformedBounds = transform.mapRect(memberBounds);

            if (bounds.isNull())
                bounds = transformedBounds;
            else
                bounds = bounds.united(transformedBounds);
        }
        return bounds;
    }
    else if (type == Element::Type::SymbolInstance)
    {
        const SymbolInstance* instance = static_cast<const SymbolInstance*>(element);
        if (_flaDocument && _flaDocument->document)
        {
            const Symbol* symbol = findSymbolByName(_flaDocument->document, instance->libraryItemName);
            if (symbol && !symbol->timelines.empty())
            {
                // Calculate bounds from symbol's first timeline
                const Timeline* timeline = symbol->timelines[0];
                QRectF bounds;

                for (const Layer* layer : timeline->layers)
                {
                    if (!layer->visible || layer->frames.empty())
                        continue;

                    const Frame* frame = layer->frames[0];
                    for (const Element* elem : frame->elements)
                    {
                        QRectF elemBounds = getElementBounds(elem);

                        // Apply element's transform
                        QTransform transform(elem->transform.m11, elem->transform.m12,
                                           elem->transform.m21, elem->transform.m22,
                                           elem->transform.tx, elem->transform.ty);
                        QRectF transformedBounds = transform.mapRect(elemBounds);

                        if (bounds.isNull())
                            bounds = transformedBounds;
                        else
                            bounds = bounds.united(transformedBounds);
                    }
                }
                return bounds;
            }
        }
        // Default bounds for symbol instances without data
        return QRectF(-50, -50, 100, 100);
    }
    else if (type == Element::Type::BitmapInstance)
    {
        // Approximate bounds for bitmap instances
        return QRectF(0, 0, 100, 100);
    }
    else if (type == Element::Type::StaticText)
    {
        // Approximate bounds for text
        return QRectF(0, 0, 200, 50);
    }

    // Default bounds
    return QRectF(-10, -10, 20, 20);
}

QRectF PhoenixView::calculateShapeBounds(const Shape* shape)
{
    QRectF bounds;
    double maxStrokeWeight = 0.0;

    for (const Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        // Track maximum stroke weight for bounds expansion
        const StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(edge->strokeStyle);
        if (strokeStyle && strokeStyle->stroke)
        {
            maxStrokeWeight = qMax(maxStrokeWeight, strokeStyle->stroke->weight);
        }

        for (const PathSegment& segment : edge->segments)
        {
            if (!segment.visible)
                continue;

            // Check segment-specific stroke
            if (segment.lineStyleIndex != -1)
            {
                const StrokeStyle* segStroke = shape->getStrokeStyleByIndex(segment.lineStyleIndex);
                if (segStroke && segStroke->stroke)
                {
                    maxStrokeWeight = qMax(maxStrokeWeight, segStroke->stroke->weight);
                }
            }

            for (const PathSection& section : segment.sections)
            {
                // Add all points from this section to bounds
                for (const Point& pt : section.points)
                {
                    QPointF qpt(pt.x, pt.y);

                    if (bounds.isNull())
                        bounds = QRectF(qpt, QSizeF(0, 0));
                    else
                    {
                        if (qpt.x() < bounds.left())
                            bounds.setLeft(qpt.x());
                        if (qpt.x() > bounds.right())
                            bounds.setRight(qpt.x());
                        if (qpt.y() < bounds.top())
                            bounds.setTop(qpt.y());
                        if (qpt.y() > bounds.bottom())
                            bounds.setBottom(qpt.y());
                    }
                }

                // Note: For perfect accuracy with cubic/quad curves, we should
                // check control points too, but endpoints are sufficient for culling
            }
        }
    }

    // Expand bounds by half the stroke weight (stroke extends on both sides)
    if (!bounds.isNull() && maxStrokeWeight > 0)
    {
        double expansion = maxStrokeWeight / 2.0 + 2.0; // +2 for safety margin
        bounds = bounds.adjusted(-expansion, -expansion, expansion, expansion);
    }
    else if (!bounds.isNull())
    {
        // Default small margin for anti-aliasing
        bounds = bounds.adjusted(-2, -2, 2, 2);
    }

    return bounds;
}
