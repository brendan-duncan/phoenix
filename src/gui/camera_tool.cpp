#include "camera_tool.h"

#include "phoenix_view.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"
#include "../edit/timeline_edit.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <algorithm>

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

CameraTool::CameraTool(fla::CommandStack& commands, std::function<fla::Layer*()> camera)
    : _commands(commands)
    , _camera(std::move(camera))
{
}

fla::Frame* CameraTool::keyframe(PhoenixView& view)
{
    fla::Layer* layer = _camera ? _camera() : nullptr;
    if (!layer)
        return nullptr;
    const int at = view.playheadFrame();
    for (fla::Frame* frame : layer->frames)
        if (frame && frame->index == at)
            return frame;
    // A keyframe here, holding what the camera shows here, and the span
    // before it tweening into it.
    _commands.push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Camera",
        [layer, at](std::vector<fla::Frame*>&) {
            fla::Frame* span = fla::timeline::spanAt(*layer, at);
            fla::timeline::insertKeyframe(*layer, at, false);
            if (span && span->index < at)
                span->tweenType = fla::TweenType::Motion;
        })));
    _madeKey = true;
    for (fla::Frame* frame : layer->frames)
        if (frame && frame->index == at)
            return frame;
    return nullptr;
}

bool CameraTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    const fla::FLADocument* fla = view.flaDocument();
    const fla::Document* document = fla ? fla->document : nullptr;
    if (!document || view.inSymbol())
        return true;
    _commands.beginMacro("Camera");
    _madeKey = false;
    fla::Layer* layer = _camera ? _camera() : nullptr;
    const fla::Timeline* scene = document->currentScene();
    if (!layer || !scene)
    {
        _commands.endMacro();
        return true;
    }
    // Where the camera is now, tweened or not, is where the drag starts.
    _start = fla::camera::stateAt(*scene, view.playheadFrame(), document->width, document->height);
    _frame = keyframe(view);
    if (!_frame)
    {
        _commands.endMacro();
        return true;
    }
    _before = _frame->extraAttributes;
    const fla::Layer* keyed = dynamic_cast<const fla::Layer*>(_frame->parent);
    _instance = keyed && fla::camera::isFileCamera(*keyed) ? fla::camera::fileCameraOf(*_frame) : nullptr;
    if (_instance)
    {
        // A keyframe made in a tween holds the color tweened to there.
        if (_madeKey)
            _instance->colorTransform = _start.color;
        _beforeMatrix = _instance->transform;
    }
    key(_start);
    _grab = documentPos;
    _dragging = true;
    return true;
}

bool CameraTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_dragging || !_frame)
        return false;
    const fla::FLADocument* fla = view.flaDocument();
    const fla::Document* document = fla ? fla->document : nullptr;
    if (!document)
        return true;
    fla::camera::State state = _start;
    const QPointF delta = documentPos - _grab;
    if (event->modifiers() & Qt::ShiftModifier)
    {
        // Up moves in, down moves out.
        state.zoom = std::clamp(_start.zoom * std::exp(-delta.y() / 200.0), 5.0, 5000.0);
    }
    else if (event->modifiers() & Qt::AltModifier)
    {
        // The picture turns with the hand about the stage's middle, so the
        // camera turns the other way.
        const QPointF middle(document->width / 2.0, document->height / 2.0);
        const QPointF from = _grab - middle;
        const QPointF to = documentPos - middle;
        const double turn = (std::atan2(to.y(), to.x()) - std::atan2(from.y(), from.x())) * 180.0 / kPi;
        state.rotation = _start.rotation - turn;
    }
    else
    {
        // The picture follows the hand: the camera moves the other way, as
        // far in the scene as the hand went on the stage.
        const double r = _start.rotation * kPi / 180.0;
        const double scale = 100.0 / std::max(1.0, _start.zoom);
        state.x = _start.x - (delta.x() * std::cos(r) - delta.y() * std::sin(r)) * scale;
        state.y = _start.y - (delta.x() * std::sin(r) + delta.y() * std::cos(r)) * scale;
    }
    key(state);
    view.clearCaches();
    view.update();
    return true;
}

bool CameraTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_dragging)
        return false;
    mouseMove(view, event, documentPos);
    finish(view, true);
    return true;
}

bool CameraTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && _dragging)
    {
        finish(view, false);
        return true;
    }
    return false;
}

void CameraTool::finish(PhoenixView& view, bool keep)
{
    if (!_dragging)
        return;
    _dragging = false;
    if (_frame && _instance)
    {
        const fla::Transform after = _instance->transform;
        _instance->transform = _beforeMatrix;
        if (keep)
            _commands.push(fla::CommandPtr(
                new fla::SetPropertyCommand<fla::Transform>(_instance->transform, _beforeMatrix, after, "Camera")));
    }
    else if (_frame)
    {
        const auto after = _frame->extraAttributes;
        _frame->extraAttributes = _before;
        if (keep && after != _before)
            _commands.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                _frame->extraAttributes, _before, after, "Camera")));
    }
    _commands.endMacro();
    // A keyframe made for a gesture that was called off goes with it.
    if (!keep && _madeKey && _commands.canUndo())
        _commands.discardLast();
    _commands.breakMergeChain();
    _frame = nullptr;
    _instance = nullptr;
    view.clearCaches();
    view.update();
}

void CameraTool::key(const fla::camera::State& state)
{
    if (_instance)
        _instance->transform = fla::camera::fileTransformOf(state);
    else if (_frame)
        fla::camera::set(_frame->extraAttributes, state);
}

void CameraTool::deactivate(PhoenixView& view)
{
    finish(view, false);
}

void CameraTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    // What the camera is doing on the frame shown, in the stage's corner.
    const fla::FLADocument* fla = view.flaDocument();
    const fla::Document* document = fla ? fla->document : nullptr;
    const fla::Timeline* scene = document ? document->currentScene() : nullptr;
    if (!scene || view.inSymbol())
        return;
    const fla::camera::State state = fla::camera::stateAt(*scene, view.playheadFrame(), document->width, document->height);
    const QString text = fla::camera::filmingOf(*scene)
        ? QString("Camera  %1, %2   %3%   %4\u00b0")
              .arg(state.x, 0, 'f', 0)
              .arg(state.y, 0, 'f', 0)
              .arg(state.zoom, 0, 'f', 0)
              .arg(state.rotation, 0, 'f', 0)
        : QString("Camera: drag to pan, Shift-drag to zoom, Alt-drag to turn");
    // In the view's own corner, wherever the stage has been scrolled to.
    (void)scale;
    painter.save();
    painter.resetTransform();
    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    const QRectF box(8.0, 8.0, painter.fontMetrics().horizontalAdvance(text) + 14.0, 22.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, text);
    painter.restore();
}
