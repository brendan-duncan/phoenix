#include "web_stage.h"

#include "gpu_device.h"

#include <QWindow>
#include <QtMath>

#include <emscripten/val.h>

#include <string>

namespace {

/// What Emscripten finds the stage's canvas by (Module.specialHTMLTargets).
const char* const kSelector = "!phoenixstage";

/// The page element of the canvas Qt draws \a widget's window into.
emscripten::val qtCanvasOf(QWidget* widget)
{
    QWindow* window = widget && widget->window() ? widget->window()->windowHandle() : nullptr;
    if (!window)
        return emscripten::val::undefined();
    const std::string selector = "!qtwindow" + std::to_string(window->winId());
    return emscripten::val::module_property("specialHTMLTargets")[selector];
}

emscripten::val stageCanvas()
{
    return emscripten::val::module_property("specialHTMLTargets")[kSelector];
}

} // namespace

WebStage::WebStage(QWidget* over)
    : _widget(over)
{
}

WebStage::~WebStage()
{
    if (_surface)
        wgpuSurfaceRelease(_surface);
    emscripten::val canvas = stageCanvas();
    if (!canvas.isUndefined() && !canvas["parentElement"].isNull())
        canvas["parentElement"].call<void>("removeChild", canvas);
    emscripten::val::module_property("specialHTMLTargets").delete_(std::string(kSelector));
}

bool WebStage::make()
{
    if (_canvas)
        return true;
    GpuDevice* device = GpuDevice::get();
    if (!device)
    {
        // Asked of the browser; the stage is drawn again once it answers.
        if (!_waiting)
        {
            _waiting = true;
            QPointer<QWidget> widget = _widget;
            GpuDevice::whenReady([widget](bool) {
                if (widget)
                    widget->update();
            });
        }
        return false;
    }
    emscripten::val qtCanvas = qtCanvasOf(_widget);
    if (qtCanvas.isUndefined() || qtCanvas.isNull())
        return false;
    // Beside Qt's canvas in the window's element, over it, letting the
    // pointer through.
    emscripten::val canvas = emscripten::val::global("document").call<emscripten::val>("createElement", std::string("canvas"));
    canvas["classList"].call<void>("add", std::string("phoenix-stage"));
    emscripten::val style = canvas["style"];
    style.set("position", "absolute");
    style.set("pointerEvents", "none");
    style.set("display", "none");
    canvas.call<void>("setAttribute", std::string("aria-hidden"), std::string("true"));
    qtCanvas["parentElement"].call<void>("appendChild", canvas);
    emscripten::val::module_property("specialHTMLTargets").set(kSelector, canvas);

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    source.selector = {kSelector, WGPU_STRLEN};
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    _surface = wgpuInstanceCreateSurface(device->instance(), &descriptor);
    if (!_surface)
        return false;
    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(_surface, device->adapter(), &capabilities) == WGPUStatus_Success &&
        capabilities.formatCount > 0)
        _format = capabilities.formats[0];
    else
        _format = WGPUTextureFormat_BGRA8Unorm;
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    _canvas = std::make_unique<RiveCanvas>(*device);
    return _canvas->valid();
}

bool WebStage::paint(const QColor& clear, const std::function<void(QPainter&)>& paint)
{
    if (!_widget || !make())
        return false;
    GpuDevice* device = GpuDevice::get();
    emscripten::val qtCanvas = qtCanvasOf(_widget);
    emscripten::val canvas = stageCanvas();
    if (qtCanvas.isUndefined() || canvas.isUndefined())
        return false;

    // Over the widget: where it is in its window, from where Qt's canvas is.
    const qreal ratio = _widget->devicePixelRatioF();
    const QPoint at = _widget->mapTo(_widget->window(), QPoint(0, 0));
    const QSize logical = _widget->size();
    const QSize pixels(qCeil(logical.width() * ratio), qCeil(logical.height() * ratio));
    if (pixels.isEmpty())
        return false;
    emscripten::val style = canvas["style"];
    style.set("left", std::to_string(qtCanvas["offsetLeft"].as<int>() + at.x()) + "px");
    style.set("top", std::to_string(qtCanvas["offsetTop"].as<int>() + at.y()) + "px");
    style.set("width", std::to_string(logical.width()) + "px");
    style.set("height", std::to_string(logical.height()) + "px");
    if (pixels != _configured)
    {
        canvas.set("width", pixels.width());
        canvas.set("height", pixels.height());
        WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
        configuration.device = device->device();
        configuration.format = _format;
        configuration.usage = WGPUTextureUsage_RenderAttachment;
        configuration.width = static_cast<uint32_t>(pixels.width());
        configuration.height = static_cast<uint32_t>(pixels.height());
        configuration.alphaMode = WGPUCompositeAlphaMode_Opaque;
        configuration.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(_surface, &configuration);
        _configured = pixels;
    }
    WGPUSurfaceTexture current = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(_surface, &current);
    if (!current.texture)
        return false;
    const bool drawn = _canvas->drawInto(current.texture, _format, pixels, ratio, clear, paint);
    wgpuTextureRelease(current.texture);
    if (drawn && !_shown)
    {
        style.set("display", "block");
        _shown = true;
    }
    return drawn;
}

void WebStage::hide()
{
    if (!_shown)
        return;
    emscripten::val canvas = stageCanvas();
    if (!canvas.isUndefined())
        canvas["style"].set("display", "none");
    _shown = false;
}
