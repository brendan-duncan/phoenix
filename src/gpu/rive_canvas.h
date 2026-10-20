#pragma once

#include <webgpu/webgpu.h>

#include <QColor>
#include <QImage>
#include <QSize>

#include <functional>
#include <memory>

class QPainter;
class GpuDevice;
namespace wgpu {
class CommandBuffer;
}

/// The stage drawn by the Rive renderer on the GPU (WebGPU through Dawn).
/// What is painted with QPainter on its device goes straight to Rive: paths
/// keep their own coordinates with the painter's transform beside them, so
/// a shape drawn again is not converted again, and images are uploaded once
/// while they go on being drawn.
class RiveCanvas
{
public:
    /// How long the last frame took, in milliseconds.
    struct Timing
    {
        /// Painting: Phoenix walking the document and Rive taking it in.
        double paintMs = 0.0;
        /// Rive preparing the frame for the GPU.
        double flushMs = 0.0;
        /// From submitting until the GPU is done.
        double gpuMs = 0.0;
        /// Reading the pixels back.
        double readMs = 0.0;
        int paths = 0;
        int pathsMade = 0;
    };

    /// A canvas on \a device; check valid().
    explicit RiveCanvas(GpuDevice& device);
    ~RiveCanvas();

    bool valid() const;

#ifndef __EMSCRIPTEN__
    /// A frame of \a pixels device pixels (\a ratio of them to a logical
    /// pixel) cleared to \a clear, painted by \a paint on a QPainter whose
    /// device measures in logical pixels as a widget does; its pixels, read
    /// back from the GPU.
    QImage draw(const QSize& pixels, qreal ratio, const QColor& clear, const std::function<void(QPainter&)>& paint);
#endif
    /// The same frame drawn into \a texture (of \a format), such as a page
    /// canvas's, and handed to the GPU: the browser shows it once the paint
    /// is done. False when it could not be drawn.
    bool drawInto(WGPUTexture texture, WGPUTextureFormat format, const QSize& pixels, qreal ratio, const QColor& clear,
        const std::function<void(QPainter&)>& paint);

    const Timing& timing() const { return _timing; }
    /// Forgets the converted paths and uploaded images (after an edit that
    /// may have changed them in place).
    void forget();

private:
    class Impl;
    /// The frame drawn into \a texture, as commands for the GPU.
    wgpu::CommandBuffer render(WGPUTexture texture, WGPUTextureFormat format, const QSize& pixels, qreal ratio,
        const QColor& clear, const std::function<void(QPainter&)>& paint);
    std::unique_ptr<Impl> _impl;
    Timing _timing;
};
