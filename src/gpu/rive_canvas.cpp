#include "rive_canvas.h"

#include "gpu_device.h"

#include "../gui/coloring_engine.h"

#include <rive/renderer/render_context.hpp>
#include <rive/renderer/rive_render_image.hpp>
#include <rive/renderer/rive_renderer.hpp>
#include <rive/renderer/webgpu/render_context_webgpu_impl.hpp>

#include <QElapsedTimer>
#include <QGradient>
#include <QHash>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTextItem>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

rive::Mat2D matrixOf(const QTransform& t)
{
    return rive::Mat2D(static_cast<float>(t.m11()), static_cast<float>(t.m12()), static_cast<float>(t.m21()),
        static_cast<float>(t.m22()), static_cast<float>(t.dx()), static_cast<float>(t.dy()));
}

/// \a color as Rive's (unpremultiplied) ARGB.
rive::ColorInt colorOf(const QColor& color)
{
    return (static_cast<uint32_t>(color.alpha()) << 24) | (static_cast<uint32_t>(color.red()) << 16) |
        (static_cast<uint32_t>(color.green()) << 8) | static_cast<uint32_t>(color.blue());
}

/// A fingerprint of \a path's fill rule and points: the same for paths
/// that are the same, so a shape drawn again finds the Rive path made for it
/// (QPainterPath copies its points, so its own data cannot tell).
quint64 contentKey(const QPainterPath& path)
{
    quint64 key = static_cast<quint64>(path.fillRule()) + 0x100u * static_cast<quint64>(path.elementCount());
    const auto add = [&key](quint64 value) {
        key = (key ^ value) * 0x9E3779B97F4A7C15ull;
        key ^= key >> 29;
    };
    for (int i = 0; i < path.elementCount(); ++i)
    {
        const QPainterPath::Element e = path.elementAt(i);
        quint64 x;
        quint64 y;
        std::memcpy(&x, &e.x, sizeof(x));
        std::memcpy(&y, &e.y, sizeof(y));
        add(static_cast<quint64>(e.type));
        add(x);
        add(y);
    }
    return key;
}

bool blendOf(QPainter::CompositionMode mode, rive::BlendMode& blend, float& additive)
{
    additive = 0.0f;
    switch (mode)
    {
    case QPainter::CompositionMode_SourceOver: blend = rive::BlendMode::srcOver; return true;
    case QPainter::CompositionMode_Multiply: blend = rive::BlendMode::multiply; return true;
    case QPainter::CompositionMode_Screen: blend = rive::BlendMode::screen; return true;
    case QPainter::CompositionMode_Overlay: blend = rive::BlendMode::overlay; return true;
    case QPainter::CompositionMode_Darken: blend = rive::BlendMode::darken; return true;
    case QPainter::CompositionMode_Lighten: blend = rive::BlendMode::lighten; return true;
    case QPainter::CompositionMode_ColorDodge: blend = rive::BlendMode::colorDodge; return true;
    case QPainter::CompositionMode_ColorBurn: blend = rive::BlendMode::colorBurn; return true;
    case QPainter::CompositionMode_HardLight: blend = rive::BlendMode::hardLight; return true;
    case QPainter::CompositionMode_SoftLight: blend = rive::BlendMode::softLight; return true;
    case QPainter::CompositionMode_Difference: blend = rive::BlendMode::difference; return true;
    case QPainter::CompositionMode_Exclusion: blend = rive::BlendMode::exclusion; return true;
    case QPainter::CompositionMode_Plus:
        // Source over, with the source added.
        blend = rive::BlendMode::srcOver;
        additive = 1.0f;
        return true;
    default:
        blend = rive::BlendMode::srcOver;
        return false;
    }
}

} // namespace

class RiveCanvas::Impl
{
public:
    class Engine;
    class Device;

    explicit Impl(GpuDevice& device);
    ~Impl();

    GpuDevice& gpu;
    std::unique_ptr<rive::gpu::RenderContext> context;
    rive::rcp<rive::gpu::RenderTargetWebGPU> target;
    WGPUTexture texture = nullptr;
    QSize size;
    /// What the render target was made for.
    QSize targetSize;
    WGPUTextureFormat targetFormat = WGPUTextureFormat_Undefined;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Device> device;
    rive::RiveRenderer* renderer = nullptr;
    int frame = 0;

