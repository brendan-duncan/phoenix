#include "phoenix_view.h"
#include "theme.h"
#include "../edit/pegs.h"
#include "preferences.h"
#include "../edit/three_d.h"
#include "../edit/layer_effects.h"
#include "../edit/image_blur.h"

#include <optional>

#include "../edit/guide_command.h"
#include "../edit/timeline_edit.h"
#include "../edit/brush_texture.h"
#include "../edit/camera.h"
#include "../edit/graphic_loop.h"
#include "../parser/ik_tree.h"
#include "filters.h"
#include "stroke_effects.h"
#include "library_panel.h"
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>

#include "draw_placement.h"

#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/property_command.h"
#include "../edit/layer_commands.h"
#include "../edit/document_settings.h"
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
#include "../data/raw_element.h"
#include "paint_recorder.h"
#include "../data/linear_gradient.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/solid_color.h"
#include "../data/bitmap_decoder.h"
#include "../edit/transform_blend.h"
#include "../edit/tween_ease.h"
#include "../edit/shape_tween.h"
#include "../edit/safe_zones.h"
#include "../edit/ik_pose.h"
#include "../geom/fill_tracing.h"
#include "../edit/frame_commands.h"
#include "../geom/curve.h"
#include "../geom/nine_slice.h"
#include "../geom/shape_geometry.h"
#include "../data/bitmap_fill.h"
#include "../data/static_text.h"
#include "text_layout.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/frame.h"
#include "../data/morph_shape.h"
#include "../data/morph_curves.h"
#include <algorithm>
#include <set>
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
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QBuffer>
#include <QtMath>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static const int supersampleFactor = 2;

namespace {

/// Whether shapes' fills are drawn from their planar map rather than traced
/// from their edges: PHOENIX_RENDER_FROM_MAP=1. Off by default; see
/// fla::fillOutlinesFromMap.
bool renderFillsFromMap()
{
    static const bool enabled = qEnvironmentVariableIntValue("PHOENIX_RENDER_FROM_MAP") != 0;
    return enabled;
}

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
    if (!Preferences::layerHighlight())
        return Preferences::highlightColor();
    if (const fla::Layer* layer = owningLayer(element))
        return QColor(layer->color[0], layer->color[1], layer->color[2]);
    return QColor(0, 170, 255);
}

/// An oval primitive's outline: the whole ellipse, or the sector from its
/// start angle clockwise to its end angle, with a hole or a notch of the
/// inner radius (a percentage of the outer). Animate measures angles
/// clockwise from three o'clock; Qt's arcs run the other way.
/// An oval primitive with Close Path switched off and a part of the oval
/// cut away: Animate draws its arc as an open line, with no fill.
bool ovalIsOpen(const fla::OvalPrimitive& oval)
{
    bool closed = true;
    for (const auto& attribute : oval.extraAttributes)
    {
        if (attribute.first == "closePath")
            closed = attribute.second != "false";
    }
    const double sweep = std::fmod(std::fmod(oval.endAngle - oval.startAngle, 360.0) + 360.0, 360.0);
    return !closed && sweep != 0.0;
}

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
    if (ovalIsOpen(oval))
    {
        // Close Path off: the arcs alone, a line rather than a wedge.
        if (inner > 0.0)
        {
            path.arcMoveTo(hole, -oval.startAngle);
            path.arcTo(hole, -oval.startAngle, -sweep);
        }
        return path;
    }
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
    // The pasteboard is the theme's.
    const auto pasteboard = [this]() {
        QPalette colors = palette();
        colors.setColor(QPalette::Base, theme::color("stage.pasteboard"));
        setPalette(colors);
    };
    pasteboard();
    connect(theme::notifier(), &theme::Notifier::changed, this, pasteboard);

    // Keys reach the stage once it has been clicked, so Delete, Escape and the
    // nudge keys go to the tools rather than nowhere.
    setFocusPolicy(Qt::StrongFocus);
    // Input methods type into text; the view asks the tool when (see
    // inputMethodQuery).
    setAttribute(Qt::WA_InputMethodEnabled, true);

    // Enable mouse tracking for smooth panning
    setMouseTracking(true);
    // A pen hovering over the stage is followed too, so turning it over to
    // the eraser end switches tools before it touches.
    setTabletTracking(true);
    // Library items are dragged onto the stage to place them.
    setAcceptDrops(true);

    // Long enough that a continuous wheel gesture never refines mid-scroll,
    // short enough that the sharp frame feels immediate once it stops.
    _settleTimer = new QTimer(this);
    _settleTimer->setSingleShot(true);
    _settleTimer->setInterval(120);
    connect(_settleTimer, &QTimer::timeout, this, &PhoenixView::endInteraction);

    connect(_player, &Player::currentFrameChanged, this, &PhoenixView::onPlayerFrameChanged);
    // Inside a symbol the tools snap to the guides in the symbol's space.
    connect(this, &PhoenixView::scopeChanged, this, [this]() { pushGuidesToSnapper(); });
}

PhoenixView::~PhoenixView()
{
}

void PhoenixView::onPlayerFrameChanged(int frame)
{
    (void)frame;

    // An animated instance is as big as the frame it shows, so what
    // snapping lines up with is gathered again.
    _snapBoundsValid = false;

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
    _insideSelectedValid = false;
    update();
}

void PhoenixView::setDocument(const fla::FLADocument* document)
{
    _bitmapCache.clear(); // Clear bitmap cache when loading new document
    _paddedFills.clear();
    _bitmapsByName.clear();
    _textureTiles.clear();
    _motionCache.clear();
    _armatureCache.clear();
    clearCaches();
    _flaDocument = document;
    _panX = 0;
    _panY = 0;
    _zoom = 1.0;
    // A new document starts square to the screen, turning about its center.
    _rotation = 0.0;
    _rotationPivot = document && document->document
        ? QPointF(document->document->width / 2.0, document->document->height / 2.0) : QPointF();
    emit rotationChanged(0.0);
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

bool PhoenixView::exportFrameSvg(const QString& filePath, int frame, bool transparent, bool hidden, bool linked,
    const QRectF& wanted, QStringList* written)
{
    if (!_flaDocument || !_flaDocument->document)
        return false;
    fla::Document* document = _flaDocument->document;
    if (document->width <= 0 || document->height <= 0)
        return false;
    const QRectF area = wanted.isValid() && !wanted.isEmpty() ? wanted : QRectF(0, 0, document->width, document->height);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    {
        // SVG 1.1, which has clip paths: masks are written as them.
        QSvgGenerator generator(QSvgGenerator::SvgVersion::Svg11);
        generator.setOutputDevice(&buffer);
        generator.setSize(QSize(qCeil(area.width()), qCeil(area.height())));
        // CSS pixels, so the drawing is as wide as the stage.
        generator.setResolution(96);
        generator.setViewBox(area);
        generator.setTitle(QFileInfo(filePath).completeBaseName());
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QTransform savedView = viewTransform;
        const QRectF savedVisible = _visibleRect;
        const int savedOverride = _frameOverride;
        viewTransform = painter.transform();
        _visibleRect = area;
        _frameOverride = std::max(0, frame);
        _drawHidden = hidden;
        _exporting = true;
        _vectorMasks = true;
        if (!transparent)
            painter.fillRect(area, QColor(document->backgroundColor[0], document->backgroundColor[1],
                                       document->backgroundColor[2], document->backgroundColor[3]));
        drawDocument(painter, document);
        _exporting = false;
        _vectorMasks = false;
        _drawHidden = false;
        _frameOverride = savedOverride;
        viewTransform = savedView;
        _visibleRect = savedVisible;
        painter.end();
    }
    QString svg = QString::fromUtf8(buffer.data());
    if (linked)
    {
        // Each embedded image as a file beside the SVG, named for what it
        // holds, so one used on every frame is written once.
        static const QRegularExpression embedded("xlink:href=\"data:image/(png|jpeg|jpg);base64,([^\"]+)\"");
        const QDir folder = QFileInfo(filePath).absoluteDir();
        const QString stem = QFileInfo(filePath).completeBaseName().remove(QRegularExpression("\\d+$"));
        QString out;
        qsizetype last = 0;
        for (auto it = embedded.globalMatch(svg); it.hasNext();)
        {
            const QRegularExpressionMatch match = it.next();
            const QByteArray bytes = QByteArray::fromBase64(match.captured(2).toLatin1());
            const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(10));
            const QString name = QString("%1_image_%2.%3").arg(stem.isEmpty() ? QString("svg") : stem, hash,
                match.captured(1) == "png" ? QString("png") : QString("jpg"));
            if (!written || !written->contains(name))
            {
                QFile image(folder.filePath(name));
                if (image.open(QIODevice::WriteOnly))
                    image.write(bytes);
                if (written)
                    written->append(name);
            }
            out += svg.mid(last, match.capturedStart() - last) + "xlink:href=\"" + name + "\"";
            last = match.capturedEnd();
        }
        out += svg.mid(last);
        svg = out;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(svg.toUtf8());
    return true;
}

bool PhoenixView::exportToSvg(const QString& filePath)
{
    // The frame showing, as an export draws it: hidden layers left out,
    // images embedded, masks as clip paths.
    const int frame = _player ? _player->currentFrame() : 0;
    return exportFrameSvg(filePath, frame, false, false, false, QRectF());
}

QImage PhoenixView::renderFrame(int frame, const QSize& size, bool transparent)
{
    return renderFrame(frame, size, transparent, QRectF());
}

QRectF PhoenixView::framesBounds(int first, int last)
{
    if (!_flaDocument || !_flaDocument->document)
        return QRectF();
    const int was = _player ? _player->currentFrame() : 0;
    QRectF bounds;
    // At most a few hundred frames looked at, spread over the range.
    const int count = std::max(1, last - first + 1);
    const int step = std::max(1, count / 200);
    for (int frame = first; frame <= last; frame += step)
    {
        if (_player)
            _player->setCurrentFrame(frame);
        invalidateBounds();
        const QRectF here = contentBounds();
        bounds = bounds.isValid() ? bounds.united(here) : here;
    }
    if (_player)
        _player->setCurrentFrame(was);
    invalidateBounds();
    return bounds;
}

QImage PhoenixView::renderFrame(int frame, const QSize& size, bool transparent, const QRectF& wanted)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    // Too large to allocate: a null image, which every export checks for.
    if (image.isNull())
        return image;
    image.fill(Qt::transparent);
    if (!_flaDocument || !_flaDocument->document || size.isEmpty())
        return image;
    fla::Document* document = _flaDocument->document;
    if (document->width <= 0 || document->height <= 0)
        return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!transparent)
    {
        painter.fillRect(image.rect(), QColor(document->backgroundColor[0], document->backgroundColor[1],
            document->backgroundColor[2], document->backgroundColor[3]));
    }
    const QRectF area = wanted.isValid() && !wanted.isEmpty() ? wanted : QRectF(0, 0, document->width, document->height);
    painter.scale(static_cast<double>(size.width()) / area.width(), static_cast<double>(size.height()) / area.height());
    painter.translate(-area.left(), -area.top());

    const QTransform savedView = viewTransform;
    const QRectF savedVisible = _visibleRect;
    const int savedOverride = _frameOverride;
    viewTransform = painter.transform();
    _visibleRect = area;
    _frameOverride = std::max(0, frame);
    _exporting = true;
    drawDocument(painter, document);
    _exporting = false;
    // A review copy can carry the safe zones.
    if (_exportSafeZones)
        drawSafeZones(painter, painter.transform(), document);
    _frameOverride = savedOverride;
    viewTransform = savedView;
    _visibleRect = savedVisible;
    painter.end();
    return image;
}

std::vector<RecordedItem> PhoenixView::recordFrame(int frame, const QSize& size)
{
    PaintRecorder recorder(size);
    if (!_flaDocument || !_flaDocument->document || size.isEmpty())
        return recorder.items;
    fla::Document* document = _flaDocument->document;
    if (document->width <= 0 || document->height <= 0)
        return recorder.items;
    QPainter painter(&recorder);
    painter.scale(static_cast<double>(size.width()) / document->width, static_cast<double>(size.height()) / document->height);
    const QTransform savedView = viewTransform;
    const QRectF savedVisible = _visibleRect;
    const int savedOverride = _frameOverride;
    viewTransform = painter.transform();
    _visibleRect = QRectF(0, 0, document->width, document->height);
    _frameOverride = std::max(0, frame);
    _exporting = true;
    _vectorMasks = true;
    drawDocument(painter, document);
    _vectorMasks = false;
    _exporting = false;
    _frameOverride = savedOverride;
    viewTransform = savedView;
    _visibleRect = savedVisible;
    painter.end();
    return recorder.items;
}

QImage PhoenixView::renderElements(const std::vector<const fla::Element*>& elements, double scale)
{
    QRectF bounds;
    for (const fla::Element* element : elements)
    {
        const QRectF box = element ? elementBounds(element) : QRectF();
        if (box.isValid())
            bounds = bounds.isValid() ? bounds.united(box) : box;
    }
    if (!bounds.isValid() || bounds.isEmpty())
        return QImage();
    // A pixel of room for the edges' antialiasing, and a sane limit.
    bounds.adjust(-1.0, -1.0, 1.0, 1.0);
    scale = std::min(scale, 4096.0 / std::max(bounds.width(), bounds.height()));
    const QSize size(std::max(1, static_cast<int>(std::ceil(bounds.width() * scale))),
        std::max(1, static_cast<int>(std::ceil(bounds.height() * scale))));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(scale, scale);
    painter.translate(-bounds.topLeft());

    const QTransform savedView = viewTransform;
    const QRectF savedVisible = _visibleRect;
    viewTransform = painter.transform();
    _visibleRect = bounds;
    for (const fla::Element* element : elements)
    {
        if (!element)
            continue;
        painter.save();
        drawElement(painter, element, nullptr, 0.0);
        painter.restore();
    }
    viewTransform = savedView;
    _visibleRect = savedVisible;
    painter.end();
    return image;
}

QByteArray PhoenixView::svgOfElements(const std::vector<const fla::Element*>& elements)
{
    QRectF bounds;
    for (const fla::Element* element : elements)
    {
        const QRectF box = element ? elementBounds(element) : QRectF();
        if (box.isValid())
            bounds = bounds.isValid() ? bounds.united(box) : box;
    }
    if (!bounds.isValid() || bounds.isEmpty())
        return QByteArray();
    bounds.adjust(-1.0, -1.0, 1.0, 1.0);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QSvgGenerator generator;
    generator.setOutputDevice(&buffer);
    generator.setSize(bounds.size().toSize());
    generator.setViewBox(QRectF(QPointF(0, 0), bounds.size()));
    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(-bounds.topLeft());
    const QTransform savedView = viewTransform;
    const QRectF savedVisible = _visibleRect;
    viewTransform = painter.transform();
    _visibleRect = bounds;
    for (const fla::Element* element : elements)
    {
        if (!element)
            continue;
        painter.save();
        drawElement(painter, element, nullptr, 0.0);
        painter.restore();
    }
    viewTransform = savedView;
    _visibleRect = savedVisible;
    painter.end();
    return buffer.data();
}

QRectF PhoenixView::symbolFramesBounds(const fla::Symbol* symbol, int first, int last)
{
    QRectF bounds;
    if (!symbol || symbol->timelines.empty())
        return bounds;
    for (int frame = first; frame <= last; ++frame)
    {
        const QRectF here = shownSymbolBounds(symbol, fla::LoopType::SingleFrame, frame, 1);
        if (here.isValid())
            bounds = bounds.isValid() ? bounds.united(here) : here;
    }
    return bounds;
}

QImage PhoenixView::renderSymbolFrame(const fla::Symbol* symbol, int frame, const QRectF& area, double scale)
{
    if (!symbol || symbol->timelines.empty() || !area.isValid() || area.isEmpty())
        return QImage();
    const QSize size(std::max(1, static_cast<int>(std::ceil(area.width() * scale))),
        std::max(1, static_cast<int>(std::ceil(area.height() * scale))));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(scale, scale);
    painter.translate(-area.topLeft());
    const QTransform savedView = viewTransform;
    const QRectF savedVisible = _visibleRect;
    const bool wasExporting = _exporting;
    viewTransform = painter.transform();
    _visibleRect = area;
    _exporting = true;
    _clipSequences.clear();
    drawTimeline(painter, symbol->timelines.front(), fla::LoopType::SingleFrame, std::max(0, frame));
    _exporting = wasExporting;
    viewTransform = savedView;
    _visibleRect = savedVisible;
    painter.end();
    return image;
}

void PhoenixView::paintEvent(QPaintEvent *event)
{
    if (_zoom != _toldZoom || _rotation != _toldRotation)
    {
        _toldZoom = _zoom;
        _toldRotation = _rotation;
        emit zoomChanged();
    }

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
    if (_rotation != 0.0)
    {
        // Turned, the view sees a turned rectangle of the stage; its bounds
        // are what can show.
        bool invertible = false;
        const QTransform toStage = rootToWidget().inverted(&invertible);
        if (invertible)
            _visibleRect = toStage.mapRect(widgetRect);
    }

    double penWidth = 1.0 / scale;
    _radius = 2.0 / scale;
    _overlayPen = QPen(QColor(0, 255, 255, 255), penWidth);
    _overlayBrush = QBrush(QColor(0, 255, 255, 150));

    // Supersampling: render at 2x resolution and scale down for smoother
    // edges. The buffer is kept between paints (at 4K it is over 100 MB, too
    // much to allocate every time), and when there is no room for it the
    // stage is drawn straight to the window.
    bool supersampled = false;
    if (_highQualityAntiAliasing && !_interacting)
    {
        const QSize wanted(width() * supersampleFactor, height() * supersampleFactor);
        if (_supersample.size() != wanted)
            _supersample = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        supersampled = !_supersample.isNull();
    }
    if (supersampled)
    {
        QImage& buffer = _supersample;
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
            key.rotation = _rotation;
            key.pivot = _rotationPivot;
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

    // The grid over the art.
    if (_showGrid && _gridOver)
    {
        painter.save();
        painter.setTransform(rootToWidget(), true);
        drawGrid(painter, document);
        painter.restore();
    }

    // Off the stage hidden: by the pasteboard's color when content is
    // clipped, by the window's when there is no pasteboard.
    if ((_clipOutsideStage || !_showPasteboard) && !inSymbol())
    {
        QPainterPath outside;
        outside.addRect(rect());
        QPainterPath stage;
        stage.addPolygon(rootToWidget().map(QPolygonF(QRectF(0, 0, docWidth, docHeight))));
        stage.closeSubpath();
        painter.save();
        painter.fillPath(outside.subtracted(stage), _showPasteboard ? backgroundColor : palette().color(QPalette::Window));
        painter.restore();
    }

    // Draw dimming overlay outside document area if enabled
    if (_dimOutsideDocument)
    {
        QPainterPath dimPath;
        dimPath.addRect(rect());
        QPainterPath docPath;
        docPath.addPolygon(rootToWidget().map(QPolygonF(QRectF(0, 0, docWidth, docHeight))));
        docPath.closeSubpath();
        QPainterPath maskedPath = dimPath.subtracted(docPath);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillPath(maskedPath, QColor(0, 0, 0, 180));
        painter.restore();
    }

    // Safe zones and aspect-ratio frames, over the art and the dimming.
    if (_showSafeZones && !_exporting)
        drawSafeZones(painter, rootToWidget(), document);

    // Draw visible rect if debug mode is enabled
    if (_showBounds)
    {
        painter.save();
        painter.setTransform(rootToWidget(), true);
        painter.setPen(QPen(QColor(255, 0, 0, 100), 2.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(0, 0, docWidth, docHeight));

        drawDocumentBounds(painter, document);

        painter.restore();
    }

    if (!_exporting)
    {
        drawPegs(painter);
        drawDeformers(painter);
    }

    // Guides over the art, rulers over everything.
    drawRulersAndGuides(painter);
}

void PhoenixView::drawEffectLayer(QPainter& painter, const fla::Layer* layer, const fla::Layer* target,
    fla::LoopType loopType, int firstFrame, bool editing)
{
    const fla::effects::Effect effect = fla::effects::effectOf(*layer);
    const QSize size(painter.device()->width(), painter.device()->height());
    if (size.isEmpty())
        return;
    const QTransform toDevice = painter.transform();
    const auto render = [&](const fla::Layer* which) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter into(&image);
        into.setRenderHint(QPainter::Antialiasing, true);
        into.setTransform(toDevice);
        drawLayer(into, which, loopType, firstFrame, nullptr);
        return image;
    };

    QImage art = render(layer);
    // Softened by the blur, its width scaling with the view.
    const double scale = std::max(std::hypot(toDevice.m11(), toDevice.m12()), 1.0e-6);
    const double width = effect.blur * scale;
    if (width >= 1.0)
        fla::boxBlur(reinterpret_cast<uint32_t*>(art.bits()), art.width(), art.height(), width, width, 3);

    // Colored with the effect's color, as strong as the art's coverage, and
    // kept to the layer under it.
    QImage clip;
    if (effect.clip && target)
        clip = render(target);
    const double opacity = effect.opacity * effect.color[3] / 255.0;
    for (int y = 0; y < art.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(art.scanLine(y));
        const QRgb* inside = clip.isNull() ? nullptr : reinterpret_cast<const QRgb*>(clip.constScanLine(y));
        for (int x = 0; x < art.width(); ++x)
        {
            double alpha = qAlpha(line[x]) / 255.0 * opacity;
            if (!clip.isNull())
                alpha *= qAlpha(inside[x]) / 255.0;
            const int a = static_cast<int>(std::lround(alpha * 255.0));
            line[x] = qRgba(effect.color[0] * a / 255, effect.color[1] * a / 255, effect.color[2] * a / 255, a);
        }
    }

    painter.save();
    painter.resetTransform();
    switch (effect.kind)
    {
    case fla::effects::Kind::Tone: painter.setCompositionMode(QPainter::CompositionMode_Multiply); break;
    case fla::effects::Kind::Highlight: painter.setCompositionMode(QPainter::CompositionMode_Screen); break;
    case fla::effects::Kind::Glow: painter.setCompositionMode(QPainter::CompositionMode_Plus); break;
    }
    painter.drawImage(0, 0, art);
    painter.restore();

    // Drawing on it, the art itself shows faintly, to see what is drawn.
    if (editing && layer->selected)
    {
        painter.save();
        painter.setOpacity(painter.opacity() * 0.25);
        drawLayer(painter, layer, loopType, firstFrame, nullptr);
        painter.restore();
    }
}

void PhoenixView::setRenderPass(RenderPass pass)
{
    if (pass == _renderPass)
        return;
    _renderPass = pass;
    clearCaches();
}

void PhoenixView::setLightTable(bool on)
{
    _lightTable = on;
    clearCaches();
    update();
}

void PhoenixView::drawPegs(QPainter& painter)
{
    // A peg draws nothing of its own; while editing, its pivot shows as a
    // crosshair in a circle, in its layer's color, where its children turn.
    if (!_flaDocument || !_flaDocument->document)
        return;
    const fla::Timeline* timeline = scopeTimeline() ? scopeTimeline() : _flaDocument->document->currentScene();
    if (!timeline)
        return;
    const int frame = shownFrame();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const fla::Layer* layer : timeline->layers)
    {
        if (!layer || !layer->isVisible())
            continue;
        const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
        if (!state.frame)
            continue;
        for (size_t i = 0; i < state.frame->elements.size(); ++i)
        {
            const auto* instance = dynamic_cast<const fla::SymbolInstance*>(state.frame->elements[i]);
            if (!instance || !instance->symbol || !fla::pegs::isPeg(*instance->symbol))
                continue;
            const fla::Transform shown = shownTransform(state, i);
            const QTransform placed = QTransform(shown.m11, shown.m12, shown.m21, shown.m22, shown.tx, shown.ty) *
                                      parentingAt(layer, state.frame, frame);
            const QPointF at = stageToWidget(placed.map(QPointF(0.0, 0.0)));
            const QColor ink(layer->color[0], layer->color[1], layer->color[2]);
            painter.setTransform(QTransform());
            painter.setPen(QPen(QColor(0, 0, 0, 120), 3.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(at, 9.0, 9.0);
            painter.setPen(QPen(ink, 1.6));
            painter.drawEllipse(at, 9.0, 9.0);
            painter.drawLine(at + QPointF(-14.0, 0.0), at + QPointF(14.0, 0.0));
            painter.drawLine(at + QPointF(0.0, -14.0), at + QPointF(0.0, 14.0));
            // Which way it faces: a tick along its own x axis.
            const QPointF along = stageToWidget(placed.map(QPointF(24.0, 0.0))) - at;
            const double length = std::hypot(along.x(), along.y());
            if (length > 0.5)
                painter.drawLine(at, at + along / length * 22.0);
        }
    }
    painter.restore();
}

std::vector<PhoenixView::DeformerTarget> PhoenixView::deformerTargets()
{
    std::vector<DeformerTarget> targets;
    if (!_flaDocument || !_flaDocument->document || _exporting || !_activeTool || _activeTool != _modifierTool)
        return targets;
    const fla::Timeline* timeline = scopeTimeline() ? scopeTimeline() : _flaDocument->document->currentScene();
    if (!timeline)
        return targets;
    const int frame = shownFrame();
    for (const fla::Layer* layer : timeline->layers)
    {
        if (!layer || !layer->isVisible())
            continue;
        // On its keyframe only: in between, what shows is the tween's.
        const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
        if (!state.frame || state.offset != 0)
            continue;
        for (size_t i = 0; i < state.frame->elements.size(); ++i)
        {
            auto* instance = dynamic_cast<fla::SymbolInstance*>(state.frame->elements[i]);
            if (!instance || !_selection || !_selection->contains(instance))
                continue;
            std::optional<fla::deform::Deformer> deformer = fla::deform::of(*instance);
            if (!deformer)
                continue;
            const fla::Transform shown = shownTransform(state, i);
            DeformerTarget target;
            target.instance = instance;
            target.placed = QTransform(shown.m11, shown.m12, shown.m21, shown.m22, shown.tx, shown.ty) *
                            parentingAt(layer, state.frame, frame);
            target.deformer = std::move(*deformer);
            target.locked = layer->isLocked();
            targets.push_back(std::move(target));
        }
    }
    return targets;
}

void PhoenixView::drawDeformers(QPainter& painter)
{
    const std::vector<DeformerTarget> targets = deformerTargets();
    if (targets.empty())
        return;
    painter.save();
    painter.setTransform(QTransform());
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = theme::color("accent");
    for (const DeformerTarget& target : targets)
    {
        const std::vector<fla::Point>& posed = target.deformer.posed;
        const auto at = [&](size_t i) { return stageToWidget(target.placed.map(QPointF(posed[i].x, posed[i].y))); };
        QPainterPath path;
        if (target.deformer.kind == fla::deform::Kind::Curve)
        {
            // The spine, and each anchor's arms out to its handles.
            path.moveTo(at(0));
            for (size_t i = 1; i + 2 < posed.size(); i += 3)
                path.cubicTo(at(i), at(i + 1), at(i + 2));
            painter.setPen(QPen(ink, 1.0));
            for (size_t i = 1; i < posed.size(); i += 3)
            {
                painter.drawLine(at(i - 1), at(i));
                painter.drawLine(at(i + 1), at(i + 2));
            }
        }
        else
        {
            // The envelope's edges, exactly, and its inner rows and columns.
            const auto edge = [&](size_t a, size_t step) {
                path.moveTo(at(a));
                path.cubicTo(at(a + step), at(a + 2 * step), at(a + 3 * step));
            };
            edge(0, 1);
            edge(12, 1);
            edge(0, 4);
            edge(3, 4);
            painter.setPen(QPen(ink, 1.0, Qt::DotLine));
            for (size_t k = 1; k <= 2; ++k)
            {
                painter.drawPolyline(QPolygonF({at(4 * k), at(4 * k + 1), at(4 * k + 2), at(4 * k + 3)}));
                painter.drawPolyline(QPolygonF({at(k), at(k + 4), at(k + 8), at(k + 12)}));
            }
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 120), 3.5));
        painter.drawPath(path);
        painter.setPen(QPen(ink, 1.8));
        painter.drawPath(path);
        for (size_t i = 0; i < posed.size(); ++i)
        {
            const bool anchor = target.deformer.kind == fla::deform::Kind::Curve
                ? i % 3 == 0 : (i == 0 || i == 3 || i == 12 || i == 15);
            painter.setPen(QPen(ink, 1.5));
            painter.setBrush(target.locked ? QColor(128, 128, 128) : QColor(255, 255, 255));
            if (anchor)
                painter.drawRect(QRectF(at(i) - QPointF(4.0, 4.0), QSizeF(8.0, 8.0)));
            else
                painter.drawEllipse(at(i), 3.5, 3.5);
        }
    }
    painter.restore();
}

