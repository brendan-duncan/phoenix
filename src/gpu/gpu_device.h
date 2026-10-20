#pragma once

#include <webgpu/webgpu.h>

#include <QImage>
#include <QSize>
#include <QString>

/// The WebGPU device the stage draws with (Dawn natively). Made once, the
/// first time it is asked for; null when the machine has no adapter.
class GpuDevice
{
public:
    /// The device, made the first time; null when none could be made, with
    /// why in \a error when given.
    static GpuDevice* get(QString* error = nullptr);
    ~GpuDevice();

    WGPUInstance instance() const { return _instance; }
    WGPUAdapter adapter() const { return _adapter; }
    WGPUDevice device() const { return _device; }
    WGPUQueue queue() const { return _queue; }
    /// The adapter and backend, for a log.
    QString description() const { return _description; }

    /// A texture to draw into and read back: 8-bit BGRA, as the renderers
    /// want their targets.
    WGPUTexture makeTarget(const QSize& size) const;
    /// Waits until the work given to the queue so far is done.
    void finish() const;
    /// \a texture's pixels (BGRA8, premultiplied), as an image.
    QImage read(WGPUTexture texture, const QSize& size) const;

private:
    GpuDevice() = default;
    bool make(QString& error);
    void wait(WGPUFuture future) const;

    WGPUInstance _instance = nullptr;
    WGPUAdapter _adapter = nullptr;
    WGPUDevice _device = nullptr;
    WGPUQueue _queue = nullptr;
    QString _description;
};