    struct CachedPath
    {
        rive::rcp<rive::RenderPath> path;
        int frame = 0;
    };
    QHash<quint64, CachedPath> paths;
    struct CachedImage
    {
        rive::rcp<rive::RenderImage> image;
        int frame = 0;
    };
    QHash<qint64, CachedImage> images;
    QHash<quint64, rive::rcp<rive::RenderPaint>> fills;
    QHash<QByteArray, rive::rcp<rive::RenderPaint>> strokes;
    int pathsUsed = 0;
    int pathsMade = 0;

    /// \a path as Rive's, converted the first time it is drawn.
    rive::RenderPath* pathOf(const QPainterPath& path);
    /// \a image as Rive's, uploaded the first time it is drawn (by its
    /// cache key \a key, its pixels made by \a pixels only then).
    rive::RenderImage* imageOf(qint64 key, const std::function<QImage()>& pixels);
    /// Drops what the last frames did not use.
    void sweep();
};

/// A device measuring as a widget does (logical pixels, with their ratio),
/// whose painting goes to Rive.
class RiveCanvas::Impl::Device : public QPaintDevice
{
public:
    Device(Impl& impl)
        : _impl(impl)
    {
    }
    QSize logical;
    qreal ratio = 1.0;
    QPaintEngine* paintEngine() const override;

protected:
    int metric(PaintDeviceMetric metric) const override
    {
        switch (metric)
        {
        case PdmWidth: return logical.width();
        case PdmHeight: return logical.height();
        case PdmWidthMM: return logical.width() * 254 / 960;
        case PdmHeightMM: return logical.height() * 254 / 960;
        case PdmNumColors: return 0x7fffffff;
        case PdmDepth: return 32;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY: return 96;
        case PdmDevicePixelRatio: return static_cast<int>(std::lround(ratio));
        case PdmDevicePixelRatioScaled: return static_cast<int>(std::lround(ratio * devicePixelRatioFScale()));
        default: return QPaintDevice::metric(metric);
        }
    }

private:
    Impl& _impl;
};

/// QPainter's calls as Rive's: each drawing call becomes a draw under the
/// painter's transform and clips, with its opacity and blend mode.
class RiveCanvas::Impl::Engine : public QPaintEngine, public ColoringEngine
{
public:
    void pushColor(const fla::ColorTransform& color) override
    {
        _colors.push_back(color);
        _colorsKey = keyOf(_colors);
    }
    void popColor() override
    {
        if (!_colors.empty())
            _colors.pop_back();
        _colorsKey = keyOf(_colors);
    }

    explicit Engine(Impl& impl)
        : QPaintEngine(QPaintEngine::AllFeatures)
        , _impl(impl)
    {
    }

    bool begin(QPaintDevice*) override
    {
        _transform = QTransform();
        _clips.clear();
        _clipEnabled = true;
        _opacity = 1.0;
        _mode = QPainter::CompositionMode_SourceOver;
        _brush = QBrush();
        _pen = QPen();
        _colors.clear();
        _colorsKey = 0;
        return true;
    }
    bool end() override { return true; }
    Type type() const override { return QPaintEngine::User; }

    void updateState(const QPaintEngineState& state) override
    {
        const DirtyFlags dirty = state.state();
        if (dirty & DirtyTransform)
            _transform = state.transform();
        if (dirty & DirtyBrush)
            _brush = state.brush();
        if (dirty & DirtyPen)
            _pen = state.pen();
        if (dirty & DirtyOpacity)
            _opacity = state.opacity();
        if (dirty & DirtyCompositionMode)
            _mode = state.compositionMode();
        if (dirty & DirtyClipEnabled)
            _clipEnabled = state.isClipEnabled();
        if (dirty & DirtyClipPath)
            clip(state.clipOperation(), state.clipPath());
        if (dirty & DirtyClipRegion)
        {
            QPainterPath path;
            path.addRegion(state.clipRegion());
            clip(state.clipOperation(), path);
        }
    }

    void drawPath(const QPainterPath& path) override
    {
        if (_brush.style() != Qt::NoBrush)
            fill(path, _brush);
        if (_pen.style() != Qt::NoPen && _pen.brush().style() != Qt::NoBrush)
            stroke(path);
    }

