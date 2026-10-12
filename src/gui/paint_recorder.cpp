#include "paint_recorder.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QTextItem>

class RecordingPaintEngine : public QPaintEngine
{
public:
    explicit RecordingPaintEngine(PaintRecorder* recorder)
        : QPaintEngine(QPaintEngine::AllFeatures)
        , _recorder(recorder)
    {
    }

    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    void updateState(const QPaintEngineState&) override {}
    Type type() const override { return QPaintEngine::User; }

    void drawPath(const QPainterPath& path) override
    {
        QPainter* p = painter();
        const QTransform transform = p->transform();
        // The fill.
        if (p->brush().style() != Qt::NoBrush)
            addFill(transform.map(path), p->brush(), transform);
        // The outline, as the area it covers.
        const QPen pen = p->pen();
        if (pen.style() != Qt::NoPen && pen.brush().style() != Qt::NoBrush)
        {
            QPainterPathStroker stroker(pen);
            if (pen.isCosmetic())
            {
                // Its width is on the device, whatever the transform.
                stroker.setWidth(pen.widthF() > 0.0 ? pen.widthF() : 1.0);
                addFill(stroker.createStroke(transform.map(path)), pen.brush(), transform);
            }
            else
            {
                stroker.setWidth(pen.widthF() > 0.0 ? pen.widthF() : 1.0);
                addFill(transform.map(stroker.createStroke(path)), pen.brush(), transform);
            }
        }
    }

    void drawPolygon(const QPointF* points, int count, PolygonDrawMode mode) override
    {
        QPainterPath path;
        if (count <= 0)
            return;
        path.moveTo(points[0]);
        for (int i = 1; i < count; ++i)
            path.lineTo(points[i]);
        if (mode != PolylineMode)
            path.closeSubpath();
        path.setFillRule(mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
        if (mode == PolylineMode)
        {
            // A polyline has no inside: only its outline.
            QPainter* p = painter();
            const QBrush saved = p->brush();
            p->setBrush(Qt::NoBrush);
            drawPath(path);
            p->setBrush(saved);
            return;
        }
        drawPath(path);
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

    void drawEllipse(const QRectF& rect) override
    {
        QPainterPath path;
        path.addEllipse(rect);
        drawPath(path);
    }

    void drawLines(const QLineF* lines, int count) override
    {
        for (int i = 0; i < count; ++i)
        {
            QPainterPath path;
            path.moveTo(lines[i].p1());
            path.lineTo(lines[i].p2());
            QPainter* p = painter();
            const QBrush saved = p->brush();
            p->setBrush(Qt::NoBrush);
            drawPath(path);
            p->setBrush(saved);
        }
    }

    void drawTextItem(const QPointF& at, const QTextItem& item) override
    {
        // Text as its glyphs' outlines, in the pen's color.
        QPainterPath path;
        path.addText(at, item.font(), item.text());
        QPainter* p = painter();
        addFill(p->transform().map(path), p->pen().brush(), p->transform());
    }

    void drawPixmap(const QRectF& target, const QPixmap& pixmap, const QRectF& source) override
    {
        drawImage(target, pixmap.toImage(), source, Qt::AutoColor);
    }

    void drawImage(const QRectF& target, const QImage& image, const QRectF& source, Qt::ImageConversionFlags) override
    {
        if (image.isNull() || source.isEmpty())
            return;
        QPainter* p = painter();
        RecordedItem item;
        item.kind = RecordedItem::Kind::Image;
        item.image = image.copy(source.toAlignedRect()).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        // The image's pixels onto the target, then onto the device.
        const QTransform onto = QTransform::fromScale(target.width() / source.width(), target.height() / source.height()) *
            QTransform::fromTranslate(target.left(), target.top());
        item.imageTransform = onto * p->transform();
        QPainterPath box;
        box.addRect(QRectF(0, 0, item.image.width(), item.image.height()));
        item.path = item.imageTransform.map(box);
        record(item);
    }

    void drawTiledPixmap(const QRectF& rect, const QPixmap& pixmap, const QPointF& offset) override
    {
        QBrush brush(pixmap);
        brush.setTransform(QTransform::fromTranslate(-offset.x(), -offset.y()));
        QPainterPath path;
        path.addRect(rect);
        QPainter* p = painter();
        addFill(p->transform().map(path), brush, p->transform());
    }

private:
    void addFill(const QPainterPath& area, const QBrush& brush, const QTransform& transform)
    {
        if (area.isEmpty())
            return;
        RecordedItem item;
        item.path = area;
        item.brush = brush;
        // The brush's own space, then the painter's, onto the device.
        item.brush.setTransform(brush.transform() * transform);
        record(item);
    }

    void record(RecordedItem& item)
    {
        QPainter* p = painter();
        item.opacity = p->opacity();
        item.composition = p->compositionMode();
        if (p->hasClipping())
        {
            item.clipped = true;
            item.clip = p->transform().map(p->clipPath());
        }
        _recorder->items.push_back(std::move(item));
    }

    PaintRecorder* _recorder;
};

PaintRecorder::PaintRecorder(const QSize& size)
    : _size(size)
    , _engine(std::make_unique<RecordingPaintEngine>(this))
{
}

PaintRecorder::~PaintRecorder() = default;

QPaintEngine* PaintRecorder::paintEngine() const
{
    return _engine.get();
}

int PaintRecorder::metric(PaintDeviceMetric metric) const
{
    switch (metric)
    {
    case PdmWidth: return _size.width();
    case PdmHeight: return _size.height();
    case PdmWidthMM: return _size.width() * 254 / 960;
    case PdmHeightMM: return _size.height() * 254 / 960;
    case PdmNumColors: return 0x7fffffff;
    case PdmDepth: return 32;
    case PdmDpiX:
    case PdmDpiY:
    case PdmPhysicalDpiX:
    case PdmPhysicalDpiY: return 96;
    case PdmDevicePixelRatio: return 1;
    case PdmDevicePixelRatioScaled: return static_cast<int>(QPaintDevice::devicePixelRatioFScale());
    default: return 0;
    }
}

QPainterPath coveredArea(const std::vector<RecordedItem>& items)
{
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    for (const RecordedItem& item : items)
    {
        if (item.kind == RecordedItem::Kind::Fill && item.brush.style() == Qt::NoBrush)
            continue;
        QPainterPath piece = item.path;
        if (item.clipped)
            piece = piece.intersected(item.clip);
        area.addPath(piece);
    }
    return area.simplified();
}