bool PhoenixView::beginDeformerDrag(const QPointF& widgetPos)
{
    for (const DeformerTarget& target : deformerTargets())
    {
        if (target.locked)
            continue;
        bool invertible = false;
        const QTransform toLocal = target.placed.inverted(&invertible);
        if (!invertible)
            continue;
        const std::vector<fla::Point>& posed = target.deformer.posed;
        // The handle nearest the press, within reach.
        double nearest = 7.0;
        int found = -1;
        for (size_t i = 0; i < posed.size(); ++i)
        {
            const QPointF at = stageToWidget(target.placed.map(QPointF(posed[i].x, posed[i].y)));
            const double distance = std::hypot(at.x() - widgetPos.x(), at.y() - widgetPos.y());
            if (distance <= nearest)
            {
                nearest = distance;
                found = static_cast<int>(i);
            }
        }
        if (found < 0)
            continue;
        _deformerDrag.instance = target.instance;
        _deformerDrag.toLocal = toLocal;
        _deformerDrag.index = static_cast<size_t>(found);
        _deformerDrag.grab = toLocal.map(widgetToStage(widgetPos));
        _deformerDrag.start = target.deformer;
        _deformerDrag.before = target.instance->extraAttributes;
        return true;
    }
    return false;
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
        // A see-through stage shows a checkerboard under its color, eight
        // screen pixels a square, as image editors show clear pixels.
        if (document->backgroundColor[3] < 255 && !_exporting)
        {
            QImage checks(16, 16, QImage::Format_RGB32);
            checks.fill(QColor(255, 255, 255));
            QPainter cells(&checks);
            cells.fillRect(0, 0, 8, 8, QColor(204, 204, 204));
            cells.fillRect(8, 8, 8, 8, QColor(204, 204, 204));
            cells.end();
            QBrush brush(checks);
            bool invertible = false;
            const QTransform toStage = painter.transform().inverted(&invertible);
            if (invertible)
                brush.setTransform(toStage);
            painter.fillRect(QRectF(0, 0, docWidth, docHeight), brush);
        }
        painter.fillRect(QRectF(0, 0, docWidth, docHeight),
            QColor(document->backgroundColor[0], document->backgroundColor[1],
                document->backgroundColor[2], document->backgroundColor[3]));

        if (_showGrid && !_gridOver)
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
        // The frames to show: so many either side, or so many keyframes
        // either side when only keyframes are wanted.
        std::vector<int> before;
        std::vector<int> after;
        if (_onionKeyframesOnly)
        {
            std::set<int> keys;
            const fla::Timeline* timeline = scopeTimeline() ? scopeTimeline() : document->currentScene();
            if (timeline)
            {
                for (const fla::Layer* layer : timeline->layers)
                {
                    if (!layer || !layer->isVisible() || layer->layerType == fla::Layer::Type::Guide)
                        continue;
                    for (const fla::Frame* frame : layer->frames)
                        keys.insert(frame->index);
                }
            }
            for (auto it = keys.rbegin(); it != keys.rend() && static_cast<int>(before.size()) < _onionBefore; ++it)
            {
                if (*it < current)
                    before.push_back(*it);
            }
            for (auto it = keys.begin(); it != keys.end() && static_cast<int>(after.size()) < _onionAfter; ++it)
            {
                if (*it > current)
                    after.push_back(*it);
            }
        }
        else
        {
            for (int k = 1; k <= _onionBefore && current - k >= 0; ++k)
                before.push_back(current - k);
            for (int k = 1; k <= _onionAfter; ++k)
                after.push_back(current + k);
        }
        // Furthest first, fading with the distance.
        for (int k = static_cast<int>(before.size()); k >= 1; --k)
            drawOnionFrame(painter, document, before[static_cast<size_t>(k - 1)], _onionBeforeColor,
                _onionOpacity * (1.0 - (k - 1.0) / (before.size() + 1.0)));
        for (int k = static_cast<int>(after.size()); k >= 1; --k)
            drawOnionFrame(painter, document, after[static_cast<size_t>(k - 1)], _onionAfterColor,
                _onionOpacity * (1.0 - (k - 1.0) / (after.size() + 1.0)));
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
        // library is shown on its own. A group being edited fades the rest
        // of its timeline too, and draws over it.
        const bool inGroup = _scope.back().group != nullptr;
        int symbolLevel = -1;
        for (size_t i = 0; i < _scope.size(); ++i)
        {
            if (!_scope[i].group)
                symbolLevel = static_cast<int>(i);
        }
        if (symbolLevel < 0 || _scope.front().instance)
        {
            painter.setOpacity(0.3);
            drawDocument(painter, document);
            painter.setOpacity(1.0);
        }

        painter.setTransform(scopeToDocument() * rootFull, false);
        if (symbolLevel >= 0 && _scope[static_cast<size_t>(symbolLevel)].timeline)
        {
            painter.setOpacity(inGroup ? 0.3 : 1.0);
            drawTimeline(painter, _scope[static_cast<size_t>(symbolLevel)].timeline, fla::LoopType::PlayOnce, 0);
            painter.setOpacity(1.0);
        }
        if (inGroup && _scope.back().frame)
        {
            for (const fla::Element* element : _scope.back().frame->elements)
            {
                if (element && element->visible && !_scope.back().outside.count(element))
                    drawElement(painter, element, nullptr, 0.0);
            }
        }
        painter.setTransform(rootFull, false);
    }

    // Edit Multiple Frames: the other keyframes in range, in full.
    if (_editMultiple && (_scope.empty() || !_scope.back().group) && _player)
    {
        const fla::Timeline* timeline = scopeTimeline() ? scopeTimeline() : document->currentScene();
        std::set<int> keys;
        for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            for (const fla::Frame* frame : multipleFrames(layer))
                keys.insert(frame->index);
        if (!keys.empty())
        {
            painter.setTransform(scopeToDocument() * rootFull, false);
            const int savedOverride = _frameOverride;
            for (int key : keys)
            {
                _frameOverride = key;
                _multiKeysOnly = true;
                drawTimeline(painter, timeline, fla::LoopType::PlayOnce, 0);
            }
            _multiKeysOnly = false;
            _frameOverride = savedOverride;
            painter.setTransform(rootFull, false);
        }
    }

    if (_drawPhase == DrawPhase::All || _drawPhase == DrawPhase::Above)
    {
        painter.setPen(QPen(QColor(0, 0, 0, 255), 1.0));
        painter.drawRect(QRectF(0, 0, docWidth, docHeight));
    }
}

void PhoenixView::setEditMultipleFrames(bool on)
{
    _editMultiple = on;
    _staticValid = false;
    update();
}

std::vector<fla::Frame*> PhoenixView::multipleFrames(const fla::Layer* layer) const
{
    std::vector<fla::Frame*> frames;
    if (!_editMultiple || !_player || !layer || !layer->isVisible() || layer->isLocked() ||
        layer->layerType == fla::Layer::Type::Folder)
        return frames;
    const int current = _player->currentFrame();
    for (fla::Frame* frame : layer->frames)
    {
        if (!frame || frame->index < current - _onionBefore || frame->index > current + _onionAfter)
            continue;
        if (frame->index <= current && current < frame->index + std::max(1, frame->duration))
            continue;
        frames.push_back(frame);
    }
    std::sort(frames.begin(), frames.end(), [current](const fla::Frame* a, const fla::Frame* b) {
        return std::abs(a->index - current) < std::abs(b->index - current);
    });
    return frames;
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

int PhoenixView::symbolFrameShown(const fla::SymbolInstance& instance, const fla::Frame& frame)
{
    ElapsedScope elapsed(_elapsed, std::max(0, shownFrame() - frame.index));
    return symbolFrame(instance);
}

void PhoenixView::setOnionSkin(bool on, int before, int after)
{
    _onionSkin = on;
    _onionBefore = std::max(0, before);
    _onionAfter = std::max(0, after);
    clearCaches();
    update();
}

void PhoenixView::setOnionStyle(bool outline, bool keyframesOnly, double opacity, const QColor& before,
    const QColor& after)
{
    _onionOutline = outline;
    _onionKeyframesOnly = keyframesOnly;
    _onionOpacity = std::clamp(opacity, 0.05, 1.0);
    _onionBeforeColor = before;
    _onionAfterColor = after;
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
        // In outline mode the frame's art is drawn as lines in the tint.
        const QColor savedForced = _forcedOutline;
        if (_onionOutline)
            _forcedOutline = tint;
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
        _forcedOutline = savedForced;
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

    // The clips' frame actions are read again each time, after any edit.
    _clipSequences.clear();
    // One scene at a time, as Animate shows them.
    const fla::Timeline* scene = document->currentScene();
    if (!scene || !scene->visible)
        return;
    // The camera's color effect is over everything it sees: the scene drawn
    // on its own, colored as one picture, and laid on the stage.
    const fla::ColorTransform color = _drawPhase == DrawPhase::Scan ? fla::ColorTransform() : cameraColor();
    QPaintDevice* device = painter.device();
    if (color.isIdentity() || !device)
    {
        drawTimeline(painter, scene);
        return;
    }
    const qreal ratio = std::max<qreal>(1.0, device->devicePixelRatioF());
    // An image or a pixmap measures in its own pixels, a widget in its
    // logical ones.
    QSizeF logical(device->width(), device->height());
    if (device->devType() == QInternal::Image || device->devType() == QInternal::Pixmap)
        logical /= ratio;
    QImage image(qCeil(logical.width() * ratio), qCeil(logical.height() * ratio), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        drawTimeline(painter, scene);
        return;
    }
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    {
        QPainter into(&image);
        into.setRenderHints(painter.renderHints());
        into.setTransform(painter.transform());
        drawTimeline(into, scene);
    }
    applyColorTransform(image, color);
    painter.save();
    painter.resetTransform();
    painter.drawImage(QPointF(0.0, 0.0), image);
    painter.restore();
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
    // For an export that writes vectors: each mask as the area it covers on
    // the device, to clip what it masks to.
    QMap<int, QPainterPath> vectorMasks;
    QTransform painterTransform = painter.transform();
    const QRect deviceRect(0, 0, painter.device()->width(), painter.device()->height());

    // On the timeline being edited, as on Animate's stage, guide layers show
    // and a mask masks only once it is locked; until then its own art shows
    // and the layers under it show whole. Anywhere else, and in an exported
    // movie, guides are left out and masks always mask.
    // At the top, with no symbol open, the scene shown is the one edited.
    const fla::Timeline* edited = scopeTimeline();
    if (!edited && _flaDocument && _flaDocument->document)
        edited = _flaDocument->document->currentScene();
    const bool editing = !_exporting && !_maskPass && timeline == edited;
    const auto masking = [editing](const fla::Layer* mask) { return !editing || mask->isLocked(); };
    // A layer's eye hides it where its timeline is shown for editing: the
    // scene, or the symbol open. Inside a symbol drawn on the stage, its
    // hidden layers draw, as the published movie has them.
    const bool hiding = _drawHidden ? false
        : timeline == edited || (_flaDocument && _flaDocument->document && timeline == _flaDocument->document->currentScene());
    const auto shown = [hiding](const fla::Layer* layer) { return !hiding || layer->isVisible(); };

    // First pass: render mask layers to pixmaps
    for (int i = 0; i < timeline->layers.size(); ++i)
    {
        const fla::Layer* layer = timeline->layers[i];
        if (layer->layerType != fla::Layer::Type::Mask || !shown(layer) || !masking(layer))
        {
            continue;
        }

        if (_vectorMasks)
        {
            // The mask's fills recorded as vectors rather than drawn.
            PaintRecorder recorder(deviceRect.size());
            QPainter recording(&recorder);
            recording.setTransform(painterTransform);
            // Restored rather than cleared: a mask inside a symbol drawn by
            // an outer mask pass is still in that pass.
            const bool wasMaskPass = _maskPass;
            _maskPass = true;
            drawLayer(recording, layer, loopType, firstFrame, nullptr);
            _maskPass = wasMaskPass;
            recording.end();
            vectorMasks[i] = coveredArea(recorder.items);
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
            ElapsedScope elapsed(_elapsed, shown.offset);
            for (const fla::Element* element : shown.frame->elements)
            {
                if (!element || !element->visible)
                    continue;
                // An instance by everything its symbol draws over all its
                // frames, as culling has it.
                const QRectF bounds = element->elementType() == fla::Element::Type::SymbolInstance
                    ? memberReach(element)
                    : getElementBounds(element);
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
        if (maskPixmap.isNull())
        {
            maskCache[i] = { QPixmap(), QRect() };
            continue;
        }
        maskPixmap.fill(Qt::transparent);

        QPainter maskPainter(&maskPixmap);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setRenderHint(QPainter::TextAntialiasing, true);
        maskPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        maskPainter.setTransform(painterTransform * QTransform::fromTranslate(-crop.x(), -crop.y()));

        const bool wasMaskPass = _maskPass;
        _maskPass = true;
        drawLayer(maskPainter, layer, loopType, firstFrame, nullptr);
        _maskPass = wasMaskPass;
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

    // The light table fades every layer but the one drawn on.
    const bool lightTable = editing && _lightTable &&
        std::any_of(timeline->layers.begin(), timeline->layers.end(), [](const fla::Layer* layer) { return layer && layer->selected; });
    struct Faded
    {
        QPainter& painter;
        Faded(QPainter& p, qreal opacity)
            : painter(p)
        {
            painter.save();
            painter.setOpacity(painter.opacity() * opacity);
        }
        ~Faded() { painter.restore(); }
    };

    // Second pass: draw layers with masks applied
    for (int i = timeline->layers.size() - 1; i >= 0; --i)
    {
        const fla::Layer* layer = timeline->layers[i];

        if (!shown(layer))
            continue;
        // One layer of the scene on its own, for a sequence per layer.
        if (_onlyLayer && layer != _onlyLayer &&
            std::find(timeline->layers.begin(), timeline->layers.end(), _onlyLayer) != timeline->layers.end())
            continue;
        std::optional<Faded> faded;
        if (lightTable && !layer->selected)
            faded.emplace(painter, 0.3);
        // An effect composites over the layer under it, on the stage and in
        // what is exported.
        if (fla::effects::isEffect(*layer))
        {
            const fla::Layer* target = nullptr;
            for (int j = i + 1; j < static_cast<int>(timeline->layers.size()) && !target; ++j)
            {
                const fla::Layer* below = timeline->layers[j];
                if (below && below->layerType != fla::Layer::Type::Folder && !fla::effects::isEffect(*below))
                    target = below;
            }
            drawEffectLayer(painter, layer, target, loopType, firstFrame, editing);
            continue;
        }
        if (layer->layerType == fla::Layer::Type::Mask)
        {
            // An unlocked mask, while editing, is art like any other.
            if (!masking(layer))
                drawLayer(painter, layer, loopType, firstFrame, nullptr);
            continue;
        }

        if (layer->layerType == fla::Layer::Type::Masked && _vectorMasks)
        {
            const auto mask = vectorMasks.find(layer->parentLayerIndex);
            if (mask != vectorMasks.end())
            {
                // Clipped to the mask's area: a clip path in an SVG.
                painter.save();
                painter.setTransform(QTransform());
                painter.setClipPath(mask.value(), Qt::IntersectClip);
                painter.setTransform(painterTransform);
                drawLayer(painter, layer, loopType, firstFrame, nullptr);
                painter.restore();
            }
            else
                drawLayer(painter, layer, loopType, firstFrame, nullptr);
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
                if (contentPixmap.isNull())
                    continue;
                contentPixmap.fill(Qt::transparent);

                QPainter contentPainter(&contentPixmap);
                contentPainter.setRenderHint(QPainter::Antialiasing, true);
                contentPainter.setRenderHint(QPainter::TextAntialiasing, true);
                contentPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                contentPainter.setTransform(painterTransform * QTransform::fromTranslate(-crop.x(), -crop.y()));

                drawLayer(contentPainter, layer, loopType, firstFrame, nullptr);

                // The content masked in place: no third image.
                contentPainter.resetTransform();
                contentPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                contentPainter.drawPixmap(0, 0, maskPixmap);
                contentPainter.end();

                // Draw to main painter
                painter.save();
                painter.resetTransform();
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawPixmap(crop.topLeft(), contentPixmap);
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
            // Guides are for the one editing: shown on its timeline, never
            // in what is published.
            if (editing)
                drawLayer(painter, layer, loopType, firstFrame, nullptr);
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
        // The frame asked for, as a graphic's own playhead (graphicFrame)
        // gives it: a layer that starts later shows nothing before it does.
        currentFrameIndex = firstFrame;
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
        currentFrameIndex = firstFrame + shownFrame();
    }

    for (const fla::Frame* frame : layer->frames)
    {
        if (frame->index <= currentFrameIndex)
        {
            currentFrame = frame;
        }
    }
    // Past a layer's last frame it shows nothing, though the timeline it is
    // in runs on: a symbol whose layers take turns shows one at a time.
    if (currentFrame && currentFrameIndex >= currentFrame->index + std::max(currentFrame->duration, 1))
        currentFrame = nullptr;

    if (currentFrame && currentFrame->tweenType != fla::TweenType::None)
    {
        int tweenStartIndex = currentFrame->index;
        int tweenEndIndex = currentFrame->index + currentFrame->duration;

        if (currentFrameIndex > tweenStartIndex && currentFrameIndex < tweenEndIndex)
        {
            tweenProgress = static_cast<double>(currentFrameIndex - tweenStartIndex) / currentFrame->duration;
            tweenProgress = fla::easeProgress(tweenProgress, currentFrame->acceleration,
                currentFrame->hasCustomEase ? currentFrame->customEase : std::vector<fla::Point>(),
                currentFrame->easeMethod);

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
    state.offset = currentFrame ? std::max(0, currentFrameIndex - currentFrame->index) : 0;
    return state;
}

const fla::MotionObject* PhoenixView::motionOf(const fla::Frame* frame)
{
    if (!frame || frame->otherTweenType != "motion object")
        return nullptr;
    const std::string* xml = nullptr;
    for (const std::string& child : frame->extraChildren)
    {
        const size_t at = child.find('<');
        if (at != std::string::npos && child.compare(at, 16, "<motionObjectXML") == 0)
            xml = &child;
    }
    if (!xml)
        return nullptr;
    CachedMotion& entry = _motionCache[frame];
    if (entry.motion && entry.revision == _motionRevision)
        return entry.motion.get();
    if (!entry.motion || entry.xml != *xml)
    {
        auto motion = std::make_shared<fla::MotionObject>();
        entry.xml = *xml;
        entry.motion = motion->parse(*xml) ? motion : nullptr;
    }
    entry.revision = _motionRevision;
    return entry.motion.get();
}

const PhoenixView::CachedArmature* PhoenixView::symbolArmatureOf(const fla::Frame* frame)
{
    if (!frame || frame->otherTweenType != "IK pose" || frame->duration <= 1)
        return nullptr;
    const std::string* xml = nullptr;
    for (const std::string& child : frame->extraChildren)
    {
        const size_t at = child.find('<');
        if (at != std::string::npos && child.compare(at, 7, "<IKTree") == 0)
            xml = &child;
    }
    if (!xml)
        return nullptr;
    CachedArmature& entry = _armatureCache[frame];
    if (!entry.parsed || entry.revision != _motionRevision)
    {
        entry.revision = _motionRevision;
        if (!entry.parsed || entry.xml != *xml)
        {
            entry.parsed = true;
            auto armature = std::make_shared<fla::IKArmature>();
            entry.xml = *xml;
            entry.joints.clear();
            entry.armature = armature->parse(*xml) && !armature->bendsShape() && armature->frames() > 1 ? armature : nullptr;
            for (int joint = 0; entry.armature && joint < static_cast<int>(armature->jointsAt(0).size()); ++joint)
            {
                const std::string reference = armature->referenceOf(joint);
                if (!reference.empty())
                    entry.joints.emplace(reference, joint);
            }
        }
    }
    return entry.armature ? &entry : nullptr;
}

bool PhoenixView::armatureTransform(const fla::Frame* frame, const fla::Element* element, int offset, fla::Transform& shown)
{
    const CachedArmature* entry = element && element->elementType() == fla::Element::Type::SymbolInstance
        ? symbolArmatureOf(frame) : nullptr;
    if (!entry)
        return false;
    for (const auto& attribute : static_cast<const fla::SymbolInstance*>(element)->extraAttributes)
    {
        if (attribute.first != "referenceID")
            continue;
        const auto joint = entry->joints.find(attribute.second);
        if (joint == entry->joints.end())
            return false;
        shown = entry->armature->carried(joint->second, element->transform, offset);
        return true;
    }
    return false;
}

fla::Transform PhoenixView::shownTransform(const LayerFrame& state, size_t elementIndex)
{
    if (!state.frame || elementIndex >= state.frame->elements.size() || !state.frame->elements[elementIndex])
        return fla::Transform();
    const fla::Element* element = state.frame->elements[elementIndex];
    // An armature of symbols places its instances from its joints.
    fla::Transform posed;
    if (armatureTransform(state.frame, element, state.offset, posed))
        return posed;
    if (const fla::MotionObject* motion = motionOf(state.frame))
    {
        const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
            ? _flaDocument->document->frameRate : 24.0;
        return fla::applyMotion(element->transform, element->transformationPoint, motion->at(0.0, rate),
            motion->at(state.offset, rate));
    }
    const std::vector<int> pairs = state.tweenFrame && state.frame->tweenType == fla::TweenType::Motion
        ? fla::pairTweenElements(state.frame->elements, state.tweenFrame->elements) : std::vector<int>();
    const int paired = elementIndex < pairs.size() ? pairs[elementIndex] : -1;
    if (paired >= 0 && state.tweenFrame->elements[static_cast<size_t>(paired)])
    {
        const fla::Element* end = state.tweenFrame->elements[static_cast<size_t>(paired)];
        const fla::Transform tweened = interpolateTransform(element->transform, end->transform, state.tweenProgress);
        return guidedTransform(state, *element, *end, tweened);
    }
    return element->transform;
}

const fla::GuidePath* PhoenixView::guidePathOf(const fla::Frame* frame)
{
    const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    const fla::Layer* guide = layer ? layer->parentLayer : nullptr;
    if (!guide || guide->layerType != fla::Layer::Type::Guide)
        return nullptr;
    const fla::Frame* guideFrame = resolveLayerFrame(guide, fla::LoopType::PlayOnce, 0).frame;
    if (!guideFrame)
        return nullptr;
    std::vector<const fla::Shape*> shapes;
    for (const fla::Element* element : guideFrame->elements)
    {
        if (element && element->elementType() == fla::Element::Type::Shape)
            shapes.push_back(static_cast<const fla::Shape*>(element));
    }
    if (shapes.empty())
        return nullptr;
    // Kept per guide frame while its shapes are the same ones.
    auto& entry = _guideCache[guideFrame];
    if (!entry.second || entry.first != shapes)
    {
        auto path = std::make_shared<fla::GuidePath>();
        path->build(shapes);
        entry.first = shapes;
        entry.second = path->empty() ? nullptr : path;
    }
    return entry.second.get();
}

fla::Transform PhoenixView::guidedTransform(const LayerFrame& state, const fla::Element& start,
    const fla::Element& end, const fla::Transform& tweened)
{
    if (!state.frame || state.frame->tweenType != fla::TweenType::Motion)
        return tweened;
    const fla::GuidePath* path = guidePathOf(state.frame);
    if (!path)
        return tweened;
    // The transformation point rides the guide from the place on it nearest
    // where it starts to the one nearest where it ends.
    const fla::Point& pivot = start.transformationPoint;
    const auto placed = [&pivot](const fla::Transform& t) {
        return fla::Point(t.m11 * pivot.x + t.m21 * pivot.y + t.tx, t.m12 * pivot.x + t.m22 * pivot.y + t.ty);
    };
    const double from = path->nearest(placed(start.transform));
    const double to = path->nearest(placed(end.transform));
    const double along = from + (to - from) * state.tweenProgress;
    const fla::Point on = path->at(along);
    const fla::Point at = placed(tweened);
    fla::Transform moved = tweened;
    moved.tx += on.x - at.x;
    moved.ty += on.y - at.y;

    bool orient = false;
    for (const auto& attribute : state.frame->extraAttributes)
        orient = orient || (attribute.first == "motionTweenOrientToPath" && attribute.second == "true");
    if (orient)
    {
        // Turned as the path turns since the start, about the point.
        const double turn = (path->angleAt(along) - path->angleAt(from)) * M_PI / 180.0;
        const QTransform about = QTransform::fromTranslate(-on.x, -on.y) * QTransform().rotateRadians(turn) *
                                 QTransform::fromTranslate(on.x, on.y);
        const QTransform result = QTransform(moved.m11, moved.m12, moved.m21, moved.m22, moved.tx, moved.ty) * about;
        moved = fla::Transform(result.m11(), result.m12(), result.m21(), result.m22(), result.dx(), result.dy());
    }
    return moved;
}

bool PhoenixView::motionHere(const fla::Element* element, MotionHere& here)
{
    fla::Frame* frame = element ? dynamic_cast<fla::Frame*>(element->parent) : nullptr;
    const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    const fla::MotionObject* motion = motionOf(frame);
    if (!layer || !motion)
        return false;
    const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
    if (state.frame != frame)
        return false;
    const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
    const double frames = motion->frames(rate);
    here.frame = frame;
    here.offset = state.offset;
    here.time = frames > 0.0 ? motion->duration() * state.offset / frames : 0.0;
    here.start = motion->at(0.0, rate);
    here.now = motion->at(state.offset, rate);
    return true;
}

bool PhoenixView::armatureHere(const fla::Element* element, fla::Transform& shown)
{
    const fla::Frame* frame = element ? dynamic_cast<const fla::Frame*>(element->parent) : nullptr;
    const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    if (!layer || !symbolArmatureOf(frame))
        return false;
    const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
    return state.frame == frame && armatureTransform(frame, element, state.offset, shown);
}

fla::Transform PhoenixView::shownTransformOf(const fla::Element* element)
{
    MotionHere here;
    if (!element)
        return fla::Transform();
    fla::Transform posed;
    if (armatureHere(element, posed))
        return posed;
    if (!motionHere(element, here))
        return element->transform;
    return fla::applyMotion(element->transform, element->transformationPoint, here.start, here.now);
}

fla::Transform PhoenixView::storedTransformFor(const fla::Element* element, const fla::Transform& shown)
{
    MotionHere here;
    fla::Transform posed;
    if (element && armatureHere(element, posed))
    {
        // The joints' move undone: shown, back off where the stored matrix
        // shows, onto the stored matrix.
        const auto q = [](const fla::Transform& t) { return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty); };
        bool invertible = false;
        const QTransform back = q(posed).inverted(&invertible);
        if (!invertible)
            return shown;
        const QTransform stored = q(shown) * back * q(element->transform);
        return fla::Transform(stored.m11(), stored.m12(), stored.m21(), stored.m22(), stored.dx(), stored.dy());
    }
    if (!element || !motionHere(element, here))
        return shown;
    return fla::unapplyMotion(shown, element->transformationPoint, here.start, here.now);
}

fla::CommandPtr PhoenixView::transformCommand(fla::Element* element, const fla::Transform& before,
    const fla::Transform& after, const std::string& name)
{
    MotionHere here;
    std::string* xml = element && motionHere(element, here) ? fla::timeline::motionOf(*here.frame) : nullptr;
    if (!xml)
        return fla::CommandPtr(new fla::SetElementTransformCommand(element, before, after, name));

    std::vector<std::string> children = here.frame->extraChildren;
    std::string* edited = nullptr;
    for (std::string& child : children)
    {
        if (child.compare(0, 16, "<motionObjectXML") == 0)
            edited = &child;
    }
    const fla::Point& pivot = element->transformationPoint;
    fla::Transform stored = before;
    if (here.offset > 0)
    {
        // A property keyframe here holding what the edit shows.
        const fla::Transform wanted = fla::applyMotion(after, pivot, here.start, here.now);
        fla::motion::setKeys(*edited, here.time, fla::motion::solve(before, pivot, here.start, wanted, here.now));
    }
    else
    {
        // The start itself: its keyframes take the new turn, skew and scale,
        // and the later positions stay where they were.
        fla::MotionState start = fla::motion::stateOf(after);
        start.rotation = here.start.rotation + std::remainder(start.rotation - here.start.rotation, 360.0);
        fla::motion::setKeys(*edited, 0.0, start);
        const double dx = (after.m11 * pivot.x + after.m21 * pivot.y + after.tx) -
                          (before.m11 * pivot.x + before.m21 * pivot.y + before.tx);
        const double dy = (after.m12 * pivot.x + after.m22 * pivot.y + after.ty) -
                          (before.m12 * pivot.x + before.m22 * pivot.y + before.ty);
        fla::motion::moveStart(*edited, dx, dy);
        stored = after;
    }
    auto* command = new fla::CompoundCommand(name);
    command->addCommand(fla::CommandPtr(new fla::SetElementTransformCommand(element, before, stored, name)));
    command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
        here.frame->extraChildren, here.frame->extraChildren, children, name)));
    return fla::CommandPtr(command);
}

fla::CommandPtr PhoenixView::colorCommand(fla::SymbolInstance* instance, const fla::ColorTransform& before,
    const fla::ColorTransform& after, const std::string& name)
{
    MotionHere here;
    if (!instance || !motionHere(instance, here) || here.offset <= 0)
        return fla::CommandPtr(new fla::SetPropertyCommand<fla::ColorTransform>(
            instance->colorTransform, before, after, name));
    std::vector<std::string> children = here.frame->extraChildren;
    for (std::string& child : children)
    {
        if (child.compare(0, 16, "<motionObjectXML") != 0)
            continue;
        fla::MotionObject motion;
        if (!motion.parse(child))
            continue;
        const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
            ? _flaDocument->document->frameRate : 24.0;
        // What the tween shows here, and what the edit made of it.
        const fla::ColorTransform shown = motion.colorAt(here.offset, rate, before);
        for (int i = 0; i < fla::motion::kColorChannelCount; ++i)
        {
            const std::string id = fla::motion::kColorChannels[i].id;
            const double was = fla::motion::colorValue(before, id);
            const double now = fla::motion::colorValue(after, id);
            if (std::fabs(was - now) < 1.0e-9)
                continue;
            std::vector<fla::motion::PropertyKey> keys = fla::motion::keysOf(child, id);
            if (keys.empty())
                keys.push_back(fla::motion::PropertyKey::at(0.0, was));
            const size_t index = fla::motion::splitAt(keys, here.time);
            // The keyframe takes the change the edit made to what showed.
            const double value = fla::motion::colorValue(shown, id) + (now - was);
            const double change = value - keys[index].value;
            keys[index].value = value;
            keys[index].nextValue += change;
            keys[index].previousValue += change;
            fla::motion::setKeysOf(child, id, keys);
        }
    }
    auto* command = new fla::CompoundCommand(name);
    command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<fla::ColorTransform>(
        instance->colorTransform, before, before, name)));
    command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
        here.frame->extraChildren, here.frame->extraChildren, children, name)));
    return fla::CommandPtr(command);
}

