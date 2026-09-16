#pragma once

#include "tool.h"

#include "../edit/editable_path.h"

#include <QPointF>

namespace fla {
class CommandStack;
class Edge;
class Selection;
class Shape;
}

/// Animate's subselection tool: show a shape's anchors and drag them.
///
/// Selecting a shape with it reveals every anchor and handle on the shape's
/// edges. Dragging an anchor moves it with its handles; dragging a handle bends
/// the curve, keeping the tangent straight through a smooth point unless alt is
/// held to break it.
///
/// A drag edits the geometry live and pushes one command on release, the same
/// way the transform tools work.
class SubselectionTool : public Tool
{
public:
    SubselectionTool(fla::Selection& selection, fla::CommandStack& commandStack)
        : _selection(selection)
        , _commandStack(commandStack)
    {}

    QString name() const override { return "Subselection"; }

    QCursor cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// Which part of an anchor a drag grabbed.
    enum class Grip
    {
        None,
        Anchor,
        InHandle,
        OutHandle
    };

    /// The shape currently showing its anchors, or null.
    fla::Shape* editedShape() const;

    /// Maps the shape's own coordinates to document coordinates.
    QTransform shapeToDocument(PhoenixView& view) const;

    void cancelDrag(PhoenixView& view);

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;

    /// The edge being dragged and the geometry it had when the drag began.
    fla::Edge* _edge = nullptr;
    fla::EditablePath _pathAtDragStart;
    fla::EditablePath _path;

    Grip _grip = Grip::None;
    size_t _anchorIndex = 0;
    QPointF _dragStart;
};
