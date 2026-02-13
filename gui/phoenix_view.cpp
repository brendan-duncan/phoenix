#include "phoenix_view.h"
#include "../data/bitmap.h"
#include "../data/bitmap_instance.h"
#include "../data/group.h"
#include "../data/linear_gradient.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/symbol_instance.h"
#include <map>

#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QDebug>

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

void PhoenixView::setDocument(const fla::FLADocument* document)
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
    QColor backgroundColor(37, 37, 37); // Dark gray background
    painter.fillRect(rect(), backgroundColor);

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

    // Calculate visible rect in document coordinates for culling
    double invScale = 1.0 / scale;
    _visibleRect = QRectF(
        (-_panX - centerX) * invScale,
        (-_panY - centerY) * invScale,
        widgetRect.width() * invScale,
        widgetRect.height() * invScale
    );

    // Apply transformations: pan + zoom + center offset
    painter.save();

    painter.translate(_panX + centerX, _panY + centerY);
    painter.scale(scale, scale);

    // Draw white background for document area
    painter.fillRect(0, 0, docWidth, docHeight, QColor(255, 255, 255));

    // Draw document content
    drawDocument(painter, _flaDocument->document);

    // Draw visible rect if debug mode is enabled
    /*if (_showBounds)
    {
        painter.save();
        painter.setPen(QPen(QColor(255, 0, 0, 100), 2.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(_visibleRect);
        painter.restore();
    }*/

    painter.restore();

    if (_highQualityAntiAliasing)
    {
        painter.save();
        // An extra draw slightly shifted eliminates the white line artifacts that can appear
        // between shapes due to anti-aliasing and subpixel rendering issues.
        painter.translate(_panX + centerX + 1, _panY + centerY + 1);
        painter.scale(scale, scale);
        drawDocument(painter, _flaDocument->document);

        painter.restore();
    }

    // Draw visible rect if debug mode is enabled
    if (_showBounds)
    {
        painter.save();
        painter.translate(_panX + centerX, _panY + centerY);
        painter.scale(scale, scale);

        painter.setPen(QPen(QColor(255, 0, 0, 100), 2.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(0, 0, docWidth, docHeight));

        drawDocumentBounds(painter, _flaDocument->document);

        painter.restore();
    }
}

void PhoenixView::drawDocument(QPainter& painter, const fla::Document* document)
{
    // Check document visibility
    if (!document->visible)
        return;

    _skippedElement = 0;

    for (const fla::Timeline* timeline : document->timelines)
    {
        drawTimeline(painter, timeline);
    }

    //qDebug() << "Skipped elements due to culling:" << _skippedElement;
}

void PhoenixView::drawTimeline(QPainter& painter, const fla::Timeline* timeline)
{
    // Check timeline visibility
    if (!timeline->visible)
        return;

    for (int i = timeline->layers.size() - 1; i >= 0; --i)
    {
        const fla::Layer* layer = timeline->layers[i];
        if (layer->visible)
            drawLayer(painter, layer);
    }
}

void PhoenixView::drawLayer(QPainter& painter, const fla::Layer* layer)
{
    // Check layer visibility (already checked in drawTimeline, but keeping for safety)
    if (!layer->visible)
        return;

    QColor color;
    color.setRgb(layer->color[0], layer->color[1], layer->color[2], layer->color[3]);
    painter.setPen(QPen(color, 1.0));

    for (const fla::Frame* frame : layer->frames)
    {
        drawFrame(painter, frame);
        break;
    }
}

void PhoenixView::drawFrame(QPainter& painter, const fla::Frame* frame)
{
    // Check frame visibility
    if (!frame->visible)
        return;

    for (const fla::Element* element : frame->elements)
    {
        drawElement(painter, element);
    }
}

fla::Symbol* PhoenixView::findSymbolByName(const fla::Document* document, const std::string& name)
{
    auto it = document->symbolMap.find(name);
    if (it != document->symbolMap.end())
        return it->second;
    return nullptr;
}

int indent = 0;