fla::CommandPtr PhoenixView::filtersCommand(fla::SymbolInstance* instance, const std::vector<fla::Filter>& before,
    const std::vector<fla::Filter>& after, const std::string& name)
{
    MotionHere here;
    bool sameKinds = before.size() == after.size();
    for (size_t i = 0; sameKinds && i < before.size(); ++i)
        sameKinds = before[i].type == after[i].type;
    if (!instance || !sameKinds || !motionHere(instance, here) || here.offset <= 0)
        return fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::Filter>>(
            instance->filters, before, after, name));

    // The numbers a tween animates are keyed here; anything else (a color,
    // a switch) is the filter's own, for the whole span.
    std::vector<fla::Filter> kept = after;
    std::vector<std::string> children = here.frame->extraChildren;
    for (std::string& child : children)
    {
        if (child.compare(0, 16, "<motionObjectXML") != 0)
            continue;
        fla::MotionObject motion;
        if (!motion.parse(child))
            continue;
        const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
            ? _flaDocument->document->frameRate : 24.0;
        const std::vector<fla::Filter> shown = motion.filtersAt(here.offset, rate, before);
        for (size_t i = 0; i < before.size(); ++i)
        {
            for (const std::string& field : fla::motion::filterFields(before[i]))
            {
                const double was = fla::motion::filterValue(before[i], field);
                const double now = fla::motion::filterValue(after[i], field);
                if (std::fabs(was - now) < 1.0e-9)
                    continue;
                const std::string id = fla::motion::filterPropertyId(before[i], i, field);
                std::vector<fla::motion::PropertyKey> keys = fla::motion::keysOf(child, id);
                if (keys.empty())
                    keys.push_back(fla::motion::PropertyKey::at(0.0, was));
                const size_t index = fla::motion::splitAt(keys, here.time);
                const double value = fla::motion::filterValue(shown[i], field) + (now - was);
                const double change = value - keys[index].value;
                keys[index].value = value;
                keys[index].nextValue += change;
                keys[index].previousValue += change;
                fla::motion::setKeysOf(child, id, keys);
                // The filter keeps its own value; the curve has the new one.
                fla::Filter& filter = kept[i];
                const fla::Filter& original = before[i];
                if (field == "blurX") filter.blurX = original.blurX;
                else if (field == "blurY") filter.blurY = original.blurY;
                else if (field == "strength") filter.strength = original.strength;
                else if (field == "distance") filter.distance = original.distance;
                else if (field == "angle") filter.angle = original.angle;
                else if (field == "brightness") filter.brightness = original.brightness;
                else if (field == "contrast") filter.contrast = original.contrast;
                else if (field == "saturation") filter.saturation = original.saturation;
                else if (field == "hue") filter.hue = original.hue;
            }
        }
    }
    auto* command = new fla::CompoundCommand(name);
    command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::Filter>>(
        instance->filters, before, kept, name)));
    command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
        here.frame->extraChildren, here.frame->extraChildren, children, name)));
    return fla::CommandPtr(command);
}

fla::Frame* PhoenixView::shapeHintTween(bool& start)
{
    fla::Frame* frame = activeFrame();
    const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    const int shown = shownFrame();
    if (!layer || frame->index != shown)
        return nullptr;
    const auto keyAt = [layer](int index) -> fla::Frame* {
        for (fla::Frame* candidate : layer->frames)
            if (candidate && candidate->index == index)
                return candidate;
        return nullptr;
    };
    if (frame->tweenType == fla::TweenType::Shape && keyAt(frame->index + std::max(1, frame->duration)))
    {
        start = true;
        return frame;
    }
    for (fla::Frame* previous : layer->frames)
    {
        if (previous && previous->tweenType == fla::TweenType::Shape &&
            previous->index + std::max(1, previous->duration) == shown)
        {
            start = false;
            return previous;
        }
    }
    return nullptr;
}

bool PhoenixView::beginHintDrag(const QPointF& documentPos)
{
    bool start = true;
    fla::Frame* tween = shapeHintTween(start);
    if (!tween)
        return false;
    const std::vector<fla::ShapeHint> hints = fla::hintsOf(*tween);
    const double reach = 9.0 / std::max(_zoom, 1.0e-6);
    for (size_t i = hints.size(); i-- > 0;)
    {
        const fla::Point& p = start ? hints[i].start : hints[i].end;
        if (QLineF(QPointF(p.x, p.y), documentPos).length() <= reach)
        {
            _hintDrag = HintDrag();
            _hintDrag.tween = tween;
            _hintDrag.start = start;
            _hintDrag.index = static_cast<int>(i);
            _hintDrag.before = tween->extraAttributes;
            return true;
        }
    }
    return false;
}

void PhoenixView::dragHint(const QPointF& documentPos)
{
    if (!_hintDrag.tween)
        return;
    std::vector<fla::ShapeHint> hints = fla::hintsOf(*_hintDrag.tween);
    if (_hintDrag.index < 0 || _hintDrag.index >= static_cast<int>(hints.size()))
        return;
    fla::Point& p = _hintDrag.start ? hints[static_cast<size_t>(_hintDrag.index)].start
                                    : hints[static_cast<size_t>(_hintDrag.index)].end;
    p = fla::Point(documentPos.x(), documentPos.y());
    fla::setHints(_hintDrag.tween->extraAttributes, hints);
    clearCaches();
    update();
}

void PhoenixView::endHintDrag(bool commit)
{
    if (!_hintDrag.tween)
        return;
    fla::Frame* tween = _hintDrag.tween;
    std::vector<fla::ShapeHint> hints = fla::hintsOf(*tween);
    const auto before = _hintDrag.before;
    const int index = _hintDrag.index;
    const bool start = _hintDrag.start;
    _hintDrag = HintDrag();
    // Dropped off the stage, the hint goes, as in Animate.
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (commit && document && index >= 0 && index < static_cast<int>(hints.size()) && !inSymbol())
    {
        const fla::Point& p = start ? hints[static_cast<size_t>(index)].start : hints[static_cast<size_t>(index)].end;
        if (p.x < 0.0 || p.y < 0.0 || p.x > document->width || p.y > document->height)
        {
            hints.erase(hints.begin() + index);
            fla::setHints(tween->extraAttributes, hints);
        }
    }
    const auto after = tween->extraAttributes;
    tween->extraAttributes = before;
    if (commit && after != before && _commandStack)
    {
        _commandStack->push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
            tween->extraAttributes, before, after, "Move Shape Hint")));
        _commandStack->breakMergeChain();
    }
    clearCaches();
    update();
}

std::vector<PhoenixView::MotionPath> PhoenixView::motionPaths()
{
    std::vector<MotionPath> paths;
    if (!_selection)
        return paths;
    const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
    for (fla::DOMElement* selected : _selection->elements())
    {
        auto* element = dynamic_cast<fla::Element*>(selected);
        MotionHere here;
        if (!element || !motionHere(element, here))
            continue;
        const fla::MotionObject* motion = motionOf(here.frame);
        const fla::Layer* layer = dynamic_cast<const fla::Layer*>(here.frame->parent);
        if (!motion || !layer)
            continue;
        MotionPath path;
        path.element = element;
        path.frame = here.frame;
        path.current = here.offset;
        path.color = QColor(layer->color[0], layer->color[1], layer->color[2]);
        const fla::Transform& t = element->transform;
        const fla::Point& p = element->transformationPoint;
        const QPointF pivot(t.m11 * p.x + t.m21 * p.y + t.tx, t.m12 * p.x + t.m22 * p.y + t.ty);
        for (int k = 0; k < here.frame->duration; ++k)
        {
            const fla::MotionState state = motion->at(k, rate);
            path.points.push_back(pivot + QPointF(state.x - here.start.x, state.y - here.start.y));
        }
        // The frames with keyframes.
        const double frames = motion->frames(rate);
        for (double time : motion->keyTimes())
        {
            const int key = frames > 0.0 && motion->duration() > 0.0
                ? static_cast<int>(std::lround(time / motion->duration() * frames)) : 0;
            if (key >= 0 && key < here.frame->duration &&
                std::find(path.keys.begin(), path.keys.end(), key) == path.keys.end())
                path.keys.push_back(key);
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

bool PhoenixView::beginMotionPathDrag(const QPointF& documentPos)
{
    const double reach = 5.0 / std::max(_zoom, 1.0e-6);
    for (const MotionPath& path : motionPaths())
    {
        // The nearest point of the path within reach, keyframes first.
        int hit = -1;
        double best = reach;
        for (int pass = 0; pass < 2 && hit < 0; ++pass)
        {
            for (int k = 0; k < static_cast<int>(path.points.size()); ++k)
            {
                const bool key = std::find(path.keys.begin(), path.keys.end(), k) != path.keys.end();
                if ((pass == 0) != key)
                    continue;
                const double d = QLineF(path.points[k], documentPos).length();
                if (d <= best)
                {
                    best = d;
                    hit = k;
                }
            }
        }
        if (hit < 0)
            continue;
        _pathDrag = PathDrag();
        _pathDrag.element = path.element;
        _pathDrag.frame = path.frame;
        _pathDrag.index = hit;
        _pathDrag.key = std::find(path.keys.begin(), path.keys.end(), hit) != path.keys.end();
        _pathDrag.grab = documentPos;
        _pathDrag.children = path.frame->extraChildren;
        _pathDrag.transform = path.element->transform;
        return true;
    }
    return false;
}

void PhoenixView::dragMotionPath(const QPointF& documentPos)
{
    if (!_pathDrag.element)
        return;
    fla::Frame* frame = _pathDrag.frame;
    fla::Element* element = _pathDrag.element;
    frame->extraChildren = _pathDrag.children;
    element->transform = _pathDrag.transform;
    std::string* xml = fla::timeline::motionOf(*frame);
    fla::MotionObject motion;
    if (!xml || !motion.parse(*xml))
        return;
    const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
    const QPointF delta = documentPos - _pathDrag.grab;
    const double frames = motion.frames(rate);
    const double time = frames > 0.0 ? motion.duration() * _pathDrag.index / frames : 0.0;
    const fla::MotionState state = motion.at(_pathDrag.index, rate);
    if (_pathDrag.index == 0)
    {
        // The start: the object moves, the rest of the path stays.
        element->transform.tx += delta.x();
        element->transform.ty += delta.y();
        fla::motion::moveStart(*xml, delta.x(), delta.y());
    }
    else if (_pathDrag.key)
    {
        // A keyframe moves where it is.
        fla::MotionState moved = state;
        moved.x += delta.x();
        moved.y += delta.y();
        fla::motion::setKeys(*xml, time, moved);
    }
    else
    {
        // Between keyframes the path bends.
        // A position keyed only before here is keyed on the last frame.
        const double last = frames > 0.0 ? motion.duration() * (frame->duration - 1) / frames : motion.duration();
        fla::motion::bend(*xml, time, last, state.x + delta.x(), state.y + delta.y());
    }
    invalidateBounds();
    update();
}

void PhoenixView::endMotionPathDrag(bool commit)
{
    if (!_pathDrag.element)
        return;
    fla::Frame* frame = _pathDrag.frame;
    fla::Element* element = _pathDrag.element;
    const std::vector<std::string> children = frame->extraChildren;
    const fla::Transform transform = element->transform;
    frame->extraChildren = _pathDrag.children;
    element->transform = _pathDrag.transform;
    const PathDrag drag = _pathDrag;
    _pathDrag = PathDrag();
    if (commit && _commandStack && children != drag.children)
    {
        const char* name = drag.index == 0 || drag.key ? "Move Path Keyframe" : "Bend Motion Path";
        auto* command = new fla::CompoundCommand(name);
        command->addCommand(fla::CommandPtr(new fla::SetElementTransformCommand(element, drag.transform, transform, name)));
        command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
            frame->extraChildren, drag.children, children, name)));
        _commandStack->push(fla::CommandPtr(command));
        _commandStack->breakMergeChain();
    }
    invalidateBounds();
    update();
}