    void drawRects(const QRectF* rects, int count) override
    {
        for (int i = 0; i < count; ++i)
        {
            QPainterPath path;
            path.addRect(rects[i]);
            drawPath(path);
        }
    }
    void drawRects(const QRect* rects, int count) override
    {
        for (int i = 0; i < count; ++i)
        {
            QPainterPath path;
            path.addRect(QRectF(rects[i]));
            drawPath(path);
        }
    }
    void drawEllipse(const QRectF& rect) override
    {
        QPainterPath path;
        path.addEllipse(rect);
        drawPath(path);
    }
    void drawLines(const QLineF* lines, int count) override
    {
        if (_pen.style() == Qt::NoPen)
            return;
        QPainterPath path;
        for (int i = 0; i < count; ++i)
        {
            path.moveTo(lines[i].p1());
            path.lineTo(lines[i].p2());
        }
        stroke(path);
    }
    void drawPolygon(const QPointF* points, int count, PolygonDrawMode mode) override
    {
        if (count <= 0)
            return;
        QPainterPath path;
        path.moveTo(points[0]);
        for (int i = 1; i < count; ++i)
            path.lineTo(points[i]);
        if (mode != PolylineMode)
            path.closeSubpath();
        path.setFillRule(mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
        if (mode != PolylineMode && _brush.style() != Qt::NoBrush)
            fill(path, _brush);
        if (_pen.style() != Qt::NoPen)
            stroke(path);
    }
    void drawPoints(const QPointF* points, int count) override
    {
        if (_pen.style() == Qt::NoPen)
            return;
        // Dots as wide as the pen.
        const double radius = std::max(0.5, _pen.widthF() / 2.0);
        QPainterPath path;
        for (int i = 0; i < count; ++i)
            path.addEllipse(points[i], radius, radius);
        fill(path, _pen.brush());
    }
    void drawTextItem(const QPointF& at, const QTextItem& item) override
    {
        QPainterPath path;
        path.addText(at, item.font(), item.text());
        fill(path, _pen.brush());
    }
    void drawImage(const QRectF& rect, const QImage& image, const QRectF& source, Qt::ImageConversionFlags) override
    {
        if (image.isNull() || source.isEmpty())
            return;
        const QRect whole = image.rect();
        const bool all = source.toAlignedRect() == whole;
        const qint64 key = all ? image.cacheKey() : image.cacheKey() ^ (qHash(source.toAlignedRect()) * 0x9E3779B97F4A7C15ull);
        rive::RenderImage* made = _impl.imageOf(imageKey(key),
            [&]() { return tinted(all ? image : image.copy(source.toAlignedRect())); });
        place(made, rect, source.size());
    }
    void drawPixmap(const QRectF& rect, const QPixmap& pixmap, const QRectF& source) override
    {
        if (pixmap.isNull() || source.isEmpty())
            return;
        const bool all = source.toAlignedRect() == pixmap.rect();
        const qint64 key = all ? pixmap.cacheKey() : pixmap.cacheKey() ^ (qHash(source.toAlignedRect()) * 0x9E3779B97F4A7C15ull);
        rive::RenderImage* made = _impl.imageOf(imageKey(key), [&]() {
            return tinted(all ? pixmap.toImage() : pixmap.copy(source.toAlignedRect()).toImage());
        });
        place(made, rect, source.size());
    }
    void drawTiledPixmap(const QRectF& rect, const QPixmap& pixmap, const QPointF& offset) override
    {
        QBrush brush(pixmap);
        brush.setTransform(QTransform::fromTranslate(rect.x() - offset.x(), rect.y() - offset.y()));
        QPainterPath path;
        path.addRect(rect);
        fill(path, brush);
    }

    /// Drawing calls asked of a blend mode Rive does not have, drawn as
    /// source over; counted for a log.
    int unsupportedModes = 0;

private:
    /// What the color effects in force make of \a color: the innermost
    /// first.
    QColor colored(const QColor& color) const
    {
        if (_colors.empty())
            return color;
        int r = color.red();
        int g = color.green();
        int b = color.blue();
        int a = color.alpha();
        for (auto it = _colors.rbegin(); it != _colors.rend(); ++it)
            it->apply(r, g, b, a);
        return QColor(r, g, b, a);
    }

    /// \a image as the color effects in force make it, pixel by pixel.
    QImage colored(const QImage& image) const
    {
        QImage out = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < out.height(); ++y)
        {
            auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x)
            {
                if (qAlpha(line[x]) == 0)
                    continue;
                int r = qRed(line[x]);
                int g = qGreen(line[x]);
                int b = qBlue(line[x]);
                int a = qAlpha(line[x]);
                for (auto it = _colors.rbegin(); it != _colors.rend(); ++it)
                    it->apply(r, g, b, a);
                line[x] = qRgba(r, g, b, a);
            }
        }
        return out;
    }

    static quint64 keyOf(const std::vector<fla::ColorTransform>& colors)
    {
        quint64 key = 0;
        const auto add = [&key](double value) {
            quint64 bits;
            std::memcpy(&bits, &value, sizeof(bits));
            key = (key ^ bits) * 0x9E3779B97F4A7C15ull;
            key ^= key >> 29;
        };
        for (const fla::ColorTransform& c : colors)
        {
            add(c.tintMultiplier);
            add(c.tintColor[0] | (c.tintColor[1] << 8) | (c.tintColor[2] << 16));
            add(c.brightness);
            add(c.redMultiplier);
            add(c.greenMultiplier);
            add(c.blueMultiplier);
            add(c.alphaMultiplier);
            add(c.redOffset);
            add(c.greenOffset);
            add(c.blueOffset);
            add(c.alphaOffset);
        }
        return key;
    }

    /// An image's key with the color effects in force: the same image
    /// colored another way is another texture.
    qint64 imageKey(qint64 key) const { return _colors.empty() ? key : static_cast<qint64>(key ^ _colorsKey); }

    struct Clip
    {
        rive::RenderPath* path = nullptr;
        rive::Mat2D matrix;
    };

    void clip(Qt::ClipOperation operation, const QPainterPath& path)
    {
        if (operation == Qt::NoClip)
        {
            _clips.clear();
            return;
        }
        if (operation == Qt::ReplaceClip)
            _clips.clear();
        _clips.push_back({_impl.pathOf(path), matrixOf(_transform)});
    }

    /// Starts a draw: the clips, then the transform \a matrix and the
    /// opacity. The draw ends with the renderer's restore().
    void start(rive::RiveRenderer& renderer, const rive::Mat2D& matrix)
    {
        renderer.save();
        rive::Mat2D current;
        const auto moveTo = [&](const rive::Mat2D& to) {
            renderer.transform(current.invertOrIdentity() * to);
            current = to;
        };
        if (_clipEnabled)
            for (const Clip& each : _clips)
            {
                moveTo(each.matrix);
                renderer.clipPath(each.path);
            }
        moveTo(matrix);
        if (_opacity < 1.0)
            renderer.modulateOpacity(static_cast<float>(_opacity));
    }

    rive::RenderPaint* paintFor(const QBrush& brush, rive::BlendMode blend, float additive)
    {
        if (brush.style() == Qt::SolidPattern)
        {
            const rive::ColorInt color = colorOf(colored(brush.color()));
            const quint64 key = (static_cast<quint64>(color) << 32) | (static_cast<quint64>(blend) << 8) |
                static_cast<quint64>(additive > 0.0f);
            auto found = _impl.fills.find(key);
            if (found == _impl.fills.end())
            {
                rive::rcp<rive::RenderPaint> paint = _impl.context->makeRenderPaint();
                paint->style(rive::RenderPaintStyle::fill);
                paint->color(color);
                paint->blendMode(blend);
                paint->additiveness(additive);
                found = _impl.fills.insert(key, paint);
            }
            return found->get();
        }
        // A gradient or an image: made for this draw.
        _scratch = _impl.context->makeRenderPaint();
        _scratch->style(rive::RenderPaintStyle::fill);
        _scratch->blendMode(blend);
        _scratch->additiveness(additive);
        shade(*_scratch, brush);
        return _scratch.get();
    }

    /// \a paint colored by \a brush: a gradient in the brush's space, an
    /// image tiled from where the brush puts it, or the brush's color.
    void shade(rive::RenderPaint& paint, const QBrush& brush)
    {
        if (const QGradient* gradient = brush.gradient())
        {
            std::vector<rive::ColorInt> colors;
            std::vector<float> stops;
            for (const QGradientStop& stop : gradient->stops())
            {
                colors.push_back(colorOf(colored(stop.second)));
                stops.push_back(static_cast<float>(stop.first));
            }
            rive::rcp<rive::RenderShader> shader;
            if (gradient->type() == QGradient::LinearGradient)
            {
                const auto* linear = static_cast<const QLinearGradient*>(gradient);
                shader = _impl.context->makeLinearGradient(static_cast<float>(linear->start().x()),
                    static_cast<float>(linear->start().y()), static_cast<float>(linear->finalStop().x()),
                    static_cast<float>(linear->finalStop().y()), colors.data(), stops.data(), colors.size());
            }
            else if (gradient->type() == QGradient::RadialGradient)
            {
                const auto* radial = static_cast<const QRadialGradient*>(gradient);
                shader = _impl.context->makeRadialGradient(static_cast<float>(radial->center().x()),
                    static_cast<float>(radial->center().y()), static_cast<float>(radial->radius()), colors.data(),
                    stops.data(), colors.size());
            }
            if (shader)
            {
                paint.shader(std::move(shader));
                paint.shaderTransform(matrixOf(brush.transform()));
                return;
            }
        }
        if (brush.style() == Qt::TexturePattern)
        {
            const QImage image = brush.textureImage();
            const QPixmap pixmap = image.isNull() ? brush.texture() : QPixmap();
            const QSize size = image.isNull() ? pixmap.size() : image.size();
            rive::RenderImage* made = image.isNull()
                ? _impl.imageOf(imageKey(pixmap.cacheKey()), [&]() { return tinted(pixmap.toImage()); })
                : _impl.imageOf(imageKey(image.cacheKey()), [&]() { return tinted(image); });
            if (made && !size.isEmpty())
            {
                paint.color(0xffffffff);
                // From the image's unit square to where the brush puts it.
                const QTransform unit = QTransform::fromScale(size.width(), size.height()) * brush.transform();
                paint.modulatedImage(made, rive::ImageSampler::LinearWrap(), matrixOf(unit));
                return;
            }
        }
        paint.color(colorOf(colored(brush.color())));
    }

    /// \a image colored by the effects in force, if any.
    QImage tinted(const QImage& image) const { return _colors.empty() ? image : colored(image); }

    void fill(const QPainterPath& path, const QBrush& brush)
    {
        rive::RiveRenderer* renderer = _impl.renderer;
        if (!renderer || path.isEmpty())
            return;
        rive::BlendMode blend;
        float additive;
        if (!blendOf(_mode, blend, additive))
            ++unsupportedModes;
        rive::RenderPath* made = _impl.pathOf(path);
        rive::RenderPaint* paint = paintFor(brush, blend, additive);
        start(*renderer, matrixOf(_transform));
        renderer->drawPath(made, paint);
        renderer->restore();
    }

    void stroke(const QPainterPath& path)
    {
        rive::RiveRenderer* renderer = _impl.renderer;
        if (!renderer || path.isEmpty())
            return;
        rive::BlendMode blend;
        float additive;
        if (!blendOf(_mode, blend, additive))
            ++unsupportedModes;
        // A cosmetic pen keeps its width on the device whatever the scale.
        double width = _pen.widthF() > 0.0 ? _pen.widthF() : 1.0;
        if (_pen.isCosmetic())
        {
            const double scale = std::sqrt(std::fabs(_transform.determinant()));
            if (scale > 0.0)
                width /= scale;
        }
        const rive::StrokeCap cap = _pen.capStyle() == Qt::RoundCap ? rive::StrokeCap::round
            : _pen.capStyle() == Qt::SquareCap                    ? rive::StrokeCap::square
                                                                  : rive::StrokeCap::butt;
        const rive::StrokeJoin join = _pen.joinStyle() == Qt::RoundJoin ? rive::StrokeJoin::round
            : _pen.joinStyle() == Qt::BevelJoin                        ? rive::StrokeJoin::bevel
                                                                       : rive::StrokeJoin::miter;
        rive::RenderPaint* paint = nullptr;
        const QBrush& brush = _pen.brush();
        if (brush.style() == Qt::SolidPattern)
        {
            QByteArray key(sizeof(uint32_t) + sizeof(float) + 4, 0);
            const uint32_t color = colorOf(colored(brush.color()));
            const float thickness = static_cast<float>(width);
            std::memcpy(key.data(), &color, sizeof(color));
            std::memcpy(key.data() + 4, &thickness, sizeof(thickness));
            key[8] = static_cast<char>(cap);
            key[9] = static_cast<char>(join);
            key[10] = static_cast<char>(blend);
            key[11] = static_cast<char>(additive > 0.0f);
            auto found = _impl.strokes.find(key);
            if (found == _impl.strokes.end())
            {
                rive::rcp<rive::RenderPaint> made = _impl.context->makeRenderPaint();
                made->style(rive::RenderPaintStyle::stroke);
                made->thickness(thickness);
                made->cap(cap);
                made->join(join);
                made->color(color);
                made->blendMode(blend);
                made->additiveness(additive);
                found = _impl.strokes.insert(key, made);
            }
            paint = found->get();
        }
        else
        {
            _scratch = _impl.context->makeRenderPaint();
            _scratch->style(rive::RenderPaintStyle::stroke);
            _scratch->thickness(static_cast<float>(width));
            _scratch->cap(cap);
            _scratch->join(join);
            _scratch->blendMode(blend);
            _scratch->additiveness(additive);
            shade(*_scratch, brush);
            paint = _scratch.get();
        }
        // Dashes are drawn solid: Rive's renderer has none of its own.
        rive::RenderPath* made = _impl.pathOf(path);
        start(*renderer, matrixOf(_transform));
        renderer->drawPath(made, paint);
        renderer->restore();
    }

    /// \a image (of \a pixels) stretched over \a rect.
    void place(rive::RenderImage* image, const QRectF& rect, const QSizeF& pixels)
    {
        rive::RiveRenderer* renderer = _impl.renderer;
        if (!renderer || !image || pixels.isEmpty())
            return;
        rive::BlendMode blend;
        float additive;
        if (!blendOf(_mode, blend, additive))
            ++unsupportedModes;
        // Rive draws an image over its own pixels from the origin.
        const QTransform onto = QTransform::fromScale(rect.width() / pixels.width(), rect.height() / pixels.height()) *
            QTransform::fromTranslate(rect.left(), rect.top()) * _transform;
        rive::ImageSampler sampler = rive::ImageSampler::LinearClamp();
        if (painter() && !painter()->testRenderHint(QPainter::SmoothPixmapTransform))
            sampler.filter = rive::ImageFilter::nearest;
        start(*renderer, matrixOf(onto));
        renderer->drawImage(image, sampler, blend, 1.0f, additive);
        renderer->restore();
    }

    Impl& _impl;
    QTransform _transform;
    std::vector<Clip> _clips;
    bool _clipEnabled = true;
    double _opacity = 1.0;
    QPainter::CompositionMode _mode = QPainter::CompositionMode_SourceOver;
    QBrush _brush;
    QPen _pen;
    rive::rcp<rive::RenderPaint> _scratch;
    std::vector<fla::ColorTransform> _colors;
    quint64 _colorsKey = 0;
};