void PhoenixView::drawElement(QPainter& painter, const fla::Element* element)
{
    // Check element visibility
    if (!element->visible)
        return;

    // Prepare transform data
    QTransform transform(element->transform.m11, element->transform.m12,
                       element->transform.m21, element->transform.m22,
                       element->transform.tx, element->transform.ty);
    QPointF instancePoint(element->transformationPoint.x, element->transformationPoint.y);

    painter.save();

    // Why does a group have a transform, if applying the groups transform
    // is incorrect because the children are already transformed by the group transform?
    if (element->elementType() != fla::Element::Type::Group)
    {
        painter.translate(instancePoint);
        painter.setTransform(transform, true);
        painter.translate(-instancePoint);   
    }

    fla::Element::Type type = element->elementType();

    if (type == fla::Element::Type::Shape)
    {
        drawShape(painter, static_cast<const fla::Shape*>(element));
    }
    else if (type == fla::Element::Type::SymbolInstance)
    {
        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
        const fla::Symbol* symbol = findSymbolByName(_flaDocument->document, instance->libraryItemName);
        if (symbol && symbol->visible)
        {
            for (const fla::Timeline* timeline : symbol->timelines)
            {
                drawTimeline(painter, timeline);
            }
        }
    }
    else if (type == fla::Element::Type::Group)
    {
        const fla::Group* group = static_cast<const fla::Group*>(element);
        for (const fla::Element* member : group->members)
        {
            drawElement(painter, member);
        }
    }
    else if (type == fla::Element::Type::Rectangle)
    {
        const fla::RectanglePrimitive* rectangle = static_cast<const fla::RectanglePrimitive*>(element);
        QPen pen = getPen(rectangle->strokeStyle);
        QBrush brush = getFillBrush(rectangle->fillStyle);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawRect(rectangle->rect.topLeft.x, rectangle->rect.topLeft.y, rectangle->rect.width(), rectangle->rect.height());
    }
    else if (type == fla::Element::Type::Oval)
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(element);
        QPen pen = getPen(oval->strokeStyle);
        QBrush brush = getFillBrush(oval->fillStyle);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawEllipse(oval->rect.topLeft.x, oval->rect.topLeft.y, oval->rect.width(), oval->rect.height());
    }
    else if (type == fla::Element::Type::StaticText)
    {
        painter.setBrush(Qt::NoBrush);
        const fla::StaticText* staticText = static_cast<const fla::StaticText*>(element);
        for (const fla::TextRun& run : staticText->runs)
        {
            painter.setPen(QPen(QColor(run.fillColor[0], run.fillColor[1], run.fillColor[2], run.fillColor[3]), 1.0));
            QString fontFace = run.face.empty() ? "Arial" : QString::fromStdString(run.face);

            if (!_fontCache.contains(fontFace))
            {
                 bool isItalic = false;
                if (fontFace.endsWith("-Italic"))
                {
                    fontFace = fontFace.left(fontFace.length() - 7);
                    isItalic = true;
                }

                QString fontFamily;
                fontFamily = fontFace[0];
                for (int ci = 1; ci < fontFace.length(); ++ci)
                {
                    QChar c = fontFace[ci];
                    if (c >= 'A' && c <= 'Z')
                    {
                        QChar c0 = fontFace[ci - 1];
                        if (c0 >= 'a' && c0 <= 'z')
                        {
                            fontFamily += " ";
                        }
                    }
                    fontFamily += c;
                }

                QFont font(fontFamily, (int)run.size, -1, isItalic);
                _fontCache[fontFace] = font;
            }

            QFont font = _fontCache[fontFace];
            painter.setFont(font);
            // The origin is the top-left, not the bottom-left, so we use the text size as an approximation for line height
            painter.drawText(0, run.size, QString::fromStdString(run.text));
        }
    }
    else if (type == fla::Element::Type::BitmapInstance)
    {
        const fla::BitmapInstance* instance = static_cast<const fla::BitmapInstance*>(element);
        const fla::Bitmap* bitmap = nullptr;
        for (fla::Resource* resource : _flaDocument->document->resources)
        {
            if (resource->resourceType() == fla::Resource::Type::Bitmap)
            {
                fla::Bitmap* bmp = static_cast<fla::Bitmap*>(resource);
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

static bool pathToPainterPath(const fla::Edge* edge, QPainterPath& painterPath)
{
    for (const fla::PathSegment& segment : edge->segments)
    {
        if (!segment.visible)
            continue;

        for (const fla::PathSection& section : segment.sections)
        {
            if (section.command == fla::PathSection::Command::Move)
            {
                painterPath.moveTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == fla::PathSection::Command::Line)
            {
                painterPath.lineTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == fla::PathSection::Command::Quad)
            {
                painterPath.quadTo(section.points[0].x, section.points[0].y,
                                   section.points[1].x, section.points[1].y);
            }
            else if (section.command == fla::PathSection::Command::Cubic)
            {
                painterPath.cubicTo(section.points[0].x, section.points[0].y,
                                    section.points[1].x, section.points[1].y,
                                    section.points[2].x, section.points[2].y);
            }
            else if (section.command == fla::PathSection::Command::Close)
            {
                painterPath.closeSubpath();
            }
        }
    }

    return true;
}

QPen PhoenixView::getPen(const fla::StrokeStyle* strokeStyle)
{
    if (!strokeStyle || !strokeStyle->fill)
    {
        return QPen(Qt::NoPen);
    }

    double weight = MAX(strokeStyle->weight, 0.5);
    QPen pen(QColor(0, 0, 0, 255), weight);

    if (strokeStyle->style() == fla::StrokeStyle::Style::Solid)
        pen.setStyle(Qt::SolidLine);
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Dashed)
        pen.setStyle(Qt::DashLine);
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Ragged)
        pen.setStyle(Qt::DashDotLine);
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Stipple)
        pen.setStyle(Qt::DashDotDotLine);
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Dotted)
        pen.setStyle(Qt::DotLine);

    if (strokeStyle->fill->type() == fla::FillStyle::Type::SolidColor)
    {
        const fla::SolidColor* fill = static_cast<const fla::SolidColor*>(strokeStyle->fill);
        QColor color(fill->color[0], fill->color[1], fill->color[2], fill->color[3]);
        pen.setColor(color);
    }
    else if (strokeStyle->fill->type() == fla::FillStyle::Type::LinearGradient)
    {
        const fla::LinearGradient* fill = static_cast<const fla::LinearGradient*>(strokeStyle->fill);
        QLinearGradient gradient(0, 0, 100, 100);
        for (const fla::GradientEntry& entry : fill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }
        pen.setBrush(QBrush(gradient));
    }
    else if (strokeStyle->fill->type() == fla::FillStyle::Type::RadialGradient)
    {
        const fla::RadialGradient* fill = static_cast<const fla::RadialGradient*>(strokeStyle->fill);
        QRadialGradient gradient(50, 50, 50);
        for (const fla::RadialEntry& entry : fill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }
        pen.setBrush(QBrush(gradient));
    }
    
    return pen;
}