QTransform PhoenixView::parentTransform(const fla::Layer* layer, const fla::Frame* frame, fla::LoopType loopType,
    int firstFrame, int depth)
{
    // The scene's layers are seen through its camera, once, outermost.
    if (depth == 0)
        return rigTransform(layer, frame, loopType, firstFrame, 0) * cameraTransform(layer);
    return rigTransform(layer, frame, loopType, firstFrame, depth);
}

QTransform PhoenixView::cameraTransform(const fla::Layer* layer)
{
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    const fla::Timeline* scene = document ? document->currentScene() : nullptr;
    if (!_cameraView || !layer || !scene || layer->parent != scene || fla::camera::isCamera(*layer))
        return QTransform();
    if (!fla::camera::cameraOf(*scene))
        return QTransform();
    const int frame = _frameDepth <= 1 ? shownFrame() : (_player ? _player->currentFrame() : 0);
    const fla::camera::State state = fla::camera::stateAt(*scene, frame, document->width, document->height);
    fla::Transform t;
    if (!fla::camera::view(state, fla::camera::depthOf(*layer), document->width, document->height, t))
        return QTransform(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

fla::ColorTransform PhoenixView::cameraColor()
{
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    const fla::Timeline* scene = document ? document->currentScene() : nullptr;
    if (!_cameraView || !scene || !fla::camera::cameraOf(*scene))
        return fla::ColorTransform();
    return fla::camera::stateAt(*scene, shownFrame(), document->width, document->height).color;
}

QTransform PhoenixView::parentingAt(const fla::Layer* layer, const fla::Frame* frame, int index)
{
    const int saved = _frameOverride;
    _frameOverride = std::max(0, index);
    const QTransform result = rigTransform(layer, frame, fla::LoopType::PlayOnce, 0, 0);
    _frameOverride = saved;
    return result;
}

void PhoenixView::setCameraView(bool on)
{
    _cameraView = on;
    clearCaches();
    update();
}

QTransform PhoenixView::rigTransform(const fla::Layer* layer, const fla::Frame* frame, fla::LoopType loopType,
    int firstFrame, int depth)
{
    if (!layer || !frame || depth > 16)
        return QTransform();
    int rig = -1;
    for (const auto& attribute : frame->extraAttributes)
        if (attribute.first == "parentLayerIndex")
            rig = std::atoi(attribute.second.c_str());
    const fla::Timeline* timeline = dynamic_cast<const fla::Timeline*>(layer->parent);
    if (rig < 0 || !timeline)
        return QTransform();
    for (const fla::Layer* candidate : timeline->layers)
    {
        if (!candidate || candidate == layer)
            continue;
        bool match = false;
        for (const auto& attribute : candidate->extraAttributes)
            match = match || (attribute.first == "layerRiggingIndex" && std::atoi(attribute.second.c_str()) == rig);
        if (!match)
            continue;
        const LayerFrame parent = resolveLayerFrame(candidate, loopType, firstFrame);
        if (!parent.frame || parent.frame->elements.empty())
            return QTransform();
        const fla::Transform t = shownTransform(parent, 0);
        const QTransform world = QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty) *
            rigTransform(candidate, parent.frame, loopType, firstFrame, depth + 1);
        // The parent's scale, skew or flip, as the layer takes them on.
        const fla::layers::ParentPropagation propagation = fla::layers::propagationOf(*layer);
        if (propagation.all())
            return world;
        const fla::Transform kept = fla::layers::propagatedTransform(
            fla::Transform(world.m11(), world.m12(), world.m21(), world.m22(), world.dx(), world.dy()), propagation);
        return QTransform(kept.m11, kept.m12, kept.m21, kept.m22, kept.tx, kept.ty);
    }
    return QTransform();
}

void PhoenixView::drawLayer(QPainter& painter, const fla::Layer* layer, fla::LoopType loopType, int firstFrame, const QPixmap* maskPixmap)
{
    // Edit Multiple Frames draws a layer's other keyframes, one at a time.
    if (_multiKeysOnly && _frameDepth == 0)
    {
        const std::vector<fla::Frame*> others = multipleFrames(layer);
        const bool keyed = std::any_of(others.begin(), others.end(),
            [this](const fla::Frame* frame) { return frame->index == _frameOverride; });
        if (!keyed)
            return;
    }

    // Onion skin leaves locked layers out, as Animate does, so a locked
    // background is not drawn again faded under every frame.
    if (_frameOverride >= 0 && !_exporting && layer->isLocked() && !inSymbol())
        return;

    QColor color;
    color.setRgb(layer->color[0], layer->color[1], layer->color[2], layer->color[3]);
    painter.setPen(QPen(color, 1.0));

    // Outline view draws the layer's art as lines in the layer color. It is
    // a way of looking at the timeline being edited: a layer saved outlined
    // inside a symbol draws normally where the symbol is placed, and an
    // exported movie has no outlines. A mask is coverage whatever the view,
    // so it is never outlined.
    const QColor previousOutline = _outlineColor;
    if (layer->outline && !_maskPass && !_exporting && _frameDepth == 0)
        _outlineColor = color;
    if (_forcedOutline.isValid() && !_maskPass)
        _outlineColor = _forcedOutline;

    const LayerFrame state = resolveLayerFrame(layer, loopType, firstFrame);

    // A transparent layer shows faded while editing, so what is under it
    // shows through; exports draw it in full.
    const int transparency = _exporting || _frameDepth != 0 || _maskPass ? 0 : fla::layers::transparencyOf(*layer);
    const double opacityBefore = painter.opacity();
    if (transparency > 0)
        painter.setOpacity(opacityBefore * transparency / 100.0);
    struct OpacityScope
    {
        QPainter& painter;
        double opacity;
        ~OpacityScope() { painter.setOpacity(opacity); }
    } opacityScope{painter, opacityBefore};

    if (state.frame && state.frame->visible)
    {
        // A parented layer's content sits in its parent's element's space.
        const QTransform parent = parentTransform(layer, state.frame, loopType, firstFrame);
        const bool parented = !parent.isIdentity();
        if (parented)
        {
            painter.save();
            painter.setTransform(parent, true);
        }
        const LayerFrame* savedState = _drawnState;
        _drawnState = &state;
        drawFrame(painter, state.frame, state.tweenFrame, state.tweenProgress);
        _drawnState = savedState;
        if (parented)
            painter.restore();
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
    else if (frame->motionTweenRotate == "none")
        _tweenRotation = fla::TweenRotation::None;
    if (_tweenRotation == fla::TweenRotation::Clockwise || _tweenRotation == fla::TweenRotation::CounterClockwise)
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
    // An object motion tween moves its elements by its curves, from the
    // frame being drawn; their stored bounds are not where they show.
    // So does a classic tween along a motion guide.
    const bool moving = _drawnState && _drawnState->frame == frame &&
                        (motionOf(frame) || symbolArmatureOf(frame) ||
                            (nextTweenFrame && frame->tweenType == fla::TweenType::Motion && guidePathOf(frame)));
    const LayerFrame* motionState = moving ? _drawnState : nullptr;
    // A group's members are drawn by its frame's playhead, as it is.
    ElapsedScope elapsed(_elapsed, _drawnState && _drawnState->frame == frame ? _drawnState->offset : _elapsed);
    _drawnState = nullptr;
    const bool culling = !nextTweenFrame && !moving && painter.device()->width() > 0;

    NestingGuard nesting(_nesting);
    if (!nesting.ok)
        return;
    struct Depth
    {
        int& counter;
        ~Depth() { --counter; }
    } depth{_frameDepth};
    ++_frameDepth;
    const bool filtered = _frameDepth == 1 && !_maskPass && _drawPhase != DrawPhase::All;

    int nextElementIndex = 0;
    // What each element tweens toward, paired as Animate pairs them.
    const std::vector<int> pairs = nextTweenFrame && frame->tweenType == fla::TweenType::Motion
        ? fla::pairTweenElements(frame->elements, nextTweenFrame->elements) : std::vector<int>();
    for (const fla::Element* element : frame->elements)
    {
        if (element->visible)
        {
            const int paired = static_cast<size_t>(nextElementIndex) < pairs.size() ? pairs[nextElementIndex] : -1;
            const fla::Element* nextElement = nextTweenFrame && frame->tweenType != fla::TweenType::Motion &&
                    nextTweenFrame->elements.size() > static_cast<size_t>(nextElementIndex)
                ? nextTweenFrame->elements[nextElementIndex]
                : (paired >= 0 ? nextTweenFrame->elements[static_cast<size_t>(paired)] : nullptr);

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
                // An instance by everything its symbol draws over all its
                // frames: the first frame's box would cull an animated
                // symbol whose later frames are on screen.
                const QRectF bounds = element->elementType() == fla::Element::Type::SymbolInstance
                    ? memberReach(element)
                    : getElementBounds(element);
                if (bounds.isValid())
                {
                    const QRectF onDevice = painter.transform().mapRect(bounds).adjusted(-2.0, -2.0, 2.0, 2.0);
                    skip = !onDevice.intersects(deviceRect) ||
                        (onDevice.width() < 0.25 && onDevice.height() < 0.25);
                }
            }
            if (!skip)
            {
                if (motionState)
                {
                    _overrideElement = element;
                    _overrideTransform = shownTransform(*motionState, static_cast<size_t>(nextElementIndex));
                    nextElement = nullptr;
                    // A motion tween's color and filter curves.
                    const fla::MotionObject* motion = motionOf(frame);
                    const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element);
                    if (motion && instance && (motion->animatesColor() || motion->animatesFilters()))
                    {
                        const double rate = _flaDocument && _flaDocument->document &&
                                _flaDocument->document->frameRate > 0.0
                            ? _flaDocument->document->frameRate : 24.0;
                        _overrideStyleElement = element;
                        _overrideColor = motion->colorAt(motionState->offset, rate, instance->colorTransform);
                        _overrideFilters = motion->filtersAt(motionState->offset, rate, instance->filters);
                    }
                }
                drawElement(painter, element, nextElement, tweenProgress);
                _overrideElement = nullptr;
                _overrideStyleElement = nullptr;
            }
        }

        nextElementIndex++;
    }
}

fla::Transform PhoenixView::interpolateTransform(const fla::Transform& a, const fla::Transform& b, double t)
{
    return fla::blendTransforms(a, b, t, _tweenRotation, _tweenTurns);
}

int PhoenixView::symbolLength(const fla::Symbol* symbol)
{
    int length = 1;
    if (!symbol)
        return length;
    for (const fla::Timeline* timeline : symbol->timelines)
    {
        for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
        {
            for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            {
                if (frame)
                    length = std::max(length, frame->index + std::max(frame->duration, 1));
            }
        }
    }
    return length;
}

