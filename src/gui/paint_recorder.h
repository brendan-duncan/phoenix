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
        Image
    };
    Kind kind = Kind::Fill;
    /// A fill's area, or an image's box.
    QPainterPath path;
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

protected:
    int metric(PaintDeviceMetric metric) const override;

private:
    QSize _size;
    std::unique_ptr<RecordingPaintEngine> _engine;
};

/// The area of \a items' fills (and images' boxes), clipped as each was,
/// as one path on the device: what a mask layer covers.
QPainterPath coveredArea(const std::vector<RecordedItem>& items);
