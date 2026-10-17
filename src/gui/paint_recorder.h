#pragma once

#include <QBrush>
#include <QImage>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <memory>
#include <vector>

/// What was painted, as vectors: each fill (strokes and text turned into
/// the areas they cover) with its brush, and each image with where it
/// went, all in the device's coordinates, in painting order. Exports that
/// write vectors (SWF, masks as SVG clip paths) paint through it.
struct RecordedItem
{
    enum class Kind
    {
        Fill,
        Image,
        /// With PaintRecorder::keepStrokes: an outline as drawn, its path
        /// and pen on the device.
        Stroke,
        /// With PaintRecorder::keepText: a run of text as drawn.
        Text
    };
    Kind kind = Kind::Fill;
    /// A fill's area, an image's box, or a stroke's path.
    QPainterPath path;
    /// A stroke's pen, its width on the device; cosmetic when it keeps its
    /// width whatever the scale.
    QPen pen;
    /// A text run: its words and font, where its baseline starts, and the
    /// transform from there to the device (its color is the brush).
    QString text;
    QFont font;
    QPointF at;
    QTransform transform;
    /// The fill's brush, its transform taking brush space to the device.
    QBrush brush;
    /// An image, and its pixels' placement on the device.
    QImage image;
    QTransform imageTransform;
    double opacity = 1.0;
    /// How it mixed with what was under it.
    QPainter::CompositionMode composition = QPainter::CompositionMode_SourceOver;
    /// The clip in force, on the device; empty when there is none.
    QPainterPath clip;
    bool clipped = false;
};

class RecordingPaintEngine;

/// A paint device of a given size that keeps what is painted on it.
class PaintRecorder : public QPaintDevice
{
public:
    explicit PaintRecorder(const QSize& size);
    ~PaintRecorder() override;

    QPaintEngine* paintEngine() const override;

    std::vector<RecordedItem> items;
    /// Outlines kept as strokes (a pen and a path) where the transform
    /// keeps their shape, rather than as the area they cover; text kept as
    /// text rather than as its glyphs' outlines. Both off by default.
    bool keepStrokes = false;
    bool keepText = false;

protected:
    int metric(PaintDeviceMetric metric) const override;

private:
    QSize _size;
    std::unique_ptr<RecordingPaintEngine> _engine;
};

/// The area of \a items' fills (and images' boxes), clipped as each was,
/// as one path on the device: what a mask layer covers.
QPainterPath coveredArea(const std::vector<RecordedItem>& items);
