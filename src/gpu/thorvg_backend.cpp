// ThorVG's WebGPU engine as a VectorBackend.

#include "gpu_device.h"
#include "vector_backend.h"

#include <thorvg.h>

#include <QElapsedTimer>
#include <QGradient>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

tvg::Matrix matrixOf(const QTransform& t)
{
    return tvg::Matrix{static_cast<float>(t.m11()), static_cast<float>(t.m21()), static_cast<float>(t.dx()),
        static_cast<float>(t.m12()), static_cast<float>(t.m22()), static_cast<float>(t.dy()), 0.0f, 0.0f, 1.0f};
}

uint8_t byte(double value)
{
    return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
}

/// \a path as \a shape's outline; closed figures closed, so strokes join
/// where they meet.
void appendPath(tvg::Shape* shape, const QPainterPath& path)
{
    QPointF start;
    QPointF last;
    for (int i = 0; i < path.elementCount(); ++i)
    {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo())
        {
            if (i > 0 && last == start)
                shape->close();
            shape->moveTo(static_cast<float>(e.x), static_cast<float>(e.y));
            start = last = QPointF(e.x, e.y);
        }
        else if (e.isLineTo())
        {
            shape->lineTo(static_cast<float>(e.x), static_cast<float>(e.y));
            last = QPointF(e.x, e.y);
        }
        else if (e.isCurveTo() && i + 2 < path.elementCount())
        {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            shape->cubicTo(static_cast<float>(e.x), static_cast<float>(e.y), static_cast<float>(c2.x),
                static_cast<float>(c2.y), static_cast<float>(end.x), static_cast<float>(end.y));
            last = QPointF(end.x, end.y);
            i += 2;
        }
    }
    if (path.elementCount() > 1 && last == start)
        shape->close();
}

class ThorvgBackend : public VectorBackend
{
public:
    explicit ThorvgBackend(GpuDevice& device)
        : _device(device)
    {
        tvg::Initializer::init(0);
        _canvas = tvg::WgCanvas::gen();
    }

    ~ThorvgBackend() override
    {
        delete _canvas;
        if (_texture)
            wgpuTextureRelease(_texture);
        tvg::Initializer::term();
    }

    QString name() const override { return "ThorVG"; }

    QImage render(const std::vector<RecordedItem>& items, const QSize& pixels, const QColor& clear,
        Timing& timing) override
    {
        if (!_canvas)
            return QImage();
        if (pixels != _size)
        {
            if (_texture)
                wgpuTextureRelease(_texture);
            _texture = _device.makeTarget(pixels);
            _size = pixels;
            const tvg::Result targeted = _canvas->target(_device.device(), _device.instance(), _texture,
                static_cast<uint32_t>(pixels.width()), static_cast<uint32_t>(pixels.height()), tvg::ColorSpace::ABGR8888, 1);
            if (targeted != tvg::Result::Success)
                qWarning("ThorVG: target failed (%d)", static_cast<int>(targeted));
        }
        QElapsedTimer clock;
        clock.start();
        _canvas->remove();
        // The background, as the first shape.
        tvg::Shape* background = tvg::Shape::gen();
        background->appendRect(0, 0, static_cast<float>(pixels.width()), static_cast<float>(pixels.height()));
        background->fill(static_cast<uint8_t>(clear.red()), static_cast<uint8_t>(clear.green()),
            static_cast<uint8_t>(clear.blue()), static_cast<uint8_t>(clear.alpha()));
        _canvas->add(background);
        for (const RecordedItem& item : items)
            if (tvg::Paint* paint = paintOf(item))
                _canvas->add(paint);
        const tvg::Result drawn = _canvas->draw(true);
        if (drawn != tvg::Result::Success)
            qWarning("ThorVG: draw failed (%d)", static_cast<int>(drawn));
        timing.encodeMs = clock.nsecsElapsed() / 1.0e6;
        clock.restart();
        const tvg::Result synced = _canvas->sync();
        if (synced != tvg::Result::Success)
            qWarning("ThorVG: sync failed (%d)", static_cast<int>(synced));
        _device.finish();
        timing.gpuMs = clock.nsecsElapsed() / 1.0e6;
        clock.restart();
        QImage image = _device.read(_texture, pixels);
        timing.readMs = clock.nsecsElapsed() / 1.0e6;
        return image;
    }

private:
    /// \a brush as a ThorVG gradient, its stops faded by \a opacity; null
    /// for a brush that is not one.
    tvg::Fill* gradientOf(const QBrush& brush, double opacity)
    {
        const QGradient* gradient = brush.gradient();
        if (!gradient)
            return nullptr;
        tvg::Fill* fill = nullptr;
        if (gradient->type() == QGradient::LinearGradient)
        {
            const auto* linear = static_cast<const QLinearGradient*>(gradient);
            auto* made = tvg::LinearGradient::gen();
            made->linear(static_cast<float>(linear->start().x()), static_cast<float>(linear->start().y()),
                static_cast<float>(linear->finalStop().x()), static_cast<float>(linear->finalStop().y()));
            fill = made;
        }
        else if (gradient->type() == QGradient::RadialGradient)
        {
            const auto* radial = static_cast<const QRadialGradient*>(gradient);
            auto* made = tvg::RadialGradient::gen();
            made->radial(static_cast<float>(radial->center().x()), static_cast<float>(radial->center().y()),
                static_cast<float>(radial->radius()), static_cast<float>(radial->focalPoint().x()),
                static_cast<float>(radial->focalPoint().y()), static_cast<float>(radial->focalRadius()));
            fill = made;
        }
        else
        {
            ++unsupported["conical gradient"];
            return nullptr;
        }
        if (gradient->coordinateMode() != QGradient::LogicalMode)
            ++unsupported["gradient not in logical coordinates"];
        std::vector<tvg::Fill::ColorStop> stops;
        for (const QGradientStop& stop : gradient->stops())
            stops.push_back({static_cast<float>(stop.first), static_cast<uint8_t>(stop.second.red()),
                static_cast<uint8_t>(stop.second.green()), static_cast<uint8_t>(stop.second.blue()),
                byte(stop.second.alphaF() * opacity)});
        fill->colorStops(stops.data(), static_cast<uint32_t>(stops.size()));
        fill->spread(gradient->spread() == QGradient::ReflectSpread ? tvg::FillSpread::Reflect
                : gradient->spread() == QGradient::RepeatSpread    ? tvg::FillSpread::Repeat
                                                                    : tvg::FillSpread::Pad);
        fill->transform(matrixOf(brush.transform()));
        return fill;
    }

