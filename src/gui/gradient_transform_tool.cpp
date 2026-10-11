#include "gradient_transform_tool.h"

#include "phoenix_view.h"

#include "../data/bitmap_fill.h"
#include "../data/frame.h"
#include "../data/fill_style.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/shape_merge.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

/// Half of Flash's gradient square, in the units its matrix maps.
constexpr double kHalfSpan = 819.2;

/// How near the cursor must be to a handle to take it, in screen pixels.
constexpr double kGrab = 7.0;

double length(const QPointF& v)
{
    return std::hypot(v.x(), v.y());
}

QPointF rotated(const QPointF& v, double radians)
{
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}

} // namespace

fla::FillStyle* GradientTransformTool::fill() const
{
    if (!_shape || _fillIndex < 0 || !_frame ||
        std::find(_frame->elements.begin(), _frame->elements.end(), _shape) == _frame->elements.end())
        return nullptr;
    const auto found = _shape->fillsMap.find(_fillIndex);
    return found == _shape->fillsMap.end() ? nullptr : found->second;
}

bool GradientTransformTool::readFrame(Frame& frame) const
{
    const fla::FillStyle* style = fill();
    if (!style)
        return false;
    const fla::Transform& m = style->transform;
    if (style->type() == fla::FillStyle::Type::BitmapFill)
    {
        // A bitmap's pixels go to the shape at a twentieth of the matrix.
        const double w = std::max(1.0, _bitmapSize.width());
        const double h = std::max(1.0, _bitmapSize.height());
        frame.axisX = QPointF(m.m11, m.m12) / 20.0 * (w / 2.0);
        frame.axisY = QPointF(m.m21, m.m22) / 20.0 * (h / 2.0);
        frame.center = QPointF(m.tx, m.ty) + frame.axisX + frame.axisY;
        return true;
    }
    if (style->type() != fla::FillStyle::Type::LinearGradient && style->type() != fla::FillStyle::Type::RadialGradient)
        return false;
    frame.center = QPointF(m.tx, m.ty);
    frame.axisX = QPointF(m.m11, m.m12) * kHalfSpan;
    frame.axisY = QPointF(m.m21, m.m22) * kHalfSpan;
    return true;
}

void GradientTransformTool::writeFrame(const Frame& frame)
{
    fla::FillStyle* style = fill();
    if (!style)
        return;
    fla::Transform& m = style->transform;
    if (style->type() == fla::FillStyle::Type::BitmapFill)
    {
        const double w = std::max(1.0, _bitmapSize.width());
        const double h = std::max(1.0, _bitmapSize.height());
        m.m11 = frame.axisX.x() * 20.0 / (w / 2.0);
        m.m12 = frame.axisX.y() * 20.0 / (w / 2.0);
        m.m21 = frame.axisY.x() * 20.0 / (h / 2.0);
        m.m22 = frame.axisY.y() * 20.0 / (h / 2.0);
        const QPointF origin = frame.center - frame.axisX - frame.axisY;
        m.tx = origin.x();
        m.ty = origin.y();
        return;
    }
    m.m11 = frame.axisX.x() / kHalfSpan;
    m.m12 = frame.axisX.y() / kHalfSpan;
    m.m21 = frame.axisY.x() / kHalfSpan;
    m.m22 = frame.axisY.y() / kHalfSpan;
    m.tx = frame.center.x();
    m.ty = frame.center.y();
}

QPointF GradientTransformTool::handlePoint(Handle handle, const Frame& frame) const
{
    const fla::FillStyle* style = fill();
    const bool radial = style && style->type() == fla::FillStyle::Type::RadialGradient;
    const bool bitmap = style && style->type() == fla::FillStyle::Type::BitmapFill;
    switch (handle)
    {
    case Handle::Center:
        return frame.center;
    case Handle::Width:
        return frame.center + frame.axisX;
    case Handle::Height:
        return frame.center + frame.axisY;
    case Handle::Size:
        return frame.center + (frame.axisX + frame.axisY) * std::sqrt(0.5);
    case Handle::Rotate:
        if (radial)
            return frame.center + frame.axisY;
        if (bitmap)
            return frame.center + frame.axisX - frame.axisY;
        return frame.center + frame.axisX - frame.axisY;
    case Handle::Focal:
        if (radial)
            return frame.center + frame.axisX * static_cast<const fla::RadialGradient*>(style)->focalPointRatio;
        return frame.center;
    case Handle::None:
        break;
    }
    return frame.center;
}

GradientTransformTool::Handle GradientTransformTool::handleAt(PhoenixView& view, const QPointF& documentPos) const
{
    Frame frame;
    if (!readFrame(frame))
        return Handle::None;
    const fla::FillStyle* style = fill();
    const bool radial = style->type() == fla::FillStyle::Type::RadialGradient;
    const bool bitmap = style->type() == fla::FillStyle::Type::BitmapFill;
    std::vector<Handle> handles = {Handle::Width, Handle::Rotate};
    if (radial)
    {
        handles.push_back(Handle::Size);
        handles.push_back(Handle::Focal);
    }
    if (bitmap)
        handles.push_back(Handle::Height);
    handles.push_back(Handle::Center);
    const double reach = kGrab / std::max(view.zoom(), 1.0e-6);
    for (Handle handle : handles)
    {
        const QPointF at = _toDocument.map(handlePoint(handle, frame));
        if (length(at - documentPos) <= reach)
            return handle;
    }
    return Handle::None;
}

