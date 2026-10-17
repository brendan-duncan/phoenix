#include "swf_export.h"

#include "../data/symbol_instance.h"
#include "paint_recorder.h"
#include "phoenix_view.h"

#include <QCryptographicHash>
#include <QGradient>

#include <algorithm>
#include <cmath>

namespace {

/// A path's outline for SWF: lines kept, cubic curves as quadratic ones
/// (four to a cubic), every subpath closed.
std::vector<swf::Segment> swfOutline(const QPainterPath& path)
{
    using K = swf::Segment::Kind;
    std::vector<swf::Segment> out;
    QPointF start, at;
    bool open = false;
    const auto close = [&]() {
        if (open && at != start)
            out.push_back({K::Line, start.x(), start.y()});
        open = false;
    };
    for (int i = 0; i < path.elementCount(); ++i)
    {
        const QPainterPath::Element& e = path.elementAt(i);
        if (e.type == QPainterPath::MoveToElement)
        {
            close();
            start = at = QPointF(e.x, e.y);
            out.push_back({K::Move, e.x, e.y});
            open = true;
        }
        else if (e.type == QPainterPath::LineToElement)
        {
            at = QPointF(e.x, e.y);
            out.push_back({K::Line, e.x, e.y});
        }
        else if (e.type == QPainterPath::CurveToElement && i + 2 < path.elementCount())
        {
            const QPointF p0 = at;
            const QPointF p1(e.x, e.y);
            const QPointF p2(path.elementAt(i + 1).x, path.elementAt(i + 1).y);
            const QPointF p3(path.elementAt(i + 2).x, path.elementAt(i + 2).y);
            const auto point = [&](double t) {
                const double u = 1.0 - t;
                return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
            };
            const auto tangent = [&](double t) {
                const double u = 1.0 - t;
                return (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * t) + (p3 - p2) * (3 * t * t);
            };
            // Each quarter as a quadratic whose control sits where the
            // tangents at its ends meet, or midway when they do not.
            for (int k = 0; k < 4; ++k)
            {
                const double t0 = k / 4.0, t1 = (k + 1) / 4.0;
                const QPointF a = point(t0), b = point(t1);
                const QPointF da = tangent(t0), db = tangent(t1);
                const double cross = da.x() * db.y() - da.y() * db.x();
                QPointF control = (a + b) / 2.0;
                if (std::fabs(cross) > 1.0e-9)
                {
                    const QPointF d = b - a;
                    const double s = (d.x() * db.y() - d.y() * db.x()) / cross;
                    const QPointF meet = a + da * s;
                    if (std::hypot(meet.x() - control.x(), meet.y() - control.y()) < std::hypot(d.x(), d.y()) * 2.0)
                        control = meet;
                }
                out.push_back({K::Quad, b.x(), b.y(), control.x(), control.y()});
            }
            at = p3;
            i += 2;
        }
    }
    close();
    return out;
}

swf::Color swfColor(const QColor& color, double opacity)
{
    return swf::Color{static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(std::lround(color.alpha() * std::clamp(opacity, 0.0, 1.0)))};
}

/// The premultiplied ARGB bytes SWF bitmaps hold.
std::vector<uint8_t> swfPixels(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(image.width()) * image.height() * 4);
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            out.push_back(static_cast<uint8_t>(qAlpha(line[x])));
            out.push_back(static_cast<uint8_t>(qRed(line[x])));
            out.push_back(static_cast<uint8_t>(qGreen(line[x])));
            out.push_back(static_cast<uint8_t>(qBlue(line[x])));
        }
    }
    return out;
}

} // namespace

swf::Matrix swfMatrix(const QTransform& t)
{
    return swf::Matrix{t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
}

SwfArt::SwfArt(swf::Writer& writer)
    : _writer(writer)
{}

int SwfArt::bitmapOf(const QImage& image)
{
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(pixels.constBits()), pixels.sizeInBytes()));
    hash.addData(QByteArray::number(pixels.width()) + "x" + QByteArray::number(pixels.height()));
    const QByteArray key = hash.result();
    const auto found = _bitmaps.find(key);
    if (found != _bitmaps.end())
        return found->second;
    const int id = _writer.defineBitmap(pixels.width(), pixels.height(), swfPixels(pixels));
    _bitmaps[key] = id;
    return id;
}

