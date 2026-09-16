#include "phoenix_view.h"

#include <QTimer>

#include "../edit/selection.h"
#include "../edit/snapping.h"
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
#include "../data/frame.h"
#include "../data/morph_shape.h"
#include "../data/morph_curves.h"
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
#include <QImage>
#include <QBitmap>
#include <QSvgGenerator>
#include <QtMath>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const int supersampleFactor = 2;

namespace {

fla::Point lerpMorphPoint(const fla::Point& a, const fla::Point& b, double t)
{
    return fla::Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

void expandBounds(QRectF& r, const QPointF& p, bool& first)
{
    if (first)
    {
        r = QRectF(p, QSizeF(0, 0));
        first = false;
    }
    else
    {
        r.setLeft(qMin(r.left(), p.x()));
        r.setRight(qMax(r.right(), p.x()));
        r.setTop(qMin(r.top(), p.y()));
        r.setBottom(qMax(r.bottom(), p.y()));
    }
}

/// Build morphed outline from Flash MorphShape (quadratic segments), in morph document space.
bool buildMorphShapePath(const fla::MorphShape* morph, double t, QPainterPath& outPath, QRectF& outBounds)
{
    outPath = QPainterPath();
    outPath.setFillRule(Qt::WindingFill);
    bool first = true;
    QRectF bounds;
    bool any = false;

    for (const fla::MorphSegment* seg : morph->segments)
    {
        if (!seg)
            continue;

        fla::Point s = lerpMorphPoint(seg->startPointA, seg->startPointB, t);
        QPointF start(s.x, s.y);
        outPath.moveTo(start);
        expandBounds(bounds, start, first);
        any = true;

        for (const fla::MorphCurves* curve : seg->curves)
        {
            if (!curve)
                continue;
            fla::Point cp = lerpMorphPoint(curve->controlPointA, curve->controlPointB, t);
            fla::Point ap = lerpMorphPoint(curve->anchorPointA, curve->anchorPointB, t);
            QPointF qcp(cp.x, cp.y);
            QPointF qap(ap.x, ap.y);
            outPath.quadTo(qcp, qap);
            expandBounds(bounds, qcp, first);
            expandBounds(bounds, qap, first);
        }
        outPath.closeSubpath();
    }

    outBounds = bounds;
    return any && !outPath.isEmpty();
}

/// Interpolate axis-aligned bounds between two keyframe shapes (position + size).
fla::Rect lerpLocalBounds(const fla::Rect& a, const fla::Rect& b, double t)
{
    fla::Rect r;
    r.topLeft.x = a.topLeft.x + (b.topLeft.x - a.topLeft.x) * t;
    r.topLeft.y = a.topLeft.y + (b.topLeft.y - a.topLeft.y) * t;
    r.bottomRight.x = a.bottomRight.x + (b.bottomRight.x - a.bottomRight.x) * t;
    r.bottomRight.y = a.bottomRight.y + (b.bottomRight.y - a.bottomRight.y) * t;
    return r;
}

QTransform morphBoundsToShapeBounds(const QRectF& morphBounds, const fla::Rect& shapeLocalBounds)
{
    QRectF shapeBounds(
        shapeLocalBounds.topLeft.x,
        shapeLocalBounds.topLeft.y,
        qMax(0.0, shapeLocalBounds.width()),
        qMax(0.0, shapeLocalBounds.height()));

    if (morphBounds.width() < 1e-9 || morphBounds.height() < 1e-9 ||
        shapeBounds.width() < 1e-9 || shapeBounds.height() < 1e-9)
        return QTransform();

    const double sx = shapeBounds.width() / morphBounds.width();
    const double sy = shapeBounds.height() / morphBounds.height();
    const QPointF mc = morphBounds.center();
    const QPointF sc = shapeBounds.center();

    QTransform xf;
    xf.translate(sc.x(), sc.y());
    xf.scale(sx, sy);
    xf.translate(-mc.x(), -mc.y());
    return xf;
}

const fla::Frame* owningFrame(const fla::Shape* shape)
{
    for (const fla::DOMElement* p = shape->parent; p; p = p->parent)
    {
        if (p->domType() == fla::DOMElement::DOMType::Frame)
            return static_cast<const fla::Frame*>(p);
    }
    return nullptr;
}

} // namespace

PhoenixView::PhoenixView(Player* player, QWidget *parent)
    : QWidget(parent)
    , _flaDocument(nullptr)
    , _player(player)
    , _highQualityAntiAliasing(true)
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

    // Long enough that a continuous wheel gesture never refines mid-scroll,
    // short enough that the sharp frame feels immediate once it stops.
    _settleTimer = new QTimer(this);
    _settleTimer->setSingleShot(true);
    _settleTimer->setInterval(120);
    connect(_settleTimer, &QTimer::timeout, this, &PhoenixView::endInteraction);

    connect(_player, &Player::currentFrameChanged, this, &PhoenixView::onPlayerFrameChanged);
}

PhoenixView::~PhoenixView()
{
}

void PhoenixView::onPlayerFrameChanged(int frame)
{
    update(); // Trigger repaint when player frame changes
}