    /// The shape \a item's clip is, on the device; null when it has none.
    tvg::Shape* clipOf(const RecordedItem& item)
    {
        if (!item.clipped)
            return nullptr;
        tvg::Shape* clip = tvg::Shape::gen();
        appendPath(clip, item.clip);
        clip->fillRule(item.clip.fillRule() == Qt::OddEvenFill ? tvg::FillRule::EvenOdd : tvg::FillRule::NonZero);
        clip->fill(255, 255, 255, 255);
        return clip;
    }

    bool blendOf(QPainter::CompositionMode mode, tvg::BlendMethod& method)
    {
        switch (mode)
        {
        case QPainter::CompositionMode_SourceOver: method = tvg::BlendMethod::Normal; return true;
        case QPainter::CompositionMode_Multiply: method = tvg::BlendMethod::Multiply; return true;
        case QPainter::CompositionMode_Screen: method = tvg::BlendMethod::Screen; return true;
        case QPainter::CompositionMode_Overlay: method = tvg::BlendMethod::Overlay; return true;
        case QPainter::CompositionMode_Darken: method = tvg::BlendMethod::Darken; return true;
        case QPainter::CompositionMode_Lighten: method = tvg::BlendMethod::Lighten; return true;
        case QPainter::CompositionMode_ColorDodge: method = tvg::BlendMethod::ColorDodge; return true;
        case QPainter::CompositionMode_ColorBurn: method = tvg::BlendMethod::ColorBurn; return true;
        case QPainter::CompositionMode_HardLight: method = tvg::BlendMethod::HardLight; return true;
        case QPainter::CompositionMode_SoftLight: method = tvg::BlendMethod::SoftLight; return true;
        case QPainter::CompositionMode_Difference: method = tvg::BlendMethod::Difference; return true;
        case QPainter::CompositionMode_Exclusion: method = tvg::BlendMethod::Exclusion; return true;
        case QPainter::CompositionMode_Plus: method = tvg::BlendMethod::Add; return true;
        default: return false;
        }
    }