int SwfArt::shapeOf(const RecordedItem& item, double& alpha, swf::Writer::Blend& blend)
{
    alpha = item.opacity;
    blend = swf::Writer::Blend::Normal;
    QPainterPath area = item.path;
    if (item.clipped)
        area = area.intersected(item.clip);
    area = area.simplified();
    if (area.isEmpty())
        return 0;
    swf::Fill fill;
    if (item.kind == RecordedItem::Kind::Image)
    {
        fill.type = swf::Fill::Type::Bitmap;
        fill.bitmap = bitmapOf(item.image);
        fill.matrix = swfMatrix(item.imageTransform);
    }
    else
    {
        const QBrush& brush = item.brush;
        const QGradient* gradient = brush.gradient();
        if (gradient && (gradient->type() == QGradient::LinearGradient || gradient->type() == QGradient::RadialGradient))
        {
            fill.type = gradient->type() == QGradient::LinearGradient ? swf::Fill::Type::Linear : swf::Fill::Type::Radial;
            for (const QGradientStop& stop : gradient->stops())
                fill.stops.push_back({stop.first, swfColor(stop.second, alpha)});
            fill.spread = gradient->spread() == QGradient::ReflectSpread ? 1 : gradient->spread() == QGradient::RepeatSpread ? 2 : 0;
            QTransform square;
            if (fill.type == swf::Fill::Type::Linear)
            {
                const auto* linear = static_cast<const QLinearGradient*>(gradient);
                const QPointF from = linear->start(), to = linear->finalStop();
                const QPointF d = to - from;
                square = QTransform(d.x() / 1638.4, d.y() / 1638.4, -d.y() / 1638.4, d.x() / 1638.4,
                    (from.x() + to.x()) / 2.0, (from.y() + to.y()) / 2.0);
            }
            else
            {
                const auto* radial = static_cast<const QRadialGradient*>(gradient);
                const QPointF center = radial->center();
                const QPointF toward = radial->focalPoint() - center;
                const double radius = std::max(radial->radius(), 1.0e-6);
                const double angle = std::hypot(toward.x(), toward.y()) > 1.0e-9 ? std::atan2(toward.y(), toward.x()) : 0.0;
                const double k = radius / 819.2;
                square = QTransform(std::cos(angle) * k, std::sin(angle) * k, -std::sin(angle) * k, std::cos(angle) * k,
                    center.x(), center.y());
                fill.focal = std::clamp(std::hypot(toward.x(), toward.y()) / radius, 0.0, 0.99);
            }
            fill.matrix = swfMatrix(square * brush.transform());
            alpha = 1.0;
        }
        else if (brush.style() == Qt::TexturePattern)
        {
            fill.type = swf::Fill::Type::Bitmap;
            fill.repeat = true;
            fill.bitmap = bitmapOf(brush.textureImage());
            fill.matrix = swfMatrix(brush.transform());
        }
        else
        {
            fill.color = swfColor(brush.color(), alpha);
            alpha = 1.0;
        }
    }
    const std::vector<swf::Segment> outline = swfOutline(area);
    // The same outline and fill: the same shape.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const swf::Segment& segment : outline)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&segment), sizeof(segment)));
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(&fill.type), sizeof(fill.type)));
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(&fill.color), sizeof(fill.color)));
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(&fill.matrix), sizeof(fill.matrix)));
    hash.addData(QByteArray::number(fill.bitmap) + "/" + QByteArray::number(fill.spread) + "/" +
        QByteArray::number(fill.focal) + "/" + QByteArray::number(fill.repeat));
    for (const swf::Fill::Stop& stop : fill.stops)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&stop), sizeof(stop)));
    const QByteArray key = hash.result();
    int id = 0;
    const auto found = _shapes.find(key);
    if (found != _shapes.end())
        id = found->second;
    else
    {
        id = _writer.defineShape(fill, outline);
        _shapes[key] = id;
    }
    switch (item.composition)
    {
    case QPainter::CompositionMode_Multiply: blend = swf::Writer::Blend::Multiply; break;
    case QPainter::CompositionMode_Screen: blend = swf::Writer::Blend::Screen; break;
    case QPainter::CompositionMode_Lighten: blend = swf::Writer::Blend::Lighten; break;
    case QPainter::CompositionMode_Darken: blend = swf::Writer::Blend::Darken; break;
    case QPainter::CompositionMode_Difference: blend = swf::Writer::Blend::Difference; break;
    case QPainter::CompositionMode_Plus: blend = swf::Writer::Blend::Add; break;
    case QPainter::CompositionMode_Overlay: blend = swf::Writer::Blend::Overlay; break;
    case QPainter::CompositionMode_HardLight: blend = swf::Writer::Blend::HardLight; break;
    default: break;
    }
    return id;
}

SwfViewDrawer::SwfViewDrawer(PhoenixView& view, SwfArt& art)
    : _view(view)
    , _art(art)
{}

std::vector<fla::swfmovie::Piece> SwfViewDrawer::layerFrame(const fla::Layer& layer, int frame, swf::Writer&)
{
    std::vector<fla::swfmovie::Piece> pieces;
    for (const PhoenixView::CapturedPiece& captured : _view.captureLayerFrame(&layer, frame))
    {
        fla::swfmovie::Piece piece;
        piece.element = captured.element;
        if (captured.instance || captured.text)
        {
            piece.instance = captured.instance;
            piece.text = captured.text;
            piece.matrix = swfMatrix(captured.matrix);
            piece.color = captured.color;
            piece.filters = captured.filters;
            if (captured.instance)
                piece.blendMode = captured.instance->blendMode;
        }
        else
            for (const RecordedItem& item : captured.art)
            {
                fla::swfmovie::Piece::Shape shape;
                shape.id = _art.shapeOf(item, shape.alpha, shape.blend);
                if (shape.id)
                    piece.shapes.push_back(shape);
            }
        pieces.push_back(std::move(piece));
    }
    return pieces;
}
