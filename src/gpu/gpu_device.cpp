#include "gpu_device.h"

#include <QDebug>

#include <cstring>
#include <memory>
#include <vector>

namespace {

QString text(WGPUStringView view)
{
    if (!view.data)
        return QString();
    return QString::fromUtf8(view.data, view.length == WGPU_STRLEN ? -1 : static_cast<qsizetype>(view.length));
}

QString backendName(WGPUBackendType type)
{
    switch (type)
    {
    case WGPUBackendType_D3D11: return "D3D11";
    case WGPUBackendType_D3D12: return "D3D12";
    case WGPUBackendType_Metal: return "Metal";
    case WGPUBackendType_Vulkan: return "Vulkan";
    case WGPUBackendType_OpenGL: return "OpenGL";
    case WGPUBackendType_OpenGLES: return "OpenGL ES";
    case WGPUBackendType_WebGPU: return "WebGPU";
    default: return "other";
    }
}

} // namespace

#ifndef __EMSCRIPTEN__
GpuDevice* GpuDevice::get(QString* error)
{
    static std::unique_ptr<GpuDevice> made;
    static bool tried = false;
    static QString why;
    if (!tried)
    {
        tried = true;
        std::unique_ptr<GpuDevice> device(new GpuDevice());
        if (device->make(why))
            made = std::move(device);
        else
            qWarning("GPU: %s", qPrintable(why));
    }
    if (error)
        *error = why;
    return made.get();
}

void GpuDevice::whenReady(const std::function<void(bool)>& done)
{
    done(get() != nullptr);
}
#else
namespace {

enum class State
{
    NotAsked,
    Asking,
    Ready,
    Failed
};
State state = State::NotAsked;
GpuDevice* made = nullptr;
QString why;
std::vector<std::function<void(bool)>> waiting;

void settle(bool ready, const QString& error = QString())
{
    state = ready ? State::Ready : State::Failed;
    why = error;
    if (!ready)
        qWarning("GPU: %s", qPrintable(error));
    std::vector<std::function<void(bool)>> told;
    told.swap(waiting);
    for (const auto& done : told)
        done(ready);
}

} // namespace

GpuDevice* GpuDevice::get(QString* error)
{
    if (state == State::NotAsked)
    {
        state = State::Asking;
        made = new GpuDevice();
        made->request();
    }
    if (error)
        *error = state == State::Asking ? QStringLiteral("The browser is still starting WebGPU.") : why;
    return state == State::Ready ? made : nullptr;
}

void GpuDevice::whenReady(const std::function<void(bool)>& done)
{
    get();
    if (state == State::Ready || state == State::Failed)
        done(state == State::Ready);
    else
        waiting.push_back(done);
}

void GpuDevice::request()
{
    _instance = wgpuCreateInstance(nullptr);
    if (!_instance)
    {
        settle(false, "This browser has no WebGPU.");
        return;
    }
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    // Answered when the page's promise settles, with nothing waiting on it.
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                                   void* userdata1, void*) {
        auto* self = static_cast<GpuDevice*>(userdata1);
        if (status != WGPURequestAdapterStatus_Success || !adapter)
        {
            settle(false, "No WebGPU adapter: " + text(message));
            return;
        }
        self->_adapter = adapter;
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success)
        {
            self->_description = text(info.description).isEmpty() ? text(info.vendor) + " " + text(info.architecture)
                                                                   : text(info.description);
            self->_description += " (WebGPU)";
            wgpuAdapterInfoFreeMembers(info);
        }
        WGPUDeviceDescriptor deviceDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        deviceDescriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType,
                                                                    WGPUStringView message, void*, void*) {
            qWarning("GPU: %s", qPrintable(text(message)));
        };
        WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                                      void* userdata1, void*) {
            auto* self = static_cast<GpuDevice*>(userdata1);
            if (status != WGPURequestDeviceStatus_Success || !device)
            {
                settle(false, "No WebGPU device: " + text(message));
                return;
            }
            self->_device = device;
            self->_queue = wgpuDeviceGetQueue(device);
            settle(true);
        };
        deviceCallback.userdata1 = self;
        wgpuAdapterRequestDevice(adapter, &deviceDescriptor, deviceCallback);
    };
    adapterCallback.userdata1 = this;
    wgpuInstanceRequestAdapter(_instance, &options, adapterCallback);
}
#endif

GpuDevice::~GpuDevice()
{
    if (_queue)
        wgpuQueueRelease(_queue);
    if (_device)
        wgpuDeviceRelease(_device);
    if (_adapter)
        wgpuAdapterRelease(_adapter);
    if (_instance)
        wgpuInstanceRelease(_instance);
}

