#include "phoenix_view.h"
#include "player.h"
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
#include <iostream>
#include <cmath>

#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QDebug>
#include <QPixmap>
#include <QBitmap>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

PhoenixView::PhoenixView(Player* player, QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
    , _highQualityAntiAliasing(false)
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

    connect(_player, &Player::currentFrameChanged, this, &PhoenixView::onPlayerFrameChanged);
}

PhoenixView::~PhoenixView()
{
}

void PhoenixView::onPlayerFrameChanged(int frame)
{
    update(); // Trigger repaint when player frame changes
}

void PhoenixView::setDocument(const fla::FLADocument* document)
{
    _bitmapCache.clear(); // Clear bitmap cache when loading new document
    _boundsCache.clear(); // Clear bounds cache for new document
    _pathCache.clear(); // Clear path cache for new document
    _flaDocument = document;
    _panX = 0;
    _panY = 0;
    _zoom = 1.0;
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
    _disableStaticText = _highQualityAntiAliasing;
    drawDocument(painter, _flaDocument->document);
    _disableStaticText = false;

    painter.restore();

    if (_highQualityAntiAliasing)
    {
        painter.save();
        // An extra draw slightly shifted eliminates the white line artifacts that can appear
        // between shapes due to anti-aliasing and subpixel rendering issues.
        painter.translate(_panX + centerX + 0.25, _panY + centerY + 0.25);
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

    for (const fla::Timeline* timeline : document->timelines)
    {
        if (timeline->visible)
        {
            drawTimeline(painter, timeline);
        }
    }
}

void PhoenixView::drawTimeline(QPainter& painter, const fla::Timeline* timeline, fla::LoopType loopType, int firstFrame)
{
    struct MaskData
    {
        QPixmap maskPixmap;
        QPointF offset;
    };
    QMap<int, MaskData> maskCache;
    QTransform painterTransform = painter.transform();

    // First pass: render mask layers to pixmaps
    for (int i = 0; i < timeline->layers.size(); ++i)
    {
        const fla::Layer* layer = timeline->layers[i];
        if (layer->layerType != fla::Layer::Type::Mask || !layer->isVisible())
        {
            continue;
        }

        // Use full widget resolution for mask
        QRect r = rect();
        QPixmap maskPixmap(r.width(), r.height());
        maskPixmap.fill(Qt::transparent);

        QPainter maskPainter(&maskPixmap);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setRenderHint(QPainter::TextAntialiasing, true);
        maskPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        maskPainter.setTransform(painterTransform);

        drawLayer(maskPainter, layer, loopType, firstFrame, nullptr);

        maskCache[i] = { maskPixmap, QPointF(0, 0) };
    }

    // Second pass: draw layers with masks applied
    for (int i = timeline->layers.size() - 1; i >= 0; --i)
    {
        const fla::Layer* layer = timeline->layers[i];

        if (layer->layerType == fla::Layer::Type::Mask || !layer->isVisible())
        {
            continue;
        }

        if (layer->layerType == fla::Layer::Type::Masked)
        {
            auto maskIt = maskCache.find(layer->parentLayerIndex);
            if (maskIt != maskCache.end())
            {
                const QPixmap& maskPixmap = maskIt.value().maskPixmap;
                const QPointF& maskOffset = maskIt.value().offset;

                // Use full widget resolution for content
                QRect r = rect();
                QPixmap contentPixmap(r.width(), r.height());
                contentPixmap.fill(Qt::transparent);

                QPainter contentPainter(&contentPixmap);
                contentPainter.setRenderHint(QPainter::Antialiasing, true);
                contentPainter.setRenderHint(QPainter::TextAntialiasing, true);
                contentPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                contentPainter.setTransform(painterTransform);

                drawLayer(contentPainter, layer, loopType, firstFrame, nullptr);

                // Composite: content masked by mask
                QPixmap maskedResult(contentPixmap.size());
                maskedResult.fill(Qt::transparent);
                {
                    QPainter composite(&maskedResult);
                    composite.setCompositionMode(QPainter::CompositionMode_Source);
                    composite.drawPixmap(0, 0, contentPixmap);
                    composite.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                    composite.drawPixmap(0, 0, maskPixmap);
                }

                // Draw to main painter
                painter.save();
                painter.resetTransform();
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawPixmap(0, 0, maskedResult);
                painter.restore();

                continue;
            }
        }
        else if (layer->layerType == fla::Layer::Type::Normal)
        {
            drawLayer(painter, layer, loopType, firstFrame, nullptr);
        }
        else if (layer->layerType == fla::Layer::Type::Folder)
        {
            // Folders don't have content
        }
        else if (layer->layerType == fla::Layer::Type::Guide)
        {
            // Guide layers not rendered
        }
    }
}

void PhoenixView::drawLayer(QPainter& painter, const fla::Layer* layer, fla::LoopType loopType, int firstFrame, const QPixmap* maskPixmap)
{
    QColor color;
    color.setRgb(layer->color[0], layer->color[1], layer->color[2], layer->color[3]);
    painter.setPen(QPen(color, 1.0));

    const fla::Frame* currentFrame = nullptr;

    int currentFrameIndex = firstFrame + _player->currentFrame();

    for (const fla::Frame* frame : layer->frames)
    {
        if (frame->index <= currentFrameIndex)
        {
            currentFrame = frame;
        }
    }

    if (currentFrame && currentFrame->visible)
    {
        drawFrame(painter, currentFrame);
    }
}

void PhoenixView::drawFrame(QPainter& painter, const fla::Frame* frame)
{
    for (const fla::Element* element : frame->elements)
    {
        if (element->visible)
        {
            drawElement(painter, element);
        }
    }
}

fla::Symbol* PhoenixView::findSymbolByName(const fla::Document* document, const std::string& name)
{
    auto it = document->symbolMap.find(name);
    if (it != document->symbolMap.end())
    {
        return it->second;
    }
    return nullptr;
}

int indent = 0;

void PhoenixView::drawElement(QPainter& painter, const fla::Element* element)
{
    // Prepare transform data
    QTransform transform(element->transform.m11, element->transform.m12,
                       element->transform.m21, element->transform.m22,
                       element->transform.tx, element->transform.ty);

    painter.save();

    // Why does a group have a transform, if applying the groups transform
    // is incorrect because the children are already transformed by the group transform?
    if (element->elementType() != fla::Element::Type::Group)
    {
        painter.setTransform(transform, true);
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
                if (timeline->visible)
                {
                    painter.save();
                    drawTimeline(painter, timeline, instance->loopType, instance->firstFrame);
                    painter.restore();
                }
            }
        }
    }
    else if (type == fla::Element::Type::Group)
    {
        const fla::Group* group = static_cast<const fla::Group*>(element);
        for (const fla::Element* member : group->members)
        {
            if (member->visible)
            {
                painter.save();
                drawElement(painter, member);
                painter.restore();
            }
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
        if (_disableStaticText)
        {
            painter.restore();
            return;
        }

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

                if (fontFamily.endsWith(" MT"))
                {
                    fontFamily = fontFamily.left(fontFamily.length() - 3);
                }
                else if (fontFamily.endsWith(" PS"))
                {
                    fontFamily = fontFamily.left(fontFamily.length() - 3);
                }
                else if (fontFamily.endsWith(" PSMT"))
                {
                    fontFamily = fontFamily.left(fontFamily.length() - 5);
                }

                qDebug() << "Loading font:" << fontFamily << "size:" << run.size << "italic:" << isItalic;

                QFontDatabase fontDatabase;
                if (!fontDatabase.families().contains(fontFamily))
                {
                    qDebug() << "Font family not found:" << fontFamily << "- using default";
                    /*for (const QString& availableFamily : fontDatabase.families())
                    {
                        qDebug() << "    family:" << availableFamily;
                    }*/
                    fontFamily = "Arial";
                }

                QFont font(fontFamily, (int)run.size, -1, isItalic);
                _fontCache[fontFace] = font;
            }

            QFont font = _fontCache[fontFace];
            font.setWeight(QFont::Normal);
            font.setPointSizeF(run.size * 0.75); // Adjust size to better match Flash's rendering
            painter.setFont(font);
            // The origin is the top-left, not the bottom-left, so we use the text size as an approximation for line height
            painter.drawText(staticText->left, staticText->top + run.size, QString::fromStdString(run.text));
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

QPen PhoenixView::getPen(const fla::StrokeStyle* strokeStyle)
{
    if (!strokeStyle || !strokeStyle->fill)
    {
        return QPen(Qt::NoPen);
    }

    double weight = MAX(strokeStyle->weight, 0.5);
    QPen pen(QColor(0, 0, 0, 255), weight);

    if (strokeStyle->style() == fla::StrokeStyle::Style::Solid)
    {
        pen.setStyle(Qt::SolidLine);
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Dashed)
    {
        pen.setStyle(Qt::DashLine);
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Ragged)
    {
        pen.setStyle(Qt::DashDotLine);
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Stipple)
    {
        pen.setStyle(Qt::DashDotDotLine);
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Dotted)
    {
        pen.setStyle(Qt::DotLine);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
    }

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

    // Helper struct to represent an edge path with direction
    struct DirectedPath
    {
        fla::Point start;
        fla::Point end;
        const fla::Path* path;
        bool reversed; // true if this path should be traced backwards
        bool used = false;
    };

    // Collect directed paths for each fill style
    std::map<int, std::vector<DirectedPath>> fillStylePaths;
    std::map<int, const fla::FillStyle*> fillStyles;

    // Helper to get start/end points of a path
    auto getPathEndpoints = [](const fla::Path& path) -> std::pair<fla::Point, fla::Point>
    {
        fla::Point start, end;
        bool hasStart = false;

        for (const fla::PathSegment& segment : path.segments)
        {
            if (segment.command == fla::PathSegment::Command::Move)
            {
                start = segment.points[0];
                end = start;
                hasStart = true;
            }
            else if (segment.command == fla::PathSegment::Command::Line)
            {
                end = segment.points[0];
            }
            else if (segment.command == fla::PathSegment::Command::Quad)
            {
                end = segment.points[1];
            }
            else if (segment.command == fla::PathSegment::Command::Cubic)
            {
                end = segment.points[2];
            }
        }

        return {start, end};
    };

    // Process all edges and collect directed paths
    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::Path& path : edge->paths)
        {
            if (!path.visible || path.segments.empty())
                continue;

            auto [start, end] = getPathEndpoints(path);

            int fillStyleIdx1 = path.fillStyleIndex != -1 ? path.fillStyleIndex : edge->fillStyle1;
            int fillStyleIdx0 = edge->fillStyle0;

            // Add to fillStyle1 (forward direction)
            if (fillStyleIdx1 != -1)
            {
                fillStylePaths[fillStyleIdx1].push_back({start, end, &path, false, false});
                if (fillStyles.find(fillStyleIdx1) == fillStyles.end())
                {
                    fillStyles[fillStyleIdx1] = shape->getFillStyleByIndex(fillStyleIdx1);
                }
            }

            // Add to fillStyle0 (reverse direction)
            if (fillStyleIdx0 != -1 && fillStyleIdx0 != fillStyleIdx1)
            {
                fillStylePaths[fillStyleIdx0].push_back({end, start, &path, true, false});
                if (fillStyles.find(fillStyleIdx0) == fillStyles.end())
                {
                    fillStyles[fillStyleIdx0] = shape->getFillStyleByIndex(fillStyleIdx0);
                }
            }
        }
    }

    // Helper lambda to build a complete path from a path (for stroke rendering)
    auto buildPath = [](const fla::Path& path, const fla::Element* shape) -> QPainterPath
    {
        QPainterPath painterPath;
        painterPath.setFillRule(Qt::OddEvenFill);
        for (const fla::PathSegment& segment : path.segments)
        {
            if (segment.command == fla::PathSegment::Command::Move)
            {
                fla::Point p = segment.points[0];
                painterPath.moveTo(p.x, p.y);
            }
            else if (segment.command == fla::PathSegment::Command::Line)
            {
                fla::Point p = segment.points[0];
                painterPath.lineTo(p.x, p.y);
            }
            else if (segment.command == fla::PathSegment::Command::Quad)
            {
                fla::Point p1 = segment.points[0];
                fla::Point p2 = segment.points[1];
                painterPath.quadTo(p1.x, p1.y, p2.x, p2.y);
            }
            else if (segment.command == fla::PathSegment::Command::Cubic)
            {
                fla::Point p1 = segment.points[0];
                fla::Point p2 = segment.points[1];
                fla::Point p3 = segment.points[2];
                painterPath.cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
            }
            else if (segment.command == fla::PathSegment::Command::Close)
            {
                painterPath.closeSubpath();
            }
        }
        return painterPath;
    };

    // Helper to add path to path in forward or reverse direction
    auto addSegmentToPath = [](QPainterPath& painterPath, const fla::Path& path, bool reversed, const fla::Element* shape)
    {
        if (!reversed)
        {
            // Add path forward (skip initial Move)
            for (const fla::PathSegment& segment : path.segments)
            {
                if (segment.command == fla::PathSegment::Command::Move)
                {
                    continue;
                }
                else if (segment.command == fla::PathSegment::Command::Line)
                {
                    fla::Point p = segment.points[0];
                    painterPath.lineTo(p.x, p.y);
                }
                else if (segment.command == fla::PathSegment::Command::Quad)
                {
                    fla::Point p1 = segment.points[0];
                    fla::Point p2 = segment.points[1];
                    painterPath.quadTo(p1.x, p1.y, p2.x, p2.y);
                }
                else if (segment.command == fla::PathSegment::Command::Cubic)
                {
                    fla::Point p1 = segment.points[0];
                    fla::Point p2 = segment.points[1];
                    fla::Point p3 = segment.points[2];
                    painterPath.cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
                }
            }
        }
        else
        {
            // Add path in reverse - traverse segments backwards and reverse curves
            // Build list of segments (excluding Move)
            std::vector<fla::PathSegment> pathSegments;
            fla::Point startPoint;

            for (const fla::PathSegment& segment : path.segments)
            {
                if (segment.command == fla::PathSegment::Command::Move)
                {
                    startPoint = segment.points[0];
                }
                else if (segment.command != fla::PathSegment::Command::Close)
                {
                    pathSegments.push_back(segment);
                }
            }

            // Traverse backwards and reverse each segment
            fla::Point currentPos = startPoint;
            for (int i = pathSegments.size() - 1; i >= 0; --i)
            {
                const fla::PathSegment& segment = pathSegments[i];

                // Calculate the start position of this segment (which becomes our end in reverse)
                fla::Point segmentStart = currentPos;

                // Update current position to the endpoint of this segment
                if (i > 0)
                {
                    const fla::PathSegment& prevSeg = pathSegments[i - 1];
                    if (prevSeg.command == fla::PathSegment::Command::Line)
                    {
                        currentPos = prevSeg.points[0];
                    }
                    else if (prevSeg.command == fla::PathSegment::Command::Quad)
                    {
                        currentPos = prevSeg.points[1];
                    }
                    else if (prevSeg.command == fla::PathSegment::Command::Cubic)
                    {
                        currentPos = prevSeg.points[2];
                    }
                }
                else
                {
                    currentPos = startPoint;
                }

                // Reverse this segment
                if (segment.command == fla::PathSegment::Command::Line)
                {
                    fla::Point p = currentPos;
                    painterPath.lineTo(p.x, p.y);
                }
                else if (segment.command == fla::PathSegment::Command::Quad)
                {
                    // Reverse quadratic: end -> control -> start becomes start -> control -> end
                    fla::Point p1 = segment.points[0];
                    fla::Point p2 = currentPos;
                    painterPath.quadTo(p1.x, p1.y, p2.x, p2.y);
                }
                else if (segment.command == fla::PathSegment::Command::Cubic)
                {
                    // Reverse cubic: end -> c2 -> c1 -> start becomes start -> c2 -> c1 -> end
                    fla::Point p1 = segment.points[1];
                    fla::Point p2 = segment.points[0];
                    fla::Point p3 = currentPos;
                    painterPath.cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
                }
            }
        }
    };

    // Helper to check if two points match
    // Increased tolerance to account for floating-point accumulation errors
    // from twips conversion (1/20 pixel) and path transformations
    auto pointsMatch = [](const fla::Point& p1, const fla::Point& p2) -> bool
    {
        return qAbs(p1.x - p2.x) < 0.5 && qAbs(p1.y - p2.y) < 0.5;
    };

    // For each fill style, connect paths into closed loops
    for (auto& [fillIdx, directedPaths] : fillStylePaths)
    {
        const fla::FillStyle* fillStyle = fillStyles[fillIdx];
        if (!fillStyle)
            continue;

        QPainterPath compoundPath;
        compoundPath.setFillRule(Qt::OddEvenFill);

        // First pass: render paths that are already closed
        int closedCount = 0;
        for (DirectedPath& directedPath : directedPaths)
        {
            if (directedPath.used)
                continue;

            // Check if this path is already closed (start == end)
            if (pointsMatch(directedPath.start, directedPath.end))
            {
                QPainterPath loopPath;
                fla::Point startPoint = directedPath.start;
                loopPath.moveTo(startPoint.x, startPoint.y);
                addSegmentToPath(loopPath, *directedPath.path, directedPath.reversed, shape);
                loopPath.closeSubpath();
                compoundPath.addPath(loopPath);
                directedPath.used = true;
                closedCount++;
            }
        }

        // Second pass: connect remaining paths into closed loops
        int loopNum = 0;
        for (DirectedPath& directedPath : directedPaths)
        {
            if (directedPath.used)
                continue;

            loopNum++;

            QPainterPath loopPath;
            fla::Point startPoint = directedPath.start;
            loopPath.moveTo(startPoint.x, startPoint.y);
            addSegmentToPath(loopPath, *directedPath.path, directedPath.reversed, shape);
            directedPath.used = true;

            fla::Point currentEnd = directedPath.end;
            fla::Point loopStart = directedPath.start;
            int maxIterations = directedPaths.size() * 2;
            int iterations = 0;
            int connectedCount = 1;

            while (iterations++ < maxIterations)
            {
                // Check if we've closed the loop
                if (pointsMatch(currentEnd, loopStart))
                {
                    loopPath.closeSubpath();
                    break;
                }

                // Find next connecting segment - try to find the closest match first
                bool found = false;
                double bestDistance = 1e9;
                DirectedPath* bestMatch = nullptr;

                for (DirectedPath& nextDirectedPath : directedPaths)
                {
                    if (nextDirectedPath.used)
                        continue;

                    double dx = currentEnd.x - nextDirectedPath.start.x;
                    double dy = currentEnd.y - nextDirectedPath.start.y;
                    double distance = dx * dx + dy * dy;

                    if (distance < 0.5 * 0.5 && distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestMatch = &nextDirectedPath;
                        found = true;
                    }
                }

                if (found && bestMatch)
                {
                    addSegmentToPath(loopPath, *bestMatch->path, bestMatch->reversed, shape);
                    bestMatch->used = true;
                    currentEnd = bestMatch->end;
                    connectedCount++;
                }
                else
                {
                    break;
                }
            }

            // Close the loop if endpoints match
            if (pointsMatch(currentEnd, loopStart))
            {
                loopPath.closeSubpath();
            }

            compoundPath.addPath(loopPath);
        }

        // Check for unused paths
        int unusedCount = 0;
        for (const DirectedPath& directedPath : directedPaths)
        {
            if (!directedPath.used)
                unusedCount++;
        }
        if (unusedCount > 0)
        {
            std::cout << "  WARNING: " << unusedCount << " unused paths!" << std::endl;
        }

        QBrush brush = getFillBrush(fillStyle);
        painter.setBrush(brush);
        painter.setPen(Qt::NoPen);
        painter.drawPath(compoundPath);

        cacheEntries.push_back({ brush, Qt::NoPen, compoundPath });
    }

    // Now render strokes separately (per segment)
    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::Path& path : edge->paths)
        {
            if (!path.visible || path.segments.empty())
                continue;

            int strokeStyleIdx = path.lineStyleIndex != -1 ? path.lineStyleIndex : edge->strokeStyle;
            if (strokeStyleIdx == -1)
                continue;

            const fla::StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(strokeStyleIdx);
            if (!strokeStyle || !strokeStyle->fill)
                continue;

            QPainterPath strokePath = buildPath(path, shape);

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
    if (newZoom < _minZoom || newZoom > _maxZoom)
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
                bounds = transform.mapRect(bounds);

                return bounds;
            }
        }

        // Default bounds for symbol instances without data
        QRectF bounds = QRectF(-50, -50, 100, 100);
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds = transform.mapRect(bounds);
        return bounds;
    }
    else if (type == fla::Element::Type::BitmapInstance)
    {
        // Approximate bounds for bitmap instances
        QRectF bounds(0, 0, 100, 100);
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds = transform.mapRect(bounds);
        return bounds;
    }
    else if (type == fla::Element::Type::StaticText)
    {
        // Approximate bounds for text
        QRectF bounds(0, 0, 200, 50);
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds = transform.mapRect(bounds);
        return bounds;
    }
    else if (type == fla::Element::Type::Rectangle)
    {
        const fla::RectanglePrimitive* rectangle = static_cast<const fla::RectanglePrimitive*>(element);
        QRectF bounds(rectangle->rect.topLeft.x, rectangle->rect.topLeft.y, rectangle->rect.width(), rectangle->rect.height());
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds = transform.mapRect(bounds);
        return bounds;
    }
    else if (type == fla::Element::Type::Oval)
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(element);
        QRectF bounds(oval->rect.topLeft.x, oval->rect.topLeft.y, oval->rect.width(), oval->rect.height());
        QTransform transform(element->transform.m11, element->transform.m12,
                            element->transform.m21, element->transform.m22,
                            element->transform.tx, element->transform.ty);
        bounds = transform.mapRect(bounds);
        return bounds;
    }

    // Default bounds
    QRectF bounds(-10, -10, 20, 20);
    QTransform transform(element->transform.m11, element->transform.m12,
                        element->transform.m21, element->transform.m22,
                        element->transform.tx, element->transform.ty);
    bounds = transform.mapRect(bounds);
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

        for (const fla::Path& path : edge->paths)
        {
            if (!path.visible)
                continue;

            // Check segment-specific stroke
            if (path.lineStyleIndex != -1)
            {
                const fla::StrokeStyle* segStroke = shape->getStrokeStyleByIndex(path.lineStyleIndex);
                if (segStroke)
                {
                    maxStrokeWeight = qMax(maxStrokeWeight, segStroke->weight);
                }
            }

            for (const fla::PathSegment& segment : path.segments)
            {
                // Add all points from this segment to bounds
                for (const fla::Point& pt : segment.points)
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
    bounds = transform.mapRect(bounds);

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
        if (element->elementType() == fla::Element::Type::SymbolInstance)
        {
            painter.setPen(QPen(QColor(0, 0, 255, 255), 1.0, Qt::DashLine)); // Blue dashed for shapes
        }
        else
        {
            painter.setPen(QPen(QColor(255, 0, 0, 255), 1.0, Qt::DashLine)); // Red dashed for element bounds
        }
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
            QTransform transform(element->transform.m11, element->transform.m12,
                                 element->transform.m21, element->transform.m22,
                                 element->transform.tx, element->transform.ty);

            painter.save();
            painter.setTransform(transform, true);

            for (const fla::Timeline* timeline : symbol->timelines)
            {
                drawTimelineBounds(painter, timeline);
            }

            painter.restore();
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
            {
                continue; // Only draw bounds for first frame of each layer for clarity
            }

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