int PhoenixView::symbolFrame(const fla::SymbolInstance& instance) const
{
    const int elapsed = std::max(0, _elapsed);
    const int length = symbolLength(instance.symbol);
    if (instance.symbolType != fla::SymbolType::Graphic)
    {
        // A movie clip plays its own timeline in an export and on the stage
        // while the document plays; stopped, the stage shows its first frame
        // to edit.
        const bool playing = _exporting || (_player && _player->isPlaying());
        if (!playing || instance.symbolType != fla::SymbolType::MovieClip)
            return 0;
        if (_frameActions && instance.symbol && !instance.symbol->timelines.empty() && instance.symbol->timelines.front())
        {
            auto found = _clipSequences.find(instance.symbol);
            if (found == _clipSequences.end())
                found = _clipSequences.emplace(instance.symbol,
                    fla::frameactions::TimelineActions(*instance.symbol->timelines.front()).sequence()).first;
            return std::min(found->second.frameAt(elapsed), length - 1);
        }
        return elapsed % length;
    }
    // A looping graphic's first frame can be past its end: a synced tween
    // (Animate's Sync) counts it on from keyframe to keyframe without
    // wrapping, and Animate plays it round the loop from there rather than
    // holding the last frame.
    return fla::graphicFrame(instance.loopType, instance.firstFrame, instance.lastFrame, length, elapsed);
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
    // Text and bitmaps are color, not line.
    if (_renderPass == RenderPass::Lines && (element->elementType() == fla::Element::Type::StaticText ||
                                                element->elementType() == fla::Element::Type::BitmapInstance))
        return;
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
    if (element == _overrideElement)
    {
        transform = _overrideTransform;
        _overrideElement = nullptr;
    }
    else if (tweenElement)
    {
        transform = interpolateTransform(element->transform, tweenElement->transform, tweenProgress);
    }

    QTransform qTransform(transform.m11, transform.m12,
                       transform.m21, transform.m22,
                       transform.tx, transform.ty);
    qTransform = qTransform * threeDTransform(element, qTransform, tweenElement, tweenProgress);

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
            // A graphic plays along with the timeline it is on; on Animate's
            // stage a movie clip or a button shows its first frame.
            int frameOffset = symbolFrame(*instance);
            fla::LoopType loopType = fla::LoopType::SingleFrame;

            // An Asset Warp puppet shows its first frame, the art as it was
            // placed, warped by the instance's pins (between two keyframes'
            // pins in a classic tween).
            struct WarpScope
            {
                PhoenixView& view;
                bool was;
                std::function<fla::Point(const fla::Point&)> map;
                QTransform base;
                QTransform inverse;
                ~WarpScope()
                {
                    view._warping = was;
                    view._warpMap = std::move(map);
                    view._warpBase = base;
                    view._warpBaseInverse = inverse;
                }
            } warpScope{*this, _warping, _warpMap, _warpBase, _warpBaseInverse};
            const auto warpWith = [&](std::function<fla::Point(const fla::Point&)> map) {
                bool invertible = false;
                const QTransform base = painter.transform();
                const QTransform inverse = base.inverted(&invertible);
                if (!invertible)
                    return;
                _warping = true;
                _warpMap = std::move(map);
                _warpBase = base;
                _warpBaseInverse = inverse;
            };
            if (instance->otherSymbolType == "puppet")
            {
                frameOffset = 0;
                loopType = fla::LoopType::SingleFrame;
                const auto handlesOf = [](const fla::Element* element) {
                    const fla::SymbolInstance* puppet = dynamic_cast<const fla::SymbolInstance*>(element);
                    if (puppet)
                        for (const auto& attribute : puppet->extraAttributes)
                            if (attribute.first == "puppetHandles")
                                return fla::AssetWarp::pins(attribute.second);
                    return std::vector<fla::WarpPin>();
                };
                std::vector<fla::WarpPin> pins = handlesOf(instance);
                if (tweenElement)
                    pins = fla::AssetWarp::blend(pins, handlesOf(tweenElement), tweenProgress);
                if (!pins.empty())
                    warpWith([pins = std::move(pins)](const fla::Point& point) { return fla::AssetWarp::warp(pins, point); });
            }
            // A deformer bends the art by its handles, between two
            // keyframes' poses in a classic tween.
            else if (std::optional<fla::deform::Deformer> deformer = fla::deform::of(*instance))
            {
                const auto* other = dynamic_cast<const fla::SymbolInstance*>(tweenElement);
                if (other)
                    if (std::optional<fla::deform::Deformer> to = fla::deform::of(*other))
                        deformer = fla::deform::blend(*deformer, *to, tweenProgress);
                if (!fla::deform::atRest(*deformer))
                    warpWith([deformer = *deformer](const fla::Point& point) { return fla::deform::apply(deformer, point); });
            }

            // Part way through a classic tween, the color is between the
            // two keyframes' colors.
            const bool styled = _overrideStyleElement == element;
            fla::ColorTransform color = styled ? _overrideColor : instance->colorTransform;
            if (!styled && tweenElement && tweenElement->elementType() == fla::Element::Type::SymbolInstance)
            {
                color = fla::blendColorTransforms(color,
                    static_cast<const fla::SymbolInstance*>(tweenElement)->colorTransform, tweenProgress);
            }
            const bool alphaOnly = color.tintMultiplier == 0.0 && color.brightness == 0.0 &&
                color.redMultiplier == 1.0 && color.greenMultiplier == 1.0 &&
                color.blueMultiplier == 1.0 && color.redOffset == 0 &&
                color.greenOffset == 0 && color.blueOffset == 0 && color.alphaOffset == 0;

            // And its filters are between the two keyframes' filters.
            std::vector<fla::Filter> filters;
            const std::vector<fla::Filter> shownFilters = styled ? _overrideFilters :
                tweenElement && tweenElement->elementType() == fla::Element::Type::SymbolInstance
                ? fla::blendFilters(instance->filters,
                      static_cast<const fla::SymbolInstance*>(tweenElement)->filters, tweenProgress)
                : instance->filters;
            for (const fla::Filter& filter : shownFilters)
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

            // 9-slice scaling: a movie clip with scale guides, stretched,
            // draws as nine cells, the corners at their own size, the edges
            // stretched one way and the middle both. Each cell is the symbol
            // drawn again under a clip with the cell's own stretch. The cells
            // are cut for the stretch along the symbol's own axes and the
            // rest of the matrix turns or slants them as a whole, so a turned
            // or skewed instance keeps its corners too (see nine_slice.h for
            // why not Flash Player's way). It holds with or without a color
            // effect, filter or blend, which only change where the cells
            // are drawn to.
            const fla::Transform linear(qTransform.m11(), qTransform.m12(), qTransform.m21(), qTransform.m22(), 0.0, 0.0);
            const bool sliced = symbol->scale9 && instance->symbolType == fla::SymbolType::MovieClip &&
                fla::nineslice::stretches(linear);
            const auto drawSymbolTimelines = [&](QPainter& target,
                                                 const std::function<void(QPainter&, const fla::Timeline*)>& drawOne) {
                if (!sliced)
                {
                    for (const fla::Timeline* timeline : symbol->timelines)
                    {
                        if (timeline->visible)
                            drawOne(target, timeline);
                    }
                    return;
                }
                const QRectF local = calculateSymbolLocalBounds(symbol);
                const std::vector<fla::nineslice::Cell> cells = fla::nineslice::cells(
                    fla::Rect(fla::Point(local.left(), local.top()), fla::Point(local.right(), local.bottom())),
                    symbol->scaleGridLeft, symbol->scaleGridTop, symbol->scaleGridRight, symbol->scaleGridBottom,
                    fla::nineslice::axisScale(linear));
                // A cell's clip stops only where it meets another cell; on
                // the symbol's own sides it reaches past them, so the art's
                // smoothed edges there are kept. Turned or slanted, the clip
                // is a region of whole pixels: a smoothed clip edge would
                // blend each layer of the art over the last along the seam
                // and leave a faint line of what is behind.
                const double outside = std::max(local.width(), local.height()) + 100.0;
                const QTransform device = target.transform();
                const bool square = device.type() <= QTransform::TxScale;
                for (const fla::nineslice::Cell& slice : cells)
                {
                    const QRectF source(QPointF(slice.source.topLeft.x, slice.source.topLeft.y),
                        QPointF(slice.source.bottomRight.x, slice.source.bottomRight.y));
                    const QRectF cellArea(QPointF(slice.target.topLeft.x, slice.target.topLeft.y),
                        QPointF(slice.target.bottomRight.x, slice.target.bottomRight.y));
                    const QTransform cell = QTransform::fromTranslate(-source.x(), -source.y()) *
                        QTransform::fromScale(cellArea.width() / source.width(), cellArea.height() / source.height()) *
                        QTransform::fromTranslate(cellArea.x(), cellArea.y());
                    QRectF clip = cellArea;
                    if (source.left() <= local.left())
                        clip.setLeft(clip.left() - outside);
                    if (source.top() <= local.top())
                        clip.setTop(clip.top() - outside);
                    if (source.right() >= local.right())
                        clip.setRight(clip.right() + outside);
                    if (source.bottom() >= local.bottom())
                        clip.setBottom(clip.bottom() + outside);
                    target.save();
                    if (square)
                    {
                        target.setClipRect(clip, Qt::IntersectClip);
                    }
                    else
                    {
                        const QPolygonF corners = device.map(QPolygonF(clip));
                        QPolygon pixels;
                        for (const QPointF& corner : corners)
                            pixels << QPoint(static_cast<int>(std::lround(corner.x())),
                                static_cast<int>(std::lround(corner.y())));
                        target.resetTransform();
                        target.setClipRegion(QRegion(pixels), Qt::IntersectClip);
                        target.setTransform(device);
                    }
                    target.setTransform(cell, true);
                    for (const fla::Timeline* timeline : symbol->timelines)
                    {
                        if (timeline->visible)
                            drawOne(target, timeline);
                    }
                    target.restore();
                }
            };

            if (!isolate)
            {
                // Flash scales every shape's own alpha, so overlapping
                // shapes inside the symbol show through each other: the
                // painter's opacity does exactly that.
                painter.save();
                if (!color.isIdentity() && !_maskPass)
                    painter.setOpacity(painter.opacity() * std::max(0.0, std::min(1.0, color.alphaMultiplier)));

                drawSymbolTimelines(painter, [&](QPainter& target, const fla::Timeline* timeline) {
                    target.save();
                    drawTimeline(target, timeline, loopType, frameOffset);
                    target.restore();
                });
                painter.restore();
            }
            else
            {
                // Everything the symbol draws, over all its frames, on the
                // device, so the offscreen is sized to the instance as seen
                // (full parent transform and zoom, nothing off the stage
                // lost) rather than to the stage at 1x.
                const QRectF local = symbolReach(symbol);

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
                const int innerFrame = symbolFrame(*instance);
                if (cached != _filterCache.end() && !cached->clipped && cached->frame == shownFrame &&
                    cached->inner == innerFrame && cached->revision == _revision && sameLinear(cached->full, full))
                {
                    const QPoint shift(static_cast<int>(std::lround(full.dx() - cached->full.dx())),
                                       static_cast<int>(std::lround(full.dy() - cached->full.dy())));
                    painter.save();
                    painter.resetTransform();
                    Filters::composite(painter, cached->origin + shift, cached->image, instance->blendMode, _layerGroupDepth > 0);
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

                    drawSymbolTimelines(symbolPainter, [&](QPainter& target, const fla::Timeline* timeline) {
                        // A Layer group's offscreen is what alpha and
                        // erase instances inside it cut into; an inner
                        // isolated instance starts a group of its own.
                        const int outerDepth = _layerGroupDepth;
                        _layerGroupDepth = instance->blendMode == "layer" ? outerDepth + 1 : 0;
                        target.save();
                        drawTimeline(target, timeline, loopType, frameOffset);
                        target.restore();
                        _layerGroupDepth = outerDepth;
                    });
                    symbolPainter.end();

                    if (!color.isIdentity() && !alphaOnly)
                        applyColorTransform(symbolImage, color);
                    if (!filters.empty())
                        symbolImage = Filters::apply(symbolImage, filters, filterScale);

                    painter.save();
                    painter.resetTransform();
                    Filters::composite(painter, crop.topLeft(), symbolImage, instance->blendMode, _layerGroupDepth > 0);
                    painter.restore();

                    // Kept for the next paint unless it is huge.
                    if (cacheable && symbolImage.width() * symbolImage.height() <= 4 * 1024 * 1024)
                    {
                        FilteredImage entry;
                        entry.full = full;
                        entry.frame = shownFrame;
                        entry.inner = innerFrame;
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
        QBrush brush = ovalIsOpen(*oval) ? QBrush(Qt::NoBrush) : getFillBrush(oval->fillStyle, oval->localBounds);
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
    else if (type == fla::Element::Type::Raw && !static_cast<const fla::RawElement*>(element)->videoItem.empty())
    {
        // A video: its frame, a film strip down each side and its name, as
        // Phoenix does not play its frames.
        const fla::RawElement* video = static_cast<const fla::RawElement*>(element);
        const QRectF frame(video->localBounds.topLeft.x, video->localBounds.topLeft.y,
            std::max(video->localBounds.width(), 32.0), std::max(video->localBounds.height(), 24.0));
        painter.fillRect(frame, QColor(30, 30, 34));
        const double hole = std::max(2.0, std::min(frame.width(), frame.height()) / 16.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 200, 205));
        for (double y = frame.top() + hole; y + hole < frame.bottom(); y += hole * 2.5)
        {
            painter.drawRect(QRectF(frame.left() + hole * 0.5, y, hole, hole));
            painter.drawRect(QRectF(frame.right() - hole * 1.5, y, hole, hole));
        }
        {
            QFont font = painter.font();
            font.setPixelSize(static_cast<int>(std::max(8.0, std::min(frame.height() / 8.0, 24.0))));
            painter.setFont(font);
            painter.setPen(QColor(220, 220, 225));
            painter.drawText(frame.adjusted(hole * 2.5, 0, -hole * 2.5, 0), Qt::AlignCenter | Qt::TextWordWrap,
                QString::fromStdString(video->videoItem));
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

        if (bitmap)
        {
            const QPixmap pixmap = libraryBitmap(QString::fromStdString(bitmap->name));
            if (!pixmap.isNull())
            {
                painter.setRenderHint(QPainter::SmoothPixmapTransform, bitmap->allowSmoothing);
                if (_warping)
                    drawWarpedPixmap(painter, pixmap);
                else
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

    // A mask is its fills' area: Animate leaves the lines out of it.
    if (_maskPass)
        return QPen(Qt::NoPen);

    return pen;
}

void PhoenixView::paintEntry(QPainter& painter, const PathCacheEntry& entry)
{
    // A compositing pass: the lines alone, or the color alone.
    if ((_renderPass == RenderPass::Lines && !entry.strokeStyle) || (_renderPass == RenderPass::Color && entry.strokeStyle))
        return;
    // Lines take no part in a mask.
    if (_maskPass && entry.strokeStyle)
        return;
    if (_outlineColor.isValid())
    {
        // Outline view: the fill's boundary and the stroke's center line,
        // both as a hairline in the layer color.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(_outlineColor, 0.0));
        painter.drawPath(entry.painterPath);
        return;
    }
    if (entry.strokeStyle)
    {
        if (const fla::Symbol* brush = brushOf(*entry.strokeStyle))
        {
            drawBrushStroke(painter, entry, *brush);
            return;
        }
    }
    if (!entry.effectPath.isEmpty())
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(entry.pen.brush());
        painter.drawPath(entry.effectPath);
        return;
    }
    // In a mask only the area counts.
    if (_maskPass)
    {
        if (entry.fillBrush.style() == Qt::NoBrush)
            return;
        painter.setBrush(Qt::black);
        painter.setPen(Qt::NoPen);
        painter.drawPath(entry.painterPath);
        return;
    }
    if (entry.fixedGrain)
    {
        // The art's scale against the frame's, taken out of the grain.
        const double art = std::sqrt(std::fabs(painter.worldTransform().determinant()));
        const double frame = std::sqrt(std::fabs(viewTransform.determinant()));
        const double scale = art > 1.0e-9 && frame > 1.0e-9 ? art / frame : 1.0;
        QBrush brush = entry.fillBrush;
        brush.setTransform(brush.transform() * QTransform::fromScale(1.0 / scale, 1.0 / scale));
        painter.setBrush(brush);
    }
    else
    {
        painter.setBrush(entry.fillBrush);
    }
    painter.setPen(penForHere(entry));
    painter.drawPath(entry.painterPath);
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

QImage PhoenixView::textureTile(const std::string& name)
{
    if (const QImage* cached = _textureTiles.object(QString::fromStdString(name)))
        return *cached;
    QImage tile;
    const std::string prefix = fla::texture::kBitmapPrefix;
    if (name.compare(0, prefix.size(), prefix) == 0)
    {
        // A library bitmap: its lightness is the coverage.
        const QImage image = libraryBitmap(QString::fromStdString(name.substr(prefix.size()))).toImage();
        if (!image.isNull())
            tile = image.convertToFormat(QImage::Format_Grayscale8);
    }
    else
    {
        const fla::texture::Tile made = fla::texture::builtInTile(name);
        if (made.size > 0)
        {
            tile = QImage(made.size, made.size, QImage::Format_Grayscale8);
            for (int y = 0; y < made.size; ++y)
                std::copy(made.coverage.begin() + static_cast<std::ptrdiff_t>(y) * made.size,
                    made.coverage.begin() + static_cast<std::ptrdiff_t>(y + 1) * made.size, tile.scanLine(y));
        }
    }
    _textureTiles.insert(QString::fromStdString(name), new QImage(tile),
        std::max<qsizetype>(1, tile.sizeInBytes() / 1024));
    return tile;
}

QBrush PhoenixView::texturedBrush(const QColor& color, const fla::texture::Spec& spec)
{
    const QImage tile = textureTile(spec.name);
    if (tile.isNull())
        return QBrush(color);
    // The color, its alpha taken down where the grain is light.
    QImage paint(tile.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tile.height(); ++y)
    {
        const uchar* from = tile.constScanLine(y);
        QRgb* to = reinterpret_cast<QRgb*>(paint.scanLine(y));
        for (int x = 0; x < tile.width(); ++x)
        {
            const double cover = 1.0 - spec.strength + spec.strength * from[x] / 255.0;
            const int a = static_cast<int>(std::lround(color.alpha() * cover));
            to[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
        }
    }
    QBrush brush(paint);
    QTransform placed;
    placed.translate(spec.offsetX, spec.offsetY);
    placed.rotate(spec.rotation);
    placed.scale(spec.scale, spec.scale);
    brush.setTransform(placed);
    return brush;
}

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
        const double s = 1.0 / 20.0;
        const QTransform placed(
            bitmapFill->transform.m11 * s, bitmapFill->transform.m12 * s,
            bitmapFill->transform.m21 * s, bitmapFill->transform.m22 * s,
            bitmapFill->transform.tx, bitmapFill->transform.ty);
        if (!bitmapFill->bitmapIsClipped)
        {
            QBrush brush(pixmap);
            brush.setTransform(placed);
            return brush;
        }

        // A clipped fill shows the bitmap once, its edge pixels running on
        // past it, as Flash draws it. The brush still tiles, so the image is
        // grown by its own edges to cover the area filled, in the bitmap's
        // own pixels, and the tiling never shows.
        bool invertible = false;
        const QTransform toBitmap = placed.inverted(&invertible);
        const int w = pixmap.width();
        const int h = pixmap.height();
        QRectF need(0.0, 0.0, w, h);
        if (invertible && bounds.width() > 0.0 && bounds.height() > 0.0 && bounds.width() < 1.0e29)
        {
            need = need.united(toBitmap.mapRect(QRectF(bounds.topLeft.x, bounds.topLeft.y,
                bounds.width(), bounds.height())));
        }
        const auto grow = [](double amount) {
            return std::min(4096, std::max(0, static_cast<int>(std::ceil(amount))) + 1);
        };
        const int left = grow(-need.left());
        const int top = grow(-need.top());
        const int right = grow(need.right() - w);
        const int bottom = grow(need.bottom() - h);
        // Made once for each bitmap and padding, not on every paint.
        const QString key = QString("%1|%2|%3|%4|%5|%6")
                                .arg(pixmap.cacheKey())
                                .arg(QString::fromStdString(bitmapFill->bitmapPath))
                                .arg(left).arg(top).arg(right).arg(bottom);
        if (const QPixmap* cached = _paddedFills.object(key))
        {
            QBrush brush(*cached);
            brush.setTransform(QTransform::fromTranslate(-left, -top) * placed);
            return brush;
        }
        const QImage source = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage padded(w + left + right, h + top + bottom, QImage::Format_ARGB32_Premultiplied);
        if (padded.isNull() || source.isNull())
        {
            // No room for the padded image: the bitmap tiles past its edge
            // rather than nothing being drawn.
            QBrush brush(pixmap);
            brush.setTransform(placed);
            return brush;
        }
        for (int y = 0; y < padded.height(); ++y)
        {
            const QRgb* from = reinterpret_cast<const QRgb*>(source.constScanLine(std::clamp(y - top, 0, h - 1)));
            QRgb* to = reinterpret_cast<QRgb*>(padded.scanLine(y));
            std::fill(to, to + left, from[0]);
            std::copy(from, from + w, to + left);
            std::fill(to + left + w, to + padded.width(), from[w - 1]);
        }
        const QPixmap grown = QPixmap::fromImage(padded);
        _paddedFills.insert(key, new QPixmap(grown), std::max<qsizetype>(1, padded.sizeInBytes() / 1024));
        QBrush brush(grown);
        brush.setTransform(QTransform::fromTranslate(-left, -top) * placed);
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

const fla::Bitmap* PhoenixView::findBitmap(const QString& name)
{
    if (!_flaDocument || !_flaDocument->document)
        return nullptr;
    const std::string wanted = name.toStdString();
    const auto known = _bitmapsByName.constFind(name);
    if (known != _bitmapsByName.constEnd() && known.value() && known.value()->name == wanted)
    {
        // Still the library's? A bitmap deleted from the library is gone
        // from resources (and may be freed).
        const std::vector<fla::Resource*>& resources = _flaDocument->document->resources;
        if (std::find(resources.begin(), resources.end(), known.value()) != resources.end())
            return known.value();
    }
    // Added, removed or renamed since the map was made: made again.
    _bitmapsByName.clear();
    for (fla::Resource* resource : _flaDocument->document->resources)
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
        {
            const fla::Bitmap* bitmap = static_cast<const fla::Bitmap*>(resource);
            _bitmapsByName.insert(QString::fromStdString(bitmap->name), bitmap);
        }
    return _bitmapsByName.value(name, nullptr);
}

QPixmap PhoenixView::libraryBitmap(const QString& name)
{
    if (!_flaDocument || !_flaDocument->document)
    {
        const CachedBitmap* cached = _bitmapCache.object(name);
        return cached ? cached->pixmap : QPixmap();
    }

    if (const fla::Bitmap* bitmap = findBitmap(name))
    {
        // Kept while the bitmap's bytes are the ones it was made from: a
        // bitmap brush, or undoing one, gives it others. Told from a token,
        // so a bitmap not yet read from a Phoenix document is not read just
        // to be compared.
        const std::weak_ptr<const void> source =
            bitmap->imageData.empty() ? bitmap->datData.token() : bitmap->imageData.token();
        if (const CachedBitmap* cached = _bitmapCache.object(name))
        {
            const std::shared_ptr<const void> was = cached->source.lock();
            if (was && was == source.lock())
                return cached->pixmap;
        }

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
                // Null when Qt cannot set aside the memory.
                for (int y = 0; !image.isNull() && y < decoded.height; ++y)
                {
                    std::memcpy(image.scanLine(y),
                        decoded.pixels.data() + static_cast<size_t>(y) * decoded.width,
                        static_cast<size_t>(decoded.width) * 4);
                }
                pixmap = QPixmap::fromImage(image);
            }
        }
        const qsizetype kilobytes = std::max<qsizetype>(1,
            static_cast<qsizetype>(pixmap.width()) * pixmap.height() * 4 / 1024);
        _bitmapCache.insert(name, new CachedBitmap{pixmap, source}, kilobytes);
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

    // Each fill's outlines as one compound path. Read off the shape's planar
    // map when that is switched on and the map agrees with the file, each
    // face its outer boundary and its holes; otherwise traced from the edges
    // (see traceFills), which also joins up outlines that do not quite close.
    std::map<int, QPainterPath> fillPaths;
    std::map<int, std::vector<std::vector<fla::Curve>>> mapOutlines;
    if (renderFillsFromMap() && fla::fillOutlinesFromMap(*shape, mapOutlines))
    {
        for (const auto& [fillIdx, loops] : mapOutlines)
        {
            QPainterPath& compoundPath = fillPaths[fillIdx];
            compoundPath.setFillRule(Qt::WindingFill);
            for (const std::vector<fla::Curve>& loop : loops)
            {
                QPainterPath loopPath;
                loopPath.moveTo(loop.front().start().x, loop.front().start().y);
                for (const fla::Curve& curve : loop)
                {
                    if (curve.isLine())
                    {
                        loopPath.lineTo(curve.end().x, curve.end().y);
                    }
                    else
                    {
                        loopPath.cubicTo(curve.controlPoint(1).x, curve.controlPoint(1).y,
                            curve.controlPoint(2).x, curve.controlPoint(2).y,
                            curve.end().x, curve.end().y);
                    }
                }
                loopPath.closeSubpath();
                compoundPath.addPath(loopPath);
            }
        }
    }
    else
    {
        for (const auto& [fillIdx, loops] : fla::traceFills(*shape))
        {
            QPainterPath& compoundPath = fillPaths[fillIdx];
            compoundPath.setFillRule(Qt::WindingFill);
            for (const fla::TracedLoop& loop : loops)
            {
                if (loop.legs.empty())
                    continue;
                const auto [first, last] = getPathEndpoints(loop.legs.front().path);
                const fla::Point start = loop.legs.front().reversed ? last : first;
                QPainterPath loopPath;
                loopPath.moveTo(start.x, start.y);
                for (const fla::TracedLeg& leg : loop.legs)
                    addSegmentToPath(loopPath, leg.path, leg.reversed, shape);
                if (loop.closed)
                    loopPath.closeSubpath();
                compoundPath.addPath(loopPath);
            }
        }
    }

    for (const auto& [fillIdx, compoundPath] : fillPaths)
    {
        const fla::FillStyle* fillStyle = shape->getFillStyleByIndex(fillIdx);
        if (!fillStyle)
            continue;

        QRectF bounds = compoundPath.boundingRect();
        fla::Rect rect({bounds.left(), bounds.top()}, {bounds.left() + bounds.width(), bounds.top() + bounds.height()});
        QBrush brush = getFillBrush(fillStyle, rect);
        // A textured brush's paint takes on its texture's grain.
        if (!_maskPass && fillStyle && fillStyle->type() == fla::FillStyle::Type::SolidColor)
        {
            const fla::texture::Spec spec = fla::texture::of(*shape);
            if (spec.valid())
                brush = texturedBrush(brush.color(), spec);
            PathCacheEntry entry{ brush, Qt::NoPen, compoundPath, nullptr, fillIdx };
            entry.fixedGrain = spec.valid() && spec.fixed;
            cacheEntries.push_back(entry);
            continue;
        }
        cacheEntries.push_back({ brush, Qt::NoPen, compoundPath, nullptr, fillIdx });
    }

    // Whether two points are the same to a twip.
    auto pointsMatch = [](const fla::Point& p1, const fla::Point& p2) -> bool
    {
        return std::lround(p1.x * 20.0) == std::lround(p2.x * 20.0) &&
            std::lround(p1.y * 20.0) == std::lround(p2.y * 20.0);
    };

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

            PathCacheEntry entry{ QBrush(Qt::NoBrush), pen, strokePath, strokeStyle };
            if (StrokeEffects::isProcedural(*strokeStyle))
                entry.effectPath = StrokeEffects::outline(strokePath, *strokeStyle);
            cacheEntries.push_back(entry);
        }
    }
}

const PhoenixView::PathCacheList& PhoenixView::shapePaths(const fla::Shape* shape)
{
    const auto it = _pathCache.find(shape);
    if (it != _pathCache.end())
        return it.value();

    // Kept for every later drawing of the shape, so built in its own colors
    // even when first drawn as a mask; paintEntry fills a mask's area plain.
    PathCacheList entries;
    const bool wasMaskPass = _maskPass;
    _maskPass = false;
    buildShapePaths(shape, entries);
    _maskPass = wasMaskPass;
    return *_pathCache.insert(shape, entries);
}

void PhoenixView::drawShape(QPainter& painter, const fla::Shape* shape, const fla::Shape* tweenShape,
    double tweenProgress, const QTransform& ownTransform)
{
    const bool isSelected = isElementSelected(shape);

    // Inside a warped puppet the shape is drawn through the warp, in the
    // puppet's own space.
    if (_warping)
    {
        const QTransform toPuppet = painter.transform() * _warpBaseInverse;
        painter.save();
        painter.setTransform(_warpBase);
        for (const PathCacheEntry& entry : shapePaths(shape))
        {
            PathCacheEntry warped = entry;
            warped.painterPath = warpedPath(entry.painterPath, toPuppet);
            if (!entry.effectPath.isEmpty())
                warped.effectPath = warpedPath(entry.effectPath, toPuppet);
            paintEntry(painter, warped);
        }
        painter.restore();
        return;
    }

    // A shape tween replaces the shape's own geometry for the duration of
    // the tween: the morph the file stores is built at this progress, with
    // its segments' own fills and strokes blended between the keyframes,
    // and drawn through the same path machinery as any shape.
    const fla::Frame* frame = owningFrame(shape);
    // A tween Phoenix made, or one whose keyframes have been edited since,
    // has its morph worked out here.
    const fla::Frame* endFrame = tweenShape ? owningFrame(tweenShape) : nullptr;
    if (frame && endFrame && frame->tweenType == fla::TweenType::Shape)
        fla::ensureMorph(const_cast<fla::Frame&>(*frame), *endFrame);
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
                paintEntry(painter, entry);
            if (isSelected)
                drawOverlayPoints(painter, shape);
            return;
        }
    }

    for (const PathCacheEntry& entry : shapePaths(shape))
        paintEntry(painter, entry);

    // Control points on a tree-selected shape are an inspection aid, so they
    // come with the bounding-box debug view rather than the look of a selection.
    if (isSelected && _showBounds)
    {
        drawOverlayPoints(painter, shape);
    }
}

std::vector<std::pair<QPainterPath, const fla::StrokeStyle*>> PhoenixView::strokeRuns(const fla::Shape* shape)
{
    std::vector<std::pair<QPainterPath, const fla::StrokeStyle*>> runs;
    if (!shape)
        return runs;
    for (const PathCacheEntry& entry : shapePaths(shape))
    {
        if (entry.strokeStyle)
            runs.emplace_back(entry.painterPath, entry.strokeStyle);
    }
    return runs;
}

QTransform PhoenixView::threeDTransform(const fla::Element* element, const QTransform& placed,
    const fla::Element* tweenElement, double progress) const
{
    const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element);
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!instance || !document || instance->symbolType != fla::SymbolType::MovieClip)
        return QTransform();
    fla::threed::Pose pose = fla::threed::of(*instance);
    if (const auto* other = dynamic_cast<const fla::SymbolInstance*>(tweenElement))
        pose = fla::threed::blend(pose, fla::threed::of(*other), progress);
    if (pose.flat())
        return QTransform();
    // Turned about its transformation point, seen from in front of the
    // stage's middle.
    const QPointF center = placed.map(QPointF(instance->transformationPoint.x, instance->transformationPoint.y));
    const double focal = fla::threed::focalLength(fla::threed::perspectiveAngle(*document), document->width);
    const std::array<double, 9> m = fla::threed::projection(pose, fla::Point(center.x(), center.y()),
        fla::Point(document->width / 2.0, document->height / 2.0), focal);
    return QTransform(m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]);
}

const fla::Symbol* PhoenixView::brushOf(const fla::StrokeStyle& stroke) const
{
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !document->symbolList)
        return nullptr;
    for (const auto& attribute : stroke.extraAttributes)
    {
        if (attribute.first != "phoenixBrush")
            continue;
        const auto found = document->symbolList->symbolMap.find(attribute.second);
        if (found != document->symbolList->symbolMap.end() && found->second && !found->second->timelines.empty())
            return found->second;
    }
    return nullptr;
}

void PhoenixView::drawBrushStroke(QPainter& painter, const PathCacheEntry& entry, const fla::Symbol& brush)
{
    const QPainterPath& path = entry.painterPath;
    const double length = path.length();
    const QRectF art = shownSymbolBounds(&brush, fla::LoopType::SingleFrame, 0, 1);
    if (length <= 0.0 || !art.isValid() || art.width() <= 0.0 || _warping)
        return;
    bool pattern = false;
    for (const auto& attribute : entry.strokeStyle->extraAttributes)
    {
        if (attribute.first == "phoenixBrushPattern")
            pattern = attribute.second == "true";
    }
    // The art's height is the stroke's size times its own; its length runs
    // along the stroke, end to end or a tile at a time.
    const double across = std::max(entry.strokeStyle->weight, 0.1);
    const double tileLength = pattern ? art.width() * across : length;
    const int tiles = pattern ? std::max(1, static_cast<int>(std::lround(length / std::max(tileLength, 1.0e-6)))) : 1;
    const double piece = length / tiles;
    const double middle = art.center().y();

    const bool wasWarping = _warping;
    const auto savedMap = _warpMap;
    const QTransform savedBase = _warpBase;
    const QTransform savedInverse = _warpBaseInverse;
    bool invertible = false;
    const QTransform base = painter.transform();
    const QTransform inverse = base.inverted(&invertible);
    if (!invertible)
        return;
    const fla::Timeline* timeline = brush.timelines.front();
    for (int tile = 0; tile < tiles; ++tile)
    {
        const double start = tile * piece;
        _warping = true;
        _warpBase = base;
        _warpBaseInverse = inverse;
        _warpMap = [&path, &art, start, piece, across, middle, length](const fla::Point& point) {
            const double along = start + std::clamp((point.x - art.left()) / art.width(), 0.0, 1.0) * piece;
            const double percent = path.percentAtLength(std::clamp(along, 0.0, length));
            const QPointF at = path.pointAtPercent(percent);
            const double angle = path.angleAtPercent(percent) * 3.14159265358979323846 / 180.0;
            // Qt turns counterclockwise with y up; the page's y runs down.
            const QPointF normal(std::sin(angle), std::cos(angle));
            const double out = (point.y - middle) * across;
            return fla::Point(at.x() + normal.x() * out, at.y() + normal.y() * out);
        };
        painter.save();
        drawTimeline(painter, timeline, fla::LoopType::SingleFrame, 0);
        painter.restore();
    }
    _warping = wasWarping;
    _warpMap = savedMap;
    _warpBase = savedBase;
    _warpBaseInverse = savedInverse;
}

void PhoenixView::drawWarpedPixmap(QPainter& painter, const QPixmap& pixmap) const
{
    // A grid over the bitmap, each cell two triangles, each triangle drawn
    // with the affine map that takes its corners where the warp takes them.
    const QTransform toPuppet = painter.transform() * _warpBaseInverse;
    const int width = pixmap.width();
    const int height = pixmap.height();
    const int columns = std::clamp(width / 12, 4, 32);
    const int rows = std::clamp(height / 12, 4, 32);
    std::vector<QPointF> source;
    std::vector<QPointF> target;
    for (int j = 0; j <= rows; ++j)
    {
        for (int i = 0; i <= columns; ++i)
        {
            const QPointF at(width * static_cast<double>(i) / columns, height * static_cast<double>(j) / rows);
            const QPointF puppet = toPuppet.map(at);
            const fla::Point moved = _warpMap(fla::Point(puppet.x(), puppet.y()));
            source.push_back(at);
            target.emplace_back(moved.x, moved.y);
        }
    }
    const auto mapOf = [](const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& A, const QPointF& B,
                           const QPointF& C, QTransform& map) {
        const QPointF u = b - a, v = c - a, U = B - A, V = C - A;
        const double det = u.x() * v.y() - u.y() * v.x();
        if (std::fabs(det) < 1.0e-12)
            return false;
        const double m11 = (U.x() * v.y() - V.x() * u.y()) / det;
        const double m21 = (V.x() * u.x() - U.x() * v.x()) / det;
        const double m12 = (U.y() * v.y() - V.y() * u.y()) / det;
        const double m22 = (V.y() * u.x() - U.y() * v.x()) / det;
        map = QTransform(m11, m12, m21, m22, A.x() - (m11 * a.x() + m21 * a.y()), A.y() - (m12 * a.x() + m22 * a.y()));
        return true;
    };
    const QTransform base = _warpBase;
    for (int j = 0; j < rows; ++j)
    {
        for (int i = 0; i < columns; ++i)
        {
            const int k = j * (columns + 1) + i;
            const int corners[2][3] = {{k, k + 1, k + columns + 2}, {k, k + columns + 2, k + columns + 1}};
            for (const auto& triangle : corners)
            {
                const QPointF a = source[triangle[0]], b = source[triangle[1]], c = source[triangle[2]];
                QTransform map;
                if (!mapOf(a, b, c, target[triangle[0]], target[triangle[1]], target[triangle[2]], map))
                    continue;
                // A little past its edges, so neighbors leave no seam.
                const QPointF middle = (a + b + c) / 3.0;
                QPolygonF clip;
                for (const QPointF& corner : {a, b, c})
                {
                    const QPointF out = corner - middle;
                    const double length = std::hypot(out.x(), out.y());
                    clip << (length > 0.0 ? corner + out / length * 0.75 : corner);
                }
                painter.save();
                painter.setTransform(map * base);
                QPainterPath path;
                path.addPolygon(clip);
                path.closeSubpath();
                painter.setClipPath(path);
                const QRectF piece = clip.boundingRect().adjusted(-1.0, -1.0, 1.0, 1.0).intersected(QRectF(0, 0, width, height));
                painter.drawPixmap(piece, pixmap, piece);
                painter.restore();
            }
        }
    }
}