    tvg::Paint* paintOf(const RecordedItem& item)
    {
        if (item.clipped && item.clip.isEmpty())
            return nullptr;
        tvg::Paint* paint = nullptr;
        switch (item.kind)
        {
        case RecordedItem::Kind::Fill:
        {
            const QBrush& brush = item.brush;
            if (brush.style() == Qt::NoBrush)
                return nullptr;
            if (brush.style() == Qt::TexturePattern)
            {
                // A bitmap fill: the image placed by the brush, cut to the
                // area (not tiled).
                ++unsupported["bitmap fill drawn untiled"];
                const QImage image = brush.textureImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
                tvg::Picture* picture = tvg::Picture::gen();
                picture->load(reinterpret_cast<const uint32_t*>(image.constBits()), static_cast<uint32_t>(image.width()),
                    static_cast<uint32_t>(image.height()), tvg::ColorSpace::ARGB8888, true);
                picture->transform(matrixOf(brush.transform()));
                tvg::Shape* area = tvg::Shape::gen();
                appendPath(area, item.path);
                area->fill(255, 255, 255, 255);
                picture->clip(area);
                picture->opacity(byte(item.opacity));
                paint = picture;
                break;
            }
            tvg::Shape* shape = tvg::Shape::gen();
            appendPath(shape, item.path);
            shape->fillRule(item.path.fillRule() == Qt::OddEvenFill ? tvg::FillRule::EvenOdd : tvg::FillRule::NonZero);
            if (tvg::Fill* gradient = gradientOf(brush, item.opacity))
                shape->fill(gradient);
            else if (brush.style() == Qt::SolidPattern)
            {
                const QColor c = brush.color();
                shape->fill(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()),
                    byte(c.alphaF() * item.opacity));
            }
            else
            {
                ++unsupported["brush pattern " + QString::number(brush.style())];
                const QColor c = brush.color();
                shape->fill(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()),
                    byte(c.alphaF() * item.opacity));
            }
            paint = shape;
            break;
        }
        case RecordedItem::Kind::Stroke:
        {
            tvg::Shape* shape = tvg::Shape::gen();
            appendPath(shape, item.path);
            const QPen& pen = item.pen;
            const double width = pen.widthF() > 0.0 ? pen.widthF() : 1.0;
            shape->strokeWidth(static_cast<float>(width));
            if (tvg::Fill* gradient = gradientOf(item.brush, item.opacity))
                shape->strokeFill(gradient);
            else
            {
                const QColor c = item.brush.color();
                shape->strokeFill(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()),
                    static_cast<uint8_t>(c.blue()), byte(c.alphaF() * item.opacity));
            }
            shape->strokeCap(pen.capStyle() == Qt::RoundCap ? tvg::StrokeCap::Round
                    : pen.capStyle() == Qt::SquareCap      ? tvg::StrokeCap::Square
                                                           : tvg::StrokeCap::Butt);
            shape->strokeJoin(pen.joinStyle() == Qt::RoundJoin ? tvg::StrokeJoin::Round
                    : pen.joinStyle() == Qt::BevelJoin        ? tvg::StrokeJoin::Bevel
                                                              : tvg::StrokeJoin::Miter);
            shape->strokeMiterlimit(static_cast<float>(pen.miterLimit()));
            if (pen.style() != Qt::SolidLine)
            {
                std::vector<float> dashes;
                for (const qreal dash : pen.dashPattern())
                    dashes.push_back(static_cast<float>(dash * width));
                if (!dashes.empty())
                    shape->strokeDash(dashes.data(), static_cast<uint32_t>(dashes.size()),
                        static_cast<float>(pen.dashOffset() * width));
            }
            paint = shape;
            break;
        }
        case RecordedItem::Kind::Image:
        {
            if (item.image.isNull())
                return nullptr;
            tvg::Picture* picture = tvg::Picture::gen();
            picture->load(reinterpret_cast<const uint32_t*>(item.image.constBits()),
                static_cast<uint32_t>(item.image.width()), static_cast<uint32_t>(item.image.height()),
                tvg::ColorSpace::ARGB8888, true);
            picture->transform(matrixOf(item.imageTransform));
            picture->opacity(byte(item.opacity));
            paint = picture;
            break;
        }
        case RecordedItem::Kind::Text:
            ++unsupported["text kept as text"];
            return nullptr;
        }
        if (!paint)
            return nullptr;
        tvg::BlendMethod method = tvg::BlendMethod::Normal;
        if (!blendOf(item.composition, method))
            ++unsupported["composition mode " + QString::number(item.composition)];
        else if (method != tvg::BlendMethod::Normal)
            paint->blend(method);
        if (tvg::Shape* clip = clipOf(item))
            paint->clip(clip);
        return paint;
    }

    GpuDevice& _device;
    tvg::WgCanvas* _canvas = nullptr;
    WGPUTexture _texture = nullptr;
    QSize _size;
};

} // namespace

std::unique_ptr<VectorBackend> makeThorvgBackend(GpuDevice& device)
{
    return std::make_unique<ThorvgBackend>(device);
}