QPaintEngine* RiveCanvas::Impl::Device::paintEngine() const
{
    return _impl.engine.get();
}

RiveCanvas::Impl::Impl(GpuDevice& device)
    : gpu(device)
{
    context = rive::gpu::RenderContextWebGPUImpl::MakeContext(wgpu::Adapter(device.adapter()),
        wgpu::Device(device.device()), wgpu::Queue(device.queue()), rive::gpu::RenderContextWebGPUImpl::ContextOptions());
    engine = std::make_unique<Engine>(*this);
    this->device = std::make_unique<Device>(*this);
}

RiveCanvas::Impl::~Impl()
{
    paths.clear();
    images.clear();
    fills.clear();
    strokes.clear();
    target.reset();
    context.reset();
    if (texture)
        wgpuTextureRelease(texture);
}

rive::RenderPath* RiveCanvas::Impl::pathOf(const QPainterPath& path)
{
    ++pathsUsed;
    const quint64 key = contentKey(path);
    auto found = paths.find(key);
    if (found != paths.end())
    {
        found->frame = frame;
        return found->path.get();
    }
    ++pathsMade;
    rive::RawPath raw;
    const int count = path.elementCount();
    QPointF start;
    QPointF last;
    for (int i = 0; i < count; ++i)
    {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo())
        {
            // A figure ending where it began is closed, so a stroke joins there.
            if (i > 0 && last == start)
                raw.close();
            raw.moveTo(static_cast<float>(e.x), static_cast<float>(e.y));
            start = last = QPointF(e.x, e.y);
        }
        else if (e.isLineTo())
        {
            raw.lineTo(static_cast<float>(e.x), static_cast<float>(e.y));
            last = QPointF(e.x, e.y);
        }
        else if (e.isCurveTo() && i + 2 < count)
        {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            raw.cubicTo(static_cast<float>(e.x), static_cast<float>(e.y), static_cast<float>(c2.x), static_cast<float>(c2.y),
                static_cast<float>(end.x), static_cast<float>(end.y));
            last = QPointF(end.x, end.y);
            i += 2;
        }
    }
    if (count > 1 && last == start)
        raw.close();
    CachedPath cached;
    cached.path = context->makeRenderPath(raw, path.fillRule() == Qt::OddEvenFill ? rive::FillRule::evenOdd
                                                                                   : rive::FillRule::nonZero);
    cached.frame = frame;
    return paths.insert(key, std::move(cached))->path.get();
}