QBrush PhoenixView::getFillBrush(const fla::FillStyle* fillStyle)
{
    if (!fillStyle)
        return Qt::NoBrush;

    if (fillStyle->type() == fla::FillStyle::Type::SolidColor)
    {
        const fla::SolidColor* solidFill = static_cast<const fla::SolidColor*>(fillStyle);
        QColor color(solidFill->color[0], solidFill->color[1], solidFill->color[2], solidFill->color[3]);
        return QBrush(color);
    }
    else if (fillStyle->type() == fla::FillStyle::Type::LinearGradient)
    {
        const fla::LinearGradient* linearFill = static_cast<const fla::LinearGradient*>(fillStyle);
        QLinearGradient gradient(0, 0, 100, 100);
        for (const fla::GradientEntry& entry : linearFill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }
        return QBrush(gradient);
    }
    else if (fillStyle->type() == fla::FillStyle::Type::RadialGradient)
    {
        const fla::RadialGradient* radialFill = static_cast<const fla::RadialGradient*>(fillStyle);
        QRadialGradient gradient(50, 50, 50);
        for (const fla::RadialEntry& entry : radialFill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }
        return QBrush(gradient);
    }

    return Qt::NoBrush;
}

void PhoenixView::drawShape(QPainter& painter, const fla::Shape* shape)
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
    auto buildPath = [](const fla::PathSegment& segment) -> QPainterPath
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (const fla::PathSection& section : segment.sections)
        {
            if (section.command == fla::PathSection::Command::Move)
            {
                path.moveTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == fla::PathSection::Command::Line)
            {
                path.lineTo(section.points[0].x, section.points[0].y);
            }
            else if (section.command == fla::PathSection::Command::Quad)
            {
                path.quadTo(section.points[0].x, section.points[0].y,
                           section.points[1].x, section.points[1].y);
            }
            else if (section.command == fla::PathSection::Command::Cubic)
            {
                path.cubicTo(section.points[0].x, section.points[0].y,
                            section.points[1].x, section.points[1].y,
                            section.points[2].x, section.points[2].y);
            }
            else if (section.command == fla::PathSection::Command::Close)
            {
                path.closeSubpath();
            }
        }
        return path;
    };

    // Group segments by fill style
    std::map<int, QPainterPath> fillStylePaths;
    std::map<int, const fla::FillStyle*> fillStyles;

    // Collect all segments and group by their fill styles
    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::PathSegment& segment : edge->segments)
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
                {
                    fillStyles[fillStyleIdx1] = shape->getFillStyleByIndex(fillStyleIdx1);
                }
            }

            // Add this path to fillStyle0 if present and different from fillStyle1
            if (fillStyleIdx0 != -1 && fillStyleIdx0 != fillStyleIdx1)
            {
                fillStylePaths[fillStyleIdx0].addPath(segmentPath);
                if (fillStyles.find(fillStyleIdx0) == fillStyles.end())
                {
                    fillStyles[fillStyleIdx0] = shape->getFillStyleByIndex(fillStyleIdx0);
                }
            }
        }
    }

    // Helper lambda to set fill brush from FillStyle
    auto setFillBrush = [&](const fla::FillStyle* fill)
    {
        QBrush brush = getFillBrush(fill);
        painter.setBrush(brush);
    };

    // Render each fill style with its compound path
    for (const auto& [fillIdx, compoundPath] : fillStylePaths)
    {
        const fla::FillStyle* fillStyle = fillStyles[fillIdx];
        if (!fillStyle)
            continue;

        setFillBrush(fillStyle);
        painter.setPen(Qt::NoPen);
        painter.drawPath(compoundPath);

        cacheEntries.push_back({ painter.brush(), Qt::NoPen, compoundPath });
    }

    // Now render strokes separately (per segment)
    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::PathSegment& segment : edge->segments)
        {
            if (!segment.visible || segment.sections.empty())
                continue;

            int strokeStyleIdx = segment.lineStyleIndex != -1 ? segment.lineStyleIndex : edge->strokeStyle;
            if (strokeStyleIdx == -1)
                continue;

            const fla::StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(strokeStyleIdx);
            if (!strokeStyle || !strokeStyle->fill)
                continue;

            QPainterPath strokePath = buildPath(segment);

            QPen pen = getPen(strokeStyle);

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

void PhoenixView::clearCaches()
{
    _pathCache.clear();
    _boundsCache.clear();
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

QRectF PhoenixView::getElementBounds(const fla::Element* element)
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

QRectF PhoenixView::calculateElementBounds(const fla::Element* element)
{
    fla::Element::Type type = element->elementType();

    if (type == fla::Element::Type::Shape)
    {
        return calculateShapeBounds(static_cast<const fla::Shape*>(element));
    }
    else if (type == fla::Element::Type::Group)
    {
        const fla::Group* group = static_cast<const fla::Group*>(element);
        QRectF bounds;
        for (const fla::Element* member : group->members)
        {
            QRectF memberBounds = getElementBounds(member);
            if (bounds.isNull())
                bounds = memberBounds;
            else
                bounds = bounds.united(memberBounds);
        }
        return bounds;
    }
    else if (type == fla::Element::Type::SymbolInstance)
    {
        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
        if (_flaDocument && _flaDocument->document)
        {
            const fla::Symbol* symbol = findSymbolByName(_flaDocument->document, instance->libraryItemName);
            if (symbol && !symbol->timelines.empty())
            {
                // Calculate bounds from symbol's first timeline
                const fla::Timeline* timeline = symbol->timelines[0];
                QRectF bounds;

                for (const fla::Layer* layer : timeline->layers)
                {
                    if (!layer->visible || layer->frames.empty())
                        continue;

                    const fla::Frame* frame = layer->frames[0];
                    for (const fla::Element* elem : frame->elements)
                    {
                        QRectF elemBounds = getElementBounds(elem);

                        if (bounds.isNull())
                            bounds = elemBounds;
                        else
                            bounds = bounds.united(elemBounds);
                    }
                }

                QTransform transform(element->transform.m11, element->transform.m12,
                                    element->transform.m21, element->transform.m22,
                                    element->transform.tx, element->transform.ty);
                bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
                bounds = transform.mapRect(bounds);
                bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);

                return bounds;
            }
        }

        // Default bounds for symbol instances without data
        QRectF bounds = QRectF(-50, -50, 100, 100);
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
        bounds = transform.mapRect(bounds);
        bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);
        return bounds;
    }
    else if (type == fla::Element::Type::BitmapInstance)
    {
        // Approximate bounds for bitmap instances
        QRectF bounds(0, 0, 100, 100);
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
        bounds = transform.mapRect(bounds);
        bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);
        return bounds;
    }
    else if (type == fla::Element::Type::StaticText)
    {
        // Approximate bounds for text
        QRectF bounds(0, 0, 200, 50);
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
        bounds = transform.mapRect(bounds);
        bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);
        return bounds;
    }
    else if (type == fla::Element::Type::Rectangle)
    {
        const fla::RectanglePrimitive* rectangle = static_cast<const fla::RectanglePrimitive*>(element);
        QRectF bounds(rectangle->rect.topLeft.x, rectangle->rect.topLeft.y, rectangle->rect.width(), rectangle->rect.height());
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
        bounds = transform.mapRect(bounds);
        bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);
        return bounds;
    }
    else if (type == fla::Element::Type::Oval)
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(element);
        QRectF bounds(oval->rect.topLeft.x, oval->rect.topLeft.y, oval->rect.width(), oval->rect.height());
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
        bounds = transform.mapRect(bounds);
        bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);
        return bounds;
    }

    // Default bounds
    QRectF bounds(-10, -10, 20, 20);
    QTransform transform(element->transform.m11, element->transform.m12,
                        element->transform.m21, element->transform.m22,
                        element->transform.tx, element->transform.ty);
    bounds.translate(element->transformationPoint.x, element->transformationPoint.y);
    bounds = transform.mapRect(bounds);
    bounds.translate(-element->transformationPoint.x, -element->transformationPoint.y);
    return bounds;
}

