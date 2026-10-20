// The Rive renderer's WebGPU backend as a VectorBackend.

#include "gpu_device.h"
#include "vector_backend.h"

#include <rive/renderer/render_context.hpp>
#include <rive/renderer/rive_render_image.hpp>
#include <rive/renderer/rive_renderer.hpp>
#include <rive/renderer/webgpu/render_context_webgpu_impl.hpp>

#include <QElapsedTimer>
#include <QGradient>
#include <QHash>
#include <QPainterPath>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

rive::Mat2D matrixOf(const QTransform& t)
{
    return rive::Mat2D(static_cast<float>(t.m11()), static_cast<float>(t.m12()), static_cast<float>(t.m21()),
        static_cast<float>(t.m22()), static_cast<float>(t.dx()), static_cast<float>(t.dy()));
}

/// \a color, its alpha faded by \a opacity, as Rive's (unpremultiplied) ARGB.
rive::ColorInt colorOf(const QColor& color, double opacity)
{
    const uint32_t alpha = static_cast<uint32_t>(std::clamp(std::lround(color.alphaF() * opacity * 255.0), 0L, 255L));
    return (alpha << 24) | (static_cast<uint32_t>(color.red()) << 16) | (static_cast<uint32_t>(color.green()) << 8)
        | static_cast<uint32_t>(color.blue());
}

/// \a path as a Rive path; closed figures closed, so strokes join where
/// they meet.
rive::rcp<rive::RenderPath> pathOf(rive::Factory& factory, const QPainterPath& path)
{
    rive::rcp<rive::RenderPath> made = factory.makeEmptyRenderPath();
    made->fillRule(path.fillRule() == Qt::OddEvenFill ? rive::FillRule::evenOdd : rive::FillRule::nonZero);
    QPointF start;
    QPointF last;
    for (int i = 0; i < path.elementCount(); ++i)
    {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo())
        {
            if (i > 0 && last == start)
                made->close();
            made->moveTo(static_cast<float>(e.x), static_cast<float>(e.y));
            start = last = QPointF(e.x, e.y);
        }
        else if (e.isLineTo())
        {
            made->lineTo(static_cast<float>(e.x), static_cast<float>(e.y));
            last = QPointF(e.x, e.y);
        }
        else if (e.isCurveTo() && i + 2 < path.elementCount())
        {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            made->cubicTo(static_cast<float>(e.x), static_cast<float>(e.y), static_cast<float>(c2.x),
                static_cast<float>(c2.y), static_cast<float>(end.x), static_cast<float>(end.y));
            last = QPointF(end.x, end.y);
            i += 2;
        }
    }
    if (path.elementCount() > 1 && last == start)
        made->close();
    return made;
}

const char* interlockName(rive::gpu::InterlockMode mode)
{
    switch (mode)
    {
    case rive::gpu::InterlockMode::rasterOrdering: return "rasterOrdering";
    case rive::gpu::InterlockMode::atomics: return "atomics";
    case rive::gpu::InterlockMode::clockwise: return "clockwise";
    case rive::gpu::InterlockMode::clockwiseAtomic: return "clockwiseAtomic";
    case rive::gpu::InterlockMode::depthStencil: return "depthStencil";
    }
    return "unknown";
}

class RiveBackend : public VectorBackend
{
public:
    explicit RiveBackend(GpuDevice& device)
        : _device(device)
    {
        _context = rive::gpu::RenderContextWebGPUImpl::MakeContext(wgpu::Adapter(device.adapter()),
            wgpu::Device(device.device()), wgpu::Queue(device.queue()),
            rive::gpu::RenderContextWebGPUImpl::ContextOptions());
        if (!_context)
            qWarning("Rive: no render context");
    }

    ~RiveBackend() override
    {
        _images.clear();
        _target.reset();
        _context.reset();
        if (_texture)
            wgpuTextureRelease(_texture);
    }

    QString name() const override { return "Rive"; }

