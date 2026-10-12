#include "three_d_tool.h"

#include "phoenix_view.h"

#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"

#include <QMouseEvent>

#include <algorithm>
#include <cmath>

ThreeDTool::ThreeDTool(Mode mode, fla::CommandStack& commands, std::function<void(const QString&)> status)
    : _mode(mode)
    , _commands(commands)
    , _status(std::move(status))
{
}

void ThreeDTool::apply(PhoenixView& view, const fla::threed::Pose& pose)
{
    if (!_clip)
        return;
    _clip->extraAttributes = fla::threed::withPose(*_clip, pose);
    view.invalidateBounds();
    view.clearCaches();
    view.update();
}

void ThreeDTool::commit(PhoenixView& view)
{
    if (!_clip || _before == _clip->extraAttributes)
        return;
    const Attributes after = _clip->extraAttributes;
    _commands.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(_clip->extraAttributes, _before, after,
        _mode == Mode::Rotate ? "3D Rotation" : "3D Translation")));
    _commands.breakMergeChain();
    view.update();
}

bool ThreeDTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    auto* instance = hit && hit.element->elementType() == fla::Element::Type::SymbolInstance
        ? static_cast<fla::SymbolInstance*>(hit.element) : nullptr;
    if (!instance || instance->symbolType != fla::SymbolType::MovieClip)
    {
        if (hit && _status)
            _status("3D rotation and translation work on movie clips.");
        _clip = nullptr;
        view.update();
        return static_cast<bool>(hit);
    }
    _clip = instance;
    _before = instance->extraAttributes;
    _start = fla::threed::of(*instance);
    _grab = documentPos;
    _center = hit.elementToDocument.map(QPointF(instance->transformationPoint.x, instance->transformationPoint.y));
    if (_mode == Mode::Rotate && (event->modifiers() & Qt::AltModifier))
    {
        fla::threed::Pose flat = _start;
        flat.rotationX = 0.0;
        flat.rotationY = 0.0;
        apply(view, flat);
        commit(view);
        return true;
    }
    _dragging = true;
    view.update();
    return true;
}

bool ThreeDTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (!_dragging || !_clip)
        return false;
    // Half a degree, or a pixel of depth, for each pixel dragged on screen.
    const double perPixel = view.pickTolerance() / 4.0;
    const QPointF moved = (documentPos - _grab) / std::max(perPixel, 1.0e-9);
    fla::threed::Pose pose = _start;
    if (_mode == Mode::Rotate)
    {
        pose.rotationY = std::remainder(_start.rotationY + moved.x() * 0.5, 360.0);
        pose.rotationX = std::remainder(_start.rotationX - moved.y() * 0.5, 360.0);
    }
    else
    {
        pose.z = _start.z - moved.y();
    }
    apply(view, pose);
    return true;
}

bool ThreeDTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    if (!_dragging)
        return false;
    _dragging = false;
    commit(view);
    return true;
}

void ThreeDTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    if (!_clip)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    const double r = 40.0 * scale;
    if (_mode == Mode::Rotate)
    {
        // The two axes it turns about, and the ring round them.
        painter.setPen(QPen(QColor(220, 40, 40), 1.5 * scale));
        painter.drawLine(_center - QPointF(r, 0.0), _center + QPointF(r, 0.0));
        painter.setPen(QPen(QColor(40, 170, 40), 1.5 * scale));
        painter.drawLine(_center - QPointF(0.0, r), _center + QPointF(0.0, r));
        painter.setPen(QPen(QColor(40, 90, 220), 1.5 * scale));
        painter.drawEllipse(_center, r, r);
    }
    else
    {
        // The depth axis, seen end on: a dot in a ring.
        painter.setPen(QPen(QColor(40, 90, 220), 1.5 * scale));
        painter.drawEllipse(_center, 8.0 * scale, 8.0 * scale);
        painter.setBrush(QColor(40, 90, 220));
        painter.drawEllipse(_center, 3.0 * scale, 3.0 * scale);
    }
    painter.restore();
}

void ThreeDTool::deactivate(PhoenixView& view)
{
    if (_dragging)
        commit(view);
    _dragging = false;
    _clip = nullptr;
    view.update();
}