QPainterPath PhoenixView::warpedPath(const QPainterPath& path, const QTransform& toPuppet) const
{
    QPainterPath out;
    out.setFillRule(path.fillRule());
    // Straight runs are cut into pieces of a pixel or so, so the warp bends
    // them too.
    const double step = 1.0;
    for (const QPolygonF& polygon : path.toSubpathPolygons(toPuppet))
    {
        if (polygon.isEmpty())
            continue;
        const auto place = [this](const QPointF& point) {
            const fla::Point moved = _warpMap(fla::Point(point.x(), point.y()));
            return QPointF(moved.x, moved.y);
        };
        out.moveTo(place(polygon.first()));
        for (int i = 1; i < polygon.size(); ++i)
        {
            const QPointF a = polygon[i - 1];
            const QPointF b = polygon[i];
            const int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / step)));
            for (int k = 1; k <= pieces; ++k)
                out.lineTo(place(a + (b - a) * (static_cast<double>(k) / pieces)));
        }
        if (polygon.size() > 2 && polygon.first() == polygon.last())
            out.closeSubpath();
    }
    return out;
}

double PhoenixView::pickTolerance() const
{
    // A few screen pixels, expressed in document units, so picking feels the
    // same at every zoom level.
    const double screenPixels = Preferences::clickPixels();
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
        if (!layer || !layer->isVisible() || layer->isLocked())
            continue;

        // Folders hold no art. Guide layers show on the timeline being
        // edited, so their art is picked like any other.
        if (layer->layerType == fla::Layer::Type::Folder)
            continue;

        const LayerFrame state = resolveLayerFrame(layer, loopType, firstFrame);
        if (!state.frame)
            continue;

        // What is picked is what shows: the layer placed by its parent, each
        // element where its tween has it now.
        const QTransform layerToDocument = parentTransform(layer, state.frame, loopType, firstFrame) * toDocument;
        ElapsedScope elapsed(_elapsed, state.offset);

        // Layers draw last to first, so the first layer is on top and this walk
        // meets the topmost first. Within a frame later elements draw over
        // earlier ones, so they are walked backwards, and the first hit is the
        // one the user sees.
        for (size_t index = state.frame->elements.size(); index-- > 0;)
        {
            fla::Element* element = state.frame->elements[index];
            if (element)
            {
                const fla::Transform shown = shownTransform(state, index);
                hitTestElement(*element, const_cast<fla::Frame*>(state.frame), loopType,
                    layerToDocument, documentPos, tolerance, result, &shown);
            }
            if (result)
                return;
        }

        // Edit Multiple Frames: the layer's other keyframes in range.
        if (toDocument.isIdentity() || _scope.empty())
        {
            for (fla::Frame* other : multipleFrames(layer))
            {
                for (size_t index = other->elements.size(); index-- > 0;)
                {
                    if (other->elements[index])
                        hitTestElement(*other->elements[index], other, loopType, layerToDocument, documentPos,
                            tolerance, result, nullptr);
                    if (result)
                        return;
                }
            }
        }
    }
}

void PhoenixView::hitTestElement(fla::Element& element, fla::Frame* frame,
    fla::LoopType loopType, const QTransform& toDocument, const QPointF& documentPos,
    double tolerance, HitResult& result, const fla::Transform* shownTransform)
{
    NestingGuard nesting(_nesting);
    if (!nesting.ok)
        return;
    // A locked object (Modify > Arrange > Lock) stays where it is, unpicked,
    // as does all but a group being edited.
    if (!element.visible || element.isLocked || outsideGroup(&element))
        return;

    const fla::Transform& t = shownTransform ? *shownTransform : element.transform;
    QTransform elementTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
    elementTransform = elementTransform * threeDTransform(&element, elementTransform);

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
        // Nowhere near everything the symbol draws over all its frames: not
        // descended into. The box is grown by its own size each way, since
        // a warp, a deformer or 3D can carry art past it.
        {
            const QRectF reach = symbolReach(symbol);
            const double grow = std::max(reach.width(), reach.height()) + localTolerance;
            if (!reach.adjusted(-grow, -grow, grow, grow).contains(localPos))
                return;
        }
        HitResult inner;
        // A puppet is picked by its art as placed (its first frame).
        const bool puppet = instance.otherSymbolType == "puppet";
        const int frameOffset = puppet ? 0 : symbolFrame(instance);
        const fla::LoopType instanceLoop = fla::LoopType::SingleFrame;

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
    // An undo or redo may have changed the guides since the last gesture;
    // the snapper takes them afresh before this one.
    pushGuidesToSnapper();

    // The active tool gets first refusal. Middle-drag always pans, so panning
    // stays available whatever tool is selected.
    //
    // Alt deliberately is not a pan modifier: the drawing tools need it for
    // breaking a tangent and for inserting an anchor, and swallowing it here
    // left those silently doing nothing.
    const bool forcePan = event->button() == Qt::MiddleButton || _spaceHeld;

    // A ruler or a guide under the cursor takes the press, as in Animate.
    if (event->button() == Qt::LeftButton && !forcePan && beginGuideDrag(event->position()))
        return;
    // So does a deformer's handle.
    if (event->button() == Qt::LeftButton && !forcePan && beginDeformerDrag(event->position()))
        return;

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
    emit cursorMoved(mapToDocument(event->position()), event->position().toPoint());
    if (_guideDrag)
    {
        bool invertible = false;
        const QPointF root = rootToWidget().inverted(&invertible).map(event->position());
        _guideValue = _guideDrag == 1 ? root.x() : root.y();
        update();
        return;
    }
    if (_deformerDrag.instance)
    {
        // The handle follows the pointer; a curve's anchor takes its two
        // arms along.
        const QPointF moved = _deformerDrag.toLocal.map(widgetToStage(event->position())) - _deformerDrag.grab;
        fla::deform::Deformer deformer = _deformerDrag.start;
        const size_t index = _deformerDrag.index;
        std::vector<size_t> moving{index};
        if (deformer.kind == fla::deform::Kind::Curve && index % 3 == 0)
        {
            if (index > 0)
                moving.push_back(index - 1);
            if (index + 1 < deformer.posed.size())
                moving.push_back(index + 1);
        }
        for (size_t i : moving)
        {
            deformer.posed[i].x += moved.x();
            deformer.posed[i].y += moved.y();
        }
        _deformerDrag.instance->extraAttributes = fla::deform::withDeformer(*_deformerDrag.instance, deformer);
        clearCaches();
        update();
        return;
    }
    // The cursor says a guide can be picked up.
    if (_showGuides && _rotation == 0.0 && !(event->buttons() & Qt::LeftButton) && _activeTool == _modifierTool)
    {
        const QTransform toWidget = rootToWidget();
        bool overGuide = false;
        for (double x : guidesX())
            overGuide = overGuide || std::fabs(toWidget.map(QPointF(x, 0)).x() - event->position().x()) <= 3.0;
        for (double y : guidesY())
            overGuide = overGuide || std::fabs(toWidget.map(QPointF(0, y)).y() - event->position().y()) <= 3.0;
        if (overGuide)
            setCursor(Qt::SizeAllCursor);
        else if (cursor().shape() == Qt::SizeAllCursor)
            setCursor(_activeTool ? _activeTool->cursor() : QCursor(Qt::ArrowCursor));
    }

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
    if (_deformerDrag.instance)
    {
        // One undo step for the whole drag.
        fla::SymbolInstance* instance = _deformerDrag.instance;
        _deformerDrag.instance = nullptr;
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        const Attributes after = instance->extraAttributes;
        if (after != _deformerDrag.before)
        {
            instance->extraAttributes = _deformerDrag.before;
            auto command = std::make_unique<fla::SetPropertyCommand<Attributes>>(instance->extraAttributes,
                _deformerDrag.before, after, "Deform");
            if (_commandStack)
                _commandStack->push(std::move(command));
            else
                command->redo();
        }
        clearCaches();
        update();
        return;
    }

    if (_guideDrag)
    {
        const int kind = _guideDrag;
        _guideDrag = 0;
        // Let go over a ruler, or off the view, and the guide goes.
        const QPointF at = event->position();
        const bool removed = !rect().contains(at.toPoint()) ||
            (_showRulers && ((kind == 1 && at.x() < kRulerSize) || (kind == 2 && at.y() < kRulerSize)));
        std::vector<double> xs = guidesX();
        std::vector<double> ys = guidesY();
        std::vector<double>& guides = kind == 1 ? xs : ys;
        const char* name = nullptr;
        if (_guideIndex >= 0 && _guideIndex < static_cast<int>(guides.size()))
        {
            if (removed)
            {
                guides.erase(guides.begin() + _guideIndex);
                name = "Remove Guide";
            }
            else if (guides[_guideIndex] != std::round(_guideValue))
            {
                guides[_guideIndex] = std::round(_guideValue);
                name = "Move Guide";
            }
        }
        else if (!removed)
        {
            guides.push_back(std::round(_guideValue));
            name = "Add Guide";
        }
        if (name)
            setGuides(std::move(xs), std::move(ys), name);
        _guideIndex = -1;
        pushGuidesToSnapper();
        setCursor(_activeTool ? _activeTool->cursor() : QCursor(Qt::ArrowCursor));
        update();
        return;
    }

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

void PhoenixView::contextMenuEvent(QContextMenuEvent* event)
{
    emit stageMenuRequested(event->globalPos(), mapToDocument(QPointF(event->pos())));
    event->accept();
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
    // The readings are kept for the brushes, and the event is left alone so
    // that Qt goes on to deliver the same movement as mouse events, which is
    // what the tools handle.
    // Which end of the pen is in use: the eraser end erases, as in Animate.
    if (event->type() != QEvent::TabletRelease)
    {
        const bool eraser = event->pointerType() == QPointingDevice::PointerType::Eraser;
        if (eraser != _penEraser)
        {
            _penEraser = eraser;
            emit penEndChanged(eraser);
        }
    }

    fla::BrushSample sample;
    const QPointF documentPos = mapToDocument(event->position());
    sample.x = documentPos.x();
    sample.y = documentPos.y();
    sample.pressure = std::max(0.0, std::min(1.0, event->pressure()));
    sample.tiltX = event->xTilt();
    sample.tiltY = event->yTilt();
    sample.rotation = event->rotation();
    sample.time = static_cast<double>(event->timestamp());
    switch (event->type())
    {
    case QEvent::TabletPress:
        _penDown = true;
        _tabletSamples.clear();
        _tabletSamples.push_back(sample);
        _pressure = sample.pressure;
        break;
    case QEvent::TabletMove:
        if (_penDown)
            _tabletSamples.push_back(sample);
        _pressure = _penDown ? sample.pressure : 1.0;
        break;
    case QEvent::TabletRelease:
        // The release reads no pressure where the last move already stood;
        // the stroke ends on the moves before it.
        _penDown = false;
        _pressure = 1.0;
        break;
    default:
        break;
    }
    event->ignore();
}

std::vector<fla::BrushSample> PhoenixView::takeTabletSamples()
{
    std::vector<fla::BrushSample> taken;
    taken.swap(_tabletSamples);
    return taken;
}

bool PhoenixView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && _activeTool && _activeTool->wantsEnter())
    {
        const int key = static_cast<const QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter)
        {
            event->accept();
            return true;
        }
    }
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

void PhoenixView::inputMethodEvent(QInputMethodEvent* event)
{
    if (_activeTool && _activeTool->inputMethod(*this, event))
    {
        event->accept();
        return;
    }
    QWidget::inputMethodEvent(event);
}

QVariant PhoenixView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    // Only a tool taking text wants the input method at all.
    if (query == Qt::ImEnabled)
        return _activeTool && _activeTool->capturesKeyboard();
    const QVariant answer = _activeTool ? _activeTool->inputMethodQuery(*this, query) : QVariant();
    return answer.isValid() ? answer : QWidget::inputMethodQuery(query);
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
            if (!layer || !layer->isVisible() || layer->isLocked())
                continue;

            if (layer->layerType == fla::Layer::Type::Folder)
                continue;

            const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
            if (!state.frame || !state.frame->visible)
                continue;

            for (fla::Element* element : state.frame->elements)
            {
                if (!element || !element->visible || element->isLocked || outsideGroup(element))
                    continue;

                // Top-level elements carry their own transform into the bounds,
                // so these are already document-space.
                const QRectF bounds = getElementBounds(element);
                if (bounds.isValid() && documentRect.intersects(bounds))
                    found.push_back(element);
            }
            for (fla::Frame* other : multipleFrames(layer))
            {
                for (fla::Element* element : other->elements)
                {
                    if (!element || !element->visible || element->isLocked)
                        continue;
                    const QRectF bounds = getElementBounds(element);
                    if (bounds.isValid() && documentRect.intersects(bounds))
                        found.push_back(element);
                }
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

void PhoenixView::pruneToDocument()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    const std::unordered_set<const fla::DOMElement*> reachable = _flaDocument->document->reachable();
    const auto alive = [&reachable](const fla::DOMElement* element) {
        return !element || reachable.count(element) > 0;
    };

    _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                       [&](const PendingDrop& drop) { return !alive(drop.shape) || !alive(drop.frame); }),
        _pending.end());
    _floating.erase(std::remove_if(_floating.begin(), _floating.end(),
                        [&](const FloatingRegion& region) {
                            return !alive(region.source) || !alive(region.piece) || !alive(region.frame);
                        }),
        _floating.end());
    if (!alive(_selectedElement))
        _selectedElement = nullptr;
    if (!alive(_onlyLayer))
        _onlyLayer = nullptr;
    if (!alive(_overrideElement))
        _overrideElement = nullptr;
    if (!alive(_overrideStyleElement))
        _overrideStyleElement = nullptr;

    // The scope, outermost first: everything from the first level whose
    // symbol, instance or frame went is dropped as it is, with none of what
    // stepping out does (regrouping, selecting the instance), since those
    // objects are no longer the document's.
    size_t valid = 0;
    for (; valid < _scope.size(); ++valid)
    {
        const EditScope& level = _scope[valid];
        const bool ok = level.group ? alive(level.frame)
                                    : alive(level.timeline) && alive(level.symbol) && alive(level.instance);
        if (!ok)
            break;
    }
    bool changed = false;
    if (valid < _scope.size())
    {
        _scope.resize(valid);
        if (_selection)
            _selection->clear();
        changed = true;
    }
    // A group whose taking apart was undone is whole again on its frame:
    // stepping out of it now just leaves it.
    while (!_scope.empty() && _scope.back().group)
    {
        const EditScope& level = _scope.back();
        if (std::find(level.frame->elements.begin(), level.frame->elements.end(), level.group) ==
            level.frame->elements.end())
            break;
        exitScope();
    }
    clearCaches();
    if (changed)
        emit scopeChanged();
    update();
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
        if (_commandStack->topCommand() == region.split && !_commandStack->isInMacro() &&
            !_commandStack->canRedo())
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

bool PhoenixView::selectRegionsIn(const QPainterPath& area, bool additive, const fla::Element* only)
{
    if (!_selection || !_commandStack || _committingMerge)
        return false;

    std::vector<fla::DOMElement*> picked;

    for (fla::Element* element : elementsIn(area.boundingRect()))
    {
        if (only && element != only)
            continue;
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
            if (!layer || !layer->isVisible() || layer->isLocked())
                continue;

            // Folders hold no artwork; a guide layer can be drawn on, as a
            // motion guide's path is.
            if (layer->layerType == fla::Layer::Type::Folder)
                continue;

            // A guide is drawn on when it is the current layer, never by
            // default.
            if (!fallback && layer->layerType != fla::Layer::Type::Guide)
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
    pushGuidesToSnapper();
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
    if (!inSymbol())
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
            SnapEntry entry;
            entry.element = element;
            entry.bounds = elementBounds(element);
            if (!entry.bounds.isValid())
                continue;
            gatherSnapOutline(element, entry);
            _snapBounds.push_back(std::move(entry));
        }
        _snapBoundsValid = true;
    }

    for (const SnapEntry& entry : _snapBounds)
    {
        fla::Element* element = entry.element;
        bool skip = false;
        for (const fla::Element* excluded : exclude)
            skip = skip || excluded == element;
        if (skip)
            continue;

        const QRectF bounds = entry.bounds;

        snapper.addCandidateX(bounds.left());
        snapper.addCandidateX(bounds.center().x());
        snapper.addCandidateX(bounds.right());
        snapper.addCandidateY(bounds.top());
        snapper.addCandidateY(bounds.center().y());
        snapper.addCandidateY(bounds.bottom());

        // The box's corners and center are points too, for whatever has no
        // vertices of its own (an instance, a bitmap, text).
        for (const QPointF& corner : {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(),
                 bounds.bottomRight(), bounds.center()})
            snapper.addCandidatePoint(corner.x(), corner.y());
        for (const QPointF& vertex : entry.vertices)
            snapper.addCandidatePoint(vertex.x(), vertex.y());
        for (const QLineF& piece : entry.outline)
            snapper.addCandidateSegment(piece.x1(), piece.y1(), piece.x2(), piece.y2());
    }
}

void PhoenixView::gatherSnapOutline(const fla::Element* element, SnapEntry& entry)
{
    if (!element)
        return;

    if (element->elementType() == fla::Element::Type::Group)
    {
        for (const fla::Element* member : static_cast<const fla::Group*>(element)->members)
            gatherSnapOutline(member, entry);
        return;
    }
    if (element->elementType() != fla::Element::Type::Shape)
        return;

    const fla::Transform shown = shownTransformOf(element);
    const QTransform toDocument(shown.m11, shown.m12, shown.m21, shown.m22, shown.tx, shown.ty);
    const auto place = [&](const fla::Point& point) { return toDocument.map(QPointF(point.x, point.y)); };

    // Curves are laid down as a few straight pieces: a handle that lands
    // within a fraction of a pixel of the curve is close enough.
    constexpr int kCurvePieces = 8;

    for (const fla::Edge* edge : static_cast<const fla::Shape*>(element)->edges)
    {
        if (!edge || !edge->visible)
            continue;
        for (const fla::Path* path : edge->paths)
        {
            if (!path || !path->visible)
                continue;
            fla::Point current;
            bool haveCurrent = false;
            for (const fla::PathSegment* segment : path->segments)
            {
                if (!segment || segment->points.empty())
                    continue;
                const std::vector<fla::Point>& pts = segment->points;
                const fla::Point& end = pts.back();
                switch (segment->command)
                {
                case fla::PathSegment::Command::Move:
                    entry.vertices.push_back(place(end));
                    break;
                case fla::PathSegment::Command::Line:
                    entry.vertices.push_back(place(end));
                    if (haveCurrent)
                        entry.outline.push_back(QLineF(place(current), place(end)));
                    break;
                case fla::PathSegment::Command::Quad:
                case fla::PathSegment::Command::Cubic:
                {
                    entry.vertices.push_back(place(end));
                    if (!haveCurrent)
                        break;
                    const bool quad = segment->command == fla::PathSegment::Command::Quad;
                    if ((quad && pts.size() < 2) || (!quad && pts.size() < 3))
                        break;
                    const fla::Curve curve = quad ? fla::Curve::quadratic(current, pts[0], pts[1])
                                                  : fla::Curve::cubic(current, pts[0], pts[1], pts[2]);
                    QPointF previous = place(current);
                    for (int i = 1; i <= kCurvePieces; ++i)
                    {
                        const QPointF next = place(curve.pointAt(static_cast<double>(i) / kCurvePieces));
                        entry.outline.push_back(QLineF(previous, next));
                        previous = next;
                    }
                    break;
                }
                case fla::PathSegment::Command::Close:
                    continue;
                }
                current = end;
                haveCurrent = true;
            }
        }
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

    // A cross at a registration point: the symbol being edited's origin, and
    // each selected instance's, as Animate marks them.
    const auto crosshair = [&painter, scale](const QPointF& at, double reach) {
        const double r = reach * scale;
        painter.setPen(QPen(QColor(255, 255, 255), 3.0 * scale));
        painter.drawLine(QPointF(at.x() - r, at.y()), QPointF(at.x() + r, at.y()));
        painter.drawLine(QPointF(at.x(), at.y() - r), QPointF(at.x(), at.y() + r));
        painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
        painter.drawLine(QPointF(at.x() - r, at.y()), QPointF(at.x() + r, at.y()));
        painter.drawLine(QPointF(at.x(), at.y() - r), QPointF(at.x(), at.y() + r));
    };
    painter.save();
    if (inSymbol())
        crosshair(QPointF(0.0, 0.0), 9.0);
    if (_selection && showBounds)
    {
        for (fla::DOMElement* selected : _selection->elements())
        {
            const auto* instance = dynamic_cast<const fla::SymbolInstance*>(selected);
            if (instance && owningFrame(instance))
            {
                const fla::Transform shown = shownTransformOf(instance);
                crosshair(QPointF(shown.tx, shown.ty), 5.0);
            }
        }
    }
    painter.restore();

    // Shape hints on the keyframe shown: lettered circles, yellow at a tween's
    // start and green at its end, as in Animate.
    {
        bool start = true;
        if (fla::Frame* tween = shapeHintTween(start))
        {
            const std::vector<fla::ShapeHint> hints = fla::hintsOf(*tween);
            const double r = 8.0 * scale;
            QFont font = painter.font();
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(11.0 * scale))));
            font.setBold(true);
            painter.save();
            painter.setFont(font);
            for (size_t i = 0; i < hints.size() && i < 26; ++i)
            {
                const fla::Point& p = start ? hints[i].start : hints[i].end;
                const QPointF at(p.x, p.y);
                painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
                painter.setBrush(start ? QColor(255, 220, 0) : QColor(60, 200, 60));
                painter.drawEllipse(at, r, r);
                painter.setPen(QColor(0, 0, 0));
                painter.drawText(QRectF(at.x() - r, at.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
                    QString(QChar('a' + static_cast<int>(i))));
            }
            painter.restore();
        }
    }

    // The motion path of each selected object motion tween: a line through
    // where its transformation point is on each frame, a dot a frame, a
    // square at each keyframe, the frame shown ringed.
    for (const MotionPath& path : motionPaths())
    {
        painter.save();
        painter.setPen(QPen(path.color, 1.0 * scale));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(QPolygonF(QVector<QPointF>(path.points.begin(), path.points.end())));
        painter.setPen(Qt::NoPen);
        painter.setBrush(path.color);
        for (const QPointF& point : path.points)
            painter.drawEllipse(point, 1.5 * scale, 1.5 * scale);
        for (int key : path.keys)
        {
            if (key < 0 || key >= static_cast<int>(path.points.size()))
                continue;
            const double r = 3.5 * scale;
            painter.setPen(QPen(path.color, 1.0 * scale));
            painter.setBrush(QColor(255, 255, 255));
            painter.drawRect(QRectF(path.points[key].x() - r, path.points[key].y() - r, 2.0 * r, 2.0 * r));
        }
        if (path.current >= 0 && path.current < static_cast<int>(path.points.size()))
        {
            painter.setPen(QPen(path.color, 1.5 * scale));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(path.points[path.current], 5.0 * scale, 5.0 * scale);
        }
        painter.restore();
    }

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

            const QRectF bounds = shownElementBounds(element);
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

    // The bones of every armature on show, as Animate shows them on an
    // armature layer, ready to be dragged into a pose; and of one whose
    // shape or instances are selected, once for each pose frame.
    std::vector<const fla::Frame*> armatures;
    if (!_player || !_player->isPlaying())
    {
        for (const auto& entry : armatureFrames())
            armatures.push_back(entry.first);
    }
    if (_selection)
    {
        for (fla::DOMElement* selected : _selection->elements())
        {
            const fla::Frame* frame = owningFrame(selected);
            if (frame && std::find(armatures.begin(), armatures.end(), frame) == armatures.end())
                armatures.push_back(frame);
        }
    }
    if (!armatures.empty())
    {
        painter.save();
        for (const fla::Frame* frame : armatures)
            drawIKBones(painter, *frame, scale);
        painter.restore();
    }

    if (_activeTool)
    {
        painter.save();
        _activeTool->paintOverlay(*this, painter, scale);
        painter.restore();
    }
}