rive::RenderImage* RiveCanvas::Impl::imageOf(qint64 key, const std::function<QImage()>& pixels)
{
    auto found = images.find(key);
    if (found != images.end())
    {
        found->frame = frame;
        return found->image.get();
    }
    // Rive's images are premultiplied RGBA, with their mipmaps.
    const QImage image = pixels().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull())
        return nullptr;
    const uint32_t width = static_cast<uint32_t>(image.width());
    const uint32_t height = static_cast<uint32_t>(image.height());
    uint32_t levels = 1;
    while ((width | height) >> levels)
        ++levels;
    std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y)
        std::memcpy(packed.data() + static_cast<size_t>(y) * width * 4, image.constScanLine(static_cast<int>(y)),
            static_cast<size_t>(width) * 4);
    auto* impl = context->static_impl_cast<rive::gpu::RenderContextWebGPUImpl>();
    rive::rcp<rive::gpu::Texture> texture =
        impl->makeImageTexture(width, height, levels, rive::GPUTextureFormat::rgba32, packed.data(), 1, 1, false, true);
    if (!texture)
        return nullptr;
    CachedImage cached;
    cached.image = rive::make_rcp<rive::RiveRenderImage>(std::move(texture));
    cached.frame = frame;
    return images.insert(key, std::move(cached))->image.get();
}