#ifndef __EMSCRIPTEN__
void GpuDevice::wait(WGPUFuture future) const
{
    WGPUFutureWaitInfo info = {future, false};
    wgpuInstanceWaitAny(_instance, 1, &info, UINT64_MAX);
}

bool GpuDevice::make(QString& error)
{
    // Waiting on a future with a timeout is what lets the requests below be
    // answered in place.
    const WGPUInstanceFeatureName features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instanceDescriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = features;
    _instance = wgpuCreateInstance(&instanceDescriptor);
    if (!_instance)
    {
        error = "no WebGPU instance";
        return false;
    }

    struct Answer
    {
        void* handle = nullptr;
        QString message;
    };
    Answer adapterAnswer;
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapterCallback.mode = WGPUCallbackMode_WaitAnyOnly;
    adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                                   void* userdata1, void*) {
        auto* answer = static_cast<Answer*>(userdata1);
        if (status == WGPURequestAdapterStatus_Success)
            answer->handle = adapter;
        else
            answer->message = text(message);
    };
    adapterCallback.userdata1 = &adapterAnswer;
    wait(wgpuInstanceRequestAdapter(_instance, &options, adapterCallback));
    _adapter = static_cast<WGPUAdapter>(adapterAnswer.handle);
    if (!_adapter)
    {
        error = "no WebGPU adapter: " + adapterAnswer.message;
        return false;
    }
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(_adapter, &info) == WGPUStatus_Success)
    {
        _description = text(info.description).isEmpty() ? text(info.device) : text(info.description);
        _description += " (" + backendName(info.backendType) + ")";
        wgpuAdapterInfoFreeMembers(info);
    }

    Answer deviceAnswer;
    WGPUDeviceDescriptor deviceDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    deviceDescriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView message,
                                                                void*, void*) {
        qWarning("GPU: %s", qPrintable(text(message)));
    };
    WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallback.mode = WGPUCallbackMode_WaitAnyOnly;
    deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                                  void* userdata1, void*) {
        auto* answer = static_cast<Answer*>(userdata1);
        if (status == WGPURequestDeviceStatus_Success)
            answer->handle = device;
        else
            answer->message = text(message);
    };
    deviceCallback.userdata1 = &deviceAnswer;
    wait(wgpuAdapterRequestDevice(_adapter, &deviceDescriptor, deviceCallback));
    _device = static_cast<WGPUDevice>(deviceAnswer.handle);
    if (!_device)
    {
        error = "no WebGPU device: " + deviceAnswer.message;
        return false;
    }
    _queue = wgpuDeviceGetQueue(_device);
    return true;
}
#endif

WGPUTexture GpuDevice::makeTarget(const QSize& size) const
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()), 1};
    descriptor.format = WGPUTextureFormat_BGRA8Unorm;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    return wgpuDeviceCreateTexture(_device, &descriptor);
}

#ifndef __EMSCRIPTEN__
void GpuDevice::finish() const
{
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*) {};
    wait(wgpuQueueOnSubmittedWorkDone(_queue, callback));
}

QImage GpuDevice::read(WGPUTexture texture, const QSize& size) const
{
    const uint32_t width = static_cast<uint32_t>(size.width());
    const uint32_t height = static_cast<uint32_t>(size.height());
    // Rows are copied out 256 bytes aligned.
    const uint32_t rowBytes = (width * 4 + 255) / 256 * 256;
    WGPUBufferDescriptor bufferDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDescriptor.size = static_cast<uint64_t>(rowBytes) * height;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(_device, &bufferDescriptor);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(_device, nullptr);
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.bytesPerRow = rowBytes;
    destination.layout.rowsPerImage = height;
    const WGPUExtent3D extent = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(_queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    bool mapped = false;
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
        *static_cast<bool*>(userdata1) = status == WGPUMapAsyncStatus_Success;
    };
    callback.userdata1 = &mapped;
    wait(wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, bufferDescriptor.size, callback));

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (mapped)
    {
        const auto* bytes = static_cast<const uchar*>(wgpuBufferGetConstMappedRange(buffer, 0, bufferDescriptor.size));
        // BGRA bytes are ARGB32 on a little-endian machine.
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(image.scanLine(static_cast<int>(y)), bytes + static_cast<size_t>(y) * rowBytes, width * 4);
        wgpuBufferUnmap(buffer);
    }
    else
        image.fill(Qt::magenta);
    wgpuBufferRelease(buffer);
    return image;
}
#endif
