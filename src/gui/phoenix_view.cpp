#include "phoenix_view.h"
#include "filters.h"
#include "library_panel.h"
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>

#include "draw_placement.h"

#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/region_split.h"
#include "../edit/selection.h"
#include "../edit/shape_merge.h"

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
#include "../data/bitmap_decoder.h"
#include "../edit/transform_blend.h"
#include "../edit/tween_ease.h"
#include "../edit/shape_tween.h"
#include "../edit/frame_commands.h"
#include "../geom/curve.h"
#include "../data/bitmap_fill.h"
#include "../data/static_text.h"
#include "text_layout.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/frame.h"
#include "../data/morph_shape.h"
#include "../data/morph_curves.h"
#include <algorithm>
#include <memory>
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

#include <cstring>
#include <QBitmap>
#include <QSvgGenerator>
#include <QtMath>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const int supersampleFactor = 2;

namespace {

const fla::Frame* owningFrame(const fla::DOMElement* element)
{
    for (const fla::DOMElement* p = element ? element->parent : nullptr; p; p = p->parent)
    {
        if (p->domType() == fla::DOMElement::DOMType::Frame)
            return static_cast<const fla::Frame*>(p);
    }
    return nullptr;
}

const fla::Layer* owningLayer(const fla::DOMElement* element)
{
    for (const fla::DOMElement* p = element ? element->parent : nullptr; p; p = p->parent)
    {
        if (p->domType() == fla::DOMElement::DOMType::Layer)
            return static_cast<const fla::Layer*>(p);
    }
    return nullptr;
}

/// The color a selected element is highlighted in. Animate's default is the
/// outline color of the layer the element sits on, so neighboring layers
/// read differently and the highlight is the same color as the layer's
/// outline view.
QColor highlightColorFor(const fla::Element* element)
{
    if (const fla::Layer* layer = owningLayer(element))
        return QColor(layer->color[0], layer->color[1], layer->color[2]);
    return QColor(0, 170, 255);
}

/// An oval primitive's outline: the whole ellipse, or the sector from its
/// start angle clockwise to its end angle, with a hole or a notch of the
/// inner radius (a percentage of the outer). Animate measures angles
/// clockwise from three o'clock; Qt's arcs run the other way.
QPainterPath ovalPrimitivePath(const fla::OvalPrimitive& oval)
{
    const QRectF outer(QPointF(oval.rect.topLeft.x, oval.rect.topLeft.y),
        QPointF(oval.rect.bottomRight.x, oval.rect.bottomRight.y));
    const double inner = std::max(0.0, std::min(99.0, oval.innerRadius)) / 100.0;
    const QPointF center = outer.center();
    const QRectF hole(center.x() - outer.width() * inner / 2.0, center.y() - outer.height() * inner / 2.0,
        outer.width() * inner, outer.height() * inner);

    double sweep = std::fmod(oval.endAngle - oval.startAngle, 360.0);
    if (sweep < 0.0)
        sweep += 360.0;
    const bool whole = sweep == 0.0;

    QPainterPath path;
    if (whole)
    {
        path.addEllipse(outer);
        if (inner > 0.0)
            path.addEllipse(hole);
        return path;
    }

    path.arcMoveTo(outer, -oval.startAngle);
    path.arcTo(outer, -oval.startAngle, -sweep);
    if (inner > 0.0)
        path.arcTo(hole, -oval.endAngle, sweep);
    else
        path.lineTo(center);
    path.closeSubpath();
    return path;
}

/// A rectangle primitive's outline with its corner radii. Each radius is
/// clamped to half the shorter side, as Animate clamps it; a negative radius
/// scoops the corner inwards.
QPainterPath roundedRectanglePath(const fla::RectanglePrimitive& rectangle)
{
    const double x0 = rectangle.rect.topLeft.x;
    const double y0 = rectangle.rect.topLeft.y;
    const double x1 = rectangle.rect.bottomRight.x;
    const double y1 = rectangle.rect.bottomRight.y;
    const double limit = std::min(std::fabs(x1 - x0), std::fabs(y1 - y0)) / 2.0;

    const auto clamp = [&](double r) { return std::max(-limit, std::min(limit, r)); };
    const double tl = clamp(rectangle.topLeftRadius);
    const double tr = clamp(rectangle.topRightRadius);
    const double br = clamp(rectangle.bottomRightRadius);
    const double bl = clamp(rectangle.bottomLeftRadius);

    QPainterPath path;
    if (tl == 0.0 && tr == 0.0 && br == 0.0 && bl == 0.0)
    {
        path.addRect(QRectF(QPointF(x0, y0), QPointF(x1, y1)));
        return path;
    }

    // A positive radius arcs round a center inside the corner; a negative one
    // arcs round a center outside it, which is the inward scoop.
    constexpr double kappa = 0.5522847498;
    const auto corner = [&](double cx, double cy, double r, double sx, double sy,
                            const QPointF& from, const QPointF& to) {
        // from and to sit r away from the corner (cx, cy) along the two sides;
        // (sx, sy) is the direction into the rectangle from the corner.
        (void)cx; (void)cy; (void)sx; (void)sy;
        const double a = std::fabs(r);
        if (r > 0.0)
        {
            // Bulge outwards: controls pull towards the corner itself.
            const QPointF c1 = from + (QPointF(cx, cy) - from) * kappa;
            const QPointF c2 = to + (QPointF(cx, cy) - to) * kappa;
            path.cubicTo(c1, c2, to);
        }
        else
        {
            // Scoop inwards: the arc's center is the corner, so the controls
            // run along the tangents into the rectangle.
            const QPointF inner(cx + sx * a, cy + sy * a);
            const QPointF c1 = from + (inner - from) * kappa;
            const QPointF c2 = to + (inner - to) * kappa;
            path.cubicTo(c1, c2, to);
        }
    };

    path.moveTo(x0 + std::fabs(tl), y0);
    path.lineTo(x1 - std::fabs(tr), y0);
    corner(x1, y0, tr, -1.0, 1.0, QPointF(x1 - std::fabs(tr), y0), QPointF(x1, y0 + std::fabs(tr)));
    path.lineTo(x1, y1 - std::fabs(br));
    corner(x1, y1, br, -1.0, -1.0, QPointF(x1, y1 - std::fabs(br)), QPointF(x1 - std::fabs(br), y1));
    path.lineTo(x0 + std::fabs(bl), y1);
    corner(x0, y1, bl, 1.0, -1.0, QPointF(x0 + std::fabs(bl), y1), QPointF(x0, y1 - std::fabs(bl)));
    path.lineTo(x0, y0 + std::fabs(tl));
    corner(x0, y0, tl, 1.0, 1.0, QPointF(x0, y0 + std::fabs(tl)), QPointF(x0 + std::fabs(tl), y0));
    path.closeSubpath();
    return path;
}

/// Animate's magnification presets, which Zoom In and Zoom Out step through.
constexpr double kZoomPresets[] = {0.08, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};

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

    // Keys reach the stage once it has been clicked, so Delete, Escape and the
    // nudge keys go to the tools rather than nowhere.
    setFocusPolicy(Qt::StrongFocus);

    // Enable mouse tracking for smooth panning
    setMouseTracking(true);
    // Library items are dragged onto the stage to place them.
    setAcceptDrops(true);

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
    (void)frame;

    // What was selected lives on the frame just left, so the selection
    // goes with it, as in Animate; letting go also drops a drawing that
    // was still floating into the artwork of its own frame.
    if (_selection && !_selection->isEmpty() && !(_commandStack && _commandStack->isExecuting()))
        _selection->clear();
    update();
}

void PhoenixView::onElementSelected(const fla::DOMElement* element)
{
    _selectedElement = element;
    update();
}

void PhoenixView::setDocument(const fla::FLADocument* document)
{
    _bitmapCache.clear(); // Clear bitmap cache when loading new document
    clearCaches();
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
        bufferPainter.setTransform(rootToWidget(), true);
        drawStageContents(bufferPainter, document);
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
        // While something is being dragged, the rest of the stage is drawn
        // once into an image and reused for every frame of the drag, and
        // only what moves is drawn each time.
        const std::vector<const fla::Element*> moving = _interacting
            ? movingElements() : std::vector<const fla::Element*>();

        painter.save();
        bool split = false;
        if (!moving.empty())
        {
            // Where the moving elements fall in the drawing order decides
            // what goes below and above them.
            _moving = &moving;
            _drawPhase = DrawPhase::Scan;
            _drawOrdinal = 0;
            _firstMoving = _lastMoving = -1;
            painter.save();
            painter.setTransform(rootToWidget(), true);
            drawStageContents(painter, document);
            painter.restore();
            split = _firstMoving >= 0;
            _drawPhase = DrawPhase::All;
        }

        if (split)
        {
            StaticKey key;
            key.zoom = _zoom;
            key.panX = _panX;
            key.panY = _panY;
            key.size = size();
            key.frame = _player ? _player->currentFrame() : -1;
            key.revision = _revision;
            key.scopeDepth = _scope.size();
            key.grid = _showGrid;
            key.moving = moving;

            if (!_staticValid || !(_staticKey == key))
            {
                const qreal ratio = devicePixelRatioF();
                const auto render = [&](QImage& image, DrawPhase phase, const QColor& fill) {
                    image = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
                    image.setDevicePixelRatio(ratio);
                    image.fill(fill);
                    QPainter imagePainter(&image);
                    imagePainter.setRenderHint(QPainter::Antialiasing, true);
                    imagePainter.setRenderHint(QPainter::TextAntialiasing, true);
                    imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    imagePainter.setTransform(rootToWidget(), true);
                    _drawPhase = phase;
                    _drawOrdinal = 0;
                    drawStageContents(imagePainter, document);
                    _drawPhase = DrawPhase::All;
                };
                render(_belowImage, DrawPhase::Below, backgroundColor);
                render(_aboveImage, DrawPhase::Above, Qt::transparent);
                _staticKey = key;
                _staticValid = true;
            }

            painter.drawImage(0, 0, _belowImage);
            painter.setTransform(rootToWidget(), true);
            _drawPhase = DrawPhase::Live;
            _drawOrdinal = 0;
            drawStageContents(painter, document);
            _drawPhase = DrawPhase::All;
            painter.resetTransform();
            painter.drawImage(0, 0, _aboveImage);
        }
        else
        {
            painter.setTransform(rootToWidget(), true);
            drawStageContents(painter, document);
        }
        _moving = nullptr;
        painter.setTransform(documentToWidget(), false);
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

void PhoenixView::drawStageContents(QPainter& painter, const fla::Document* document)
{
    const QTransform rootFull = painter.transform();
    viewTransform = rootFull;

    const double docWidth = document->width;
    const double docHeight = document->height;

    // The stage background sits under everything and the border over it, so
    // when the stage is split around moving elements they belong to the
    // bottom and top layers.
    if (_drawPhase == DrawPhase::All || _drawPhase == DrawPhase::Below)
    {
        painter.fillRect(QRectF(0, 0, docWidth, docHeight),
            QColor(document->backgroundColor[0], document->backgroundColor[1],
                document->backgroundColor[2], document->backgroundColor[3]));

        if (_showGrid)
            drawGrid(painter, document);
    }

    // Onion skin: the frames either side of the playhead, furthest first so
    // the nearest sit on top, under the frame itself. They belong with the
    // background when the stage is split around moving elements.
    if (_onionSkin && _player && (_drawPhase == DrawPhase::All || _drawPhase == DrawPhase::Below))
    {
        const int current = _player->currentFrame();
        const DrawPhase phase = _drawPhase;
        _drawPhase = DrawPhase::All;
        for (int k = _onionBefore; k >= 1; --k)
        {
            if (current - k >= 0)
                drawOnionFrame(painter, document, current - k, QColor(0, 102, 255), 0.45 * (1.0 - (k - 1.0) / (_onionBefore + 1.0)));
        }
        for (int k = _onionAfter; k >= 1; --k)
            drawOnionFrame(painter, document, current + k, QColor(0, 170, 60), 0.45 * (1.0 - (k - 1.0) / (_onionAfter + 1.0)));
        _drawPhase = phase;
    }

    if (_scope.empty())
    {
        drawDocument(painter, document);
    }
    else
    {
        // Edit in place: everything outside the symbol being edited fades
        // back, and the symbol draws over it at full strength in its own
        // space, which is where the tools now work. A symbol edited from the
        // library is shown on its own.
        if (_scope.front().instance)
        {
            painter.setOpacity(0.3);
            drawDocument(painter, document);
            painter.setOpacity(1.0);
        }

        painter.setTransform(scopeToDocument() * rootFull, false);
        if (_scope.back().timeline)
            drawTimeline(painter, _scope.back().timeline, fla::LoopType::PlayOnce, 0);
        painter.setTransform(rootFull, false);
    }

    if (_drawPhase == DrawPhase::All || _drawPhase == DrawPhase::Above)
    {
        painter.setPen(QPen(QColor(0, 0, 0, 255), 1.0));
        painter.drawRect(QRectF(0, 0, docWidth, docHeight));
    }
}

bool PhoenixView::needsBackdrop(const fla::Element* element, int depth)
{
    if (!element || depth > 16)
        return false;
    const auto cached = _backdropCache.constFind(element);
    if (cached != _backdropCache.constEnd())
        return cached.value();

    bool needs = false;
    if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element))
    {
        bool supported = true;
        needs = Filters::blendMode(instance->blendMode, supported) != QPainter::CompositionMode_SourceOver;
        if (!needs && instance->symbol)
        {
            for (const fla::Timeline* timeline : instance->symbol->timelines)
            {
                for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
                {
                    for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                    {
                        for (const fla::Element* inner : frame ? frame->elements : std::vector<fla::Element*>())
                        {
                            if (needsBackdrop(inner, depth + 1))
                            {
                                needs = true;
                                break;
                            }
                        }
                        if (needs)
                            break;
                    }
                    if (needs)
                        break;
                }
                if (needs)
                    break;
            }
        }
    }
    else if (const auto* group = dynamic_cast<const fla::Group*>(element))
    {
        for (const fla::Element* member : group->members)
        {
            if (needsBackdrop(member, depth + 1))
            {
                needs = true;
                break;
            }
        }
    }
    _backdropCache.insert(element, needs);
    return needs;
}