std::vector<std::pair<fla::Frame*, int>> PhoenixView::armatureFrames() const
{
    std::vector<std::pair<fla::Frame*, int>> found;
    if (!_flaDocument || !_flaDocument->document)
        return found;
    const int current = _player ? _player->currentFrame() : 0;
    for (const fla::Timeline* timeline : editableTimelines())
    {
        if (!timeline || !timeline->visible)
            continue;
        for (fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->isVisible())
                continue;
            for (fla::Frame* frame : layer->frames)
            {
                if (!frame || current < frame->index || current >= frame->index + std::max(frame->duration, 1))
                    continue;
                for (const std::string& child : frame->extraChildren)
                {
                    const size_t at = child.find('<');
                    if (at != std::string::npos && child.compare(at, 7, "<IKTree") == 0)
                    {
                        found.emplace_back(frame, current - frame->index);
                        break;
                    }
                }
            }
        }
    }
    return found;
}

PhoenixView::IKBoneHit PhoenixView::ikBoneAt(const QPointF& documentPos) const
{
    IKBoneHit best;
    double bestDistance = 8.0 / std::max(_zoom, 1.0e-6);
    for (const auto& entry : armatureFrames())
    {
        fla::Frame* frame = entry.first;
        for (size_t child = 0; child < frame->extraChildren.size(); ++child)
        {
            fla::IKArmature armature;
            if (!armature.parse(frame->extraChildren[child]))
                continue;
            const std::vector<fla::IKJoint> joints = armature.shownAt(entry.second);
            for (size_t i = 0; i < joints.size(); ++i)
            {
                const int parent = joints[i].parent;
                if (parent < 0)
                    continue;
                const QPointF head(joints[parent].position.x, joints[parent].position.y);
                const QPointF tail(joints[i].position.x, joints[i].position.y);
                const QPointF along = tail - head;
                const double length2 = QPointF::dotProduct(along, along);
                double t = length2 > 0.0 ? QPointF::dotProduct(documentPos - head, along) / length2 : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const QPointF nearest = head + along * t;
                const double distance = std::hypot(documentPos.x() - nearest.x(), documentPos.y() - nearest.y());
                if (distance <= bestDistance)
                {
                    bestDistance = distance;
                    best.frame = frame;
                    best.child = child;
                    best.joint = static_cast<int>(i);
                    best.into = entry.second;
                    best.tail = tail;
                    const fla::Layer* layer = dynamic_cast<const fla::Layer*>(frame->parent);
                    best.locked = layer && layer->isLocked();
                }
            }
        }
    }
    return best;
}

void PhoenixView::drawIKBones(QPainter& painter, const fla::Frame& frame, double scale)
{
    // Where in the pose span the playhead is: the bones move with the poses.
    const int into = _player ? std::max(0, std::min(_player->currentFrame() - frame.index,
        std::max(frame.duration, 1) - 1)) : 0;
    std::vector<fla::IKBone> bones;
    for (const std::string& child : frame.extraChildren)
    {
        const size_t at = child.find('<');
        if (at != std::string::npos && child.compare(at, 7, "<IKTree") == 0)
        {
            // An armature of symbols' joints move with its instances.
            fla::IKArmature armature;
            if (armature.parse(child) && !armature.bendsShape())
            {
                const std::vector<fla::IKJoint> joints = armature.shownAt(into);
                for (const fla::IKJoint& joint : joints)
                {
                    const int parent = joint.parent;
                    if (parent < 0)
                        continue;
                    fla::IKBone bone;
                    bone.head = joints[static_cast<size_t>(parent)].position;
                    bone.tail = joint.position;
                    if (std::hypot(bone.tail.x - bone.head.x, bone.tail.y - bone.head.y) > 1.0e-6)
                        bones.push_back(bone);
                }
                continue;
            }
            const std::vector<fla::IKBone> found = fla::ikBones(child, into);
            bones.insert(bones.end(), found.begin(), found.end());
        }
    }
    if (bones.empty())
        return;

    // In the armature layer's color, as Animate draws them: a slim kite from
    // the head joint, widest a fifth of the way along, to the tail joint,
    // with a round joint at each end. Sizes are in screen pixels.
    const fla::Layer* layer = dynamic_cast<const fla::Layer*>(frame.parent);
    const QColor ink = layer ? QColor(layer->color[0], layer->color[1], layer->color[2]) : QColor(153, 51, 204);
    QColor fill = ink;
    fill.setAlpha(160);
    const QColor edge = ink.darker(160);
    const double headRadius = 6.0 * scale;
    const double tailRadius = 3.5 * scale;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(edge, 1.0 * scale));
    for (const fla::IKBone& bone : bones)
    {
        const QPointF head(bone.head.x, bone.head.y);
        const QPointF tail(bone.tail.x, bone.tail.y);
        const double length = std::hypot(tail.x() - head.x(), tail.y() - head.y());
        if (length <= 1.0e-9)
            continue;
        const QPointF along = (tail - head) / length;
        const QPointF across(-along.y(), along.x());
        const QPointF widest = head + along * std::min(length * 0.2, 16.0 * scale);
        const double width = std::max(headRadius, std::min(length * 0.1, 11.0 * scale));
        painter.setBrush(fill);
        painter.drawPolygon(QPolygonF({head + across * headRadius * 0.8, widest + across * width, tail,
            widest - across * width, head - across * headRadius * 0.8}));
    }
    // The joints over the bones, so each shows whole where bones meet.
    painter.setBrush(QColor(255, 255, 255, 220));
    for (const fla::IKBone& bone : bones)
    {
        painter.drawEllipse(QPointF(bone.head.x, bone.head.y), headRadius, headRadius);
        painter.drawEllipse(QPointF(bone.tail.x, bone.tail.y), tailRadius, tailRadius);
    }
    painter.restore();
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

    // The rectangle as turned on screen sets the zoom; then the pan is
    // whatever brings its center to the middle of the view.
    double fitZoom = zoom;
    if (_rotation != 0.0)
    {
        const QRectF turned = QTransform().rotate(_rotation).mapRect(QRectF(QPointF(0, 0), documentRect.size()));
        fitZoom = qBound(_minZoom,
            std::min((width() - 2.0 * margin) / turned.width(), (height() - 2.0 * margin) / turned.height()),
            _maxZoom);
    }
    (void)docWidth;
    (void)docHeight;
    _zoom = fitZoom;
    _panX = 0.0;
    _panY = 0.0;
    const QPointF center = rootToWidget().map(documentRect.center());
    _panX = width() / 2.0 - center.x();
    _panY = height() / 2.0 - center.y();
    update();
}

void PhoenixView::centerStage()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    const QRectF stage(0.0, 0.0, _flaDocument->document->width, _flaDocument->document->height);
    const QPointF center = rootToWidget().map(stage.center());
    _panX += width() / 2.0 - center.x();
    _panY += height() / 2.0 - center.y();
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
    if (_flaDocument && _flaDocument->document && !inSymbol())
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
    _rotation = 0.0;
    update();
}

void PhoenixView::clearCaches()
{
    // A drawing waiting to merge that an undo took off its frame is let go
    // of: the history may free it once a new edit drops the redo.
    _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                       [](const PendingDrop& pending) {
                           return !pending.frame ||
                                  std::find(pending.frame->elements.begin(), pending.frame->elements.end(),
                                      pending.shape) == pending.frame->elements.end();
                       }),
        _pending.end());
    _pathCache.clear();
    _guideCache.clear();
    _boundsCache.clear();
    _artBoundsCache.clear();
    _snapBoundsValid = false;
    _filterCache.clear();
    _backdropCache.clear();
    _symbolBoundsCache.clear();
    _symbolReachCache.clear();
    _insideSelectedValid = false;
    ++_motionRevision;
    _staticValid = false;
    ++_revision;
}

void PhoenixView::forgetShape(const fla::Shape* shape)
{
    _pathCache.remove(shape);
    _boundsCache.remove(BoundsKey(shape, -1));
    _artBoundsCache.remove(BoundsKey(shape, -1));
    // The shape may be inside any symbol.
    _symbolBoundsCache.clear();
    _symbolReachCache.clear();
    _staticValid = false;
    ++_revision;
    update();
}

void PhoenixView::invalidateBounds()
{
    ++_motionRevision;
    _boundsCache.clear();
    _artBoundsCache.clear();
    _symbolBoundsCache.clear();
    _symbolReachCache.clear();
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
        // A group's members are in the space it is in.
        if (level.group)
            continue;
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
    if (!_scope.empty() && (_scope.back().timeline || inSymbol()))
    {
        if (_scope.back().timeline)
            timelines.push_back(_scope.back().timeline);
        return timelines;
    }

    if (_flaDocument && _flaDocument->document)
    {
        if (const fla::Timeline* scene = _flaDocument->document->currentScene())
            timelines.push_back(scene);
    }
    return timelines;
}

fla::Timeline* PhoenixView::scopeTimeline() const
{
    return _scope.empty() ? nullptr : _scope.back().timeline;
}

const fla::Symbol* PhoenixView::scopeSymbol() const
{
    // A group edited in place is inside whatever symbol encloses it.
    for (auto level = _scope.rbegin(); level != _scope.rend(); ++level)
        if (level->symbol)
            return level->symbol;
    return nullptr;
}

QString PhoenixView::scopeName(int depth) const
{
    if (depth <= 0)
    {
        if (_flaDocument && _flaDocument->document && !_flaDocument->document->timelines.empty())
        {
            const std::string& name = _flaDocument->document->currentScene()->name;
            if (!name.empty())
                return QString::fromStdString(name);
        }
        return "Scene 1";
    }

    if (depth > static_cast<int>(_scope.size()))
        return QString();

    if (_scope[depth - 1].group)
        return "Group";
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

QImage PhoenixView::libraryPreview(const QString& kind, const QString& name, const QSize& size, int frame)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || size.isEmpty())
        return image;
    // On the stage color, as Animate's library shows it.
    image.fill(QColor(document->backgroundColor[0], document->backgroundColor[1], document->backgroundColor[2]));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF area = QRectF(QPointF(0, 0), QSizeF(size)).adjusted(8, 8, -8, -8);
    const auto fit = [&](const QRectF& bounds) {
        const double scale = std::min(area.width() / std::max(bounds.width(), 1.0),
            area.height() / std::max(bounds.height(), 1.0));
        // Never blown up past twice its size, so a tiny symbol stays crisp
        // rather than filling the pane with a few pixels.
        const double s = std::min(scale, 2.0);
        return QTransform::fromTranslate(-bounds.center().x(), -bounds.center().y()) *
            QTransform::fromScale(s, s) * QTransform::fromTranslate(area.center().x(), area.center().y());
    };

    if (kind == "bitmap")
    {
        const QPixmap pixmap = libraryBitmap(name);
        if (!pixmap.isNull())
        {
            painter.setTransform(fit(QRectF(QPointF(0, 0), QSizeF(pixmap.size()))));
            painter.drawPixmap(0, 0, pixmap);
        }
        return image;
    }

    if (!document->symbolList)
        return image;
    const auto found = document->symbolList->symbolMap.find(name.toStdString());
    if (found == document->symbolList->symbolMap.end() || !found->second || found->second->timelines.empty())
        return image;
    const fla::Symbol* symbol = found->second;

    // The first frame, as the library shows it, whatever the playhead says.
    const bool wasTight = _tightBounds;
    _tightBounds = true;
    QRectF bounds;
    const fla::Timeline* timeline = symbol->timelines.front();
    for (const fla::Layer* layer : timeline->layers)
    {
        if (!layer || layer->layerType == fla::Layer::Type::Guide || layer->frames.empty())
            continue;
        for (const fla::Frame* keyframe : layer->frames)
        {
            for (const fla::Element* element : keyframe->elements)
            {
                const QRectF each = calculateElementBounds(element);
                if (each.isValid())
                    bounds = bounds.isNull() ? each : bounds.united(each);
            }
            if (frame < 0)
                break;
        }
    }
    _tightBounds = wasTight;
    if (bounds.isNull())
        return image;

    const int savedOverride = _frameOverride;
    const QColor savedOutline = _outlineColor;
    _frameOverride = 0;
    _outlineColor = QColor();
    painter.setTransform(fit(bounds));
    drawTimeline(painter, timeline, fla::LoopType::SingleFrame, std::max(frame, 0));
    _frameOverride = savedOverride;
    _outlineColor = savedOutline;
    return image;
}

void PhoenixView::setRulersVisible(bool on)
{
    _showRulers = on;
    update();
}

void PhoenixView::setGuidesVisible(bool on)
{
    _showGuides = on;
    update();
}

void PhoenixView::clearGuides()
{
    if (guidesX().empty() && guidesY().empty())
        return;
    setGuides({}, {}, "Clear Guides");
}

fla::Timeline* PhoenixView::guideTimeline() const
{
    if (!_flaDocument || !_flaDocument->document || _flaDocument->document->timelines.empty())
        return nullptr;
    return _flaDocument->document->currentScene();
}

std::vector<double>& PhoenixView::guidesX()
{
    fla::Timeline* timeline = guideTimeline();
    if (!timeline)
    {
        _noGuidesX.clear();
        return _noGuidesX;
    }
    return timeline->guidesX;
}

std::vector<double>& PhoenixView::guidesY()
{
    fla::Timeline* timeline = guideTimeline();
    if (!timeline)
    {
        _noGuidesY.clear();
        return _noGuidesY;
    }
    return timeline->guidesY;
}

void PhoenixView::setGuides(std::vector<double> xs, std::vector<double> ys, const char* name)
{
    fla::Timeline* timeline = guideTimeline();
    if (!timeline)
        return;
    auto command = std::make_unique<fla::SetGuidesCommand>(timeline, std::move(xs), std::move(ys), name);
    if (_commandStack)
    {
        _commandStack->push(std::move(command));
    }
    else
    {
        command->redo();
    }
    pushGuidesToSnapper();
    update();
}

void PhoenixView::pushGuidesToSnapper()
{
    if (!_snapper)
        return;
    std::vector<double> xs;
    std::vector<double> ys;
    toolGuides(xs, ys);
    _snapper->setGuides(xs, ys);
}

double PhoenixView::cameraScaleFor(const fla::Frame* frame)
{
    const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    const QTransform camera = cameraTransform(layer);
    const double scale = std::sqrt(std::fabs(camera.determinant()));
    return scale > 1.0e-9 ? scale : 1.0;
}

void PhoenixView::toolGuides(std::vector<double>& xs, std::vector<double>& ys)
{
    // Guides are in the document's space; inside a symbol the tools work in
    // the symbol's, so each guide is carried over when that is a plain
    // scale and shift, and left out otherwise.
    xs.clear();
    ys.clear();
    if (_showGuides && _flaDocument && _flaDocument->document)
    {
        bool invertible = false;
        const QTransform toScope = scopeToDocument().inverted(&invertible);
        if (invertible && std::fabs(toScope.m12()) < 1.0e-9 && std::fabs(toScope.m21()) < 1.0e-9)
        {
            for (double x : guidesX())
                xs.push_back(toScope.map(QPointF(x, 0)).x());
            for (double y : guidesY())
                ys.push_back(toScope.map(QPointF(0, y)).y());
        }
    }
    // The safe zones' and frames' edges, while they are shown.
    if (_showSafeZones && _flaDocument && _flaDocument->document)
    {
        const fla::Document* document = _flaDocument->document;
        bool invertible = false;
        const QTransform toScope = scopeToDocument().inverted(&invertible);
        if (invertible && std::fabs(toScope.m12()) < 1.0e-9 && std::fabs(toScope.m21()) < 1.0e-9)
        {
            std::vector<double> zoneXs;
            std::vector<double> zoneYs;
            fla::safezones::snapLines(document->width, document->height, fla::safezones::of(*document), zoneXs, zoneYs);
            for (double x : zoneXs)
                xs.push_back(toScope.map(QPointF(x, 0)).x());
            for (double y : zoneYs)
                ys.push_back(toScope.map(QPointF(0, y)).y());
        }
    }
}

void PhoenixView::setShowSafeZones(bool show)
{
    _showSafeZones = show;
    pushGuidesToSnapper();
    update();
}

void PhoenixView::safeZonesChanged()
{
    pushGuidesToSnapper();
    update();
}