    QImage render(const std::vector<RecordedItem>& items, const QSize& pixels, const QColor& clear,
        Timing& timing) override
    {
        if (!_context)
            return QImage();
        auto* impl = _context->static_impl_cast<rive::gpu::RenderContextWebGPUImpl>();
        if (pixels != _size)
        {
            if (_texture)
                wgpuTextureRelease(_texture);
            _texture = _device.makeTarget(pixels);
            _size = pixels;
            _target = impl->makeRenderTarget(wgpu::TextureFormat::BGRA8Unorm, static_cast<uint32_t>(pixels.width()),
                static_cast<uint32_t>(pixels.height()));
            wgpu::Texture texture(_texture);
            _target->setTargetTextureView(texture.CreateView(), texture);
        }
        QElapsedTimer clock;
        clock.start();
        rive::gpu::RenderContext::FrameDescriptor frame;
        frame.renderTargetWidth = static_cast<uint32_t>(pixels.width());
        frame.renderTargetHeight = static_cast<uint32_t>(pixels.height());
        frame.loadAction = rive::gpu::LoadAction::clear;
        frame.clearColor = colorOf(clear, 1.0);
        // No dither, as the other renderers have none.
        frame.ditherMode = rive::gpu::DitherMode::none;
        if (!_reported)
        {
            _reported = true;
            std::printf("Rive: %s interlock mode\n", interlockName(_context->selectInterlockMode(frame)));
        }
        _context->beginFrame(frame);
        {
            rive::RiveRenderer renderer(_context.get());
            for (const RecordedItem& item : items)
                draw(renderer, item);
        }
        wgpu::Device device(_device.device());
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        rive::gpu::RenderContext::FlushResources flush;
        flush.renderTarget = _target.get();
        flush.externalCommandBuffer = encoder.Get();
        _context->flush(flush);
        wgpu::CommandBuffer commands = encoder.Finish();
        timing.encodeMs = clock.nsecsElapsed() / 1.0e6;
        clock.restart();
        wgpu::Queue(_device.queue()).Submit(1, &commands);
        _device.finish();
        timing.gpuMs = clock.nsecsElapsed() / 1.0e6;
        clock.restart();
        QImage image = _device.read(_texture, pixels);
        timing.readMs = clock.nsecsElapsed() / 1.0e6;
        // Images are kept for the next frame only when it uses them again.
        for (auto it = _images.begin(); it != _images.end();)
        {
            if (!_used.contains(it.key()))
                it = _images.erase(it);
            else
                ++it;
        }
        _used.clear();
        return image;
    }

private:
    /// \a image as a Rive image, made once while frames go on using it.
    rive::RenderImage* imageOf(const QImage& image)
    {
        const qint64 key = image.cacheKey();
        _used.insert(key);
        auto found = _images.find(key);
        if (found != _images.end())
            return found->get();
        // Rive's images are premultiplied RGBA, with their mipmaps.
        const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        const uint32_t width = static_cast<uint32_t>(pixels.width());
        const uint32_t height = static_cast<uint32_t>(pixels.height());
        uint32_t levels = 1;
        while ((width | height) >> levels)
            ++levels;
        std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(packed.data() + static_cast<size_t>(y) * width * 4, pixels.constScanLine(static_cast<int>(y)),
                static_cast<size_t>(width) * 4);
        auto* impl = _context->static_impl_cast<rive::gpu::RenderContextWebGPUImpl>();
        rive::rcp<rive::gpu::Texture> texture = impl->makeImageTexture(width, height, levels,
            rive::GPUTextureFormat::rgba32, packed.data(), 1, 1, false, true);
        if (!texture)
            return nullptr;
        return _images.insert(key, rive::make_rcp<rive::RiveRenderImage>(std::move(texture)))->get();
    }

    /// \a brush as a Rive gradient, its stops faded by \a opacity; null for
    /// a brush that is not one. It is in the brush's own space; the paint's
    /// shader transform places it.
    rive::rcp<rive::RenderShader> gradientOf(const QBrush& brush, double opacity)
    {
        const QGradient* gradient = brush.gradient();
        if (!gradient)
            return nullptr;
        std::vector<rive::ColorInt> colors;
        std::vector<float> stops;
        for (const QGradientStop& stop : gradient->stops())
        {
            colors.push_back(colorOf(stop.second, opacity));
            stops.push_back(static_cast<float>(stop.first));
        }
        if (gradient->coordinateMode() != QGradient::LogicalMode)
            ++unsupported["gradient not in logical coordinates"];
        if (gradient->spread() != QGradient::PadSpread)
            ++unsupported["gradient spread other than pad"];
        if (gradient->type() == QGradient::LinearGradient)
        {
            const auto* linear = static_cast<const QLinearGradient*>(gradient);
            return _context->makeLinearGradient(static_cast<float>(linear->start().x()),
                static_cast<float>(linear->start().y()), static_cast<float>(linear->finalStop().x()),
                static_cast<float>(linear->finalStop().y()), colors.data(), stops.data(), colors.size());
        }
        if (gradient->type() == QGradient::RadialGradient)
        {
            const auto* radial = static_cast<const QRadialGradient*>(gradient);
            if (radial->focalPoint() != radial->center() || radial->focalRadius() > 0.0)
                ++unsupported["radial gradient focal point"];
            return _context->makeRadialGradient(static_cast<float>(radial->center().x()),
                static_cast<float>(radial->center().y()), static_cast<float>(radial->radius()), colors.data(),
                stops.data(), colors.size());
        }
        ++unsupported["conical gradient"];
        return nullptr;
    }

    /// Fills \a paint with \a brush (a gradient or a color).
    void fillPaint(rive::RenderPaint& paint, const QBrush& brush, double opacity)
    {
        if (rive::rcp<rive::RenderShader> gradient = gradientOf(brush, opacity))
        {
            paint.shader(std::move(gradient));
            paint.shaderTransform(matrixOf(brush.transform()));
        }
        else
            paint.color(colorOf(brush.color(), opacity));
    }

