#pragma once

#include "rive_canvas.h"

#include <QColor>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

/// The stage in the browser: a WebGPU canvas of its own laid over the stage
/// widget, in the main window's part of the page, that Rive draws the whole
/// view into (the art, the tools' handles, the rulers). It lets the pointer
/// through to the widget under it, so the widget still takes every click and
/// key; panels, menus and dialogs, windows of their own, stay over it.
class WebStage
{
public:
    explicit WebStage(QWidget* over);
    ~WebStage();

    enum class Result
    {
        Drawn,
        /// The browser is still starting WebGPU: the widget is asked to paint
        /// again once it has.
        NotYet,
        /// No WebGPU, or not with what the stage needs, or it failed.
        Unavailable
    };
    /// Draws the view, painted by \a paint, into the canvas over the widget;
    /// why it could not in \a why.
    Result paint(const QColor& clear, const std::function<void(QPainter&)>& paint, QString* why);
    /// Hides the canvas (the widget is hidden, or draws itself again).
    void hide();
    const RiveCanvas::Timing* timing() const { return _canvas ? &_canvas->timing() : nullptr; }

private:
    Result make(QString* why);

    QPointer<QWidget> _widget;
    std::unique_ptr<RiveCanvas> _canvas;
    WGPUSurface _surface = nullptr;
    WGPUTextureFormat _format = WGPUTextureFormat_Undefined;
    QSize _configured;
    bool _shown = false;
    bool _waiting = false;
    bool _unavailable = false;
};