void RiveCanvas::Impl::sweep()
{
    // What two frames in a row left undrawn goes.
    for (auto it = paths.begin(); it != paths.end();)
        it = frame - it->frame > 1 ? paths.erase(it) : std::next(it);
    for (auto it = images.begin(); it != images.end();)
        it = frame - it->frame > 1 ? images.erase(it) : std::next(it);
    if (fills.size() > 4096)
        fills.clear();
    if (strokes.size() > 4096)
        strokes.clear();
}

RiveCanvas::RiveCanvas(GpuDevice& device)
    : _impl(std::make_unique<Impl>(device))
{
}

RiveCanvas::~RiveCanvas() = default;

bool RiveCanvas::valid(QString* why) const
{
    if (!_impl->context)
    {
        if (why)
            *why = QStringLiteral("the renderer could not start on this GPU");
        return false;
    }
    // Without the storage buffers its exact coverage needs, the renderer
    // falls back to MSAA, whose edges fall short of the CPU's.
    rive::gpu::RenderContext::FrameDescriptor frame;
    frame.renderTargetWidth = 1;
    frame.renderTargetHeight = 1;
    if (_impl->context->selectInterlockMode(frame) == rive::gpu::InterlockMode::depthStencil)
    {
        if (why)
            *why = QStringLiteral("this GPU's WebGPU lacks the storage buffers smooth edges need");
        return false;
    }
    return true;
}

