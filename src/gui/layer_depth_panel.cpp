#include "layer_depth_panel.h"

#include "../data/layer.h"
#include "../data/timeline.h"
#include "../edit/camera.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMargin = 12;
constexpr int kCameraX = 36;
constexpr int kRow = 22;
constexpr int kTop = 28;
/// The deepest the panel shows, past which layers sit at its edge.
constexpr double kFarthest = 4000.0;
constexpr double kNearest = -800.0;

} // namespace

LayerDepthPanel::LayerDepthPanel(LayerDepthLinks links, QWidget* parent)
    : QWidget(parent)
    , _links(std::move(links))
{
    setMinimumSize(320, 160);
    setToolTip("Drag a layer nearer or farther from the camera; near layers move more as the camera pans");
}

std::vector<fla::Layer*> LayerDepthPanel::layers() const
{
    std::vector<fla::Layer*> found;
    fla::Timeline* scene = _links.scene ? _links.scene() : nullptr;
    for (fla::Layer* layer : scene ? scene->layers : std::vector<fla::Layer*>())
        if (layer && layer->layerType != fla::Layer::Type::Folder && !fla::camera::isCamera(*layer) &&
            !fla::camera::isFileCamera(*layer))
            found.push_back(layer);
    return found;
}

double LayerDepthPanel::xOf(double depth) const
{
    // Even steps of distance from the camera, the stage's plane a third of
    // the way across.
    const double left = kCameraX + 8.0;
    const double right = width() - kMargin;
    const double t = (std::clamp(depth, kNearest, kFarthest) - kNearest) / (kFarthest - kNearest);
    return left + t * (right - left);
}

double LayerDepthPanel::depthAt(double x) const
{
    const double left = kCameraX + 8.0;
    const double right = width() - kMargin;
    const double t = std::clamp((x - left) / std::max(1.0, right - left), 0.0, 1.0);
    // Snapped to tens of pixels, and to the stage's plane near it.
    double depth = std::round((kNearest + t * (kFarthest - kNearest)) / 10.0) * 10.0;
    if (std::fabs(depth) < 40.0)
        depth = 0.0;
    return depth;
}

int LayerDepthPanel::rowTop(int index) const
{
    return kTop + index * kRow;
}

void LayerDepthPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Base));
    const std::vector<fla::Layer*> all = layers();
    const int bottom = std::max(height() - kMargin, rowTop(static_cast<int>(all.size())) + 4);

    // The camera and what it sees: the stage's width at the stage's plane,
    // spreading on behind it.
    const QPointF eye(kCameraX, (kTop + bottom) / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.color(QPalette::Highlight));
    painter.drawPolygon(QPolygonF({eye + QPointF(-14, -9), eye + QPointF(-14, 9), eye + QPointF(2, 0)}));
    const double stagePlane = xOf(0.0);
    const double spread = (bottom - kTop) / 2.0;
    painter.setPen(QPen(pal.color(QPalette::Mid), 1.0, Qt::DashLine));
    painter.drawLine(eye, QPointF(width() - kMargin, eye.y() - spread * (width() - kMargin - eye.x()) / (stagePlane - eye.x())));
    painter.drawLine(eye, QPointF(width() - kMargin, eye.y() + spread * (width() - kMargin - eye.x()) / (stagePlane - eye.x())));
    painter.setPen(QPen(pal.color(QPalette::Text), 1.0, Qt::DotLine));
    painter.drawLine(QPointF(stagePlane, kTop - 8), QPointF(stagePlane, bottom));
    painter.setPen(pal.color(QPalette::PlaceholderText));
    painter.drawText(QRectF(stagePlane - 40, 4, 80, 16), Qt::AlignCenter, "Stage");
    painter.drawText(QRectF(width() - kMargin - 60, 4, 60, 16), Qt::AlignRight, "Far");

    // Each layer at its depth.
    for (size_t i = 0; i < all.size(); ++i)
    {
        const fla::Layer* layer = all[i];
        const double depth = fla::camera::depthOf(*layer);
        const double x = xOf(depth);
        const int top = rowTop(static_cast<int>(i));
        const QColor color(layer->color[0], layer->color[1], layer->color[2]);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(x - 3, top + 2, 6, kRow - 4), 2, 2);
        painter.setPen(pal.color(QPalette::Text));
        const QString label = QString::fromStdString(layer->name) +
                              (depth != 0.0 ? QString("  %1").arg(depth, 0, 'f', 0) : QString());
        const bool right = x < width() - 140;
        painter.drawText(QRectF(right ? x + 8 : x - 140, top, 132, kRow),
            (right ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, label);
    }
}

void LayerDepthPanel::cancelDrag()
{
    if (!_dragging)
        return;
    _dragging->extraAttributes = _before;
    _dragging = nullptr;
    update();
}

void LayerDepthPanel::mousePressEvent(QMouseEvent* event)
{
    const std::vector<fla::Layer*> all = layers();
    const int index = (static_cast<int>(event->position().y()) - kTop) / kRow;
    if (index < 0 || index >= static_cast<int>(all.size()) || event->position().y() < kTop)
        return;
    _dragging = all[static_cast<size_t>(index)];
    _before = _dragging->extraAttributes;
    mouseMoveEvent(event);
}

void LayerDepthPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (!_dragging)
        return;
    std::vector<std::pair<std::string, std::string>> attributes = _before;
    fla::camera::setDepth(attributes, depthAt(event->position().x()));
    _dragging->extraAttributes = attributes;
    update();
    if (_links.changed)
        _links.changed();
}

void LayerDepthPanel::mouseReleaseEvent(QMouseEvent*)
{
    if (!_dragging)
        return;
    fla::Layer* layer = _dragging;
    _dragging = nullptr;
    const auto after = layer->extraAttributes;
    layer->extraAttributes = _before;
    if (after != _before)
    {
        if (fla::CommandStack* stack = _links.commands ? _links.commands() : nullptr)
        {
            stack->push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                layer->extraAttributes, _before, after, "Layer Depth")));
            stack->breakMergeChain();
        }
        else
        {
            layer->extraAttributes = after;
        }
    }
    update();
    if (_links.changed)
        _links.changed();
}