void PhoenixView::drawSafeZones(QPainter& painter, const QTransform& toDevice, const fla::Document* document)
{
    namespace sz = fla::safezones;
    if (!document || document->width <= 0 || document->height <= 0)
        return;
    const sz::Settings settings = sz::of(*document);
    const double width = document->width;
    const double height = document->height;
    const auto polygon = [&](const sz::Rect& r) { return toDevice.map(QPolygonF(QRectF(r.x, r.y, r.width, r.height))); };
    const auto point = [&](double x, double y) { return toDevice.map(QPointF(x, y)); };

    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPixelSize(10);
    painter.setFont(font);
    // A label just inside a rectangle's top-left corner (or its bottom-left,
    // so two close rectangles' labels do not overlap), on a dark backing so
    // it reads over any art.
    const auto label = [&](const QPolygonF& shape, const QString& text, const QColor& color, bool bottom) {
        if (shape.size() < 4)
            return;
        const QFontMetrics metrics(font);
        const QPointF at = bottom ? shape[3] + QPointF(4.0, -3.0 - metrics.height()) : shape[0] + QPointF(4.0, 3.0);
        const QRectF box(at, QSizeF(metrics.horizontalAdvance(text) + 6.0, metrics.height()));
        painter.fillRect(box, QColor(0, 0, 0, 120));
        painter.setPen(color);
        painter.drawText(box.adjusted(3.0, 0.0, 0.0, 0.0), Qt::AlignLeft | Qt::AlignVCenter, text);
    };
    const QPolygonF stage = polygon(sz::Rect{0.0, 0.0, width, height});

    // Shading outside the frames that preview a crop.
    for (const sz::AspectFrame& frame : settings.frames)
    {
        if (!frame.on || !frame.shade)
            continue;
        QPainterPath outside;
        outside.addPolygon(stage);
        outside.closeSubpath();
        QPainterPath inside;
        inside.addPolygon(polygon(sz::frameRect(width, height, frame)));
        inside.closeSubpath();
        painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 110));
    }

    // The frames, each in its own color.
    for (const sz::AspectFrame& frame : settings.frames)
    {
        if (!frame.on)
            continue;
        const QColor color(frame.color[0], frame.color[1], frame.color[2], frame.color[3]);
        QPen pen(color, 1.0, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPolygonF shape = polygon(sz::frameRect(width, height, frame));
        painter.drawPolygon(shape);
        label(shape, QString::fromStdString(frame.name()), color, false);
    }

    // Title and action safe, the center cross and thirds, in what they
    // follow.
    const QColor zone = theme::color("stage.safeZone");
    const sz::Rect area = sz::zoneArea(width, height, settings);
    const auto zoneRect = [&](double percent, Qt::PenStyle style, const QString& name, bool bottom) {
        QPen pen(zone, 1.0, style);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPolygonF shape = polygon(sz::inset(area, percent));
        painter.drawPolygon(shape);
        label(shape, QString("%1 %2%").arg(name).arg(percent), zone, bottom);
    };
    if (settings.actionSafe)
        zoneRect(settings.actionPercent, Qt::DotLine, "Action safe", false);
    if (settings.titleSafe)
        zoneRect(settings.titlePercent, Qt::DashLine, "Title safe", true);
    if (settings.thirds)
    {
        QColor faint = zone;
        faint.setAlpha(110);
        QPen pen(faint, 1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        for (int i = 1; i <= 2; ++i)
        {
            const double x = area.x + area.width * i / 3.0;
            const double y = area.y + area.height * i / 3.0;
            painter.drawLine(point(x, area.y), point(x, area.y + area.height));
            painter.drawLine(point(area.x, y), point(area.x + area.width, y));
        }
    }
    if (settings.centerCross)
    {
        QPen pen(zone, 1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        const QPointF center = point(area.x + area.width / 2.0, area.y + area.height / 2.0);
        painter.drawLine(center - QPointF(9.0, 0.0), center + QPointF(9.0, 0.0));
        painter.drawLine(center - QPointF(0.0, 9.0), center + QPointF(0.0, 9.0));
    }
    painter.restore();
}

bool PhoenixView::beginGuideDrag(const QPointF& widgetPos)
{
    if (!_flaDocument || !_flaDocument->document || _rotation != 0.0)
        return false;
    bool invertible = false;
    const QTransform toWidget = rootToWidget();
    const QPointF root = toWidget.inverted(&invertible).map(widgetPos);
    if (!invertible)
        return false;

    if (_showRulers && (widgetPos.y() < kRulerSize || widgetPos.x() < kRulerSize))
    {
        // Out of the top ruler comes a horizontal guide, out of the left a
        // vertical one.
        _guideDrag = widgetPos.y() < kRulerSize ? 2 : 1;
        _guideIndex = -1;
        _guideValue = _guideDrag == 1 ? root.x() : root.y();
        _showGuides = true;
        setCursor(_guideDrag == 1 ? Qt::SplitHCursor : Qt::SplitVCursor);
        update();
        return true;
    }

    // An existing guide moves with the selection tool, unless guides are
    // locked.
    if (!_showGuides || _guidesLocked || _activeTool != _modifierTool)
        return false;
    for (size_t i = 0; i < guidesX().size(); ++i)
    {
        if (std::fabs(toWidget.map(QPointF(guidesX()[i], 0)).x() - widgetPos.x()) <= 3.0)
        {
            _guideDrag = 1;
            _guideIndex = static_cast<int>(i);
            _guideValue = guidesX()[i];
            return true;
        }
    }
    for (size_t i = 0; i < guidesY().size(); ++i)
    {
        if (std::fabs(toWidget.map(QPointF(0, guidesY()[i])).y() - widgetPos.y()) <= 3.0)
        {
            _guideDrag = 2;
            _guideIndex = static_cast<int>(i);
            _guideValue = guidesY()[i];
            return true;
        }
    }
    return false;
}

void PhoenixView::drawRulersAndGuides(QPainter& painter)
{
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    const QTransform toWidget = rootToWidget();
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (_showGuides)
    {
        const QColor color(document->guidesColor[0], document->guidesColor[1], document->guidesColor[2]);
        painter.setPen(QPen(color, 1.0));
        const auto drawGuide = [&](int kind, double value) {
            if (_rotation != 0.0)
            {
                const QPointF a = toWidget.map(kind == 1 ? QPointF(value, -1.0e5) : QPointF(-1.0e5, value));
                const QPointF b = toWidget.map(kind == 1 ? QPointF(value, 1.0e5) : QPointF(1.0e5, value));
                painter.drawLine(a, b);
                return;
            }
            if (kind == 1)
            {
                const double x = std::round(toWidget.map(QPointF(value, 0)).x()) + 0.5;
                painter.drawLine(QPointF(x, 0), QPointF(x, height()));
            }
            else
            {
                const double y = std::round(toWidget.map(QPointF(0, value)).y()) + 0.5;
                painter.drawLine(QPointF(0, y), QPointF(width(), y));
            }
        };
        for (size_t i = 0; i < guidesX().size(); ++i)
            if (!(_guideDrag == 1 && _guideIndex == static_cast<int>(i)))
                drawGuide(1, guidesX()[i]);
        for (size_t i = 0; i < guidesY().size(); ++i)
            if (!(_guideDrag == 2 && _guideIndex == static_cast<int>(i)))
                drawGuide(2, guidesY()[i]);
        if (_guideDrag)
            drawGuide(_guideDrag, std::round(_guideValue));
    }

    if (_showRulers && _rotation == 0.0)
    {
        const QColor back = theme::color("stage.ruler");
        const QColor ink = theme::color("stage.rulerText");
        painter.fillRect(QRect(0, 0, width(), kRulerSize), back);
        painter.fillRect(QRect(0, 0, kRulerSize, height()), back);
        painter.setPen(theme::color("stage.rulerLine"));
        painter.drawLine(0, kRulerSize, width(), kRulerSize);
        painter.drawLine(kRulerSize, 0, kRulerSize, height());

        // A labeled tick every so many stage pixels, chosen so labels sit
        // at least 50 screen pixels apart, with minor ticks between.
        // In the document's units: v counts units, a unit being so many
        // stage pixels.
        const double perUnit = _flaDocument && _flaDocument->document
            ? fla::docsettings::pixelsPer(fla::docsettings::unitOf(*_flaDocument->document)) : 1.0;
        const double scale = std::max(std::hypot(toWidget.m11(), toWidget.m12()), 1.0e-6) * perUnit;
        double step = 1.0;
        const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000};
        for (double candidate : steps)
        {
            step = candidate;
            if ((perUnit > 1.0 || candidate >= 1.0) && candidate * scale >= 50.0)
                break;
        }
        const auto rulerLabel = [](double v) {
            return std::fabs(v - std::round(v)) < 1.0e-6 ? QString::number(static_cast<long long>(std::lround(v)))
                                                         : QString::number(v, 'g', 4);
        };
        QFont font = painter.font();
        font.setPointSizeF(7.5);
        painter.setFont(font);
        const QPointF origin = toWidget.map(QPointF(0, 0));

        const double firstX = std::floor((kRulerSize - origin.x()) / scale / step) * step;
        for (double v = firstX; origin.x() + v * scale < width(); v += step / 5.0)
        {
            const double x = std::round(origin.x() + v * scale) + 0.5;
            if (x < kRulerSize)
                continue;
            const double phase = std::fmod(std::fabs(v) + step * 1.0e-4, step);
            const bool major = phase < step * 1.0e-3 || phase > step * (1.0 - 1.0e-3);
            painter.setPen(ink);
            painter.drawLine(QPointF(x, major ? 4 : kRulerSize - 5), QPointF(x, kRulerSize));
            if (major)
                painter.drawText(QPointF(x + 2, 10), rulerLabel(v));
        }
        const double firstY = std::floor((kRulerSize - origin.y()) / scale / step) * step;
        for (double v = firstY; origin.y() + v * scale < height(); v += step / 5.0)
        {
            const double y = std::round(origin.y() + v * scale) + 0.5;
            if (y < kRulerSize)
                continue;
            const double phase = std::fmod(std::fabs(v) + step * 1.0e-4, step);
            const bool major = phase < step * 1.0e-3 || phase > step * (1.0 - 1.0e-3);
            painter.setPen(ink);
            painter.drawLine(QPointF(major ? 4 : kRulerSize - 5, y), QPointF(kRulerSize, y));
            if (major)
            {
                painter.save();
                painter.translate(10, y + 2);
                painter.rotate(-90);
                painter.drawText(QPointF(-30, 0), rulerLabel(v).rightJustified(5, ' '));
                painter.restore();
            }
        }
        painter.fillRect(QRect(0, 0, kRulerSize, kRulerSize), back);
    }
    painter.restore();
}

namespace {

/// Whether a library drag is of something the stage can place: a symbol or
/// a bitmap. Folders and sounds are dragged only within the library.
bool placeable(const QMimeData* data)
{
    if (!data->hasFormat(LibraryTree::mimeType()))
        return false;
    const QString text = QString::fromUtf8(data->data(LibraryTree::mimeType()));
    return text.startsWith("symbol:") || text.startsWith("bitmap:");
}

} // namespace

void PhoenixView::dragEnterEvent(QDragEnterEvent* event)
{
    if (placeable(event->mimeData()))
        event->acceptProposedAction();
    else
        QWidget::dragEnterEvent(event);
}

void PhoenixView::dragMoveEvent(QDragMoveEvent* event)
{
    if (placeable(event->mimeData()))
        event->acceptProposedAction();
    else
        QWidget::dragMoveEvent(event);
}

void PhoenixView::dropEvent(QDropEvent* event)
{
    if (!placeable(event->mimeData()))
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

bool PhoenixView::enterGroup(fla::Group* group)
{
    fla::Frame* frame = group ? dynamic_cast<fla::Frame*>(group->parent) : nullptr;
    if (!frame || !_commandStack || _commandStack->isInMacro() ||
        std::find(frame->elements.begin(), frame->elements.end(), group) == frame->elements.end())
        return false;

    flushPendingMerge();
    if (_selection)
        _selection->clear();

    EditScope level;
    level.group = group;
    level.frame = frame;
    level.timeline = _scope.empty() ? nullptr : _scope.back().timeline;
    level.symbol = _scope.empty() ? nullptr : _scope.back().symbol;
    level.groupAttributes = group->extraAttributes;
    for (const fla::Element* element : frame->elements)
    {
        if (element != group)
            level.outside.insert(element);
    }
    // The members go out into the frame, where the tools work.
    _commandStack->push(fla::CommandPtr(new fla::UngroupCommand(frame, group, "Edit Group", _selection)));
    _commandStack->breakMergeChain();
    if (_selection)
        _selection->clear();
    level.entered = _commandStack->topCommand();
    _scope.push_back(std::move(level));
    clearCaches();
    emit scopeChanged();
    update();
    return true;
}

void PhoenixView::exitGroups()
{
    while (!_scope.empty() && _scope.back().group)
        exitScope();
}

bool PhoenixView::outsideGroup(const fla::Element* element) const
{
    if (_scope.empty() || !_scope.back().group || !element)
        return false;
    // The element itself, or the one in a frame it is part of.
    const fla::DOMElement* top = element;
    while (top->parent && top->parent->domType() != fla::DOMElement::DOMType::Frame)
        top = top->parent;
    const EditScope& level = _scope.back();
    return top->parent != level.frame || level.outside.count(static_cast<const fla::Element*>(top)) > 0;
}

bool PhoenixView::inSymbol() const
{
    return std::any_of(_scope.begin(), _scope.end(), [](const EditScope& level) { return !level.group; });
}

bool PhoenixView::exitScope()
{
    if (_scope.empty())
        return false;

    flushPendingMerge();
    if (_scope.back().group)
    {
        // What the frame holds that was not there before goes back into a
        // group, in place of the one taken apart.
        const EditScope level = _scope.back();
        _scope.pop_back();
        fla::Frame* frame = level.frame;
        const bool restored = std::find(frame->elements.begin(), frame->elements.end(), level.group) !=
                              frame->elements.end();
        fla::Element* result = restored ? level.group : nullptr;
        if (!restored && _commandStack && _commandStack->topCommand() == level.entered &&
            !_commandStack->canRedo())
        {
            // Nothing changed: no trace of having been in it.
            _commandStack->discardLast();
            result = level.group;
        }
        else if (!restored && _commandStack)
        {
            std::vector<fla::Element*> members;
            for (fla::Element* element : frame->elements)
            {
                if (element && !level.outside.count(element))
                    members.push_back(element);
            }
            if (!members.empty())
            {
                auto* command = new fla::GroupElementsCommand(frame, members, "Edit Group", _selection);
                fla::Group* regrouped = command->group();
                regrouped->extraAttributes = level.groupAttributes;
                _commandStack->push(fla::CommandPtr(command));
                _commandStack->breakMergeChain();
                result = regrouped;
            }
        }
        clearCaches();
        if (_selection)
        {
            _selection->clear();
            if (result)
                _selection->select(result);
        }
        emit scopeChanged();
        update();
        return true;
    }
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
    if (_rotation != 0.0)
    {
        // Turned about the pivot, inside the pan, so panning stays in
        // screen directions.
        transform.translate(_rotationPivot.x(), _rotationPivot.y());
        transform.rotate(_rotation);
        transform.translate(-_rotationPivot.x(), -_rotationPivot.y());
    }
    return transform;
}

void PhoenixView::setRotation(double degrees)
{
    // Kept within a half turn either way.
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180.0)
        degrees -= 360.0;
    if (degrees <= -180.0)
        degrees += 360.0;
    if (std::fabs(degrees) < 1.0e-9)
        degrees = 0.0;
    if (degrees == _rotation)
        return;
    _rotation = degrees;
    emit rotationChanged(_rotation);
    update();
}

PhoenixView::ViewState PhoenixView::viewState() const
{
    ViewState state;
    state.valid = true;
    state.zoom = _zoom;
    state.panX = _panX;
    state.panY = _panY;
    state.rotation = _rotation;
    state.pivot = _rotationPivot;
    return state;
}

void PhoenixView::setViewState(const ViewState& state)
{
    if (!state.valid)
        return;
    _zoom = std::clamp(state.zoom, _minZoom, _maxZoom);
    _panX = state.panX;
    _panY = state.panY;
    _rotationPivot = state.pivot;
    if (_rotation != state.rotation)
    {
        _rotation = state.rotation;
        emit rotationChanged(_rotation);
    }
    update();
}

void PhoenixView::setRotationPivot(const QPointF& stagePos)
{
    // A new pivot shifts the picture by the difference in where the turn
    // takes things; the pan takes that back out.
    const QPointF before = rootToWidget().map(QPointF(0.0, 0.0));
    _rotationPivot = stagePos;
    const QPointF after = rootToWidget().map(QPointF(0.0, 0.0));
    _panX -= after.x() - before.x();
    _panY -= after.y() - before.y();
    update();
}

int PhoenixView::frameCount() const
{
    const fla::Timeline* timeline = scopeTimeline();
    if (!timeline && _flaDocument && _flaDocument->document && !_flaDocument->document->timelines.empty())
        timeline = _flaDocument->document->currentScene();
    if (!timeline)
        return 0;
    int count = 0;
    for (const fla::Layer* layer : timeline->layers)
        if (layer)
            count = std::max(count, fla::timeline::layerEnd(*layer));
    return count;
}

QPointF PhoenixView::widgetToStage(const QPointF& widgetPos) const
{
    bool invertible = false;
    const QTransform inverse = rootToWidget().inverted(&invertible);
    return invertible ? inverse.map(widgetPos) : QPointF();
}

QPointF PhoenixView::stageToWidget(const QPointF& stagePos) const
{
    return rootToWidget().map(stagePos);
}

QRectF PhoenixView::stageInScope() const
{
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return QRectF();
    const QRectF stage(0.0, 0.0, document->width, document->height);
    bool invertible = false;
    const QTransform toScope = scopeToDocument().inverted(&invertible);
    return invertible ? toScope.mapRect(stage) : stage;
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

namespace {

/// Bounds kept for so many elements and frames at most; played through, an
/// animated instance adds one for each frame.
constexpr int kBoundsCacheLimit = 200000;

} // namespace

bool PhoenixView::animates(const fla::Element* element, int depth)
{
    if (!element || depth > 16)
        return false;
    if (element->elementType() == fla::Element::Type::SymbolInstance)
        return symbolLength(static_cast<const fla::SymbolInstance*>(element)->symbol) > 1;
    if (element->elementType() == fla::Element::Type::Group)
    {
        for (const fla::Element* member : static_cast<const fla::Group*>(element)->members)
            if (animates(member, depth + 1))
                return true;
    }
    return false;
}

int PhoenixView::boundsElapsed(const fla::Element* element)
{
    if (!animates(element))
        return -1;
    // Inside a symbol's frame, or while a frame is drawn, that frame's
    // playhead is set; asked from outside, an element is as far into its
    // keyframe as the playhead is.
    if (_boundsDepth > 0 || _frameDepth > 0)
        return std::max(0, _elapsed);
    const fla::Frame* frame = owningFrame(element);
    return frame ? std::max(0, shownFrame() - frame->index) : std::max(0, _elapsed);
}

QRectF PhoenixView::elementBounds(const fla::Element* element)
{
    const BoundsKey key(element, boundsElapsed(element));
    auto it = _artBoundsCache.constFind(key);
    if (it != _artBoundsCache.constEnd())
        return it.value();
    const bool wasTight = _tightBounds;
    _tightBounds = true;
    const QRectF bounds = calculateElementBounds(element);
    _tightBounds = wasTight;
    if (_artBoundsCache.size() > kBoundsCacheLimit)
        _artBoundsCache.clear();
    _artBoundsCache.insert(key, bounds);
    return bounds;
}

QRectF PhoenixView::getElementBounds(const fla::Element* element)
{
    if (_tightBounds)
        return calculateElementBounds(element);

    // Check cache first
    const BoundsKey key(element, boundsElapsed(element));
    auto it = _boundsCache.constFind(key);
    if (it != _boundsCache.constEnd())
        return it.value();

    // Calculate bounds
    QRectF bounds = calculateElementBounds(element);
    if (_boundsCache.size() > kBoundsCacheLimit)
        _boundsCache.clear();
    _boundsCache.insert(key, bounds);
    return bounds;
}

QRectF PhoenixView::calculateElementBounds(const fla::Element* element)
{
    NestingGuard nesting(_nesting);
    if (!nesting.ok)
        return QRectF();
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
                // What the instance shows now: its symbol's frame at the
                // playhead (a graphic part way through its span), not its
                // first. A puppet shows its first frame.
                int frameOffset = 0;
                {
                    ElapsedScope elapsed(_elapsed, std::max(0, boundsElapsed(element)));
                    frameOffset = instance->otherSymbolType == "puppet" ? 0 : symbolFrame(*instance);
                }
                struct Depth
                {
                    int& counter;
                    ~Depth() { --counter; }
                } depth{_boundsDepth};
                ++_boundsDepth;
                QRectF bounds;

                for (const fla::Timeline* timeline : symbol->timelines)
                {
                    for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
                    {
                        if (!layer || !layer->visible || layer->layerType == fla::Layer::Type::Guide ||
                            layer->layerType == fla::Layer::Type::Folder)
                            continue;
                        const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::SingleFrame, frameOffset);
                        if (!state.frame)
                            continue;
                        const QTransform parent = parentTransform(layer, state.frame, fla::LoopType::SingleFrame,
                            frameOffset);
                        // A tween moves its elements from where they are kept.
                        const bool tweened = state.tweenFrame || motionOf(state.frame);
                        ElapsedScope elapsed(_elapsed, state.offset);
                        for (size_t i = 0; i < state.frame->elements.size(); ++i)
                        {
                            const fla::Element* elem = state.frame->elements[i];
                            if (!elem || !elem->visible)
                                continue;
                            QRectF elemBounds = getElementBounds(elem);
                            if (tweened && elem->elementType() != fla::Element::Type::Group)
                            {
                                const fla::Transform& kept = elem->transform;
                                const fla::Transform shown = shownTransform(state, i);
                                const QTransform from(kept.m11, kept.m12, kept.m21, kept.m22, kept.tx, kept.ty);
                                const QTransform to(shown.m11, shown.m12, shown.m21, shown.m22, shown.tx, shown.ty);
                                bool invertible = false;
                                const QTransform back = from.inverted(&invertible);
                                if (invertible && from != to)
                                    elemBounds = (back * to).mapRect(elemBounds);
                            }
                            if (!parent.isIdentity())
                                elemBounds = parent.mapRect(elemBounds);

                            if (bounds.isNull())
                                bounds = elemBounds;
                            else
                                bounds = bounds.united(elemBounds);
                        }
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
        // The bitmap's own size, placed by the instance.
        QRectF bounds = calculateElementLocalBounds(element);
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
        // Half the stroke reaches out on each side; the rest is margin for
        // antialiasing, which the art's own edges leave out.
        double expansion = maxStrokeWeight / 2.0 + (_tightBounds ? 0.0 : 2.0);
        bounds = bounds.adjusted(-expansion, -expansion, expansion, expansion);
    }
    else if (!bounds.isNull())
    {
        // Default small margin for anti-aliasing
        if (!_tightBounds)
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
    NestingGuard nesting(_nesting);
    if (!nesting.ok || !element->visible)
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
    if (const fla::Timeline* scene = document->currentScene())
        drawTimelineBounds(painter, scene);
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

QRectF PhoenixView::symbolReach(const fla::Symbol* symbol)
{
    return calculateSymbolLocalBounds(symbol);
}

QRectF PhoenixView::memberReach(const fla::Element* element)
{
    if (!element)
        return QRectF();
    const QTransform transform(element->transform.m11, element->transform.m12, element->transform.m21,
        element->transform.m22, element->transform.tx, element->transform.ty);
    switch (element->elementType())
    {
    case fla::Element::Type::SymbolInstance:
    {
        const fla::Symbol* symbol = static_cast<const fla::SymbolInstance*>(element)->symbol;
        return transform.mapRect(symbol ? calculateSymbolLocalBounds(symbol) : QRectF(-50, -50, 100, 100));
    }
    case fla::Element::Type::Group:
    {
        // A group's members are placed in the group's parent's space.
        QRectF bounds;
        for (const fla::Element* member : static_cast<const fla::Group*>(element)->members)
        {
            const QRectF memberBounds = memberReach(member);
            if (!memberBounds.isNull())
                bounds = bounds.isNull() ? memberBounds : bounds.united(memberBounds);
        }
        return bounds;
    }
    default:
        return calculateElementBounds(element);
    }
}

QRectF PhoenixView::calculateSymbolLocalBounds(const fla::Symbol* symbol)
{
    NestingGuard nesting(_nesting);
    if (!nesting.ok)
        return QRectF();
    QRectF bounds;
    if (!symbol || symbol->timelines.empty())
        return QRectF(0, 0, 100, 100);
    const auto cached = _symbolBoundsCache.constFind(symbol);
    if (cached != _symbolBoundsCache.constEnd())
        return cached.value();

    // Over every frame of every layer: an animated symbol reaches wherever
    // any of its frames does, and culling by a first-frame box would drop
    // the rest.
    for (const fla::Timeline* timeline : symbol->timelines)
    {
        if (!timeline)
            continue;
        for (const fla::Layer* layer : timeline->layers)
        {
            // Hidden layers draw inside a symbol placed on the stage (as the
            // published movie has them), so they count; guides never draw.
            if (!layer || layer->layerType == fla::Layer::Type::Guide)
                continue;
            for (const fla::Frame* frame : layer->frames)
            {
                if (!frame)
                    continue;
                for (const fla::Element* elem : frame->elements)
                {
                    if (!elem)
                        continue;
                    // Each member where it is placed in the symbol, by its
                    // own matrix.
                    const QRectF elemBounds = memberReach(elem);
                    if (elemBounds.isNull())
                        continue;
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

    _symbolBoundsCache.insert(symbol, bounds);
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
        // The bitmap's own size, in pixels, from its top left corner.
        const QPixmap pixmap = libraryBitmap(QString::fromStdString(
            static_cast<const fla::BitmapInstance*>(element)->libraryItemName));
        if (!pixmap.isNull())
            return QRectF(0, 0, pixmap.width(), pixmap.height());
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

QRectF PhoenixView::shownLocalBounds(const fla::Element* element, int depth)
{
    if (!element)
        return QRectF();
    if (element->elementType() == fla::Element::Type::SymbolInstance && depth < 16)
    {
        const fla::SymbolInstance* instance = static_cast<const fla::SymbolInstance*>(element);
        const bool puppet = instance->otherSymbolType == "puppet";
        const QRectF bounds = shownSymbolBounds(instance->symbol, fla::LoopType::SingleFrame,
            puppet ? 0 : symbolFrame(*instance), depth + 1);
        if (bounds.isValid())
            return bounds;
    }
    return calculateElementLocalBounds(element);
}

QRectF PhoenixView::shownSymbolBounds(const fla::Symbol* symbol, fla::LoopType loopType, int firstFrame, int depth)
{
    QRectF bounds;
    if (!symbol)
        return bounds;
    for (const fla::Timeline* timeline : symbol->timelines)
    {
        if (!timeline || !timeline->visible)
            continue;
        for (const fla::Layer* layer : timeline->layers)
        {
            if (!layer || !layer->visible || layer->layerType == fla::Layer::Type::Guide ||
                layer->layerType == fla::Layer::Type::Folder)
                continue;
            const LayerFrame state = resolveLayerFrame(layer, loopType, firstFrame);
            if (!state.frame)
                continue;
            const QTransform parent = parentTransform(layer, state.frame, loopType, firstFrame);
            ElapsedScope elapsed(_elapsed, state.offset);
            for (size_t i = 0; i < state.frame->elements.size(); ++i)
            {
                const fla::Element* member = state.frame->elements[i];
                if (!member || !member->visible)
                    continue;
                QRectF local = shownLocalBounds(member, depth);
                if (!local.isValid())
                    continue;
                // Groups place their members themselves.
                if (member->elementType() != fla::Element::Type::Group)
                {
                    const fla::Transform t = shownTransform(state, i);
                    local = (QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty) * parent).mapRect(local);
                }
                else
                {
                    local = parent.mapRect(local);
                }
                bounds = bounds.isValid() ? bounds.united(local) : local;
            }
        }
    }
    return bounds;
}

QRectF PhoenixView::shownElementBounds(const fla::Element* element)
{
    const fla::Frame* frame = owningFrame(element);
    const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
    if (!layer)
        return getElementBounds(element);
    const LayerFrame state = resolveLayerFrame(layer, fla::LoopType::PlayOnce, 0);
    if (state.frame != frame)
        return getElementBounds(element);
    const auto found = std::find(frame->elements.begin(), frame->elements.end(), element);
    if (found == frame->elements.end() || element->elementType() == fla::Element::Type::Group)
        return getElementBounds(element);
    ElapsedScope elapsed(_elapsed, state.offset);
    const QRectF local = shownLocalBounds(element, 0);
    if (!local.isValid())
        return getElementBounds(element);
    const fla::Transform t = shownTransform(state, static_cast<size_t>(found - frame->elements.begin()));
    return (QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty) *
            parentTransform(layer, frame, fla::LoopType::PlayOnce, 0)).mapRect(local);
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
            if (!_insideSelectedValid)
            {
                // Every element of the symbol, inside groups and the symbols
                // it places, once.
                _insideSelected.clear();
                std::set<const fla::Symbol*> visited;
                std::vector<const fla::Symbol*> symbols{selectedSymbol};
                std::vector<const fla::Element*> pending;
                while (!symbols.empty() || !pending.empty())
                {
                    if (!pending.empty())
                    {
                        const fla::Element* member = pending.back();
                        pending.pop_back();
                        if (!member || !_insideSelected.insert(member).second)
                            continue;
                        if (member->elementType() == fla::Element::Type::Group)
                            for (const fla::Element* inner : static_cast<const fla::Group*>(member)->members)
                                pending.push_back(inner);
                        else if (member->elementType() == fla::Element::Type::SymbolInstance)
                            if (const fla::Symbol* nested = static_cast<const fla::SymbolInstance*>(member)->symbol)
                                symbols.push_back(nested);
                        continue;
                    }
                    const fla::Symbol* symbol = symbols.back();
                    symbols.pop_back();
                    if (!visited.insert(symbol).second)
                        continue;
                    for (const fla::Timeline* timeline : symbol->timelines)
                        for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
                            for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                                if (frame)
                                    pending.insert(pending.end(), frame->elements.begin(), frame->elements.end());
                }
                _insideSelectedValid = true;
            }
            return _insideSelected.count(element) > 0;
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
    NestingGuard nesting(_nesting);
    if (!symbol || !nesting.ok)
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
