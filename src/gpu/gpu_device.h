#pragma once

#include <webgpu/webgpu.h>

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>

/// The WebGPU device the stage draws with: Dawn natively, the browser's
/// through emdawnwebgpu. Made once, the first time it is asked for; null
/// when the machine has no adapter.
///
/// In the browser the adapter and device come back later, as the page's
/// promises settle (nothing here waits for them): get() is null until
/// then, and whenReady() hears when they have.
class GpuDevice
{
public:
    /// The device, made the first time; null when none could be made (or,
    /// in the browser, while it is still being made), with why in \a error
    /// when given.
    static GpuDevice* get(QString* error = nullptr);
    /// Calls \a done (with whether there is a device) once the device is
    /// made or could not be: at once natively.
    static void whenReady(const std::function<void(bool)>& done);
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
#ifndef __EMSCRIPTEN__
    /// Waits until the work given to the queue so far is done.
    void finish() const;
    /// \a texture's pixels (BGRA8, premultiplied), as an image.
    QImage read(WGPUTexture texture, const QSize& size) const;
#endif

private:
    GpuDevice() = default;
#ifndef __EMSCRIPTEN__
    bool make(QString& error);
    void wait(WGPUFuture future) const;
#else
    /// Starts asking the browser for the adapter and the device.
    void request();
#endif

    WGPUInstance _instance = nullptr;
    WGPUAdapter _adapter = nullptr;
    WGPUDevice _device = nullptr;
    WGPUQueue _queue = nullptr;
    QString _description;
};
