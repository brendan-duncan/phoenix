#pragma once

#include "../gui/paint_recorder.h"

#include <QColor>
#include <QImage>
#include <QMap>
#include <QSize>
#include <QString>

#include <memory>
#include <vector>

class GpuDevice;

/// A vector renderer on the GPU, drawing what PhoenixView painted (as a
/// PaintRecorder recorded it, with strokes kept as strokes) into a texture.
/// The candidates for drawing the stage are compared through it.
class VectorBackend
{
public:
    struct Timing
    {
        /// Turning the items into the renderer's own, and its encoding.
        double encodeMs = 0.0;
        /// From submitting until the GPU is done.
        double gpuMs = 0.0;
        /// Reading the pixels back.
        double readMs = 0.0;
    };

    virtual ~VectorBackend() = default;
    virtual QString name() const = 0;
    /// \a items drawn over \a clear on a target of \a pixels, read back.
    virtual QImage render(const std::vector<RecordedItem>& items, const QSize& pixels, const QColor& clear,
        Timing& timing) = 0;

    /// What it could not draw as asked, by what it was, since it was made.
    QMap<QString, int> unsupported;
};

std::unique_ptr<VectorBackend> makeThorvgBackend(GpuDevice& device);
std::unique_ptr<VectorBackend> makeRiveBackend(GpuDevice& device);