bool GradientTransformTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    // A handle of the fill being edited takes the drag.
    const Handle handle = handleAt(view, documentPos);
    if (handle != Handle::None)
    {
        bool invertible = false;
        _pressLocal = _toDocument.inverted(&invertible).map(documentPos);
        readFrame(_pressFrame);
        const fla::FillStyle* style = fill();
        const fla::Transform& m = style->transform;
        _pressMatrix[0] = m.m11;
        _pressMatrix[1] = m.m12;
        _pressMatrix[2] = m.m21;
        _pressMatrix[3] = m.m22;
        _pressMatrix[4] = m.tx;
        _pressMatrix[5] = m.ty;
        _pressFocal = style->type() == fla::FillStyle::Type::RadialGradient
            ? static_cast<const fla::RadialGradient*>(style)->focalPointRatio : 0.0;
        _dragging = handle;
        view.beginInteraction();
        return true;
    }

    // Otherwise the fill under the cursor, if a gradient or a bitmap paints it.
    _shape = nullptr;
    _fillIndex = -1;
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (hit && hit.element->elementType() == fla::Element::Type::Shape)
    {
        fla::Shape* shape = static_cast<fla::Shape*>(hit.element);
        bool invertible = false;
        const QPointF local = hit.elementToDocument.inverted(&invertible).map(documentPos);
        for (const auto& region : view.fillRegions(*shape))
        {
            if (!region.second.contains(local))
                continue;
            const fla::FillStyle* style = shape->getFillStyleByIndex(region.first);
            if (!style)
                continue;
            const auto type = style->type();
            if (type != fla::FillStyle::Type::LinearGradient && type != fla::FillStyle::Type::RadialGradient &&
                type != fla::FillStyle::Type::BitmapFill)
                continue;
            _shape = shape;
            _frame = hit.frame;
            _fillIndex = region.first;
            _toDocument = hit.elementToDocument;
            if (type == fla::FillStyle::Type::BitmapFill)
            {
                const QPixmap pixmap = view.libraryBitmap(
                    QString::fromStdString(static_cast<const fla::BitmapFill*>(style)->bitmapPath));
                _bitmapSize = pixmap.isNull() ? QSizeF(100, 100) : QSizeF(pixmap.size());
            }
            break;
        }
    }
    view.update();
    return true;
}

bool GradientTransformTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (_dragging == Handle::None || !fill())
        return false;
    bool invertible = false;
    const QPointF local = _toDocument.inverted(&invertible).map(documentPos);
    Frame frame = _pressFrame;
    switch (_dragging)
    {
    case Handle::Center:
        frame.center = _pressFrame.center + (local - _pressLocal);
        break;
    case Handle::Width:
    case Handle::Height:
    {
        // Along its own axis, kept from collapsing.
        QPointF& axis = _dragging == Handle::Width ? frame.axisX : frame.axisY;
        const QPointF base = _dragging == Handle::Width ? _pressFrame.axisX : _pressFrame.axisY;
        const double baseLength = std::max(length(base), 1.0e-9);
        const QPointF direction = base / baseLength;
        const QPointF offset = local - frame.center;
        double reach = offset.x() * direction.x() + offset.y() * direction.y();
        if (std::fabs(reach) < 1.0)
            reach = reach < 0.0 ? -1.0 : 1.0;
        axis = direction * reach;
        break;
    }
    case Handle::Size:
    {
        const double from = std::max(length(_pressLocal - _pressFrame.center), 1.0e-9);
        const double factor = std::max(0.01, length(local - _pressFrame.center) / from);
        frame.axisX = _pressFrame.axisX * factor;
        frame.axisY = _pressFrame.axisY * factor;
        break;
    }
    case Handle::Rotate:
    {
        const QPointF a = _pressLocal - _pressFrame.center;
        const QPointF b = local - _pressFrame.center;
        const double turn = std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x());
        frame.axisX = rotated(_pressFrame.axisX, turn);
        frame.axisY = rotated(_pressFrame.axisY, turn);
        break;
    }
    case Handle::Focal:
    {
        fla::FillStyle* style = fill();
        if (style->type() == fla::FillStyle::Type::RadialGradient)
        {
            const QPointF axis = _pressFrame.axisX;
            const double axisLength2 = std::max(axis.x() * axis.x() + axis.y() * axis.y(), 1.0e-9);
            const QPointF offset = local - _pressFrame.center;
            const double ratio = (offset.x() * axis.x() + offset.y() * axis.y()) / axisLength2;
            static_cast<fla::RadialGradient*>(style)->focalPointRatio = std::max(-1.0, std::min(1.0, ratio));
        }
        break;
    }
    case Handle::None:
        break;
    }
    writeFrame(frame);
    view.forgetShape(_shape);
    return true;
}