void RiveCanvas::forget()
{
    _impl->paths.clear();
    _impl->images.clear();
}

wgpu::CommandBuffer RiveCanvas::render(WGPUTexture texture, WGPUTextureFormat format, const QSize& pixels, qreal ratio,
    const QColor& clear, const std::function<void(QPainter&)>& paint)
{
    Impl& d = *_impl;
    auto* impl = d.context->static_impl_cast<rive::gpu::RenderContextWebGPUImpl>();
    if (pixels != d.targetSize || format != d.targetFormat || !d.target)
    {
        d.target = impl->makeRenderTarget(static_cast<wgpu::TextureFormat>(format), static_cast<uint32_t>(pixels.width()),
            static_cast<uint32_t>(pixels.height()));
        d.targetSize = pixels;
        d.targetFormat = format;
    }
    wgpu::Texture target(texture);
    d.target->setTargetTextureView(target.CreateView(), target);
    ++d.frame;
    d.pathsUsed = 0;
    d.pathsMade = 0;

    QElapsedTimer clock;
    clock.start();
    rive::gpu::RenderContext::FrameDescriptor frame;
    frame.renderTargetWidth = static_cast<uint32_t>(pixels.width());
    frame.renderTargetHeight = static_cast<uint32_t>(pixels.height());
    frame.loadAction = rive::gpu::LoadAction::clear;
    frame.clearColor = colorOf(clear);
    frame.ditherMode = rive::gpu::DitherMode::none;
    d.context->beginFrame(frame);
    {
        rive::RiveRenderer renderer(d.context.get());
        d.renderer = &renderer;
        d.device->ratio = ratio;
        d.device->logical = QSize(qCeil(pixels.width() / ratio), qCeil(pixels.height() / ratio));
        QPainter painter(d.device.get());
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        paint(painter);
        painter.end();
        d.renderer = nullptr;
    }
    _timing.paintMs = clock.nsecsElapsed() / 1.0e6;
    clock.restart();
    wgpu::Device device(d.gpu.device());
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    rive::gpu::RenderContext::FlushResources flush;
    flush.renderTarget = d.target.get();
    flush.externalCommandBuffer = encoder.Get();
    d.context->flush(flush);
    wgpu::CommandBuffer commands = encoder.Finish();
    _timing.flushMs = clock.nsecsElapsed() / 1.0e6;
    _timing.paths = d.pathsUsed;
    _timing.pathsMade = d.pathsMade;
    return commands;
}

