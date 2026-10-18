#pragma once

#include "tool.h"

#include "../edit/camera.h"

#include <QPointF>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fla {
class CommandStack;
class Frame;
class Layer;
class SymbolInstance;
}

/// The Camera tool (C), for Phoenix's multiplane camera: drag to pan,
/// Shift-drag to zoom in and out, Alt-drag to turn the camera about the
/// stage's middle. What it does is keyed on the frame shown: a keyframe of
/// the camera there takes it, made (and tweened from the one before) when
/// there is none. The scene gets its camera on the first use. A scene
/// filmed by the FLA's own camera layer has that moved: its keyframes'
/// camera instance.
class CameraTool : public Tool
{
public:
    /// \a camera gives the scene's camera layer, adding one when there is
    /// none (null when there is no scene): Phoenix's, or the FLA's own.
    CameraTool(fla::CommandStack& commands, std::function<fla::Layer*()> camera);

    QString name() const override { return "Camera"; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool keyPress(PhoenixView& view, QKeyEvent* event) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    bool showsSelectionBounds() const override { return false; }
    void deactivate(PhoenixView& view) override;

private:
    /// The camera keyframe on the frame shown, made when there is none.
    fla::Frame* keyframe(PhoenixView& view);
    void finish(PhoenixView& view, bool keep);
    /// Keys \a state on the keyframe being dragged.
    void key(const fla::camera::State& state);

    fla::CommandStack& _commands;
    std::function<fla::Layer*()> _camera;

    bool _dragging = false;
    bool _madeKey = false;
    QPointF _grab;
    fla::camera::State _start;
    fla::Frame* _frame = nullptr;
    std::vector<std::pair<std::string, std::string>> _before;
    /// The FLA camera's instance on that keyframe, and its matrix before.
    fla::SymbolInstance* _instance = nullptr;
    fla::Transform _beforeMatrix;
};
