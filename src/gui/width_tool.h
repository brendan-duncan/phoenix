#pragma once

#include "tool.h"

#include "../data/stroke_style.h"

#include <QPainterPath>
#include <QPointF>
#include <QTransform>

#include <vector>

namespace fla {
class CommandStack;
class Shape;
}

/// The Width tool (U): drag out from a stroke to make it wider there, in to
/// make it thinner, as a width point on its profile; drag a width point to
/// change it, and select one and press Delete to take it away. The profile
/// belongs to the stroke's style, so strokes of the same style in a shape
/// change together. Each drag is one undo step.
class WidthTool : public Tool
{
public:
    explicit WidthTool(fla::CommandStack& commands);

    QString name() const override { return "Width"; }
    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool keyPress(PhoenixView& view, QKeyEvent* event) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    bool showsSelectionBounds() const override { return false; }
    void deactivate(PhoenixView& view) override;

    /// How far along \a path (0 to 1, by length) its point nearest \a point
    /// is, and how far away.
    static double nearestAlong(const QPainterPath& path, const QPointF& point, double& distance);

private:
    void refresh(PhoenixView& view);

    fla::CommandStack& _commands;
    fla::Shape* _shape = nullptr;
    fla::StrokeStyle* _style = nullptr;
    QPainterPath _run;
    QTransform _toDocument;
    int _selected = -1;
    bool _dragging = false;
    std::vector<fla::StrokeStyle::WidthMarker> _before;
};