void PhoenixView::onElementSelected(const fla::DOMElement* element)
{
    _selectedElement = element;
    update();
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

void PhoenixView::setHighQualityAntiAliasing(bool on)
{
    if (_highQualityAntiAliasing == on)
        return;
    _highQualityAntiAliasing = on;
    clearCaches();
    update();
}

bool PhoenixView::exportToSvg(const QString& filePath)
{
    if (!_flaDocument || !_flaDocument->document)
        return false;

    fla::Document* document = _flaDocument->document;

    double docWidth = document->width;
    double docHeight = document->height;
    if (docWidth <= 0 || docHeight <= 0)
        return false;

    QSvgGenerator generator;
    generator.setFileName(filePath);
    generator.setSize(QSize(qCeil(docWidth), qCeil(docHeight)));
    generator.setViewBox(QRectF(0, 0, docWidth, docHeight));
    generator.setTitle("Phoenix SVG Export");
    generator.setDescription("Single frame exported from Phoenix.");

    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Render in pure document space: no pan/zoom and no supersampling, so SVG
    // coordinates map 1:1 to document pixels. The traversal below reuses the
    // exact same drawing code as on-screen rendering, so vector shapes,
    // gradients, strokes and text are written out as real SVG primitives.
    // viewTransform is consulted by the color-transformed symbol path; here the
    // painter's base transform is identity (document space).
    viewTransform = painter.transform();
    _visibleRect = QRectF(0, 0, docWidth, docHeight);

    painter.fillRect(QRectF(0, 0, docWidth, docHeight),
        QColor(document->backgroundColor[0], document->backgroundColor[1],
            document->backgroundColor[2], document->backgroundColor[3]));

    drawDocument(painter, document);

    painter.end();
    return true;
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
    fla::Document* document = _flaDocument->document;

    double docWidth = document->width;
    double docHeight = document->height;

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
    double scale = _zoom;

    // Calculate center offset for initial fit
    double centerX = (widgetRect.width() - docWidth * scale) / 2.0;
    double centerY = (widgetRect.height() - docHeight * scale) / 2.0;

    // Calculate visible rect in document coordinates for culling
    double invScale = 1.0 / scale;

    _visibleRect = QRectF(
        (-_panX - centerX) * invScale,
        (-_panY - centerY) * invScale,
        widgetRect.width() * invScale,
        widgetRect.height() * invScale
    );

    double penWidth = 1.0 / scale;
    _radius = 2.0 / scale;
    _overlayPen = QPen(QColor(0, 255, 255, 255), penWidth);
    _overlayBrush = QBrush(QColor(0, 255, 255, 150));

    if (_highQualityAntiAliasing && !_interacting)
    {
        // Supersampling: render at 2x resolution and scale down for smoother edges
        int bufW = width() * supersampleFactor;
        int bufH = height() * supersampleFactor;
        QImage buffer(bufW, bufH, QImage::Format_ARGB32_Premultiplied);
        buffer.fill(backgroundColor);

        QPainter bufferPainter(&buffer);
        bufferPainter.setRenderHint(QPainter::Antialiasing, true);
        bufferPainter.setRenderHint(QPainter::TextAntialiasing, true);
        bufferPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // Same mapping as everything else, just rendered at a larger scale and
        // scaled back down afterwards.
        const double ss = static_cast<double>(supersampleFactor);
        bufferPainter.scale(ss, ss);
        bufferPainter.setTransform(documentToWidget(), true);

        viewTransform = bufferPainter.transform();

        bufferPainter.fillRect(QRectF(0, 0, docWidth, docHeight),
            QColor(document->backgroundColor[0], document->backgroundColor[1],
                document->backgroundColor[2], document->backgroundColor[3]));

        if (_showGrid)
            drawGrid(bufferPainter, document);

        drawDocument(bufferPainter, document);

        bufferPainter.setPen(QPen(QColor(0, 0, 0, 255), 1.0));
        bufferPainter.drawRect(0, 0, docWidth, docHeight);

        bufferPainter.end();

        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(rect(), buffer, buffer.rect());

        // Overlays go on at screen resolution rather than through the
        // supersampled buffer, so handles stay crisp.
        painter.save();
        painter.setTransform(documentToWidget(), true);
        drawToolOverlay(painter);
        painter.restore();
    }
    else
    {
        painter.save();
        painter.setTransform(documentToWidget(), true);

        viewTransform = painter.transform();

        painter.fillRect(0, 0, docWidth, docHeight,
            QColor(document->backgroundColor[0], document->backgroundColor[1],
                document->backgroundColor[2], document->backgroundColor[3]));

        if (_showGrid)
            drawGrid(painter, document);

        drawDocument(painter, document);

        painter.setPen(QPen(QColor(0, 0, 0, 255), 1.0));
        painter.drawRect(0, 0, docWidth, docHeight);
        drawToolOverlay(painter);
        painter.restore();
    }

    // Draw dimming overlay outside document area if enabled
    if (_dimOutsideDocument)
    {
        QRectF docRect(centerX + _panX, centerY + _panY, docWidth * scale, docHeight * scale);
        QPainterPath dimPath;
        dimPath.addRect(rect());
        QPainterPath docPath;
        docPath.addRect(docRect);
        QPainterPath maskedPath = dimPath.subtracted(docPath);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillPath(maskedPath, QColor(0, 0, 0, 180));
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

        drawDocumentBounds(painter, document);

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

        // Use full widget resolution for mask (2x for HQAA)
        const int supersampleFactor = _highQualityAntiAliasing ? 2 : 1;
        QRect r = rect();
        QPixmap maskPixmap(r.width() * supersampleFactor, r.height() * supersampleFactor);
        maskPixmap.fill(Qt::transparent);

        QPainter maskPainter(&maskPixmap);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setRenderHint(QPainter::TextAntialiasing, true);
        maskPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (supersampleFactor > 1)
        {
            maskPainter.scale(supersampleFactor, supersampleFactor);
        }
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

                // Use full widget resolution for content (2x for HQAA)
                QRect r = rect();
                QPixmap contentPixmap(r.width() * supersampleFactor, r.height() * supersampleFactor);
                contentPixmap.fill(Qt::transparent);

                QPainter contentPainter(&contentPixmap);
                contentPainter.setRenderHint(QPainter::Antialiasing, true);
                contentPainter.setRenderHint(QPainter::TextAntialiasing, true);
                contentPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                if (supersampleFactor > 1)
                {
                    contentPainter.scale(supersampleFactor, supersampleFactor);
                }
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
            else
            {
                // No mask found for this masked layer, just draw it normally
                drawLayer(painter, layer, loopType, firstFrame, nullptr);
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

/// Works out which frame of a layer is showing and how far any tween on it has
/// run. Pulled out of drawLayer so hit-testing picks exactly what is drawn.
PhoenixView::LayerFrame PhoenixView::resolveLayerFrame(const fla::Layer* layer,
    fla::LoopType loopType, int firstFrame)
{
    const fla::Frame* currentFrame = nullptr;
    const fla::Frame* nextTweenFrame = nullptr;
    double tweenProgress = 0.0;

    int currentFrameIndex;
    int layerDuration = calculateLayerDuration(layer);
    bool pingPongReverse = false;

    if (loopType == fla::LoopType::SingleFrame || layer->frames.empty())
    {
        currentFrameIndex = layer->firstFrame + firstFrame;
    }
    else if (loopType == fla::LoopType::Loop)
    {
        int relativeFrame = _player->currentFrame() % layerDuration;
        currentFrameIndex = layer->firstFrame + firstFrame + relativeFrame;
    }
    else if (loopType == fla::LoopType::PingPong)
    {
        int pingPongDuration = layerDuration * 2;
        int t = _player->currentFrame() % pingPongDuration;
        if (t >= layerDuration)
        {
            t = pingPongDuration - t;
            pingPongReverse = true;
        }
        currentFrameIndex = layer->firstFrame + firstFrame + t;
    }
    else // PlayOnce
    {
        currentFrameIndex = layer->firstFrame + firstFrame + _player->currentFrame();
    }

    for (const fla::Frame* frame : layer->frames)
    {
        if (frame->index <= currentFrameIndex)
        {
            currentFrame = frame;
        }
    }

    if (currentFrame && currentFrame->tweenType != fla::TweenType::None)
    {
        int tweenStartIndex = currentFrame->index;
        int tweenEndIndex = currentFrame->index + currentFrame->duration;

        if (currentFrameIndex > tweenStartIndex && currentFrameIndex < tweenEndIndex)
        {
            tweenProgress = static_cast<double>(currentFrameIndex - tweenStartIndex) / currentFrame->duration;

            for (const fla::Frame* frame : layer->frames)
            {
                if (frame->index == tweenEndIndex)
                {
                    nextTweenFrame = frame;
                    break;
                }
            }
        }
    }

    if (pingPongReverse && tweenProgress > 0.0)
    {
        tweenProgress = 1.0 - tweenProgress;
    }

    LayerFrame state;
    state.frame = currentFrame;
    state.tweenFrame = nextTweenFrame;
    state.tweenProgress = tweenProgress;
    return state;
}

void PhoenixView::drawLayer(QPainter& painter, const fla::Layer* layer, fla::LoopType loopType, int firstFrame, const QPixmap* maskPixmap)
{
    QColor color;
    color.setRgb(layer->color[0], layer->color[1], layer->color[2], layer->color[3]);
    painter.setPen(QPen(color, 1.0));

    const LayerFrame state = resolveLayerFrame(layer, loopType, firstFrame);

    if (state.frame && state.frame->visible)
    {
        drawFrame(painter, state.frame, state.tweenFrame, state.tweenProgress);
    }
}

void PhoenixView::drawFrame(QPainter& painter, const fla::Frame* frame, const fla::Frame* nextTweenFrame, double tweenProgress)
{
    int nextElementIndex = 0;
    for (const fla::Element* element : frame->elements)
    {
        if (element->visible)
        {
            const fla::Element* nextElement = nextTweenFrame && nextTweenFrame->elements.size() > nextElementIndex ?
                nextTweenFrame->elements[nextElementIndex] : nullptr;

            drawElement(painter, element, nextElement, tweenProgress);
        }

        nextElementIndex++;
    }
}

fla::Transform PhoenixView::interpolateTransform(const fla::Transform& a, const fla::Transform& b, double t)
{
    fla::Transform result;
    result.m11 = a.m11 + (b.m11 - a.m11) * t;
    result.m12 = a.m12 + (b.m12 - a.m12) * t;
    result.m21 = a.m21 + (b.m21 - a.m21) * t;
    result.m22 = a.m22 + (b.m22 - a.m22) * t;
    result.tx = a.tx + (b.tx - a.tx) * t;
    result.ty = a.ty + (b.ty - a.ty) * t;
    return result;
}

int PhoenixView::calculateLayerDuration(const fla::Layer* layer)
{
    int total = 0;
    for (const fla::Frame* frame : layer->frames)
    {
        total += frame->duration;
    }
    return total > 0 ? total : 1;
}

void PhoenixView::drawElement(QPainter& painter, const fla::Element* element, const fla::Element* tweenElement, double tweenProgress)
{
    fla::Transform transform = element->transform;
    if (tweenElement)
    {
        transform = interpolateTransform(element->transform, tweenElement->transform, tweenProgress);
    }

    QTransform qTransform(transform.m11, transform.m12,
                       transform.m21, transform.m22,
                       transform.tx, transform.ty);

    painter.save();

    // Why does a group have a transform, if applying the groups transform
    // is incorrect because the children are already transformed by the group transform?
    if (element->elementType() != fla::Element::Type::Group)
    {
        painter.setTransform(qTransform, true);
    }

    fla::Element::Type type = element->elementType();

    if (type == fla::Element::Type::Shape)
    {
        const fla::Shape* shape = static_cast<const fla::Shape*>(element);
        const fla::Shape* tweenShape = (tweenElement && tweenElement->elementType() == fla::Element::Type::Shape) ?
            static_cast<const fla::Shape*>(tweenElement) : nullptr;
        drawShape(painter, shape, tweenShape, tweenProgress);
    }
    else if (type == fla::Element::Type::SymbolInstance)
    {
        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
        const fla::Symbol* symbol = instance->symbol;
        if (symbol && symbol->visible)
        {
            int frameOffset = (instance->symbolType == fla::SymbolType::Button) ? 0 : instance->firstFrame;
            fla::LoopType loopType = (instance->symbolType == fla::SymbolType::Button) ? fla::LoopType::SingleFrame : instance->loopType;

            if (!instance->colorTransform.isIdentity())
            {
                QRectF symbolBounds = QRectF(0, 0, _flaDocument->document->width, _flaDocument->document->height);
                int pixWidth = qMax(1, static_cast<int>(qCeil(symbolBounds.width())));
                int pixHeight = qMax(1, static_cast<int>(qCeil(symbolBounds.height())));

                QImage symbolImage(pixWidth, pixHeight, QImage::Format_ARGB32);
                symbolImage.fill(Qt::transparent);

                QPainter symbolPainter(&symbolImage);
                symbolPainter.setRenderHint(QPainter::Antialiasing, true);
                symbolPainter.setRenderHint(QPainter::TextAntialiasing, true);
                symbolPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                symbolPainter.setTransform(qTransform, false);

                for (const fla::Timeline* timeline : symbol->timelines)
                {
                    if (timeline->visible)
                    {
                        symbolPainter.save();
                        drawTimeline(symbolPainter, timeline, loopType, frameOffset);
                        symbolPainter.restore();
                    }
                }
                symbolPainter.end();

                applyColorTransform(symbolImage, instance->colorTransform);

                painter.save();
                painter.setTransform(viewTransform, false);
                painter.drawImage(0, 0, symbolImage);
                painter.restore();
            }
            else
            {
                for (const fla::Timeline* timeline : symbol->timelines)
                {
                    if (timeline->visible)
                    {
                        painter.save();
                        drawTimeline(painter, timeline, loopType, frameOffset);
                        painter.restore();
                    }
                }
            }
        }
    }
    else if (type == fla::Element::Type::Group)
    {
        const fla::Group* group = static_cast<const fla::Group*>(element);
        const fla::Group* tweenGroup = nullptr;
        if (tweenElement && tweenElement->elementType() == fla::Element::Type::Group)
        {
            tweenGroup = static_cast<const fla::Group*>(tweenElement);
        }
        int nextElementIndex = 0;
        for (const fla::Element* member : group->members)
        {
            const fla::Element* tweenMember = nullptr;
            if (tweenGroup && nextElementIndex < tweenGroup->members.size())
            {
                tweenMember = tweenGroup->members[nextElementIndex];
            }
            if (member->visible)
            {
                painter.save();
                drawElement(painter, member, tweenMember, tweenProgress);
                painter.restore();
            }
        }
    }
    else if (type == fla::Element::Type::Rectangle)
    {
        const fla::RectanglePrimitive* rectangle = static_cast<const fla::RectanglePrimitive*>(element);
        QPen pen = getPen(rectangle->strokeStyle);
        QBrush brush = getFillBrush(rectangle->fillStyle, rectangle->localBounds);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawRect(rectangle->rect.topLeft.x, rectangle->rect.topLeft.y, rectangle->rect.width(), rectangle->rect.height());
    }
    else if (type == fla::Element::Type::Oval)
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(element);
        QPen pen = getPen(oval->strokeStyle);
        QBrush brush = getFillBrush(oval->fillStyle, oval->localBounds);
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

                QFontDatabase fontDatabase;
                if (!fontDatabase.families().contains(fontFamily))
                {
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
            double size = run.size == 0.0 ? 12.0 : run.size;
            font.setPointSizeF(size * 0.75); // Adjust size to better match Flash's rendering
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

        double cx = 50, cy = 50, radius = 50;

        QRadialGradient gradient(cx, cy, radius);

        double focalOffset = fill->focalPointRatio * radius;
        gradient.setFocalPoint(cx + focalOffset, cy);

        for (const fla::RadialEntry& entry : fill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }

        pen.setBrush(QBrush(gradient));
    }

    return pen;
}

QBrush PhoenixView::getFillBrush(const fla::FillStyle* fillStyle, const fla::Rect& bounds)
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

        const double halfSpan = 1625.0;
        QLinearGradient gradient(QPointF(-halfSpan, 0.0), QPointF(halfSpan, 0.0));
        for (const fla::GradientEntry& entry : linearFill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }
        QBrush brush(gradient);
        const double s = 0.5;
        QTransform brushTransform(
            linearFill->transform.m11 * s, linearFill->transform.m12 * s,
            linearFill->transform.m21 * s, linearFill->transform.m22 * s,
            linearFill->transform.tx,      linearFill->transform.ty
        );

        brush.setTransform(brushTransform);

        return brush;
    }
    else if (fillStyle->type() == fla::FillStyle::Type::RadialGradient)
    {
        const fla::RadialGradient* radialFill = static_cast<const fla::RadialGradient*>(fillStyle);

        fla::Point center = bounds.center();
        const double maxRatio = 0.99;
        double focalRatio = std::max(-maxRatio, std::min(maxRatio, radialFill->focalPointRatio));

        double cx = 0.0;
        double cy = 0.0;
        double radius = 1625.0;

        QRadialGradient gradient(cx, cy, radius);

        double focalOffset = focalRatio * radius;
        gradient.setFocalPoint(cx + focalOffset, cy);

        for (const fla::RadialEntry& entry : radialFill->entries)
        {
            QColor color(entry.color[0], entry.color[1], entry.color[2], entry.color[3]);
            gradient.setColorAt(entry.ratio, color);
        }

        QBrush brush(gradient);

        const double s = 0.5;
        QTransform brushTransform(radialFill->transform.m11 * s, radialFill->transform.m12 * s,
                            radialFill->transform.m21 * s, radialFill->transform.m22 * s,
                            radialFill->transform.tx, radialFill->transform.ty);

        brush.setTransform(brushTransform);

        return brush;
    }

    return Qt::NoBrush;
}

void PhoenixView::drawOverlayPoints(QPainter& painter, const fla::Shape* shape)
{
    bool isEdgeSelected = _selectedElement->domType() == fla::DOMElement::DOMType::Edge;
    bool isPathSelected = _selectedElement->domType() == fla::DOMElement::DOMType::Path;
    bool isSegmentSelected = _selectedElement->domType() == fla::DOMElement::DOMType::PathSegment;

    painter.save();
    painter.setPen(_overlayPen);
    painter.setBrush(_overlayBrush);
    for (const fla::Edge* edge : shape->edges)
    {
        if (isEdgeSelected && edge != _selectedElement)
            continue;

        for (const fla::Path* path : edge->paths)
        {
            if (isPathSelected && path != _selectedElement)
                continue;

            QPointF lastPoint;
            bool firstPoint = true;
            for (const fla::PathSegment* segment : path->segments)
            {
                for (const fla::Point& pt : segment->points)
                {
                    QPointF point = QPointF(pt.x, pt.y);
                    painter.drawEllipse(point, 5.0, 5.0);
                    if (!firstPoint)
                        painter.drawLine(lastPoint, point);
                    else
                        firstPoint = false;
                    lastPoint = point;
                }
            }
        }
    }
    painter.restore();
}

/// Builds the fill and stroke paths for a shape without touching a painter, so
/// hit-testing can ask for the same geometry the renderer draws.
void PhoenixView::buildShapePaths(const fla::Shape* shape, PathCacheList& cacheEntries)
{
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
    auto getPathEndpoints = [](const fla::Path* path) -> std::pair<fla::Point, fla::Point>
    {
        fla::Point start, end;
        bool hasStart = false;

        for (const fla::PathSegment* segment : path->segments)
        {
            if (segment->command == fla::PathSegment::Command::Move)
            {
                start = segment->points[0];
                end = start;
                hasStart = true;
            }
            else if (segment->command == fla::PathSegment::Command::Line)
            {
                end = segment->points[0];
            }
            else if (segment->command == fla::PathSegment::Command::Quad)
            {
                end = segment->points[1];
            }
            else if (segment->command == fla::PathSegment::Command::Cubic)
            {
                end = segment->points[2];
            }
        }

        return {start, end};
    };

    // Process all edges and collect directed paths
    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::Path* path : edge->paths)
        {
            if (!path->visible || path->segments.empty())
                continue;

            auto [start, end] = getPathEndpoints(path);

            int fillStyleIdx1 = path->fillStyleIndex != -1 ? path->fillStyleIndex : edge->fillStyle1;
            int fillStyleIdx0 = edge->fillStyle0;

            // Add to fillStyle1 (forward direction)
            if (fillStyleIdx1 != -1)
            {
                fillStylePaths[fillStyleIdx1].push_back({start, end, path, false, false});
                if (fillStyles.find(fillStyleIdx1) == fillStyles.end())
                {
                    fillStyles[fillStyleIdx1] = shape->getFillStyleByIndex(fillStyleIdx1);
                }
            }

            // Add to fillStyle0 (reverse direction)
            if (fillStyleIdx0 != -1 && fillStyleIdx0 != fillStyleIdx1)
            {
                fillStylePaths[fillStyleIdx0].push_back({end, start, path, true, false});
                if (fillStyles.find(fillStyleIdx0) == fillStyles.end())
                {
                    fillStyles[fillStyleIdx0] = shape->getFillStyleByIndex(fillStyleIdx0);
                }
            }
        }
    }

    // Helper lambda to build a complete path from a path (for stroke rendering)
    auto buildPath = [&](const fla::Path* path, const fla::Element* shape) -> QPainterPath
    {
        QPainterPath painterPath;
        painterPath.setFillRule(Qt::WindingFill);
        for (const fla::PathSegment* segment : path->segments)
        {
            if (segment->command == fla::PathSegment::Command::Move)
            {
                fla::Point p = segment->points[0];
                painterPath.moveTo(p.x, p.y);
            }
            else if (segment->command == fla::PathSegment::Command::Line)
            {
                fla::Point p = segment->points[0];
                painterPath.lineTo(p.x, p.y);
            }
            else if (segment->command == fla::PathSegment::Command::Quad)
            {
                fla::Point p1 = segment->points[0];
                fla::Point p2 = segment->points[1];
                painterPath.quadTo(p1.x, p1.y, p2.x, p2.y);
            }
            else if (segment->command == fla::PathSegment::Command::Cubic)
            {
                fla::Point p1 = segment->points[0];
                fla::Point p2 = segment->points[1];
                fla::Point p3 = segment->points[2];
                painterPath.cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
            }
            else if (segment->command == fla::PathSegment::Command::Close)
            {
                painterPath.closeSubpath();
            }
        }
        return painterPath;
    };

    // Helper to add path to path in forward or reverse direction
    auto addSegmentToPath = [&](QPainterPath& painterPath, const fla::Path* path, bool reversed, const fla::Element* shape)
    {
        if (!reversed)
        {
            // Add path forward (skip initial Move)
            for (const fla::PathSegment* segment : path->segments)
            {
                if (segment->command == fla::PathSegment::Command::Move)
                {
                    continue;
                }
                else if (segment->command == fla::PathSegment::Command::Line)
                {
                    fla::Point p = segment->points[0];
                    painterPath.lineTo(p.x, p.y);
                }
                else if (segment->command == fla::PathSegment::Command::Quad)
                {
                    fla::Point p1 = segment->points[0];
                    fla::Point p2 = segment->points[1];
                    painterPath.quadTo(p1.x, p1.y, p2.x, p2.y);
                }
                else if (segment->command == fla::PathSegment::Command::Cubic)
                {
                    fla::Point p1 = segment->points[0];
                    fla::Point p2 = segment->points[1];
                    fla::Point p3 = segment->points[2];
                    painterPath.cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
                }
            }
        }
        else
        {
            // Add path in reverse - traverse segments backwards and reverse curves
            // Build list of segments (excluding Move)
            std::vector<const fla::PathSegment*> pathSegments;
            std::vector<const fla::PathSegment*> tweenPathSegments;
            fla::Point startPoint;
            fla::Point tweenStartPoint;

            for (const fla::PathSegment* segment : path->segments)
            {
                if (segment->command == fla::PathSegment::Command::Move)
                {
                    startPoint = segment->points[0];
                }
                else if (segment->command != fla::PathSegment::Command::Close)
                {
                    pathSegments.push_back(segment);
                }
            }

            // Traverse backwards and reverse each segment
            fla::Point currentPos = startPoint;
            for (int i = pathSegments.size() - 1; i >= 0; --i)
            {
                const fla::PathSegment* segment = pathSegments[i];

                // Calculate the start position of this segment (which becomes our end in reverse)
                fla::Point segmentStart = currentPos;

                // Update current position to the endpoint of this segment
                if (i > 0)
                {
                    const fla::PathSegment* prevSeg = pathSegments[i - 1];

                    if (prevSeg->command == fla::PathSegment::Command::Line)
                    {
                        currentPos = prevSeg->points[0];
                    }
                    else if (prevSeg->command == fla::PathSegment::Command::Quad)
                    {
                        currentPos = prevSeg->points[1];
                    }
                    else if (prevSeg->command == fla::PathSegment::Command::Cubic)
                    {
                        currentPos = prevSeg->points[2];
                    }
                }
                else
                {
                    currentPos = startPoint;
                }

                // Reverse this segment
                if (segment->command == fla::PathSegment::Command::Line)
                {
                    fla::Point p = currentPos;
                    painterPath.lineTo(p.x, p.y);
                }
                else if (segment->command == fla::PathSegment::Command::Quad)
                {
                    // Reverse quadratic: end -> control -> start becomes start -> control -> end
                    fla::Point p1 = segment->points[0];
                    fla::Point p2 = currentPos;
                    painterPath.quadTo(p1.x, p1.y, p2.x, p2.y);
                }
                else if (segment->command == fla::PathSegment::Command::Cubic)
                {
                    // Reverse cubic: end -> c2 -> c1 -> start becomes start -> c2 -> c1 -> end
                    fla::Point p1 = segment->points[1];
                    fla::Point p2 = segment->points[0];
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
        int p1x = static_cast<int>(std::round(p1.x * 20.0));
        int p1y = static_cast<int>(std::round(p1.y * 20.0));
        int p2x = static_cast<int>(std::round(p2.x * 20.0));
        int p2y = static_cast<int>(std::round(p2.y * 20.0));
        return p1x == p2x && p1y == p2y;
    };

    // For each fill style, connect paths into closed loops
    for (auto& [fillIdx, directedPaths] : fillStylePaths)
    {
        const fla::FillStyle* fillStyle = fillStyles[fillIdx];
        if (!fillStyle)
            continue;

        QPainterPath compoundPath;
        compoundPath.setFillRule(Qt::WindingFill);

        // Heuristic: for very simple shapes (few edge paths), avoid the complex
        // stitching logic and just render each path as-is. This matches how
        // Flash often stores small symbol shapes (like the eyes) and avoids
        // over-connecting segments that should remain separate.
        if (directedPaths.size() <= 2)
        {
            // Try to connect paths into closed loops (similar to complex case)
            for (DirectedPath& directedPath : directedPaths)
            {
                if (directedPath.used)
                    continue;

                // Skip if path is already closed
                if (pointsMatch(directedPath.start, directedPath.end))
                    continue;

                QPainterPath loopPath;
                fla::Point startPoint = directedPath.start;
                loopPath.moveTo(startPoint.x, startPoint.y);
                addSegmentToPath(loopPath, directedPath.path, directedPath.reversed, shape);
                directedPath.used = true;

                fla::Point currentEnd = directedPath.end;
                fla::Point loopStart = directedPath.start;

                // Try to connect to other paths
                for (int iter = 0; iter < directedPaths.size(); iter++)
                {
                    if (pointsMatch(currentEnd, loopStart))
                    {
                        loopPath.closeSubpath();
                        break;
                    }

                    bool found = false;
                    for (DirectedPath& nextDP : directedPaths)
                    {
                        if (nextDP.used)
                            continue;

                        if (pointsMatch(currentEnd, nextDP.start))
                        {
                            addSegmentToPath(loopPath, nextDP.path, nextDP.reversed, shape);
                            nextDP.used = true;
                            currentEnd = nextDP.end;
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                        break;
                }

                if (pointsMatch(currentEnd, loopStart))
                    loopPath.closeSubpath();

                compoundPath.addPath(loopPath);
            }

            // Add any remaining unused paths that don't form loops
            for (const DirectedPath& directedPath : directedPaths)
            {
                if (directedPath.used)
                    continue;

                QPainterPath simplePath;
                simplePath.setFillRule(Qt::WindingFill);

                bool hasMove = false;
                for (const fla::PathSegment* segment : directedPath.path->segments)
                {
                    if (segment->command == fla::PathSegment::Command::Move)
                    {
                        fla::Point p = segment->points[0];
                        simplePath.moveTo(p.x, p.y);
                        hasMove = true;
                        break;
                    }
                }

                if (!hasMove)
                    continue;

                addSegmentToPath(simplePath, directedPath.path, directedPath.reversed, shape);

                if (pointsMatch(directedPath.start, directedPath.end))
                    simplePath.closeSubpath();

                compoundPath.addPath(simplePath);
            }

            QRectF bounds = compoundPath.boundingRect();
            fla::Rect rect({bounds.left(), bounds.top()}, {bounds.left() + bounds.width(), bounds.top() + bounds.height()});
            QBrush brush = getFillBrush(fillStyle, rect);
            cacheEntries.push_back({ brush, Qt::NoPen, compoundPath });
            continue;
        }

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
                addSegmentToPath(loopPath, directedPath.path, directedPath.reversed, shape);
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
            addSegmentToPath(loopPath, directedPath.path, directedPath.reversed, shape);
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

                    if (distance < (0.5 * 0.5) && distance < bestDistance)
                    {
                        bestDistance = distance;
                        bestMatch = &nextDirectedPath;
                        found = true;
                    }
                }

                if (found && bestMatch)
                {
                    addSegmentToPath(loopPath, bestMatch->path, bestMatch->reversed, shape);
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

        QRectF bounds = compoundPath.boundingRect();
        fla::Rect rect({bounds.left(), bounds.top()}, {bounds.left() + bounds.width(), bounds.top() + bounds.height()});
        QBrush brush = getFillBrush(fillStyle, rect);
        cacheEntries.push_back({ brush, Qt::NoPen, compoundPath });
    }

    // Now render strokes separately (per segment)
    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::Path* path : edge->paths)
        {
            if (!path->visible || path->segments.empty())
                continue;

            int strokeStyleIdx = path->lineStyleIndex != -1 ? path->lineStyleIndex : edge->strokeStyle;
            if (strokeStyleIdx == -1)
                continue;

            const fla::StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(strokeStyleIdx);
            if (!strokeStyle || !strokeStyle->fill)
                continue;

            QPainterPath strokePath = buildPath(path, shape);

            QPen pen = getPen(strokeStyle);

            cacheEntries.push_back({ QBrush(Qt::NoBrush), pen, strokePath });
        }
    }
}

const PhoenixView::PathCacheList& PhoenixView::shapePaths(const fla::Shape* shape)
{
    const auto it = _pathCache.find(shape);
    if (it != _pathCache.end())
        return it.value();

    PathCacheList entries;
    buildShapePaths(shape, entries);
    return *_pathCache.insert(shape, entries);
}

void PhoenixView::drawShape(QPainter& painter, const fla::Shape* shape, const fla::Shape* tweenShape, double tweenProgress)
{
    const bool isSelected = isElementSelected(shape);

    // A shape tween replaces the shape's own geometry for the duration of the
    // tween, so it is checked before falling back to the cached paths.
    // Shape tween: use MorphShape quadratic segments (interpolated A/B) mapped to this shape's bounds.
    const fla::Frame* frame = owningFrame(shape);
    if (frame && frame->morphShape && frame->tweenType == fla::TweenType::Shape && tweenShape &&
        tweenProgress > 0.0 && tweenProgress < 1.0)
    {
        QPainterPath morphPath;
        QRectF morphBounds;
        if (buildMorphShapePath(frame->morphShape, tweenProgress, morphPath, morphBounds))
        {
            // Map morph outline into the bounds that interpolate between start and end keyframes
            // so the tween moves across the stage like Animate (not locked to frame 0 only).
            const fla::Rect targetBounds = lerpLocalBounds(shape->localBounds, tweenShape->localBounds, tweenProgress);
            QTransform xf = morphBoundsToShapeBounds(morphBounds, targetBounds);
            QPainterPath drawnPath = xf.isIdentity() ? morphPath : xf.map(morphPath);

            QBrush brush = Qt::NoBrush;
            const fla::FillStyle* fillA = shape->getFillStyleByIndex(1);
            const fla::FillStyle* fillB = tweenShape->getFillStyleByIndex(1);
            if (!fillA && !shape->fillsMap.empty())
                fillA = shape->fillsMap.begin()->second;
            if (!fillB && !tweenShape->fillsMap.empty())
                fillB = tweenShape->fillsMap.begin()->second;
            if (fillA && fillB && fillA->type() == fla::FillStyle::Type::SolidColor &&
                fillB->type() == fla::FillStyle::Type::SolidColor)
            {
                const fla::SolidColor* sa = static_cast<const fla::SolidColor*>(fillA);
                const fla::SolidColor* sb = static_cast<const fla::SolidColor*>(fillB);
                auto lerpCh = [&](int i) -> int {
                    return static_cast<int>(sa->color[i] + (sb->color[i] - sa->color[i]) * tweenProgress + 0.5);
                };
                QColor c(
                    qBound(0, lerpCh(0), 255),
                    qBound(0, lerpCh(1), 255),
                    qBound(0, lerpCh(2), 255),
                    qBound(0, lerpCh(3), 255));
                brush = QBrush(c);
            }
            else if (fillA)
            {
                brush = getFillBrush(fillA, targetBounds);
            }

            painter.setBrush(brush);
            painter.setPen(Qt::NoPen);
            painter.drawPath(drawnPath);
            if (isSelected)
            {
                drawOverlayPoints(painter, shape);
            }
            return;
        }
    }

    for (const PathCacheEntry& entry : shapePaths(shape))
    {
        painter.setBrush(entry.fillBrush);
        painter.setPen(entry.pen);
        painter.drawPath(entry.painterPath);
    }

    if (isSelected)
    {
        drawOverlayPoints(painter, shape);
    }
}

double PhoenixView::pickTolerance() const
{
    // A few screen pixels, expressed in document units, so picking feels the
    // same at every zoom level.
    const double screenPixels = 4.0;
    return _zoom > 0.0 ? screenPixels / _zoom : screenPixels;
}

namespace {

/// Whether a point is within \a tolerance of a path's outline, as opposed to
/// inside the area it encloses.
bool isNearOutline(const QPainterPath& path, const QPointF& point, double tolerance)
{
    // Walking the path's own elements would mean re-flattening curves, so lean
    // on Qt: stroke the outline into a shape and test containment.
    QPainterPathStroker stroker;
    stroker.setWidth(tolerance * 2.0);
    return stroker.createStroke(path).contains(point);
}

} // namespace

HitResult PhoenixView::hitTest(const QPointF& documentPos, double tolerance) const
{
    HitResult result;
    if (!_flaDocument || !_flaDocument->document)
        return result;

    // const_cast is confined to here: hit-testing builds and caches shape paths
    // exactly as drawing does, and returns non-const elements because the caller
    // is about to edit them.
    PhoenixView* self = const_cast<PhoenixView*>(this);

    const fla::Document* document = _flaDocument->document;
    if (!document->visible)
        return result;

    for (const fla::Timeline* timeline : document->timelines)
    {
        if (!timeline->visible)
            continue;

        self->hitTestTimeline(*timeline, fla::LoopType::PlayOnce, 0,
            QTransform(), documentPos, tolerance, result);
    }

    return result;
}

void PhoenixView::hitTestTimeline(const fla::Timeline& timeline, fla::LoopType loopType,
    int firstFrame, const QTransform& toDocument, const QPointF& documentPos,
    double tolerance, HitResult& result)
{
    for (const fla::Layer* layer : timeline.layers)
    {
        if (!layer || !layer->isVisible() || layer->locked)
            continue;

        // Guide layers are authoring aids and never rendered, so they cannot be
        // picked on the stage either.
        if (layer->layerType == fla::Layer::Type::Guide ||
            layer->layerType == fla::Layer::Type::Folder)
        {
            continue;
        }

        const LayerFrame state = resolveLayerFrame(layer, loopType, firstFrame);
        if (!state.frame)
            continue;

        for (fla::Element* element : state.frame->elements)
        {
            if (element)
            {
                hitTestElement(*element, const_cast<fla::Frame*>(state.frame), loopType,
                    toDocument, documentPos, tolerance, result);
            }
        }
    }
}

void PhoenixView::hitTestElement(fla::Element& element, fla::Frame* frame,
    fla::LoopType loopType, const QTransform& toDocument, const QPointF& documentPos,
    double tolerance, HitResult& result)
{
    if (!element.visible)
        return;

    const fla::Transform& t = element.transform;
    const QTransform elementTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);

    // Groups pre-transform their children, so applying the group's own transform
    // as well would move them twice. drawElement skips it for the same reason.
    const bool isGroup = element.elementType() == fla::Element::Type::Group;
    const QTransform elementToDocument = isGroup
        ? toDocument
        : (elementTransform * toDocument);

    bool invertible = false;
    const QTransform toElement = elementToDocument.inverted(&invertible);
    if (!invertible)
        return;

    const QPointF localPos = toElement.map(documentPos);

    // Tolerance is in document units; scaling it into element space keeps the
    // grab distance constant on screen even under a scaled element.
    const double localTolerance = toleranceInLocalSpace(elementToDocument, tolerance);

    switch (element.elementType())
    {
    case fla::Element::Type::Group:
    {
        fla::Group& group = static_cast<fla::Group&>(element);
        for (fla::Element* member : group.members)
        {
            if (member)
            {
                hitTestElement(*member, frame, loopType, elementToDocument,
                    documentPos, tolerance, result);
            }
        }
        return;
    }

    case fla::Element::Type::SymbolInstance:
    {
        fla::SymbolInstance& instance = static_cast<fla::SymbolInstance&>(element);
        const fla::Symbol* symbol = instance.symbol;
        if (!symbol || !symbol->visible)
            return;

        // Record what the instance contains, but report the instance itself:
        // clicking a symbol on the stage selects the instance, not the artwork
        // inside it. Editing the contents means entering the symbol.
        HitResult inner;
        const int frameOffset = (instance.symbolType == fla::SymbolType::Button)
            ? 0 : instance.firstFrame;
        const fla::LoopType instanceLoop = (instance.symbolType == fla::SymbolType::Button)
            ? fla::LoopType::SingleFrame : instance.loopType;

        for (const fla::Timeline* timeline : symbol->timelines)
        {
            if (timeline->visible)
            {
                hitTestTimeline(*timeline, instanceLoop, frameOffset, elementToDocument,
                    documentPos, tolerance, inner);
            }
        }

        if (inner)
        {
            result.element = &element;
            result.frame = frame;
            result.part = inner.part;
            result.elementToDocument = elementToDocument;
        }
        return;
    }

    case fla::Element::Type::Shape:
    {
        const fla::Shape& shape = static_cast<const fla::Shape&>(element);
        HitResult::Part part = HitResult::Part::None;

        for (const PathCacheEntry& entry : shapePaths(&shape))
        {
            const bool stroked = entry.pen.style() != Qt::NoPen;

            if (stroked)
            {
                const double half = qMax(entry.pen.widthF() * 0.5, 0.0);
                if (isNearOutline(entry.painterPath, localPos, half + localTolerance))
                {
                    part = HitResult::Part::Stroke;
                    break;
                }
            }
            else if (entry.painterPath.contains(localPos))
            {
                // Keep looking: a stroke drawn later sits on top of this fill.
                part = HitResult::Part::Fill;
            }
        }

        if (part == HitResult::Part::None)
            return;

        // An anchor under the cursor beats the fill or stroke it belongs to,
        // since that is what the user is reaching for.
        if (isNearAnchor(shape, localPos, localTolerance))
            part = HitResult::Part::Anchor;

        result.element = &element;
        result.frame = frame;
        result.part = part;
        result.elementToDocument = elementToDocument;
        return;
    }

    default:
        break;
    }

    // Text, bitmaps and the primitive shapes have no path geometry to test, so
    // they fall back to their bounding box.
    const QRectF bounds = calculateElementLocalBounds(&element);
    if (bounds.isValid() && bounds.adjusted(-localTolerance, -localTolerance,
            localTolerance, localTolerance).contains(localPos))
    {
        result.element = &element;
        result.frame = frame;
        result.part = HitResult::Part::Fill;
        result.elementToDocument = elementToDocument;
    }
}

double PhoenixView::toleranceInLocalSpace(const QTransform& elementToDocument, double tolerance)
{
    // How much one unit of element space stretches on the way to document space.
    // Using the average of the two axes keeps this stable under a non-uniform
    // scale without needing a full singular value decomposition.
    const double sx = std::hypot(elementToDocument.m11(), elementToDocument.m12());
    const double sy = std::hypot(elementToDocument.m21(), elementToDocument.m22());
    const double scale = (sx + sy) * 0.5;
    return scale > 1.0e-9 ? tolerance / scale : tolerance;
}

bool PhoenixView::isNearAnchor(const fla::Shape& shape, const QPointF& localPos, double tolerance)
{
    const double limit = tolerance * tolerance;

    for (const fla::Edge* edge : shape.edges)
    {
        if (!edge || !edge->visible)
            continue;

        for (const fla::Path* path : edge->paths)
        {
            if (!path || !path->visible)
                continue;

            for (const fla::PathSegment* segment : path->segments)
            {
                if (!segment)
                    continue;

                for (const fla::Point& point : segment->points)
                {
                    const double dx = point.x - localPos.x();
                    const double dy = point.y - localPos.y();
                    if (dx * dx + dy * dy <= limit)
                        return true;
                }
            }
        }
    }

    return false;
}

void PhoenixView::mousePressEvent(QMouseEvent *event)
{
    // The active tool gets first refusal. Middle-drag always pans, so panning
    // stays available whatever tool is selected.
    //
    // Alt deliberately is not a pan modifier: the drawing tools need it for
    // breaking a tangent and for inserting an anchor, and swallowing it here
    // left those silently doing nothing.
    const bool forcePan = event->button() == Qt::MiddleButton;

    beginInteraction();

    if (!forcePan && _activeTool &&
        _activeTool->mousePress(*this, event, mapToDocument(event->position())))
    {
        return;
    }

    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
    {
        _isDragging = true;
        _lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void PhoenixView::mouseMoveEvent(QMouseEvent *event)
{
    if (!_isDragging && _activeTool &&
        _activeTool->mouseMove(*this, event, mapToDocument(event->position())))
    {
        return;
    }

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
    // Whatever handles the release, the gesture is over and the next frame
    // should be the good one.
    endInteraction();

    if (!_isDragging && _activeTool &&
        _activeTool->mouseRelease(*this, event, mapToDocument(event->position())))
    {
        return;
    }

    if (_isDragging)
    {
        _isDragging = false;
        setCursor(_activeTool ? _activeTool->cursor() : QCursor(Qt::ArrowCursor));
    }
}

void PhoenixView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (_activeTool &&
        _activeTool->mouseDoubleClick(*this, event, mapToDocument(event->position())))
    {
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void PhoenixView::keyPressEvent(QKeyEvent* event)
{
    if (_activeTool && _activeTool->keyPress(*this, event))
        return;

    QWidget::keyPressEvent(event);
}

std::vector<fla::Element*> PhoenixView::elementsIn(const QRectF& documentRect)
{
    std::vector<fla::Element*> found;
    if (!_flaDocument || !_flaDocument->document)
        return found;

    const fla::Document* document = _flaDocument->document;
    if (!document->visible)
        return found;

    for (const fla::Timeline* timeline : document->timelines)
    {
        if (!timeline->visible)
            continue;

        for (const fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->isVisible() || layer->locked)
                continue;

            if (layer->layerType == fla::Layer::Type::Guide ||
                layer->layerType == fla::Layer::Type::Folder)
            {
                continue;
            }

            const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
            if (!state.frame || !state.frame->visible)
                continue;

            for (fla::Element* element : state.frame->elements)
            {
                if (!element || !element->visible)
                    continue;

                // Top-level elements carry their own transform into the bounds,
                // so these are already document-space.
                const QRectF bounds = getElementBounds(element);
                if (bounds.isValid() && documentRect.intersects(bounds))
                    found.push_back(element);
            }
        }
    }

    return found;
}

void PhoenixView::setActiveTool(Tool* tool)
{
    if (_activeTool == tool)
        return;

    if (_activeTool)
        _activeTool->deactivate(*this);

    _activeTool = tool;
    setCursor(_activeTool ? _activeTool->cursor() : QCursor(Qt::ArrowCursor));
    update();
}

void PhoenixView::setSelection(fla::Selection* selection)
{
    _selection = selection;
    update();
}

fla::Frame* PhoenixView::activeFrame()
{
    if (!_flaDocument || !_flaDocument->document)
        return nullptr;

    // TODO: follow the layer the user picked once the timeline offers one. Until
    // then, prefer a layer the file marks as selected and otherwise take the
    // first one that can actually be drawn on.
    const fla::Layer* fallback = nullptr;
    const fla::Layer* chosen = nullptr;

    for (const fla::Timeline* timeline : _flaDocument->document->timelines)
    {
        if (!timeline->visible)
            continue;

        for (const fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->isVisible() || layer->locked)
                continue;

            // Folders hold no artwork, and guide layers are drawing aids that
            // never render.
            if (layer->layerType == fla::Layer::Type::Folder ||
                layer->layerType == fla::Layer::Type::Guide)
            {
                continue;
            }

            if (!fallback)
                fallback = layer;

            if (layer->selected)
            {
                chosen = layer;
                break;
            }
        }

        if (chosen)
            break;
    }

    const fla::Layer* layer = chosen ? chosen : fallback;
    if (!layer)
        return nullptr;

    const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
    return const_cast<fla::Frame*>(state.frame);
}

void PhoenixView::setSnapper(fla::Snapper* snapper)
{
    _snapper = snapper;
    update();
}

void PhoenixView::gatherSnapCandidates(fla::Snapper& snapper,
    const std::vector<fla::Element*>& exclude)
{
    snapper.clearCandidates();

    if (!snapper.objectSnapEnabled() || !_flaDocument || !_flaDocument->document)
        return;

    const fla::Document* document = _flaDocument->document;

    // The stage edges and centre are worth lining up with too.
    snapper.addCandidateX(0.0);
    snapper.addCandidateX(document->width / 2.0);
    snapper.addCandidateX(document->width);
    snapper.addCandidateY(0.0);
    snapper.addCandidateY(document->height / 2.0);
    snapper.addCandidateY(document->height);

    // Everything on stage apart from what is being dragged: an object that
    // snapped to its own edge would simply never move.
    const QRectF everything(-1.0e6, -1.0e6, 2.0e6, 2.0e6);
    for (fla::Element* element : elementsIn(everything))
    {
        bool skip = false;
        for (const fla::Element* excluded : exclude)
            skip = skip || excluded == element;
        if (skip)
            continue;

        const QRectF bounds = getElementBounds(element);
        if (!bounds.isValid())
            continue;

        snapper.addCandidateX(bounds.left());
        snapper.addCandidateX(bounds.center().x());
        snapper.addCandidateX(bounds.right());
        snapper.addCandidateY(bounds.top());
        snapper.addCandidateY(bounds.center().y());
        snapper.addCandidateY(bounds.bottom());
    }
}

void PhoenixView::drawGrid(QPainter& painter, const fla::Document* document)
{
    const double spacingX = document->gridSpacingX;
    const double spacingY = document->gridSpacingY;
    if (spacingX <= 0.0 && spacingY <= 0.0)
        return;

    painter.save();

    QColor color(document->gridColor[0], document->gridColor[1],
        document->gridColor[2], document->gridColor[3]);
    // The stored colour is meant for lines over artwork, so it is dialled well
    // back rather than drawn at full strength.
    color.setAlpha(60);
    painter.setPen(QPen(color, 1.0 / qMax(_zoom, 1.0e-6)));

    const double width = document->width;
    const double height = document->height;

    if (spacingX > 0.0)
    {
        for (double x = 0.0; x <= width; x += spacingX)
            painter.drawLine(QPointF(x, 0.0), QPointF(x, height));
    }

    if (spacingY > 0.0)
    {
        for (double y = 0.0; y <= height; y += spacingY)
            painter.drawLine(QPointF(0.0, y), QPointF(width, y));
    }

    painter.restore();
}

void PhoenixView::drawToolOverlay(QPainter& painter)
{
    // Handles have to stay the same size on screen, so everything here is drawn
    // in document units scaled back by the zoom.
    const double scale = _zoom > 0.0 ? 1.0 / _zoom : 1.0;

    const bool showBounds = !_activeTool || _activeTool->showsSelectionBounds();

    if (showBounds && _selection && !_selection->isEmpty())
    {
        painter.save();
        painter.setBrush(Qt::NoBrush);

        QPen outline(QColor(0, 170, 255), 1.5 * scale);
        outline.setCosmetic(false);
        painter.setPen(outline);

        for (fla::DOMElement* selected : _selection->elements())
        {
            if (!selected || selected->domType() == fla::DOMElement::DOMType::Element)
                continue;

            fla::Element* element = dynamic_cast<fla::Element*>(selected);
            if (!element)
                continue;

            const QRectF bounds = getElementBounds(element);
            if (!bounds.isValid())
                continue;

            painter.drawRect(bounds);

            // Corner grips, sized in screen pixels.
            const double grip = 3.0 * scale;
            painter.setBrush(QColor(0, 170, 255));
            const QPointF corners[4] = {
                bounds.topLeft(), bounds.topRight(),
                bounds.bottomRight(), bounds.bottomLeft()
            };
            for (const QPointF& corner : corners)
                painter.drawRect(QRectF(corner.x() - grip, corner.y() - grip, grip * 2, grip * 2));
            painter.setBrush(Qt::NoBrush);
        }

        painter.restore();
    }

    if (_activeTool)
    {
        painter.save();
        _activeTool->paintOverlay(*this, painter, scale);
        painter.restore();
    }
}


void PhoenixView::wheelEvent(QWheelEvent *event)
{
    double scaleFactor = 1.15;
    if (event->angleDelta().y() < 0)
        scaleFactor = 1.0 / scaleFactor;

    double newZoom = _zoom * scaleFactor;

    if (newZoom < _minZoom || newZoom > _maxZoom)
        return;

    const QPointF mousePos = event->position();

    // Keep whatever is under the cursor under the cursor: note the document
    // point first, zoom, then pan by however far it moved.
    const QPointF anchor = mapToDocument(mousePos);

    _zoom = newZoom;

    const QPointF delta = mapToWidget(anchor) - mousePos;
    _panX -= delta.x();
    _panY -= delta.y();

    // A wheel gesture has no release to end it, so each notch pushes the
    // settle back and quality returns once the wheel stops.
    beginInteraction();
    _settleTimer->start();

    update();
}

void PhoenixView::beginInteraction()
{
    if (!_interacting)
    {
        _interacting = true;
        update();
    }
}

void PhoenixView::endInteraction()
{
    if (_settleTimer)
        _settleTimer->stop();

    if (_interacting)
    {
        _interacting = false;
        update();
    }
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

void PhoenixView::invalidateBounds()
{
    _boundsCache.clear();
}

QTransform PhoenixView::documentToWidget() const
{
    double docWidth = 0.0;
    double docHeight = 0.0;
    if (_flaDocument && _flaDocument->document)
    {
        docWidth = _flaDocument->document->width;
        docHeight = _flaDocument->document->height;
    }

    // The stage sits centred in whatever space the widget has, so the centring
    // offset is part of the mapping and not just a painting detail.
    const double centerX = (width() - docWidth * _zoom) / 2.0;
    const double centerY = (height() - docHeight * _zoom) / 2.0;

    QTransform transform;
    transform.translate(_panX + centerX, _panY + centerY);
    transform.scale(_zoom, _zoom);
    return transform;
}

QPointF PhoenixView::mapToDocument(const QPointF& widgetPos) const
{
    bool invertible = false;
    const QTransform inverse = documentToWidget().inverted(&invertible);
    if (!invertible)
        return QPointF();
    return inverse.map(widgetPos);
}

QPointF PhoenixView::mapToWidget(const QPointF& documentPos) const
{
    return documentToWidget().map(documentPos);
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
            const fla::Symbol* symbol = instance->symbol;
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

        for (const fla::Path* path : edge->paths)
        {
            if (!path->visible)
                continue;

            // Check segment-specific stroke
            if (path->lineStyleIndex != -1)
            {
                const fla::StrokeStyle* segStroke = shape->getStrokeStyleByIndex(path->lineStyleIndex);
                if (segStroke)
                {
                    maxStrokeWeight = qMax(maxStrokeWeight, segStroke->weight);
                }
            }

            for (const fla::PathSegment* segment : path->segments)
            {
                // Add all points from this segment to bounds
                for (const fla::Point& pt : segment->points)
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
        const fla::Symbol* symbol = instance->symbol;
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

void PhoenixView::applyColorTransform(QImage& image, const fla::ColorTransform& colorTransform)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied &&
        image.format() != QImage::Format_ARGB32)
    {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    for (int y = 0; y < image.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            QRgb pixel = line[x];
            if (qAlpha(pixel) == 0)
                continue;

            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            int a = qAlpha(pixel);

            if (colorTransform.tintMultiplier != 0.0)
            {
                double t = colorTransform.tintMultiplier;
                double it = 1.0 - t;

                int tintR = colorTransform.tintColor[0];
                int tintG = colorTransform.tintColor[1];
                int tintB = colorTransform.tintColor[2];
                int tintA = colorTransform.tintColor[3];

                r = static_cast<int>(it * r + t * tintR);
                g = static_cast<int>(it * g + t * tintG);
                b = static_cast<int>(it * b + t * tintB);
                a = static_cast<int>(it * a + t * tintA);
            }

            if (colorTransform.brightness != 0.0)
            {
                double br = colorTransform.brightness;

                double rf = r / 255.0;
                double gf = g / 255.0;
                double bf = b / 255.0;

                double maxC = qMax(qMax(rf, gf), bf);
                double minC = qMin(qMin(rf, gf), bf);
                double delta = maxC - minC;

                double h = 0, s = 0, l = (maxC + minC) / 2.0;

                if (delta != 0.0)
                {
                    s = l < 0.5 ? delta / (maxC + minC) : delta / (2.0 - maxC - minC);

                    if (maxC == rf)
                        h = ((gf - bf) / delta) + (gf < bf ? 6.0 : 0.0);
                    else if (maxC == gf)
                        h = ((bf - rf) / delta) + 2.0;
                    else
                        h = ((rf - gf) / delta) + 4.0;

                    h /= 6.0;
                }

                l = l + br;
                l = qBound(0.0, l, 1.0);

                if (l == 0.0)
                {
                    r = g = b = 0;
                }
                else if (l == 1.0)
                {
                    r = g = b = 255;
                }
                else if (s == 0.0)
                {
                    r = g = b = static_cast<int>(l * 255.0);
                }
                else
                {
                    double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
                    double p = 2.0 * l - q;

                    auto hueToRgb = [](double p, double q, double t) -> double {
                        while (t < 0.0) t += 1.0;
                        while (t > 1.0) t -= 1.0;
                        if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
                        if (t < 1.0/2.0) return q;
                        if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
                        return p;
                    };

                    r = static_cast<int>(hueToRgb(p, q, h + 1.0/3.0) * 255.0);
                    g = static_cast<int>(hueToRgb(p, q, h) * 255.0);
                    b = static_cast<int>(hueToRgb(p, q, h - 1.0/3.0) * 255.0);
                }
            }

            r = static_cast<int>(r * colorTransform.redMultiplier) + colorTransform.redOffset;
            g = static_cast<int>(g * colorTransform.greenMultiplier) + colorTransform.greenOffset;
            b = static_cast<int>(b * colorTransform.blueMultiplier) + colorTransform.blueOffset;
            a = static_cast<int>(a * colorTransform.alphaMultiplier) + colorTransform.alphaOffset;

            r = qBound(0, r, 255);
            g = qBound(0, g, 255);
            b = qBound(0, b, 255);
            a = qBound(0, a, 255);

            line[x] = qRgba(r, g, b, a);
        }
    }
}

QRectF PhoenixView::calculateSymbolLocalBounds(const fla::Symbol* symbol)
{
    QRectF bounds;
    if (!symbol || symbol->timelines.empty())
        return QRectF(0, 0, 100, 100);

    const fla::Timeline* timeline = symbol->timelines[0];
    for (const fla::Layer* layer : timeline->layers)
    {
        if (!layer->visible || layer->frames.empty())
            continue;

        const fla::Frame* frame = layer->frames[0];
        for (const fla::Element* elem : frame->elements)
        {
            QRectF elemBounds = calculateElementLocalBounds(elem);
            if (bounds.isNull())
                bounds = elemBounds;
            else
                bounds = bounds.united(elemBounds);
        }
    }

    if (bounds.isNull())
        bounds = QRectF(0, 0, 100, 100);

    return bounds;
}

QRectF PhoenixView::calculateElementLocalBounds(const fla::Element* element)
{
    fla::Element::Type type = element->elementType();

    if (type == fla::Element::Type::Shape)
    {
        const fla::Shape* shape = static_cast<const fla::Shape*>(element);
        bool first = true;
        QRectF bounds;

        for (const fla::Edge* edge : shape->edges)
        {
            if (!edge->visible)
                continue;

            for (const fla::Path* path : edge->paths)
            {
                if (!path->visible)
                    continue;

                for (const fla::PathSegment* segment : path->segments)
                {
                    for (const fla::Point& pt : segment->points)
                    {
                        if (first)
                        {
                            first = false;
                            bounds = QRectF(pt.x, pt.y, 0, 0);
                        }
                        else
                        {
                            if (pt.x < bounds.left())
                                bounds.setLeft(pt.x);
                            if (pt.x > bounds.right())
                                bounds.setRight(pt.x);
                            if (pt.y < bounds.top())
                                bounds.setTop(pt.y);
                            if (pt.y > bounds.bottom())
                                bounds.setBottom(pt.y);
                        }
                    }
                }
            }
        }

        if (bounds.isNull())
            bounds = QRectF(0, 0, 1, 1);

        bounds = bounds.adjusted(-2, -2, 2, 2);
        return bounds;
    }
    else if (type == fla::Element::Type::Group)
    {
        const fla::Group* group = static_cast<const fla::Group*>(element);
        QRectF bounds;
        for (const fla::Element* member : group->members)
        {
            QRectF memberBounds = calculateElementLocalBounds(member);
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
        const fla::Symbol* symbol = instance->symbol;
        if (symbol)
        {
            return calculateSymbolLocalBounds(symbol);
        }
        return QRectF(0, 0, 100, 100);
    }
    else if (type == fla::Element::Type::BitmapInstance)
    {
        return QRectF(0, 0, 100, 100);
    }
    else if (type == fla::Element::Type::StaticText)
    {
        return QRectF(0, 0, 200, 50);
    }
    else if (type == fla::Element::Type::Rectangle)
    {
        const fla::RectanglePrimitive* rectangle = static_cast<const fla::RectanglePrimitive*>(element);
        return QRectF(rectangle->rect.topLeft.x, rectangle->rect.topLeft.y, rectangle->rect.width(), rectangle->rect.height());
    }
    else if (type == fla::Element::Type::Oval)
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(element);
        return QRectF(oval->rect.topLeft.x, oval->rect.topLeft.y, oval->rect.width(), oval->rect.height());
    }

    return QRectF(0, 0, 20, 20);
}

bool PhoenixView::isElementSelected(const fla::DOMElement* element) const
{
    if (!_selectedElement || !element)
        return false;

    if (_selectedElement == element)
        return true;

    if (_selectedElement->domType() == fla::DOMElement::DOMType::SymbolInstance)
    {
        const fla::SymbolInstance* selectedInstance = static_cast<const fla::SymbolInstance*>(_selectedElement);
        const fla::Symbol* selectedSymbol = selectedInstance->symbol;
        if (selectedSymbol)
        {
            return isElementInsideSymbol(element, selectedSymbol);
        }
    }

    const fla::DOMElement* current = element->parent;
    while (current)
    {
        if (current == _selectedElement)
            return true;
        current = current->parent;
    }

    if (element->domType() == fla::DOMElement::DOMType::Shape)
    {
        const fla::Shape* shape = static_cast<const fla::Shape*>(element);
        for (const fla::Edge* edge : shape->edges)
        {
            if (edge == _selectedElement)
                return true;
            for (const fla::Path* path : edge->paths)
            {
                if (path == _selectedElement)
                    return true;
            }
        }
    }

    return false;
}

bool PhoenixView::isElementInsideSymbol(const fla::DOMElement* element, const fla::Symbol* symbol) const
{
    if (!symbol)
        return false;

    for (const fla::Timeline* timeline : symbol->timelines)
    {
        for (const fla::Layer* layer : timeline->layers)
        {
            for (const fla::Frame* frame : layer->frames)
            {
                for (const fla::Element* elem : frame->elements)
                {
                    const fla::DOMElement* domElem = static_cast<const fla::DOMElement*>(elem);
                    if (domElem == element)
                        return true;

                    if (elem->elementType() == fla::Element::Type::SymbolInstance)
                    {
                        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(elem);
                        if (instance->symbol)
                        {
                            if (isElementInsideSymbol(element, instance->symbol))
                                return true;
                        }
                    }
                    else if (elem->elementType() == fla::Element::Type::Group)
                    {
                        const fla::Group* group = static_cast<const fla::Group*>(elem);
                        if (isElementInsideGroup(element, group))
                            return true;
                    }
                }
            }
        }
    }

    return false;
}

bool PhoenixView::isElementInsideGroup(const fla::DOMElement* element, const fla::Group* group) const
{
    for (const fla::Element* member : group->members)
    {
        const fla::DOMElement* domMember = static_cast<const fla::DOMElement*>(member);
        if (domMember == element)
            return true;

        if (member->elementType() == fla::Element::Type::SymbolInstance)
        {
            const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(member);
            if (instance->symbol)
            {
                if (isElementInsideSymbol(element, instance->symbol))
                    return true;
            }
        }
        else if (member->elementType() == fla::Element::Type::Group)
        {
            const fla::Group* nestedGroup = static_cast<const fla::Group*>(member);
            if (isElementInsideGroup(element, nestedGroup))
                return true;
        }
    }

    return false;
}