bool GradientTransformTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    if (_dragging == Handle::None)
        return false;
    _dragging = Handle::None;
    view.endInteraction();
    fla::FillStyle* style = fill();
    if (!style || !_shape)
        return true;

    // The edited shape is copied, the fill put back as it was, and the
    // command makes the change: one undo step with both states.
    std::unique_ptr<fla::Shape> after(fla::cloneShape(*_shape, nullptr));
    fla::Transform& m = style->transform;
    m.m11 = _pressMatrix[0];
    m.m12 = _pressMatrix[1];
    m.m21 = _pressMatrix[2];
    m.m22 = _pressMatrix[3];
    m.tx = _pressMatrix[4];
    m.ty = _pressMatrix[5];
    if (style->type() == fla::FillStyle::Type::RadialGradient)
        static_cast<fla::RadialGradient*>(style)->focalPointRatio = _pressFocal;
    _commandStack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(_shape, *after, "Transform Gradient")));
    _commandStack.breakMergeChain();
    view.forgetShape(_shape);
    return true;
}

void GradientTransformTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)scale;
    Frame frame;
    if (!readFrame(frame))
        return;
    const fla::FillStyle* style = fill();
    const bool radial = style->type() == fla::FillStyle::Type::RadialGradient;
    const bool bitmap = style->type() == fla::FillStyle::Type::BitmapFill;

    // In screen pixels, whatever the zoom: the guide lines, then the handles.
    const QTransform toScreen = _toDocument * painter.transform();
    painter.save();
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(0, 0, 0);
    const QColor paper(255, 255, 255);
    QPainterPath guide;
    if (radial)
    {
        for (int i = 0; i <= 72; ++i)
        {
            const double a = i * 3.14159265358979323846 / 36.0;
            const QPointF p = toScreen.map(frame.center + frame.axisX * std::cos(a) + frame.axisY * std::sin(a));
            if (i == 0)
                guide.moveTo(p);
            else
                guide.lineTo(p);
        }
    }
    else if (bitmap)
    {
        guide.moveTo(toScreen.map(frame.center - frame.axisX - frame.axisY));
        guide.lineTo(toScreen.map(frame.center + frame.axisX - frame.axisY));
        guide.lineTo(toScreen.map(frame.center + frame.axisX + frame.axisY));
        guide.lineTo(toScreen.map(frame.center - frame.axisX + frame.axisY));
        guide.closeSubpath();
    }
    else
    {
        // A linear gradient's two edges, where its colors begin and end.
        guide.moveTo(toScreen.map(frame.center - frame.axisX - frame.axisY));
        guide.lineTo(toScreen.map(frame.center - frame.axisX + frame.axisY));
        guide.moveTo(toScreen.map(frame.center + frame.axisX - frame.axisY));
        guide.lineTo(toScreen.map(frame.center + frame.axisX + frame.axisY));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(paper, 3.0));
    painter.drawPath(guide);
    painter.setPen(QPen(ink, 1.0));
    painter.drawPath(guide);

    const auto square = [&](const QPointF& at) {
        painter.setPen(QPen(ink, 1.0));
        painter.setBrush(paper);
        painter.drawRect(QRectF(at.x() - 4.0, at.y() - 4.0, 8.0, 8.0));
    };
    const auto circle = [&](const QPointF& at, double r) {
        painter.setPen(QPen(ink, 1.0));
        painter.setBrush(paper);
        painter.drawEllipse(at, r, r);
    };
    circle(toScreen.map(handlePoint(Handle::Center, frame)), 4.5);
    square(toScreen.map(handlePoint(Handle::Width, frame)));
    if (bitmap)
        square(toScreen.map(handlePoint(Handle::Height, frame)));
    // The turning handle, a ring.
    {
        const QPointF at = toScreen.map(handlePoint(Handle::Rotate, frame));
        painter.setPen(QPen(ink, 1.0));
        painter.setBrush(paper);
        painter.drawEllipse(at, 5.0, 5.0);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(at, 2.0, 2.0);
    }
    if (radial)
    {
        // The size handle, a circle with an arrow; the focal point, a triangle.
        const QPointF size = toScreen.map(handlePoint(Handle::Size, frame));
        circle(size, 5.0);
        painter.drawLine(size + QPointF(-2.5, -2.5), size + QPointF(2.5, 2.5));
        const QPointF focal = toScreen.map(handlePoint(Handle::Focal, frame));
        painter.setPen(QPen(ink, 1.0));
        painter.setBrush(paper);
        painter.drawPolygon(QPolygonF({focal + QPointF(0, -5), focal + QPointF(4.5, 3.5), focal + QPointF(-4.5, 3.5)}));
    }
    painter.restore();
}

void GradientTransformTool::deactivate(PhoenixView& view)
{
    if (_dragging != Handle::None)
        view.endInteraction();
    _dragging = Handle::None;
    _shape = nullptr;
    _fillIndex = -1;
    view.update();
}