QRectF PhoenixView::calculateShapeBounds(const fla::Shape* shape)
{
    bool first = true;
    QRectF bounds;
    double maxStrokeWeight = 0.0;

    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        // Track maximum stroke weight for bounds expansion
        const fla::StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(edge->strokeStyle);
        if (strokeStyle)
        {
            maxStrokeWeight = qMax(maxStrokeWeight, strokeStyle->weight);
        }

        for (const fla::PathSegment& segment : edge->segments)
        {
            if (!segment.visible)
                continue;

            // Check segment-specific stroke
            if (segment.lineStyleIndex != -1)
            {
                const fla::StrokeStyle* segStroke = shape->getStrokeStyleByIndex(segment.lineStyleIndex);
                if (segStroke)
                {
                    maxStrokeWeight = qMax(maxStrokeWeight, segStroke->weight);
                }
            }

            for (const fla::PathSection& section : segment.sections)
            {
                // Add all points from this section to bounds
                for (const fla::Point& pt : section.points)
                {
                    QPointF qpt(pt.x, pt.y);

                    if (first)
                    {
                        first = false;
                        bounds = QRectF(qpt, QSizeF(0, 0));
                    }
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

    QTransform transform(shape->transform.m11, shape->transform.m12,
                        shape->transform.m21, shape->transform.m22,
                        shape->transform.tx, shape->transform.ty);
    bounds.translate(shape->transformationPoint.x, shape->transformationPoint.y);
    bounds = transform.mapRect(bounds);
    bounds.translate(-shape->transformationPoint.x, -shape->transformationPoint.y);

    return bounds;
}

void PhoenixView::drawElementBounds(QPainter& painter, const fla::Element* element)
{
    if (!element->visible)
        return;

    QRectF bounds = getElementBounds(element);
    if (bounds.isValid())
    {
        painter.save();
        painter.setPen(QPen(QColor(255, 0, 0, 255), 2.0, Qt::DashLine)); // Red dashed for element bounds
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bounds);
        painter.restore();
    }

    if (element->elementType() == fla::Element::Type::Group)
    {
        const fla::Group* group = static_cast<const fla::Group*>(element);
        for (const fla::Element* member : group->members)
        {
            drawElementBounds(painter, member);
        }
    }
    else if (element->elementType() == fla::Element::Type::SymbolInstance)
    {
        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
        const fla::Symbol* symbol = findSymbolByName(_flaDocument->document, instance->libraryItemName);
        if (symbol && symbol->visible)
        {
            for (const fla::Timeline* timeline : symbol->timelines)
            {
                drawTimelineBounds(painter, timeline);
            }
        }
    }
}

void PhoenixView::drawTimelineBounds(QPainter& painter, const fla::Timeline* timeline)
{
    for (const fla::Layer* layer : timeline->layers)
    {
        for (const fla::Frame* frame : layer->frames)
        {
            if (frame->index != 0)
                continue; // Only draw bounds for first frame of each layer for clarity

            for (const fla::Element* element : frame->elements)
            {
                drawElementBounds(painter, element);
            }
        }
    }
}

void PhoenixView::drawDocumentBounds(QPainter& painter, const fla::Document* document)
{
    for (const fla::Timeline* timeline : document->timelines)
    {
        drawTimelineBounds(painter, timeline);
    }
}
