#pragma once

#include "tool.h"

#include "../edit/editable_path.h"

#include <QPointF>
#include <QTransform>

#include <cstddef>

namespace fla {
class CommandStack;
class Edge;
class Frame;
class Shape;
}

/// Animate's anchor point tools, in the pen's slot. They work on the shape
/// under the cursor, whose anchors show as the cursor passes over it.
///
/// - **Add Anchor Point** (=): a click on a path puts an anchor there,
///   leaving the curve as it was.
/// - **Delete Anchor Point** (-): a click on an anchor takes it out, the
///   curve refitted through the gap. The ends of an open path stay.
/// - **Convert Anchor Point** (shift+C): a click on a smooth anchor makes it
///   a corner; a drag from an anchor pulls out handles, making it smooth.
///
/// Each edit is one undo step.
class AnchorPointTool : public Tool
{
public:
    enum class Mode
    {
        Add,
        Delete,
        Convert
    };

    AnchorPointTool(Mode mode, fla::CommandStack& commandStack)
        : _mode(mode)
        , _commandStack(commandStack)
    {}

    QString name() const override;

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    bool showsFillHighlight() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// The shape under \a documentPos, as the tools pick it; false when there
    /// is none.
    bool pickShape(PhoenixView& view, const QPointF& documentPos);
    bool shapeStillThere() const;
    /// The anchor nearest \a local within \a tolerance, on any path.
    bool anchorNear(const QPointF& local, double tolerance, fla::Edge*& edge, size_t& pathIndex,
        size_t& anchor) const;
    void commit(PhoenixView& view, fla::Edge* edge, size_t pathIndex, const fla::EditablePath& before,
        const fla::EditablePath& after, const char* name);

    Mode _mode;
    fla::CommandStack& _commandStack;

    /// The shape showing its anchors, its frame (to see it is still there)
    /// and its own space to the space the tools work in.
    fla::Shape* _shape = nullptr;
    const fla::Frame* _frame = nullptr;
    QTransform _toDocument;

    /// Where on a path an anchor would go, for the add tool's marker.
    bool _hoverOnPath = false;
    QPointF _hoverPoint;

    /// The convert tool's drag: the anchor and its path as they were.
    bool _converting = false;
    bool _dragged = false;
    fla::Edge* _edge = nullptr;
    size_t _pathIndex = 0;
    size_t _anchor = 0;
    fla::EditablePath _before;
    QPointF _pressPos;
};