int PhoenixView::shownFrame() const
{
    if (_frameOverride >= 0)
        return _frameOverride;
    return _player ? _player->currentFrame() : 0;
}

void PhoenixView::setOnionSkin(bool on, int before, int after)
{
    _onionSkin = on;
    _onionBefore = std::max(0, before);
    _onionAfter = std::max(0, after);
    clearCaches();
    update();
}

void PhoenixView::drawOnionFrame(QPainter& painter, const fla::Document* document, int frame,
    const QColor& tint, double opacity)
{
    // Drawn on its own into a device-sized image, tinted there and laid on
    // the stage faded, so overlapping shapes in one frame do not darken
    // each other.
    QPaintDevice* device = painter.device();
    if (!device || device->width() <= 0 || device->height() <= 0)
        return;
    QImage image(device->width(), device->height(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter onion(&image);
        onion.setRenderHint(QPainter::Antialiasing, true);
        onion.setRenderHint(QPainter::SmoothPixmapTransform, true);
        onion.setTransform(painter.transform());
        _frameOverride = frame;
        if (_scope.empty())
        {
            drawDocument(onion, document);
        }
        else if (_scope.back().timeline)
        {
            onion.setTransform(scopeToDocument() * painter.transform(), false);
            drawTimeline(onion, _scope.back().timeline, fla::LoopType::PlayOnce, 0);
        }
        _frameOverride = -1;
        // Half the frame's own colors, half the tint.
        onion.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        QColor wash = tint;
        wash.setAlphaF(0.55);
        onion.fillRect(image.rect(), wash);
    }
    painter.save();
    painter.resetTransform();
    painter.setOpacity(painter.opacity() * opacity);
    painter.drawImage(0, 0, image);
    painter.restore();
}

std::vector<const fla::Element*> PhoenixView::movingElements() const
{
    std::vector<const fla::Element*> moving;
    const auto add = [&](const fla::DOMElement* node) {
        // Up to the element that sits directly in a frame: a group moves as
        // a whole when something inside it is selected.
        const fla::DOMElement* top = node;
        while (top && top->parent && top->parent->domType() != fla::DOMElement::DOMType::Frame)
            top = top->parent;
        const fla::Element* element = top ? dynamic_cast<const fla::Element*>(top) : nullptr;
        if (!element || !top->parent)
            return;
        if (std::find(moving.begin(), moving.end(), element) == moving.end())
            moving.push_back(element);
    };
    if (_selection)
    {
        for (const fla::DOMElement* selected : _selection->elements())
            add(selected);
    }
    for (const PendingDrop& pending : _pending)
        add(pending.shape);
    return moving;
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
        /// Where on the device the mask's pixels sit; empty when the mask
        /// covers nothing, in which case the masked layers show nothing.
        QRect crop;
    };
    QMap<int, MaskData> maskCache;
    QTransform painterTransform = painter.transform();
    const QRect deviceRect(0, 0, painter.device()->width(), painter.device()->height());

    // First pass: render mask layers to pixmaps
    for (int i = 0; i < timeline->layers.size(); ++i)
    {
        const fla::Layer* layer = timeline->layers[i];
        if (layer->layerType != fla::Layer::Type::Mask || !layer->isVisible())
        {
            continue;
        }

        // The mask is rendered at the resolution of whatever the painter is
        // drawing into -- the supersampled buffer or the widget itself -- so it
        // lines up with the content pixel for pixel whatever the quality mode,
        // and only over the part of the device the mask's art reaches, since
        // nothing outside it can show.
        QRectF reach;
        const LayerFrame shown = resolveLayerFrame(layer, loopType, firstFrame);
        if (shown.frame)
        {
            for (const fla::Element* element : shown.frame->elements)
            {
                if (!element || !element->visible)
                    continue;
                const QRectF bounds = getElementBounds(element);
                if (bounds.isValid())
                    reach = reach.isNull() ? bounds : reach.united(bounds);
            }
        }
        const QRect crop = reach.isNull() ? QRect()
            : painterTransform.mapRect(reach).adjusted(-2.0, -2.0, 2.0, 2.0).toAlignedRect().intersected(deviceRect);
        if (crop.isEmpty())
        {
            maskCache[i] = { QPixmap(), QRect() };
            continue;
        }

        QPixmap maskPixmap(crop.size());
        maskPixmap.fill(Qt::transparent);

        QPainter maskPainter(&maskPixmap);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setRenderHint(QPainter::TextAntialiasing, true);
        maskPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        maskPainter.setTransform(painterTransform * QTransform::fromTranslate(-crop.x(), -crop.y()));

        _maskPass = true;
        drawLayer(maskPainter, layer, loopType, firstFrame, nullptr);
        _maskPass = false;
        maskPainter.end();

        // A mask is coverage, not alpha: a half-transparent mask fill shows
        // what is under it in full, and Flash's mask edges are not
        // anti-aliased, so every covered pixel is wholly in or out.
        QImage coverage = maskPixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < coverage.height(); ++y)
        {
            QRgb* line = reinterpret_cast<QRgb*>(coverage.scanLine(y));
            for (int x = 0; x < coverage.width(); ++x)
                line[x] = qAlpha(line[x]) >= 128 ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 0);
        }
        maskPixmap = QPixmap::fromImage(coverage);

        maskCache[i] = { maskPixmap, crop };
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
                const QRect& crop = maskIt.value().crop;
                if (crop.isEmpty())
                    continue;

                QPixmap contentPixmap(maskPixmap.size());
                contentPixmap.fill(Qt::transparent);

                QPainter contentPainter(&contentPixmap);
                contentPainter.setRenderHint(QPainter::Antialiasing, true);
                contentPainter.setRenderHint(QPainter::TextAntialiasing, true);
                contentPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                contentPainter.setTransform(painterTransform * QTransform::fromTranslate(-crop.x(), -crop.y()));

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
                painter.drawPixmap(crop.topLeft(), maskedResult);
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
        int relativeFrame = shownFrame() % layerDuration;
        currentFrameIndex = layer->firstFrame + firstFrame + relativeFrame;
    }
    else if (loopType == fla::LoopType::PingPong)
    {
        int pingPongDuration = layerDuration * 2;
        int t = shownFrame() % pingPongDuration;
        if (t >= layerDuration)
        {
            t = pingPongDuration - t;
            pingPongReverse = true;
        }
        currentFrameIndex = layer->firstFrame + firstFrame + t;
    }
    else // PlayOnce
    {
        currentFrameIndex = layer->firstFrame + firstFrame + shownFrame();
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
            tweenProgress = fla::easeProgress(tweenProgress, currentFrame->acceleration,
                currentFrame->hasCustomEase ? currentFrame->customEase : std::vector<fla::Point>());

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
    // Onion skin leaves locked layers out, as Animate does, so a locked
    // background is not drawn again faded under every frame.
    if (_frameOverride >= 0 && layer->locked && _scope.empty())
        return;

    QColor color;
    color.setRgb(layer->color[0], layer->color[1], layer->color[2], layer->color[3]);
    painter.setPen(QPen(color, 1.0));

    // Outline view draws the layer's art as lines in the layer color. A mask
    // is coverage whatever the view, so it is never outlined.
    const QColor previousOutline = _outlineColor;
    if (layer->outline && !_maskPass)
        _outlineColor = color;

    const LayerFrame state = resolveLayerFrame(layer, loopType, firstFrame);

    if (state.frame && state.frame->visible)
    {
        drawFrame(painter, state.frame, state.tweenFrame, state.tweenProgress);
    }

    _outlineColor = previousOutline;
}

void PhoenixView::drawFrame(QPainter& painter, const fla::Frame* frame, const fla::Frame* nextTweenFrame, double tweenProgress)
{
    // The keyframe says which way its tween turns.
    const fla::TweenRotation savedRotation = _tweenRotation;
    const int savedTurns = _tweenTurns;
    _tweenRotation = fla::TweenRotation::Auto;
    _tweenTurns = 0;
    if (frame->motionTweenRotate == "clockwise")
        _tweenRotation = fla::TweenRotation::Clockwise;
    else if (frame->motionTweenRotate == "counter-clockwise")
        _tweenRotation = fla::TweenRotation::CounterClockwise;
    if (_tweenRotation != fla::TweenRotation::Auto)
        _tweenTurns = std::max(0, frame->motionTweenRotateTimes);
    struct Restore
    {
        PhoenixView& view;
        fla::TweenRotation rotation;
        int turns;
        ~Restore()
        {
            view._tweenRotation = rotation;
            view._tweenTurns = turns;
        }
    } restore{*this, savedRotation, savedTurns};

    // Culling: an element whose bounds, taken to the device through the
    // painter, miss what is being painted is not drawn, and neither is one
    // too small to show. Bounds are cached, so this is cheap, and the test
    // is skipped while a tween runs since the bounds move with it.
    const QRectF deviceRect(0, 0, painter.device()->width(), painter.device()->height());
    const bool culling = !nextTweenFrame && painter.device()->width() > 0;

    struct Depth
    {
        int& counter;
        ~Depth() { --counter; }
    } depth{_frameDepth};
    ++_frameDepth;
    const bool filtered = _frameDepth == 1 && !_maskPass && _drawPhase != DrawPhase::All;

    int nextElementIndex = 0;
    for (const fla::Element* element : frame->elements)
    {
        if (element->visible)
        {
            const fla::Element* nextElement = nextTweenFrame && nextTweenFrame->elements.size() > nextElementIndex ?
                nextTweenFrame->elements[nextElementIndex] : nullptr;

            bool skip = false;
            if (filtered)
            {
                const int ordinal = _drawOrdinal++;
                switch (_drawPhase)
                {
                case DrawPhase::Scan:
                    if (_moving && std::find(_moving->begin(), _moving->end(), element) != _moving->end())
                    {
                        if (_firstMoving < 0)
                            _firstMoving = ordinal;
                        _lastMoving = ordinal;
                    }
                    else if (_firstMoving >= 0 && needsBackdrop(element))
                    {
                        // Blends with what moves beneath it, so it is drawn
                        // live along with the moving elements.
                        _lastMoving = ordinal;
                    }
                    skip = true;
                    break;
                case DrawPhase::Below:
                    skip = ordinal >= _firstMoving;
                    break;
                case DrawPhase::Live:
                    skip = ordinal < _firstMoving || ordinal > _lastMoving;
                    break;
                case DrawPhase::Above:
                    skip = ordinal <= _lastMoving;
                    break;
                case DrawPhase::All:
                    break;
                }
            }
            if (!skip && culling)
            {
                const QRectF bounds = getElementBounds(element);
                if (bounds.isValid())
                {
                    const QRectF onDevice = painter.transform().mapRect(bounds).adjusted(-2.0, -2.0, 2.0, 2.0);
                    skip = !onDevice.intersects(deviceRect) ||
                        (onDevice.width() < 0.25 && onDevice.height() < 0.25);
                }
            }
            if (!skip)
                drawElement(painter, element, nextElement, tweenProgress);
        }

        nextElementIndex++;
    }
}