bool RiveCanvas::drawInto(WGPUTexture texture, WGPUTextureFormat format, const QSize& pixels, qreal ratio,
    const QColor& clear, const std::function<void(QPainter&)>& paint)
{
    Impl& d = *_impl;
    if (!d.context || !texture || pixels.isEmpty())
        return false;
    wgpu::CommandBuffer commands = render(texture, format, pixels, ratio, clear, paint);
    QElapsedTimer clock;
    clock.start();
    wgpu::Queue(d.gpu.queue()).Submit(1, &commands);
    _timing.gpuMs = clock.nsecsElapsed() / 1.0e6;
    _timing.readMs = 0.0;
    d.sweep();
    return true;
}

#ifndef __EMSCRIPTEN__
QImage RiveCanvas::draw(const QSize& pixels, qreal ratio, const QColor& clear, const std::function<void(QPainter&)>& paint)
{
    Impl& d = *_impl;
    if (!d.context || pixels.isEmpty())
        return QImage();
    if (pixels != d.size)
    {
        if (d.texture)
            wgpuTextureRelease(d.texture);
        d.texture = d.gpu.makeTarget(pixels);
        d.size = pixels;
    }
    wgpu::CommandBuffer commands = render(d.texture, WGPUTextureFormat_BGRA8Unorm, pixels, ratio, clear, paint);
    QElapsedTimer clock;
    clock.start();
    wgpu::Queue(d.gpu.queue()).Submit(1, &commands);
    d.gpu.finish();
    _timing.gpuMs = clock.nsecsElapsed() / 1.0e6;
    clock.restart();
    QImage image = d.gpu.read(d.texture, pixels);
    image.setDevicePixelRatio(ratio);
    _timing.readMs = clock.nsecsElapsed() / 1.0e6;
    d.sweep();
    return image;
}
#endif
