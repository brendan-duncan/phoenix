#pragma once

#include "tool.h"

#include "../edit/editable_path.h"

#include <QPointF>

#include <cstddef>
#include <vector>

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

    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

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

    /// Rewrites  edge to  path as one undo step.
    void commitGeometry(PhoenixView& view, fla::Edge* edge,
        const fla::EditablePath& before, const fla::EditablePath& after,
        const char* gesture);

    /// Finds the anchor under the cursor. Returns false when there is none.
    bool anchorAt(PhoenixView& view, const QPointF& localPos, fla::Edge*& edge,
        fla::EditablePath& path, size_t& index) const;

    /// One anchor caught by a drag: which edge it lives on, which anchor it is,
    /// and that edge's geometry when the drag began.
    struct Grabbed
    {
        fla::Edge* edge = nullptr;
        size_t anchorIndex = 0;
        fla::EditablePath before;
        fla::EditablePath current;
    };

    /// Gathers every anchor in  shape sitting at  position into _grabbed.
    void grabCoincidentAnchors(fla::Shape& shape, const fla::Point& position);

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;

    /// Every anchor the drag is moving.
    ///
    /// A corner where two pieces of outline meet is two anchors, one on each
    /// piece: a merged shape is written as one edge per piece, so all of its
    /// corners are shared. Moving only the anchor that was grabbed leaves the
    /// others behind and tears the outline apart, so an anchor drag takes every
    /// anchor sitting at that point. A handle belongs to one curve alone, so a
    /// handle drag holds a single entry.
    std::vector<Grabbed> _grabbed;

    Grip _grip = Grip::None;
    size_t _anchorIndex = 0;
    QPointF _dragStart;

    /// The anchor the last click landed on, which Delete removes.
    fla::Edge* _selectedEdge = nullptr;
    size_t _selectedAnchor = 0;
    bool _hasSelectedAnchor = false;
};