fla::Transform PhoenixView::interpolateTransform(const fla::Transform& a, const fla::Transform& b, double t)
{
    return fla::blendTransforms(a, b, t, _tweenRotation, _tweenTurns);
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
    // In outline view a text or bitmap shows as the box it occupies.
    if (_outlineColor.isValid() && (element->elementType() == fla::Element::Type::StaticText ||
                                    element->elementType() == fla::Element::Type::BitmapInstance))
    {
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(_outlineColor, 0.0));
        painter.drawRect(getElementBounds(element));
        painter.restore();
        return;
    }

    fla::Transform transform = element->transform;
    if (tweenElement)
    {
        transform = interpolateTransform(element->transform, tweenElement->transform, tweenProgress);
    }

    QTransform qTransform(transform.m11, transform.m12,
                       transform.m21, transform.m22,
                       transform.tx, transform.ty);

    painter.save();

    // The element's transform relative to the stage, for strokes whose width
    // must not scale with it; put back when this element is done.
    struct ElementScope
    {
        QTransform& slot;
        QTransform saved;
        ~ElementScope() { slot = saved; }
    };
    ElementScope elementScope{_elementTransform, _elementTransform};

    // Why does a group have a transform, if applying the groups transform
    // is incorrect because the children are already transformed by the group transform?
    if (element->elementType() != fla::Element::Type::Group)
    {
        painter.setTransform(qTransform, true);
        _elementTransform = qTransform * _elementTransform;
    }

    fla::Element::Type type = element->elementType();

    if (type == fla::Element::Type::Shape)
    {
        const fla::Shape* shape = static_cast<const fla::Shape*>(element);
        const fla::Shape* tweenShape = (tweenElement && tweenElement->elementType() == fla::Element::Type::Shape) ?
            static_cast<const fla::Shape*>(tweenElement) : nullptr;
        drawShape(painter, shape, tweenShape, tweenProgress, qTransform);
    }
    else if (type == fla::Element::Type::SymbolInstance)
    {
        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
        const fla::Symbol* symbol = instance->symbol;
        if (symbol && symbol->visible)
        {
            int frameOffset = (instance->symbolType == fla::SymbolType::Button) ? 0 : instance->firstFrame;
            fla::LoopType loopType = (instance->symbolType == fla::SymbolType::Button) ? fla::LoopType::SingleFrame : instance->loopType;

            // Part way through a classic tween, the color is between the
            // two keyframes' colors.
            fla::ColorTransform color = instance->colorTransform;
            if (tweenElement && tweenElement->elementType() == fla::Element::Type::SymbolInstance)
            {
                color = fla::blendColorTransforms(color,
                    static_cast<const fla::SymbolInstance*>(tweenElement)->colorTransform, tweenProgress);
            }
            const bool alphaOnly = color.tintMultiplier == 0.0 && color.brightness == 0.0 &&
                color.redMultiplier == 1.0 && color.greenMultiplier == 1.0 &&
                color.blueMultiplier == 1.0 && color.redOffset == 0 &&
                color.greenOffset == 0 && color.blueOffset == 0 && color.alphaOffset == 0;

            std::vector<fla::Filter> filters;
            for (const fla::Filter& filter : instance->filters)
            {
                if (filter.enabled)
                    filters.push_back(filter);
            }

            // Anything but a plain alpha change needs the instance rendered
            // on its own first: a color transform of its pixels, a filter
            // over them, or a blend mode against what is under it.
            // Outline view shows the instance's lines plainly: no color
            // effect, filter or blend, which would wash the lines out.
            const bool isolate = !_outlineColor.isValid() &&
                ((!color.isIdentity() && !alphaOnly) || !filters.empty() ||
                    Filters::needsIsolation(instance->blendMode));

            if (!isolate)
            {
                // Flash scales every shape's own alpha, so overlapping
                // shapes inside the symbol show through each other: the
                // painter's opacity does exactly that.
                painter.save();
                if (!color.isIdentity() && !_maskPass)
                    painter.setOpacity(painter.opacity() * std::max(0.0, std::min(1.0, color.alphaMultiplier)));

                // 9-slice scaling: a movie clip with scale guides, scaled
                // but not turned, draws as nine cells, the corners at their
                // own size, the edges stretched one way and the middle
                // both. Each cell is the symbol drawn again under a clip
                // with the cell's own stretch.
                const bool sliced = symbol->scale9 && instance->symbolType == fla::SymbolType::MovieClip &&
                    std::fabs(qTransform.m12()) < 1.0e-9 && std::fabs(qTransform.m21()) < 1.0e-9 &&
                    (std::fabs(qTransform.m11() - 1.0) > 1.0e-6 || std::fabs(qTransform.m22() - 1.0) > 1.0e-6) &&
                    std::fabs(qTransform.m11()) > 1.0e-6 && std::fabs(qTransform.m22()) > 1.0e-6;
                if (sliced)
                {
                    const QRectF local = calculateSymbolLocalBounds(symbol);
                    const double sx = std::fabs(qTransform.m11());
                    const double sy = std::fabs(qTransform.m22());
                    // The cut lines in symbol space, and where they land
                    // once the corners are kept at their own size: a
                    // corner that is w wide in the symbol must be w / sx
                    // wide here so that the instance's scale brings it
                    // back to w.
                    const double srcX[4] = {local.left(), symbol->scaleGridLeft, symbol->scaleGridRight, local.right()};
                    const double srcY[4] = {local.top(), symbol->scaleGridTop, symbol->scaleGridBottom, local.bottom()};
                    const double dstX[4] = {local.left(), local.left() + (srcX[1] - srcX[0]) / sx,
                        local.right() - (srcX[3] - srcX[2]) / sx, local.right()};
                    const double dstY[4] = {local.top(), local.top() + (srcY[1] - srcY[0]) / sy,
                        local.bottom() - (srcY[3] - srcY[2]) / sy, local.bottom()};
                    for (int row = 0; row < 3; ++row)
                    {
                        for (int column = 0; column < 3; ++column)
                        {
                            const QRectF source(QPointF(srcX[column], srcY[row]), QPointF(srcX[column + 1], srcY[row + 1]));
                            const QRectF target(QPointF(dstX[column], dstY[row]), QPointF(dstX[column + 1], dstY[row + 1]));
                            if (source.width() <= 1.0e-6 || source.height() <= 1.0e-6 ||
                                target.width() <= 1.0e-6 || target.height() <= 1.0e-6)
                                continue;
                            const double cellSx = target.width() / source.width();
                            const double cellSy = target.height() / source.height();
                            const QTransform cell = QTransform::fromTranslate(-source.x(), -source.y()) *
                                QTransform::fromScale(cellSx, cellSy) *
                                QTransform::fromTranslate(target.x(), target.y());
                            painter.save();
                            painter.setClipRect(target, Qt::IntersectClip);
                            painter.setTransform(cell, true);
                            for (const fla::Timeline* timeline : symbol->timelines)
                            {
                                if (timeline->visible)
                                {
                                    painter.save();
                                    drawTimeline(painter, timeline, loopType, frameOffset);
                                    painter.restore();
                                }
                            }
                            painter.restore();
                        }
                    }
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
                painter.restore();
            }
            else
            {
                // Everything the symbol draws, over all its frames, on the
                // device, so the offscreen is sized to the instance as seen
                // (full parent transform and zoom, nothing off the stage
                // lost) rather than to the stage at 1x.
                QRectF local;
                for (const fla::Timeline* timeline : symbol->timelines)
                {
                    for (const fla::Layer* layer : timeline->layers)
                    {
                        for (const fla::Frame* frame : layer->frames)
                        {
                            for (const fla::Element* member : frame->elements)
                            {
                                const QRectF bounds = calculateElementLocalBounds(member);
                                local = local.isNull() ? bounds : local.united(bounds);
                            }
                        }
                    }
                }
                if (local.isNull())
                    local = QRectF(0, 0, 100, 100);

                // Filters are sized in the parent's pixels, so they scale
                // with the parent and the zoom but not with the instance's
                // own matrix.
                const QTransform full = painter.transform();
                const double fullScale = std::sqrt(std::fabs(full.determinant()));
                const double ownScale = std::sqrt(std::fabs(qTransform.determinant()));
                const double filterScale = ownScale > 1.0e-9 ? fullScale / ownScale : fullScale;
                const double reach = Filters::margin(filters, filterScale);

                const QRectF wanted = full.mapRect(local).adjusted(-2.0 - reach, -2.0 - reach, 2.0 + reach, 2.0 + reach);
                const QRectF deviceArea(0, 0, painter.device()->width(), painter.device()->height());
                const QRect crop = wanted.intersected(deviceArea).toAlignedRect();
                const bool clipped = !deviceArea.contains(wanted);

                // The rendering is kept between paints: the same instance
                // placed the same way, apart from a shift, on the same
                // document and frame draws from the image it had, moved.
                const int shownFrame = this->shownFrame();
                // Onion frames are drawn once and passed over, so they are
                // neither looked up nor kept.
                const bool cacheable = !_maskPass && _frameOverride < 0;
                const auto cached = cacheable ? _filterCache.find(instance) : _filterCache.end();
                const auto sameLinear = [](const QTransform& a, const QTransform& b) {
                    return std::fabs(a.m11() - b.m11()) < 1.0e-9 && std::fabs(a.m12() - b.m12()) < 1.0e-9 &&
                        std::fabs(a.m21() - b.m21()) < 1.0e-9 && std::fabs(a.m22() - b.m22()) < 1.0e-9;
                };
                if (cached != _filterCache.end() && !cached->clipped && cached->frame == shownFrame &&
                    cached->revision == _revision && sameLinear(cached->full, full))
                {
                    const QPoint shift(static_cast<int>(std::lround(full.dx() - cached->full.dx())),
                                       static_cast<int>(std::lround(full.dy() - cached->full.dy())));
                    painter.save();
                    painter.resetTransform();
                    Filters::composite(painter, cached->origin + shift, cached->image, instance->blendMode);
                    painter.restore();
                }
                else if (!crop.isEmpty())
                {
                    QImage symbolImage(crop.size(), QImage::Format_ARGB32_Premultiplied);
                    symbolImage.fill(Qt::transparent);

                    QPainter symbolPainter(&symbolImage);
                    symbolPainter.setRenderHint(QPainter::Antialiasing, true);
                    symbolPainter.setRenderHint(QPainter::TextAntialiasing, true);
                    symbolPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    symbolPainter.setTransform(full * QTransform::fromTranslate(-crop.x(), -crop.y()), false);
                    if (!color.isIdentity() && alphaOnly && !_maskPass)
                        symbolPainter.setOpacity(std::max(0.0, std::min(1.0, color.alphaMultiplier)));

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

                    if (!color.isIdentity() && !alphaOnly)
                        applyColorTransform(symbolImage, color);
                    if (!filters.empty())
                        symbolImage = Filters::apply(symbolImage, filters, filterScale);

                    painter.save();
                    painter.resetTransform();
                    Filters::composite(painter, crop.topLeft(), symbolImage, instance->blendMode);
                    painter.restore();

                    // Kept for the next paint unless it is huge.
                    if (cacheable && symbolImage.width() * symbolImage.height() <= 4 * 1024 * 1024)
                    {
                        FilteredImage entry;
                        entry.full = full;
                        entry.frame = shownFrame;
                        entry.revision = _revision;
                        entry.clipped = clipped;
                        entry.image = symbolImage;
                        entry.origin = crop.topLeft();
                        _filterCache.insert(instance, entry);
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
        painter.drawPath(roundedRectanglePath(*rectangle));
    }
    else if (type == fla::Element::Type::Oval)
    {
        const fla::OvalPrimitive* oval = static_cast<const fla::OvalPrimitive*>(element);
        QPen pen = getPen(oval->strokeStyle);
        QBrush brush = getFillBrush(oval->fillStyle, oval->localBounds);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawPath(ovalPrimitivePath(*oval));
    }
    else if (type == fla::Element::Type::StaticText)
    {
        if (_disableStaticText)
        {
            painter.restore();
            return;
        }

        TextLayout::draw(painter, *static_cast<const fla::StaticText*>(element));
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

        if (bitmap)
        {
            const QPixmap pixmap = libraryBitmap(QString::fromStdString(bitmap->name));
            if (!pixmap.isNull())
            {
                painter.setRenderHint(QPainter::SmoothPixmapTransform, bitmap->allowSmoothing);
                painter.drawPixmap(0, 0, pixmap);
            }
        }
    }

    painter.restore();
}

QPen PhoenixView::getPen(const fla::StrokeStyle* strokeStyle, bool scaleForElement)
{
    if (!strokeStyle || !strokeStyle->fill)
    {
        return QPen(Qt::NoPen);
    }

    // A hairline is one device pixel wide whatever the zoom or the transform,
    // which is what a cosmetic pen of width zero draws.
    const bool hairline = strokeStyle->isHairline();
    double weight = hairline ? 0.0 : std::max(strokeStyle->weight, 0.05);
    if (!hairline && scaleForElement)
        weight = scaledStrokeWeight(weight, strokeStyle->scaleMode);
    QPen pen(QColor(0, 0, 0, 255), weight);
    pen.setCosmetic(hairline);

    // Flash's defaults are round caps and round joins; the file only records
    // them when they differ. Qt's own defaults are square and bevel, which is
    // what every pencil line and rectangle corner used to get.
    if (strokeStyle->caps == "none")
        pen.setCapStyle(Qt::FlatCap);
    else if (strokeStyle->caps == "square")
        pen.setCapStyle(Qt::SquareCap);
    else
        pen.setCapStyle(Qt::RoundCap);

    if (strokeStyle->joints == "miter")
        pen.setJoinStyle(Qt::MiterJoin);
    else if (strokeStyle->joints == "bevel")
        pen.setJoinStyle(Qt::BevelJoin);
    else
        pen.setJoinStyle(Qt::RoundJoin);
    pen.setMiterLimit(strokeStyle->miterLimit);

    // Dash patterns are in units of the pen width.
    const double unit = weight > 0.0 ? weight : 1.0;

    if (strokeStyle->style() == fla::StrokeStyle::Style::Dashed)
    {
        pen.setDashPattern({std::max(strokeStyle->dash1, 0.1) / unit,
                            std::max(strokeStyle->dash2, 0.1) / unit});
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Dotted)
    {
        // A dot is a zero-length dash grown by its round cap, so the gap has to
        // allow for the half width the cap adds at each end.
        pen.setCapStyle(Qt::RoundCap);
        pen.setDashPattern({0.001, (std::max(strokeStyle->dotSpace, 0.0) + unit) / unit});
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Ragged)
    {
        pen.setStyle(Qt::DashDotLine);
    }
    else if (strokeStyle->style() == fla::StrokeStyle::Style::Stipple)
    {
        pen.setStyle(Qt::DashDotDotLine);
    }

    if (strokeStyle->fill->type() == fla::FillStyle::Type::SolidColor)
    {
        const fla::SolidColor* fill = static_cast<const fla::SolidColor*>(strokeStyle->fill);
        QColor color(fill->color[0], fill->color[1], fill->color[2], fill->color[3]);
        pen.setColor(color);
    }
    else
    {
        // A gradient or bitmap stroke is the same brush as the fill would
        // be, matrix and all.
        pen.setBrush(getFillBrush(strokeStyle->fill, fla::Rect()));
    }

    if (_maskPass)
        pen.setBrush(QBrush(Qt::black));

    return pen;
}

QPen PhoenixView::penForHere(const PathCacheEntry& entry) const
{
    const fla::StrokeStyle* stroke = entry.strokeStyle;
    if (!stroke || stroke->isHairline() || stroke->scaleMode.empty() || stroke->scaleMode == "normal")
        return entry.pen;
    QPen pen = entry.pen;
    pen.setWidthF(scaledStrokeWeight(entry.pen.widthF(), stroke->scaleMode));
    return pen;
}

double PhoenixView::scaledStrokeWeight(double weight, const std::string& scaleMode) const
{
    if (scaleMode.empty() || scaleMode == "normal")
        return weight;

    const QTransform& m = _elementTransform;
    const double scaleX = std::hypot(m.m11(), m.m12());
    const double scaleY = std::hypot(m.m21(), m.m22());
    const double area = std::sqrt(std::fabs(m.determinant()));
    if (area <= 1.0e-9)
        return weight;

    // The pen is drawn through the element's transform, which scales it by
    // about the square root of the area change; dividing that out leaves the
    // weight as it is on the stage, and one axis's scale puts it back.
    if (scaleMode == "none")
        return weight / area;
    if (scaleMode == "horizontal")
        return weight * scaleX / area;
    if (scaleMode == "vertical")
        return weight * scaleY / area;
    return weight;
}

namespace {

double toLinearLight(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double toSrgb(double c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

struct GradientStop
{
    double ratio;
    QColor color;
};

/// The stops on the gradient. Qt blends between stops in sRGB; a gradient
/// marked linearRGB is blended in linear light, which is approximated by
/// filling each span with stops computed there.
void addGradientStops(QGradient& gradient, const std::vector<GradientStop>& stops, bool linearRgb)
{
    if (!linearRgb || stops.size() < 2)
    {
        for (const GradientStop& stop : stops)
            gradient.setColorAt(stop.ratio, stop.color);
        return;
    }

    constexpr int kSteps = 8;
    for (size_t i = 0; i + 1 < stops.size(); ++i)
    {
        const GradientStop& a = stops[i];
        const GradientStop& b = stops[i + 1];
        gradient.setColorAt(a.ratio, a.color);
        for (int k = 1; k < kSteps; ++k)
        {
            const double t = static_cast<double>(k) / kSteps;
            const auto mix = [&](double x, double y) {
                const double lx = toLinearLight(x);
                const double ly = toLinearLight(y);
                return std::max(0.0, std::min(1.0, toSrgb(lx + (ly - lx) * t)));
            };
            const QColor color = QColor::fromRgbF(
                mix(a.color.redF(), b.color.redF()),
                mix(a.color.greenF(), b.color.greenF()),
                mix(a.color.blueF(), b.color.blueF()),
                a.color.alphaF() + (b.color.alphaF() - a.color.alphaF()) * t);
            gradient.setColorAt(a.ratio + (b.ratio - a.ratio) * t, color);
        }
    }
    gradient.setColorAt(stops.back().ratio, stops.back().color);
}

} // namespace

QBrush PhoenixView::getFillBrush(const fla::FillStyle* fillStyle, const fla::Rect& bounds)
{
    if (!fillStyle)
        return Qt::NoBrush;

    // In a mask only the area counts.
    if (_maskPass)
        return QBrush(Qt::black);

    if (fillStyle->type() == fla::FillStyle::Type::SolidColor)
    {
        const fla::SolidColor* solidFill = static_cast<const fla::SolidColor*>(fillStyle);
        QColor color(solidFill->color[0], solidFill->color[1], solidFill->color[2], solidFill->color[3]);
        return QBrush(color);
    }
    else if (fillStyle->type() == fla::FillStyle::Type::LinearGradient)
    {
        const fla::LinearGradient* linearFill = static_cast<const fla::LinearGradient*>(fillStyle);

        // Flash's gradient square is 1638.4 twips across before the matrix,
        // which the half scale below brings to 819.2 either side of center.
        const double halfSpan = 1638.4;
        QLinearGradient gradient(QPointF(-halfSpan, 0.0), QPointF(halfSpan, 0.0));
        gradient.setSpread(gradientSpread(linearFill->spreadMethod));
        std::vector<GradientStop> stops;
        for (const fla::GradientEntry& entry : linearFill->entries)
            stops.push_back({entry.ratio, QColor(entry.color[0], entry.color[1], entry.color[2], entry.color[3])});
        addGradientStops(gradient, stops, linearFill->interpolationMethod == "linearRGB");
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
        double radius = 1638.4;

        QRadialGradient gradient(cx, cy, radius);
        gradient.setSpread(gradientSpread(radialFill->spreadMethod));

        double focalOffset = focalRatio * radius;
        gradient.setFocalPoint(cx + focalOffset, cy);

        std::vector<GradientStop> stops;
        for (const fla::RadialEntry& entry : radialFill->entries)
            stops.push_back({entry.ratio, QColor(entry.color[0], entry.color[1], entry.color[2], entry.color[3])});
        addGradientStops(gradient, stops, radialFill->interpolationMethod == "linearRGB");

        QBrush brush(gradient);

        const double s = 0.5;
        QTransform brushTransform(radialFill->transform.m11 * s, radialFill->transform.m12 * s,
                            radialFill->transform.m21 * s, radialFill->transform.m22 * s,
                            radialFill->transform.tx, radialFill->transform.ty);

        brush.setTransform(brushTransform);

        return brush;
    }
    else if (fillStyle->type() == fla::FillStyle::Type::BitmapFill)
    {
        const fla::BitmapFill* bitmapFill = static_cast<const fla::BitmapFill*>(fillStyle);
        const QPixmap pixmap = libraryBitmap(QString::fromStdString(bitmapFill->bitmapPath));
        if (pixmap.isNull())
            return Qt::NoBrush;

        // The fill's matrix maps bitmap pixels to twips: an unscaled bitmap
        // carries a scale of 20, so a twentieth of it maps pixels to pixels.
        // A clipped fill shows the bitmap once; a tiled brush is the nearest
        // Qt comes, so the clipping is still to do.
        QBrush brush(pixmap);
        const double s = 1.0 / 20.0;
        brush.setTransform(QTransform(
            bitmapFill->transform.m11 * s, bitmapFill->transform.m12 * s,
            bitmapFill->transform.m21 * s, bitmapFill->transform.m22 * s,
            bitmapFill->transform.tx, bitmapFill->transform.ty));
        return brush;
    }

    return Qt::NoBrush;
}

QGradient::Spread PhoenixView::gradientSpread(const std::string& method)
{
    if (method == "reflect")
        return QGradient::ReflectSpread;
    if (method == "repeat")
        return QGradient::RepeatSpread;
    return QGradient::PadSpread;
}

QPixmap PhoenixView::libraryBitmap(const QString& name)
{
    if (_bitmapCache.contains(name))
        return _bitmapCache[name];
    if (!_flaDocument || !_flaDocument->document)
        return QPixmap();

    for (fla::Resource* resource : _flaDocument->document->resources)
    {
        if (!resource || resource->resourceType() != fla::Resource::Type::Bitmap)
            continue;
        const fla::Bitmap* bitmap = static_cast<const fla::Bitmap*>(resource);
        if (bitmap->name != name.toStdString())
            continue;

        QPixmap pixmap;
        if (!bitmap->imageData.empty())
        {
            // The imported file as Animate kept it.
            const QByteArray data(reinterpret_cast<const char*>(bitmap->imageData.data()),
                static_cast<int>(bitmap->imageData.size()));
            pixmap.loadFromData(data);
        }
        if (pixmap.isNull() && fla::isJpegData(bitmap->datData))
        {
            const QByteArray data(reinterpret_cast<const char*>(bitmap->datData.data()),
                static_cast<int>(bitmap->datData.size()));
            pixmap.loadFromData(data);
        }
        if (pixmap.isNull())
        {
            // Animate's own lossless encoding of the pixels.
            fla::DecodedBitmap decoded;
            if (fla::decodeLosslessBitmap(bitmap->datData, decoded))
            {
                QImage image(decoded.width, decoded.height, QImage::Format_ARGB32_Premultiplied);
                for (int y = 0; y < decoded.height; ++y)
                {
                    std::memcpy(image.scanLine(y),
                        decoded.pixels.data() + static_cast<size_t>(y) * decoded.width,
                        static_cast<size_t>(decoded.width) * 4);
                }
                pixmap = QPixmap::fromImage(image);
            }
        }
        _bitmapCache[name] = pixmap;
        return pixmap;
    }
    return QPixmap();
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
std::map<int, QPainterPath> PhoenixView::fillRegions(const fla::Shape& shape)
{
    PathCacheList entries;
    buildShapePaths(&shape, entries);
    std::map<int, QPainterPath> regions;
    for (const PathCacheEntry& entry : entries)
    {
        if (entry.fillIndex < 0)
            continue;
        QPainterPath area = entry.painterPath;
        area.setFillRule(Qt::WindingFill);
        auto it = regions.find(entry.fillIndex);
        if (it == regions.end())
            regions[entry.fillIndex] = area;
        else
            it->second = it->second.united(area);
    }
    return regions;
}

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

            // Add to fillStyle0 (reverse direction). A piece with the same fill
            // on both sides -- a stroke running through a fill -- bounds a
            // region on each side, so it goes in both ways; only a loop closed
            // on itself with one fill inside and out is taken once.
            const bool closedOnItself =
                std::lround(start.x * 20.0) == std::lround(end.x * 20.0) &&
                std::lround(start.y * 20.0) == std::lround(end.y * 20.0);
            if (fillStyleIdx0 != -1 && (fillStyleIdx0 != fillStyleIdx1 || !closedOnItself))
            {
                fillStylePaths[fillStyleIdx0].push_back({end, start, path, true, false});
                if (fillStyles.find(fillStyleIdx0) == fillStyles.end())
                {
                    fillStyles[fillStyleIdx0] = shape->getFillStyleByIndex(fillStyleIdx0);
                }
            }
        }
    }

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

    // The direction a path sets off in, and the direction it arrives with,
    // read from its first and last legs. A reversed path swaps the two and
    // turns them round. Junctions are decided by these.
    auto forwardStartTangent = [](const fla::Path* path) -> fla::Point
    {
        fla::Point at;
        bool started = false;
        for (const fla::PathSegment* segment : path->segments)
        {
            if (segment->command == fla::PathSegment::Command::Move)
            {
                at = segment->points[0];
                started = true;
                continue;
            }
            if (!started || segment->points.empty())
                continue;
            for (const fla::Point& point : segment->points)
            {
                const fla::Point d(point.x - at.x, point.y - at.y);
                if (std::hypot(d.x, d.y) > 1.0e-9)
                    return d;
            }
            at = segment->points.back();
        }
        return fla::Point(0.0, 0.0);
    };
    auto forwardEndTangent = [](const fla::Path* path) -> fla::Point
    {
        fla::Point at;
        bool started = false;
        fla::Point tangent(0.0, 0.0);
        for (const fla::PathSegment* segment : path->segments)
        {
            if (segment->command == fla::PathSegment::Command::Move)
            {
                at = segment->points[0];
                started = true;
                continue;
            }
            if (!started || segment->points.empty())
                continue;
            // From the last point before the end, back to the segment's start
            // when the controls sit on the end.
            const fla::Point& end = segment->points.back();
            fla::Point from = at;
            for (size_t i = segment->points.size() - 1; i > 0; --i)
            {
                const fla::Point& candidate = segment->points[i - 1];
                if (std::hypot(end.x - candidate.x, end.y - candidate.y) > 1.0e-9)
                {
                    from = candidate;
                    break;
                }
            }
            const fla::Point d(end.x - from.x, end.y - from.y);
            if (std::hypot(d.x, d.y) > 1.0e-9)
                tangent = d;
            at = end;
        }
        return tangent;
    };
    auto startTangent = [&](const DirectedPath& directed) -> fla::Point
    {
        if (!directed.reversed)
            return forwardStartTangent(directed.path);
        const fla::Point t = forwardEndTangent(directed.path);
        return fla::Point(-t.x, -t.y);
    };
    auto endTangent = [&](const DirectedPath& directed) -> fla::Point
    {
        if (!directed.reversed)
            return forwardEndTangent(directed.path);
        const fla::Point t = forwardStartTangent(directed.path);
        return fla::Point(-t.x, -t.y);
    };

    // How far round to the right \a out turns from \a in, on screen (y down):
    // pi for the sharpest right turn, -pi for the sharpest left, and a
    // straight reversal counted as the sharpest left, since doubling back
    // along the edge just walked is never the way round a region.
    auto rightTurn = [](const fla::Point& in, const fla::Point& out) -> double
    {
        const double cross = in.x * out.y - in.y * out.x;
        const double dot = in.x * out.x + in.y * out.y;
        double angle = std::atan2(cross, dot);
        const double pi = 3.14159265358979323846;
        if (angle > pi - 1.0e-6)
            angle = -pi;
        return angle;
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
            cacheEntries.push_back({ brush, Qt::NoPen, compoundPath, nullptr, fillIdx });
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
            fla::Point arriving = endTangent(directedPath);
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

                // Every directed path here has its fill on the right, so the
                // boundary of one region is traced by keeping right: at a
                // junction, of the paths that set off from here, take the one
                // that turns furthest to the right. Taking whichever came
                // first would wander into the neighboring region and leave
                // this one half drawn.
                bool found = false;
                double bestTurn = -1e9;
                DirectedPath* bestMatch = nullptr;

                for (DirectedPath& nextDirectedPath : directedPaths)
                {
                    if (nextDirectedPath.used)
                        continue;

                    double dx = currentEnd.x - nextDirectedPath.start.x;
                    double dy = currentEnd.y - nextDirectedPath.start.y;
                    double distance = dx * dx + dy * dy;
                    if (distance >= (0.5 * 0.5))
                        continue;

                    const double turn = rightTurn(arriving, startTangent(nextDirectedPath));
                    if (!found || turn > bestTurn)
                    {
                        bestTurn = turn;
                        bestMatch = &nextDirectedPath;
                        found = true;
                    }
                }

                if (found && bestMatch)
                {
                    addSegmentToPath(loopPath, bestMatch->path, bestMatch->reversed, shape);
                    bestMatch->used = true;
                    currentEnd = bestMatch->end;
                    arriving = endTangent(*bestMatch);
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
        cacheEntries.push_back({ brush, Qt::NoPen, compoundPath, nullptr, fillIdx });
    }

    // Strokes. A merged shape is stored as one edge per piece, so every corner
    // is where two pieces meet. Stroking each piece on its own puts a cap at
    // every corner and never a join, so pieces of one style are first chained
    // end to end into runs, and a run that comes back to its start is closed so
    // the first corner joins too.
    struct StrokePiece
    {
        const fla::Path* path;
        fla::Point start;
        fla::Point end;
        bool used = false;
    };
    std::map<int, std::vector<StrokePiece>> strokePieces;

    for (const fla::Edge* edge : shape->edges)
    {
        if (!edge->visible)
            continue;

        for (const fla::Path* path : edge->paths)
        {
            if (!path->visible || path->segments.empty())
                continue;

            const int strokeStyleIdx = path->lineStyleIndex != -1 ? path->lineStyleIndex : edge->strokeStyle;
            if (strokeStyleIdx == -1)
                continue;

            const fla::StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(strokeStyleIdx);
            if (!strokeStyle || !strokeStyle->fill)
                continue;

            auto [start, end] = getPathEndpoints(path);
            strokePieces[strokeStyleIdx].push_back({path, start, end, false});
        }
    }

    for (auto& [strokeIdx, pieces] : strokePieces)
    {
        const fla::StrokeStyle* strokeStyle = shape->getStrokeStyleByIndex(strokeIdx);
        const QPen pen = getPen(strokeStyle, false);

        for (size_t first = 0; first < pieces.size(); ++first)
        {
            if (pieces[first].used)
                continue;

            // The run as a list of pieces, each possibly traced backwards.
            std::vector<std::pair<size_t, bool>> run{{first, false}};
            pieces[first].used = true;
            fla::Point runStart = pieces[first].start;
            fla::Point runEnd = pieces[first].end;

            // Grow forward from the end; when nothing continues it, turn the run
            // round and grow from what was the start.
            for (int pass = 0; pass < 2; ++pass)
            {
                while (!pointsMatch(runEnd, runStart))
                {
                    bool extended = false;
                    for (size_t next = 0; next < pieces.size() && !extended; ++next)
                    {
                        if (pieces[next].used)
                            continue;

                        if (pointsMatch(runEnd, pieces[next].start))
                        {
                            run.push_back({next, false});
                            runEnd = pieces[next].end;
                        }
                        else if (pointsMatch(runEnd, pieces[next].end))
                        {
                            run.push_back({next, true});
                            runEnd = pieces[next].start;
                        }
                        else
                        {
                            continue;
                        }

                        pieces[next].used = true;
                        extended = true;
                    }

                    if (!extended)
                        break;
                }

                if (pointsMatch(runEnd, runStart))
                    break;

                std::reverse(run.begin(), run.end());
                for (auto& link : run)
                    link.second = !link.second;
                std::swap(runStart, runEnd);
            }

            QPainterPath strokePath;
            strokePath.moveTo(runStart.x, runStart.y);
            for (const auto& [index, reversed] : run)
                addSegmentToPath(strokePath, pieces[index].path, reversed, shape);

            if (pointsMatch(runEnd, runStart))
                strokePath.closeSubpath();

            cacheEntries.push_back({ QBrush(Qt::NoBrush), pen, strokePath, strokeStyle });
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

void PhoenixView::drawShape(QPainter& painter, const fla::Shape* shape, const fla::Shape* tweenShape,
    double tweenProgress, const QTransform& ownTransform)
{
    const bool isSelected = isElementSelected(shape);

    // A shape tween replaces the shape's own geometry for the duration of
    // the tween: the morph the file stores is built at this progress, with
    // its segments' own fills and strokes blended between the keyframes,
    // and drawn through the same path machinery as any shape.
    const fla::Frame* frame = owningFrame(shape);
    if (frame && frame->morphShape && frame->tweenType == fla::TweenType::Shape && tweenShape &&
        tweenProgress > 0.0 && tweenProgress < 1.0)
    {
        bool invertible = false;
        const QTransform toLocal = ownTransform.inverted(&invertible);
        if (invertible)
        {
            const fla::Transform documentToLocal(toLocal.m11(), toLocal.m12(), toLocal.m21(),
                toLocal.m22(), toLocal.dx(), toLocal.dy());
            std::unique_ptr<fla::Shape> morphed(fla::morphedShape(*frame->morphShape, *shape, *tweenShape,
                tweenProgress, shape->transform, tweenShape->transform, documentToLocal));

            PathCacheList entries;
            buildShapePaths(morphed.get(), entries);
            for (const PathCacheEntry& entry : entries)
            {
                if (_outlineColor.isValid())
                {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(_outlineColor, 0.0));
                }
                else
                {
                    painter.setBrush(entry.fillBrush);
                    painter.setPen(penForHere(entry));
                }
                painter.drawPath(entry.painterPath);
            }
            if (isSelected)
                drawOverlayPoints(painter, shape);
            return;
        }
    }

    for (const PathCacheEntry& entry : shapePaths(shape))
    {
        if (_outlineColor.isValid())
        {
            // Outline view: the fill's boundary and the stroke's center line,
            // both as a hairline in the layer color.
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(_outlineColor, 0.0));
        }
        else
        {
            painter.setBrush(entry.fillBrush);
            painter.setPen(penForHere(entry));
        }
        painter.drawPath(entry.painterPath);
    }

    // Control points on a tree-selected shape are an inspection aid, so they
    // come with the bounding-box debug view rather than the look of a selection.
    if (isSelected && _showBounds)
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

    // Inside a symbol only its own timeline can be picked; the dimmed
    // document behind it is out of reach, as in Animate.
    for (const fla::Timeline* timeline : editableTimelines())
    {
        if (!timeline->visible)
            continue;

        self->hitTestTimeline(*timeline, fla::LoopType::PlayOnce, 0,
            QTransform(), documentPos, tolerance, result);
        if (result)
            break;
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

        // Layers draw last to first, so the first layer is on top and this walk
        // meets the topmost first. Within a frame later elements draw over
        // earlier ones, so they are walked backwards, and the first hit is the
        // one the user sees.
        for (auto it = state.frame->elements.rbegin(); it != state.frame->elements.rend(); ++it)
        {
            if (*it)
            {
                hitTestElement(**it, const_cast<fla::Frame*>(state.frame), loopType,
                    toDocument, documentPos, tolerance, result);
            }
            if (result)
                return;
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
        // Clicking anything inside a group picks the group, as in Animate;
        // editing the contents means entering the group.
        fla::Group& group = static_cast<fla::Group&>(element);
        HitResult inner;
        for (auto it = group.members.rbegin(); it != group.members.rend() && !inner; ++it)
        {
            if (*it)
            {
                hitTestElement(**it, frame, loopType, elementToDocument,
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
            else if (part == HitResult::Part::None &&
                entry.painterPath.controlPointRect().adjusted(-localTolerance, -localTolerance,
                    localTolerance, localTolerance).contains(localPos) &&
                isNearOutline(entry.painterPath, localPos, localTolerance))
            {
                // Just outside an unstroked fill still counts as its outline:
                // Animate lets the arrow grab and bend a fill's edge from
                // either side, stroke or no stroke.
                part = HitResult::Part::Stroke;
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

                // Only the on-curve point counts: a segment's last point is
                // where it ends, the rest are control points.
                if (segment->points.empty())
                    continue;

                const fla::Point& point = segment->points.back();
                const double dx = point.x - localPos.x();
                const double dy = point.y - localPos.y();
                if (dx * dx + dy * dy <= limit)
                    return true;
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
    const bool forcePan = event->button() == Qt::MiddleButton || _spaceHeld;

    beginInteraction();

    // Ctrl reaches for the selection tool whatever is active, so something can
    // be moved without putting the drawing tool down. The gesture then belongs
    // to that tool until the button comes up, even if ctrl is let go first.
    const bool ctrlIsArrow = (event->modifiers() & Qt::ControlModifier) && _modifierTool &&
        !(_activeTool && _activeTool->usesControlKey());
    _gestureTool = ctrlIsArrow ? _modifierTool : _activeTool;

    if (!forcePan && _gestureTool &&
        _gestureTool->mousePress(*this, event, mapToDocument(event->position())))
    {
        return;
    }

    _gestureTool = nullptr;

    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
    {
        _isDragging = true;
        _lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void PhoenixView::mouseMoveEvent(QMouseEvent *event)
{
    if (!_isDragging && gestureTool() &&
        gestureTool()->mouseMove(*this, event, mapToDocument(event->position())))
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

    Tool* tool = gestureTool();
    _gestureTool = nullptr;

    if (!_isDragging && tool &&
        tool->mouseRelease(*this, event, mapToDocument(event->position())))
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

void PhoenixView::tabletEvent(QTabletEvent* event)
{
    // Only the pressure is taken; the event is left alone so that Qt goes on
    // to deliver the same movement as mouse events, which is what the tools
    // handle.
    if (event->type() == QEvent::TabletRelease)
        _pressure = 1.0;
    else
        _pressure = std::max(0.0, std::min(1.0, event->pressure()));
    event->ignore();
}

bool PhoenixView::event(QEvent* event)
{
    // While a tool is taking typed text, a plain key is a character for it,
    // not a tool shortcut; the window's shortcuts only see keys with ctrl.
    if (event->type() == QEvent::ShortcutOverride && _activeTool && _activeTool->capturesKeyboard())
    {
        const QKeyEvent* key = static_cast<const QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers =
            key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
        const bool altGr = modifiers == (Qt::ControlModifier | Qt::AltModifier);
        const bool selectAll = modifiers == Qt::ControlModifier && key->key() == Qt::Key_A;
        if (modifiers == Qt::NoModifier || altGr || selectAll)
        {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void PhoenixView::keyPressEvent(QKeyEvent* event)
{
    if (_activeTool && _activeTool->keyPress(*this, event))
        return;

    // Space held is the hand tool for as long as it is down.
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        _spaceHeld = true;
        if (!_isDragging)
            setCursor(Qt::OpenHandCursor);
        return;
    }

    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        _selection && !_selection->isEmpty())
    {
        deleteSelection();
        return;
    }

    // Ctrl is the temporary arrow tool, and the cursor says so while it is
    // held, the way Animate does.
    if (event->key() == Qt::Key_Control && _modifierTool && !_gestureTool && !_isDragging &&
        !(_activeTool && _activeTool->usesControlKey()))
    {
        setCursor(_modifierTool->cursor());
        return;
    }

    QWidget::keyPressEvent(event);
}

void PhoenixView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyReleaseEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Space)
    {
        _spaceHeld = false;
        if (!_isDragging)
            setCursor(_activeTool ? _activeTool->cursor() : QCursor(Qt::ArrowCursor));
        return;
    }

    if (event->key() == Qt::Key_Control && !_gestureTool && !_isDragging)
    {
        setCursor(_activeTool ? _activeTool->cursor() : QCursor(Qt::ArrowCursor));
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void PhoenixView::deleteSelection()
{
    if (!_selection || _selection->isEmpty() || !_commandStack)
        return;

    struct Doomed
    {
        fla::Frame* frame;
        fla::Element* element;
    };
    std::vector<Doomed> doomed;

    for (fla::DOMElement* selected : _selection->elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element)
            continue;

        fla::Frame* frame = const_cast<fla::Frame*>(owningFrame(element));
        if (frame)
            doomed.push_back({frame, element});
    }

    if (doomed.empty())
        return;

    // A drawing that was waiting to merge has nothing left to drop in, and a
    // deleted region stays gone: the hole it leaves is what Animate leaves too.
    for (const Doomed& entry : doomed)
    {
        _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
            [&](const PendingDrop& pending) { return pending.shape == entry.element; }),
            _pending.end());
        _floating.erase(std::remove_if(_floating.begin(), _floating.end(),
            [&](const FloatingRegion& region) { return region.piece == entry.element; }),
            _floating.end());
    }

    const bool needsMacro = doomed.size() > 1;
    if (needsMacro)
        _commandStack->beginMacro("Delete");

    for (const Doomed& entry : doomed)
    {
        _commandStack->push(fla::CommandPtr(new fla::RemoveElementCommand(
            entry.frame, entry.element, "Delete", _selection)));
    }

    if (needsMacro)
        _commandStack->endMacro();
    _commandStack->breakMergeChain();

    clearCaches();
    update();
}

std::vector<fla::Element*> PhoenixView::elementsIn(const QRectF& documentRect)
{
    std::vector<fla::Element*> found;
    if (!_flaDocument || !_flaDocument->document)
        return found;

    const fla::Document* document = _flaDocument->document;
    if (!document->visible)
        return found;

    for (const fla::Timeline* timeline : editableTimelines())
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
    emit activeToolChanged(_activeTool);
}

void PhoenixView::setSelection(fla::Selection* selection)
{
    _selection = selection;
    update();
}

void PhoenixView::setPendingMerge(fla::Shape* shape, fla::Frame* frame, bool keepStrokes)
{
    if (!shape || !frame)
        return;

    for (const PendingDrop& pending : _pending)
    {
        if (pending.shape == shape)
            return;
    }

    _pending.push_back({shape, frame, keepStrokes});
}

void PhoenixView::markShapeEdited(fla::Shape* shape)
{
    // A drawing object keeps to itself: moving it never drops it into the
    // artwork underneath. Nor does a shape inside a group: only artwork sitting
    // directly in the frame is merge artwork.
    if (!shape || shape->isDrawingObject || !shape->parent ||
        shape->parent->domType() != fla::DOMElement::DOMType::Frame)
    {
        return;
    }

    setPendingMerge(shape, const_cast<fla::Frame*>(owningFrame(shape)));
}

void PhoenixView::clearPendingMerge()
{
    _pending.clear();
    _floating.clear();

    // The scope points into the document too.
    if (!_scope.empty())
    {
        _scope.clear();
        emit scopeChanged();
    }
}

const PhoenixView::FloatingRegion* PhoenixView::floatingFor(const fla::Element* element) const
{
    for (const FloatingRegion& region : _floating)
    {
        if (region.piece == element)
            return &region;
    }
    return nullptr;
}

bool PhoenixView::isFloatingRegion(const fla::Element* element) const
{
    return floatingFor(element) != nullptr;
}

void PhoenixView::settleFloating()
{
    if (_floating.empty() || !_selection || !_commandStack)
        return;

    // A command changing the selection as it runs -- grouping or converting
    // what was lifted out -- is not the user letting go of it. Settling waits
    // for the next change the user makes.
    if (_commandStack->isExecuting())
        return;

    std::vector<FloatingRegion> keep;
    for (const FloatingRegion& region : _floating)
    {
        if (_selection->contains(region.piece))
        {
            keep.push_back(region);
            continue;
        }

        // Untouched: the split is still the last edit, so it is taken back
        // and the source is whole again, with no trace in the history. Touched:
        // the ordinary drop puts the piece back in wherever it now sits.
        if (_commandStack->topCommand() == region.split && !_commandStack->isInMacro())
        {
            _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                [&](const PendingDrop& pending) { return pending.shape == region.piece; }),
                _pending.end());
            _commandStack->discardLast();
            clearCaches();
        }
    }
    _floating = keep;
}

bool PhoenixView::selectRegionAt(const HitResult& hit, const QPointF& documentPos,
    bool extended, bool additive)
{
    if (!hit || !_selection || !_commandStack || _committingMerge)
        return false;
    if (hit.element->elementType() != fla::Element::Type::Shape)
        return false;

    fla::Shape* shape = static_cast<fla::Shape*>(hit.element);
    if (shape->isDrawingObject)
        return false;

    QTransform toDocument = hit.elementToDocument;

    // A click on a region already lifted is a click on its source: it goes back
    // first, and the pick is made again on the whole shape. That is what makes a
    // double-click widen a single-click's selection.
    if (const FloatingRegion* region = floatingFor(shape))
    {
        if (!extended)
            return true;

        fla::Shape* source = region->source;
        _committingMerge = true;
        _selection->remove(shape);
        _committingMerge = false;
        settleFloating();

        // If the piece had already been edited it was dropped back in by the
        // merge, and the source carries the result either way.
        shape = source;
        const fla::Transform& t = shape->transform;
        toDocument = QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
    }

    bool invertible = false;
    const QTransform toShape = toDocument.inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF local = toShape.map(documentPos);
    const double tolerance = toleranceInLocalSpace(toDocument, pickTolerance());

    fla::RegionSplitter splitter(*shape);
    const fla::RegionPick pick = splitter.pickAt(fla::Point(local.x(), local.y()), tolerance, extended);
    if (pick.isEmpty())
        return false;

    fla::Shape* piece = nullptr;
    fla::Shape* remainder = nullptr;
    if (!splitter.split(pick, piece, remainder))
        return false;

    fla::Frame* frame = const_cast<fla::Frame*>(owningFrame(shape));
    if (!frame)
    {
        delete piece;
        delete remainder;
        return false;
    }

    piece->transform = shape->transform;

    const char* name = pick.kind == fla::RegionPick::Kind::Stroke ? "Select Stroke" : "Select Fill";
    _commandStack->push(fla::CommandPtr(new fla::SplitRegionCommand(
        shape, *remainder, frame, piece, name, _selection)));
    _commandStack->breakMergeChain();
    delete remainder;

    _floating.push_back({shape, piece, frame, _commandStack->topCommand()});
    setPendingMerge(piece, frame);
    clearCaches();

    if (additive)
        _selection->add(piece);
    else
        _selection->select(piece);

    update();
    return true;
}

namespace {

/// A closed outline as the straight pieces that will cut an arrangement.
std::vector<fla::Curve> outlineCurves(const QPainterPath& outline)
{
    std::vector<fla::Curve> curves;
    for (const QPolygonF& polygon : outline.toSubpathPolygons())
    {
        if (polygon.size() < 3)
            continue;

        for (int i = 0; i < polygon.size(); ++i)
        {
            const QPointF& a = polygon[i];
            const QPointF& b = polygon[(i + 1) % polygon.size()];
            if (a == b)
                continue;
            curves.push_back(fla::Curve::line(fla::Point(a.x(), a.y()), fla::Point(b.x(), b.y())));
        }
    }
    return curves;
}

} // namespace

bool PhoenixView::selectRegionsIn(const QPainterPath& area, bool additive)
{
    if (!_selection || !_commandStack || _committingMerge)
        return false;

    std::vector<fla::DOMElement*> picked;

    for (fla::Element* element : elementsIn(area.boundingRect()))
    {
        const QRectF bounds = getElementBounds(element);
        if (!bounds.isValid() || !(area.intersects(bounds) || area.contains(bounds)))
            continue;

        fla::Shape* shape = element->elementType() == fla::Element::Type::Shape
            ? static_cast<fla::Shape*>(element) : nullptr;
        const bool mergeArtwork = shape && !shape->isDrawingObject && !isFloatingRegion(shape);

        // Objects, and merge artwork the area swallows whole, are selected
        // as they are.
        if (!mergeArtwork || area.contains(bounds))
        {
            picked.push_back(element);
            continue;
        }

        // Partly covered merge artwork is divided along the area's outline and
        // the regions inside are lifted out, which is what lets half a fill be
        // dragged away.
        const fla::Transform& t = shape->transform;
        const QTransform toDocument(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
        bool invertible = false;
        const QTransform toShape = toDocument.inverted(&invertible);
        if (!invertible)
            continue;

        const QPainterPath localArea = toShape.map(area);
        const std::vector<fla::Curve> cutters = outlineCurves(localArea);
        if (cutters.empty())
            continue;

        fla::RegionSplitter regions(*shape, cutters);
        const auto inside = [&](const fla::Point& point) {
            return localArea.contains(QPointF(point.x, point.y));
        };
        const std::vector<int> faces = regions.facesWhere(inside);
        const std::vector<int> strokes = regions.strokesWhere(inside);
        if (faces.empty() && strokes.empty())
            continue;

        fla::Shape* piece = nullptr;
        fla::Shape* remainder = nullptr;
        if (!regions.splitSet(faces, strokes, piece, remainder))
            continue;

        fla::Frame* frame = const_cast<fla::Frame*>(owningFrame(shape));
        if (!frame)
        {
            delete piece;
            delete remainder;
            continue;
        }

        piece->transform = shape->transform;
        _commandStack->push(fla::CommandPtr(new fla::SplitRegionCommand(
            shape, *remainder, frame, piece, "Select", _selection)));
        _commandStack->breakMergeChain();
        delete remainder;

        _floating.push_back({shape, piece, frame, _commandStack->topCommand()});
        setPendingMerge(piece, frame);
        picked.push_back(piece);
    }

    clearCaches();

    if (additive)
    {
        for (fla::DOMElement* element : picked)
            _selection->add(element);
    }
    else
    {
        _selection->select(picked);
    }

    update();
    return !picked.empty();
}

void PhoenixView::flushPendingMerge()
{
    if (!_selection || !_commandStack || _committingMerge)
        return;

    _committingMerge = true;
    settleFloating();
    _committingMerge = false;

    if (_pending.empty())
        return;

    const std::vector<PendingDrop> dropping = _pending;
    _pending.clear();

    // Dropping one changes the selection and can remove another, either of which
    // would come straight back round through selectionChanged().
    _committingMerge = true;
    for (const PendingDrop& pending : dropping)
        commitPendingMerge(*this, *_commandStack, *_selection, pending.shape, pending.frame,
            pending.keepStrokes);
    _committingMerge = false;
}

void PhoenixView::selectionChanged()
{
    if (!_selection || _committingMerge)
        return;
    if (_commandStack && _commandStack->isExecuting())
        return;

    // Regions let go of untouched go straight back before anything is dropped.
    _committingMerge = true;
    settleFloating();
    _committingMerge = false;

    if (_pending.empty())
        return;

    // Anything still selected is still being worked on, and stays its own object
    // until it is let go.
    std::vector<PendingDrop> keep;
    std::vector<PendingDrop> dropping;
    for (const PendingDrop& pending : _pending)
    {
        if (_selection->contains(pending.shape))
            keep.push_back(pending);
        else
            dropping.push_back(pending);
    }

    if (dropping.empty())
        return;

    _pending = keep;

    _committingMerge = true;
    for (const PendingDrop& pending : dropping)
        commitPendingMerge(*this, *_commandStack, *_selection, pending.shape, pending.frame,
            pending.keepStrokes);
    _committingMerge = false;
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

    for (const fla::Timeline* timeline : editableTimelines())
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

fla::Frame* PhoenixView::keyframeForDrawing(fla::Frame* frame)
{
    if (!frame || !_player || !_commandStack)
        return frame;

    const fla::Layer* layer = owningLayer(frame);
    if (!layer)
        return frame;

    const int shown = _player->currentFrame();
    if (shown == frame->index || shown < frame->index)
        return frame;

    fla::InsertKeyframeCommand* insert = new fla::InsertKeyframeCommand(
        const_cast<fla::Layer*>(layer), shown, "Insert Keyframe");
    _commandStack->push(fla::CommandPtr(insert));
    clearCaches();
    return insert->frame() ? insert->frame() : frame;
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

    // The stage edges and center are worth lining up with too, at the scene;
    // inside a symbol they are not in its space.
    if (_scope.empty())
    {
        snapper.addCandidateX(0.0);
        snapper.addCandidateX(document->width / 2.0);
        snapper.addCandidateX(document->width);
        snapper.addCandidateY(0.0);
        snapper.addCandidateY(document->height / 2.0);
        snapper.addCandidateY(document->height);
    }

    // Everything on stage apart from what is being dragged: an object that
    // snapped to its own edge would simply never move. The walk over the
    // stage is kept between drags and thrown away with the bounds cache.
    if (!_snapBoundsValid)
    {
        _snapBounds.clear();
        const QRectF everything(-1.0e6, -1.0e6, 2.0e6, 2.0e6);
        for (fla::Element* element : elementsIn(everything))
        {
            const QRectF bounds = getElementBounds(element);
            if (bounds.isValid())
                _snapBounds.push_back({element, bounds});
        }
        _snapBoundsValid = true;
    }

    for (const auto& entry : _snapBounds)
    {
        fla::Element* element = entry.first;
        bool skip = false;
        for (const fla::Element* excluded : exclude)
            skip = skip || excluded == element;
        if (skip)
            continue;

        const QRectF bounds = entry.second;

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
    // The stored color is meant for lines over artwork, so it is dialled well
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
    const bool showFills = !_activeTool || _activeTool->showsFillHighlight();

    if (_selection && !_selection->isEmpty())
    {
        painter.save();

        for (fla::DOMElement* selected : _selection->elements())
        {
            if (!selected || selected->domType() == fla::DOMElement::DOMType::Element)
                continue;

            fla::Element* element = dynamic_cast<fla::Element*>(selected);
            if (!element)
                continue;

            const QColor color = highlightColorFor(element);

            // Merge-drawn artwork is shown the way Animate shows it: a dot screen
            // over the selected fills and strokes, not a box round the whole
            // shape.
            if (element->elementType() == fla::Element::Type::Shape)
            {
                const fla::Shape* shape = static_cast<const fla::Shape*>(element);
                if (!shape->isDrawingObject)
                {
                    if (showFills)
                        drawShapeHighlight(painter, shape, color);
                    continue;
                }
            }

            if (!showBounds)
                continue;

            const QRectF bounds = getElementBounds(element);
            if (!bounds.isValid())
                continue;

            // Drawing objects, groups, symbols and the rest get a thin box with
            // no grips: grips belong to the free transform tool.
            QPen outline(color, 1.0 * scale);
            outline.setCosmetic(false);
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bounds);
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


void PhoenixView::drawShapeHighlight(QPainter& painter, const fla::Shape* shape, const QColor& color)
{
    const double scale = _zoom > 0.0 ? 1.0 / _zoom : 1.0;

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const fla::Transform& t = shape->transform;
    painter.setTransform(QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty), true);

    // The screen is a texture of device pixels, so the brush is given the
    // inverse of the painter's mapping and lands on whole pixels whatever the
    // zoom and the shape's own transform.
    QPixmap texture(4, 4);
    texture.fill(Qt::transparent);
    {
        QPainter dots(&texture);
        dots.fillRect(0, 0, 1, 1, color);
        dots.fillRect(2, 0, 1, 1, color);
        dots.fillRect(0, 2, 1, 1, color);
        dots.fillRect(2, 2, 1, 1, color);
    }
    QBrush stipple(texture);
    bool invertible = false;
    const QTransform inverse = painter.transform().inverted(&invertible);
    if (invertible)
    {
        const QTransform toDevice = painter.transform();
        stipple.setTransform(QTransform::fromTranslate(
            std::round(toDevice.dx()), std::round(toDevice.dy())) * inverse);
    }

    for (const PathCacheEntry& entry : shapePaths(shape))
    {
        if (entry.pen.style() == Qt::NoPen)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(stipple);
            painter.drawPath(entry.painterPath);
        }
        else
        {
            // The stroke is screened along its own width, with a floor so a
            // hairline still shows as selected.
            QPen pen(stipple, std::max(entry.pen.widthF(), 3.0 * scale));
            pen.setCapStyle(entry.pen.capStyle());
            pen.setJoinStyle(entry.pen.joinStyle());
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(entry.painterPath);
        }
    }

    painter.restore();
}

void PhoenixView::wheelEvent(QWheelEvent *event)
{
    // As in Animate: the wheel scrolls the stage, shift scrolls it sideways,
    // and ctrl zooms about the cursor.
    if (event->modifiers() & Qt::ControlModifier)
    {
        const double factor = event->angleDelta().y() < 0 ? 1.0 / 1.15 : 1.15;
        zoomTo(_zoom * factor, event->position());
    }
    else
    {
        QPointF delta = event->pixelDelta();
        if (delta.isNull())
            delta = QPointF(event->angleDelta()) * (40.0 / 120.0);

        if (event->modifiers() & Qt::ShiftModifier)
            delta = QPointF(delta.y(), delta.x());

        _panX += delta.x();
        _panY += delta.y();
    }

    // A wheel gesture has no release to end it, so each notch pushes the
    // settle back and quality returns once the wheel stops.
    beginInteraction();
    _settleTimer->start();

    update();
}

void PhoenixView::zoomTo(double zoom, const QPointF& widgetAnchor)
{
    zoom = qBound(_minZoom, zoom, _maxZoom);
    if (zoom == _zoom)
        return;

    // Keep whatever is under the anchor under the anchor: note the document
    // point first, zoom, then pan by however far it moved.
    const QPointF anchor = mapToDocument(widgetAnchor);
    _zoom = zoom;
    const QPointF delta = mapToWidget(anchor) - widgetAnchor;
    _panX -= delta.x();
    _panY -= delta.y();
    update();
}

void PhoenixView::setZoomLevel(double zoom)
{
    zoomTo(zoom, QPointF(width() / 2.0, height() / 2.0));
}

void PhoenixView::zoomIn()
{
    for (double preset : kZoomPresets)
    {
        if (preset > _zoom * 1.001)
        {
            setZoomLevel(preset);
            return;
        }
    }
}

void PhoenixView::zoomOut()
{
    double chosen = _zoom;
    for (double preset : kZoomPresets)
    {
        if (preset < _zoom / 1.001)
            chosen = preset;
    }
    setZoomLevel(chosen);
}

void PhoenixView::zoomToRect(const QRectF& documentRect)
{
    if (!documentRect.isValid() || documentRect.width() <= 0.0 || documentRect.height() <= 0.0)
        return;

    double docWidth = 0.0;
    double docHeight = 0.0;
    if (_flaDocument && _flaDocument->document)
    {
        docWidth = _flaDocument->document->width;
        docHeight = _flaDocument->document->height;
    }

    const double margin = 20.0;
    const double zoom = qBound(_minZoom,
        std::min((width() - 2.0 * margin) / documentRect.width(),
                 (height() - 2.0 * margin) / documentRect.height()),
        _maxZoom);

    // With no pan the stage is centered, so the pan is whatever moves the
    // rectangle's center to the stage's.
    _zoom = zoom;
    _panX = (docWidth / 2.0 - documentRect.center().x()) * zoom;
    _panY = (docHeight / 2.0 - documentRect.center().y()) * zoom;
    update();
}

void PhoenixView::zoomToFit()
{
    if (!_flaDocument || !_flaDocument->document)
        return;

    zoomToRect(QRectF(0.0, 0.0, _flaDocument->document->width,
        _flaDocument->document->height));
}

QRectF PhoenixView::contentBounds()
{
    QRectF bounds;
    if (_flaDocument && _flaDocument->document && _scope.empty())
        bounds = QRectF(0.0, 0.0, _flaDocument->document->width, _flaDocument->document->height);

    const QRectF everything(-1.0e6, -1.0e6, 2.0e6, 2.0e6);
    for (fla::Element* element : elementsIn(everything))
    {
        const QRectF elementBounds = getElementBounds(element);
        if (elementBounds.isValid())
            bounds = bounds.isValid() ? bounds.united(elementBounds) : elementBounds;
    }
    return bounds;
}

void PhoenixView::zoomToShowAll()
{
    zoomToRect(contentBounds());
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
    _snapBoundsValid = false;
    _filterCache.clear();
    _backdropCache.clear();
    _staticValid = false;
    ++_revision;
}

void PhoenixView::invalidateBounds()
{
    _boundsCache.clear();
    _snapBoundsValid = false;
}

QTransform PhoenixView::documentToWidget() const
{
    // Inside a symbol the tools work in the symbol's own space, so the
    // mapping runs through every instance entered on the way in.
    return scopeToDocument() * rootToWidget();
}

QTransform PhoenixView::scopeToDocument() const
{
    QTransform transform;
    for (const EditScope& level : _scope)
    {
        if (!level.instance)
        {
            // A symbol edited on its own sits with its registration point at
            // the stage's center.
            const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
            transform = document ? QTransform::fromTranslate(document->width / 2.0, document->height / 2.0) * transform
                : transform;
            continue;
        }
        const fla::Transform& t = level.instance->transform;
        transform = QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty) * transform;
    }
    return transform;
}

std::vector<const fla::Timeline*> PhoenixView::editableTimelines() const
{
    std::vector<const fla::Timeline*> timelines;
    if (!_scope.empty())
    {
        if (_scope.back().timeline)
            timelines.push_back(_scope.back().timeline);
        return timelines;
    }

    if (_flaDocument && _flaDocument->document)
    {
        for (const fla::Timeline* timeline : _flaDocument->document->timelines)
            timelines.push_back(timeline);
    }
    return timelines;
}

fla::Timeline* PhoenixView::scopeTimeline() const
{
    return _scope.empty() ? nullptr : _scope.back().timeline;
}

QString PhoenixView::scopeName(int depth) const
{
    if (depth <= 0)
    {
        if (_flaDocument && _flaDocument->document && !_flaDocument->document->timelines.empty())
        {
            const std::string& name = _flaDocument->document->timelines.front()->name;
            if (!name.empty())
                return QString::fromStdString(name);
        }
        return "Scene 1";
    }

    if (depth > static_cast<int>(_scope.size()))
        return QString();

    const fla::SymbolInstance* instance = _scope[depth - 1].instance;
    if (!instance && _scope[depth - 1].symbol)
        return QString::fromStdString(_scope[depth - 1].symbol->name);
    if (instance && instance->symbol && !instance->symbol->name.empty())
        return QString::fromStdString(instance->symbol->name);
    return QString::fromStdString(instance ? instance->libraryItemName : std::string());
}

bool PhoenixView::enterSymbol(fla::SymbolInstance* instance)
{
    if (!instance || !instance->symbol || instance->symbol->timelines.empty())
        return false;

    // Whatever was floating in the outer scope goes in before leaving it.
    flushPendingMerge();
    if (_selection)
        _selection->clear();

    _scope.push_back({instance, instance->symbol->timelines.front(), instance->symbol});
    clearCaches();
    emit scopeChanged();
    update();
    return true;
}

bool PhoenixView::editSymbol(const fla::Symbol* symbol)
{
    if (!symbol || symbol->timelines.empty())
        return false;
    exitToDepth(0);
    flushPendingMerge();
    if (_selection)
        _selection->clear();
    _scope.push_back({nullptr, symbol->timelines.front(), symbol});
    clearCaches();
    emit scopeChanged();
    update();
    return true;
}

void PhoenixView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(LibraryTree::mimeType()))
        event->acceptProposedAction();
    else
        QWidget::dragEnterEvent(event);
}

void PhoenixView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(LibraryTree::mimeType()))
        event->acceptProposedAction();
    else
        QWidget::dragMoveEvent(event);
}

void PhoenixView::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(LibraryTree::mimeType()))
    {
        QWidget::dropEvent(event);
        return;
    }
    const QString text = QString::fromUtf8(event->mimeData()->data(LibraryTree::mimeType()));
    const int colon = text.indexOf(':');
    if (colon < 0)
        return;
    bool invertible = false;
    const QTransform toScope = documentToWidget().inverted(&invertible);
    if (!invertible)
        return;
    event->acceptProposedAction();
    emit libraryItemDropped(text.left(colon), text.mid(colon + 1), toScope.map(event->position()));
}

bool PhoenixView::exitScope()
{
    if (_scope.empty())
        return false;

    flushPendingMerge();
    fla::SymbolInstance* instance = _scope.back().instance;
    _scope.pop_back();
    clearCaches();

    if (_selection)
        _selection->select(instance);

    emit scopeChanged();
    update();
    return true;
}

void PhoenixView::exitToDepth(int depth)
{
    while (static_cast<int>(_scope.size()) > std::max(0, depth))
        exitScope();
}

QTransform PhoenixView::rootToWidget() const
{
    double docWidth = 0.0;
    double docHeight = 0.0;
    if (_flaDocument && _flaDocument->document)
    {
        docWidth = _flaDocument->document->width;
        docHeight = _flaDocument->document->height;
    }

    // The stage sits centered in whatever space the widget has, so the centering
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
        QRectF bounds = TextLayout::localBounds(*static_cast<const fla::StaticText*>(element));
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

            const auto include = [&](const fla::Point& pt) {
                const QPointF qpt(pt.x, pt.y);
                if (first)
                {
                    first = false;
                    bounds = QRectF(qpt, QSizeF(0, 0));
                    return;
                }
                if (qpt.x() < bounds.left())
                    bounds.setLeft(qpt.x());
                if (qpt.x() > bounds.right())
                    bounds.setRight(qpt.x());
                if (qpt.y() < bounds.top())
                    bounds.setTop(qpt.y());
                if (qpt.y() > bounds.bottom())
                    bounds.setBottom(qpt.y());
            };

            // The curve itself, not its control polygon: a curve's extent is
            // its ends and wherever it turns back in x or y, which is tight
            // where control points would be loose.
            fla::Point current;
            bool haveCurrent = false;
            for (const fla::PathSegment* segment : path->segments)
            {
                if (!segment || segment->points.empty())
                    continue;
                const std::vector<fla::Point>& pts = segment->points;
                switch (segment->command)
                {
                case fla::PathSegment::Command::Move:
                case fla::PathSegment::Command::Line:
                    include(pts.back());
                    current = pts.back();
                    haveCurrent = true;
                    break;
                case fla::PathSegment::Command::Quad:
                case fla::PathSegment::Command::Cubic:
                {
                    if (!haveCurrent)
                    {
                        for (const fla::Point& pt : pts)
                            include(pt);
                        current = pts.back();
                        haveCurrent = true;
                        break;
                    }
                    const fla::Curve curve = segment->command == fla::PathSegment::Command::Quad
                        ? fla::Curve::quadratic(current, pts[0], pts[1])
                        : fla::Curve::cubic(current, pts[0], pts[1], pts[2]);
                    include(curve.end());
                    for (const double t : curve.extremaParameters())
                        include(curve.pointAt(t));
                    current = pts.back();
                    break;
                }
                case fla::PathSegment::Command::Close:
                    break;
                }
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

    // The arithmetic is on straight color: offsets added to premultiplied
    // channels would brighten the edges of anything translucent.
    const bool premultiplied = image.format() == QImage::Format_ARGB32_Premultiplied;

    for (int y = 0; y < image.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            QRgb pixel = line[x];
            if (qAlpha(pixel) == 0)
                continue;
            if (premultiplied)
                pixel = qUnpremultiply(pixel);

            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            int a = qAlpha(pixel);

            if (colorTransform.tintMultiplier != 0.0)
            {
                // Tint multiplies each color channel by 1 - t and adds t of
                // the tint color; alpha is left alone.
                const double t = colorTransform.tintMultiplier;
                const double it = 1.0 - t;
                r = static_cast<int>(it * r + t * colorTransform.tintColor[0]);
                g = static_cast<int>(it * g + t * colorTransform.tintColor[1]);
                b = static_cast<int>(it * b + t * colorTransform.tintColor[2]);
            }

            if (colorTransform.brightness != 0.0)
            {
                // Brightness is a multiply and add too: towards white it
                // multiplies by 1 - b and adds 255 b, towards black it
                // multiplies by 1 + b.
                const double br = colorTransform.brightness;
                if (br > 0.0)
                {
                    r = static_cast<int>(r * (1.0 - br) + 255.0 * br);
                    g = static_cast<int>(g * (1.0 - br) + 255.0 * br);
                    b = static_cast<int>(b * (1.0 - br) + 255.0 * br);
                }
                else
                {
                    r = static_cast<int>(r * (1.0 + br));
                    g = static_cast<int>(g * (1.0 + br));
                    b = static_cast<int>(b * (1.0 + br));
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

            line[x] = premultiplied ? qPremultiply(qRgba(r, g, b, a)) : qRgba(r, g, b, a);
        }
    }
}

QRectF PhoenixView::calculateSymbolLocalBounds(const fla::Symbol* symbol)
{
    QRectF bounds;
    if (!symbol || symbol->timelines.empty())
        return QRectF(0, 0, 100, 100);

    // Over every frame of every layer: an animated symbol reaches wherever
    // any of its frames does, and culling by a first-frame box would drop
    // the rest.
    for (const fla::Timeline* timeline : symbol->timelines)
    {
        if (!timeline)
            continue;
        for (const fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->visible)
                continue;
            for (const fla::Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                for (const fla::Element* elem : frame->elements)
                {
                    if (!elem)
                        continue;
                    QRectF elemBounds = calculateElementLocalBounds(elem);
                    if (bounds.isNull())
                        bounds = elemBounds;
                    else
                        bounds = bounds.united(elemBounds);
                }
            }
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
        return TextLayout::localBounds(*static_cast<const fla::StaticText*>(element));
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