    bool blendOf(QPainter::CompositionMode mode, rive::BlendMode& blend)
    {
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
        case QPainter::CompositionMode_Plus: blend = rive::BlendMode::additive; return true;
        default: return false;
        }
    }

    void draw(rive::RiveRenderer& renderer, const RecordedItem& item)
    {
        if (item.clipped && item.clip.isEmpty())
            return;
        if (item.kind == RecordedItem::Kind::Text)
        {
            ++unsupported["text kept as text"];
            return;
        }
        if (item.kind == RecordedItem::Kind::Image && item.image.isNull())
            return;
        if (item.kind == RecordedItem::Kind::Fill && item.brush.style() == Qt::NoBrush)
            return;
        rive::BlendMode blend = rive::BlendMode::srcOver;
        if (!blendOf(item.composition, blend))
            ++unsupported["composition mode " + QString::number(item.composition)];
        // Plus is srcOver with the source added, to this renderer.
        const float additive = blend == rive::BlendMode::additive ? 1.0f : 0.0f;
        if (blend == rive::BlendMode::additive)
            blend = rive::BlendMode::srcOver;

        renderer.save();
        if (item.clipped)
        {
            rive::rcp<rive::RenderPath> clip = pathOf(*_context, item.clip);
            renderer.clipPath(clip.get());
        }
        switch (item.kind)
        {
        case RecordedItem::Kind::Fill:
        {
            const QBrush& brush = item.brush;
            rive::rcp<rive::RenderPath> path = pathOf(*_context, item.path);
            rive::rcp<rive::RenderPaint> paint = _context->makeRenderPaint();
            paint->style(rive::RenderPaintStyle::fill);
            paint->blendMode(blend);
            paint->additiveness(additive);
            if (brush.style() == Qt::TexturePattern)
            {
                // A bitmap fill: the image tiled across the area from where
                // the brush puts it, its color modulating the image.
                const QImage image = brush.textureImage();
                if (rive::RenderImage* made = imageOf(image))
                {
                    paint->color(colorOf(Qt::white, item.opacity));
                    // From the image's unit square to the device.
                    const QTransform unit = QTransform::fromScale(image.width(), image.height()) * brush.transform();
                    paint->modulatedImage(made, rive::ImageSampler::LinearWrap(), matrixOf(unit));
                    renderer.drawPath(path.get(), paint.get());
                }
                break;
            }
            if (brush.style() != Qt::SolidPattern && !brush.gradient())
                ++unsupported["brush pattern " + QString::number(brush.style())];
            fillPaint(*paint, brush, item.opacity);
            renderer.drawPath(path.get(), paint.get());
            break;
        }
        case RecordedItem::Kind::Stroke:
        {
            const QPen& pen = item.pen;
            rive::rcp<rive::RenderPath> path = pathOf(*_context, item.path);
            path->fillRule(rive::FillRule::nonZero);
            rive::rcp<rive::RenderPaint> paint = _context->makeRenderPaint();
            paint->style(rive::RenderPaintStyle::stroke);
            paint->thickness(static_cast<float>(pen.widthF() > 0.0 ? pen.widthF() : 1.0));
            paint->cap(pen.capStyle() == Qt::RoundCap ? rive::StrokeCap::round
                    : pen.capStyle() == Qt::SquareCap ? rive::StrokeCap::square
                                                      : rive::StrokeCap::butt);
            paint->join(pen.joinStyle() == Qt::RoundJoin ? rive::StrokeJoin::round
                    : pen.joinStyle() == Qt::BevelJoin   ? rive::StrokeJoin::bevel
                                                         : rive::StrokeJoin::miter);
            // Its miter limit is fixed, at 4.
            if ((pen.joinStyle() == Qt::MiterJoin || pen.joinStyle() == Qt::SvgMiterJoin) && pen.miterLimit() != 4.0)
                ++unsupported["miter limit other than 4"];
            if (pen.style() != Qt::SolidLine && pen.style() != Qt::NoPen)
                ++unsupported["dashed stroke drawn solid"];
            paint->blendMode(blend);
            paint->additiveness(additive);
            fillPaint(*paint, item.brush, item.opacity);
            renderer.drawPath(path.get(), paint.get());
            break;
        }
        case RecordedItem::Kind::Image:
        {
            if (rive::RenderImage* made = imageOf(item.image))
            {
                renderer.transform(matrixOf(item.imageTransform));
                renderer.drawImage(made, rive::ImageSampler::LinearClamp(), blend, static_cast<float>(item.opacity),
                    additive);
            }
            break;
        }
        case RecordedItem::Kind::Text:
            break;
        }
        renderer.restore();
    }

    GpuDevice& _device;
    std::unique_ptr<rive::gpu::RenderContext> _context;
    rive::rcp<rive::gpu::RenderTargetWebGPU> _target;
    WGPUTexture _texture = nullptr;
    QSize _size;
    QHash<qint64, rive::rcp<rive::RenderImage>> _images;
    QSet<qint64> _used;
    bool _reported = false;
};

} // namespace

std::unique_ptr<VectorBackend> makeRiveBackend(GpuDevice& device)
{
    return std::make_unique<RiveBackend>(device);
}
