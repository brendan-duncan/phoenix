#pragma once

#include "edge_bend.h"
#include "tool.h"

#include "../edit/editable_path.h"

#include <QPointF>
#include <QTransform>

namespace fla {
class CommandStack;
class DrawingStyle;
class Edge;
class Element;
class Frame;
class Selection;
class Shape;
}

/// The pen tool: build a path a point at a time.
///
/// Click places a corner point. Press and drag pulls out handles, making a
/// smooth point whose curve runs through it. Holding alt while dragging breaks
/// the tangent, so the two sides can go their own ways. Shift holds a new
/// point or a handle to a multiple of 45 degrees. Clicking the last point
/// again retracts its outgoing handle, so the next segment leaves it straight.
/// Pressing on the first point closes the path, and dragging there shapes the
/// closing curve. Enter or a double click finishes it open; Escape abandons it.
/// Pressing on either end of a selected open path picks that path up again
/// and carries on from there.
///
/// Nothing reaches the document until the path is finished, so an abandoned path
/// leaves no trace and a finished one is a single undo step.
class PenTool : public Tool
{
public:
    PenTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Pen"; }
    bool drawsOnStage() const override { return true; }

    bool wantsEnter() const override { return _active; }

    /// The pen with an x before a path starts, an o when a click would close
    /// it, and plain in between.
    QCursor cursor() const override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    /// Finishes the path open. The press that came with the double click has
    /// already added an anchor on top of the last one, which is dropped.
    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// Turns the path so far into a shape in the document, or writes it back
    /// to the path it resumed. Does nothing when there is not enough to be
    /// worth keeping.
    void finish(PhoenixView& view, bool close);

    /// Throws the path away without touching the document.
    void cancel(PhoenixView& view);

    /// Builds the shape for the finished path. The caller owns the result.
    fla::Element* createShape(fla::Frame* frame) const;

    /// The path so far as something a painter can draw.
    QPainterPath previewPath(bool includeCursor) const;

    /// Where a point lands with shift held: on the ray from \a from at the
    /// nearest multiple of 45 degrees.
    static QPointF constrained(const QPointF& from, const QPointF& to);

    /// Loads the snapper with the stage and with the path being drawn, and
    /// snaps a press.
    QPointF snapped(PhoenixView& view, const QPointF& documentPos) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    fla::EditablePath _path;

    /// True once the first point is down and the path is being built.
    bool _active = false;

    /// True between press and release, while handles are being pulled out of
    /// the anchor at _dragIndex.
    bool _draggingHandle = false;
    size_t _dragIndex = 0;

    /// The drag only pulls the outgoing handle: the anchor was clicked again
    /// to retract it, and dragging on from there shapes the next segment
    /// without curving the one that arrived.
    bool _outOnly = false;

    /// The press landed on the first anchor, so the release closes the path
    /// and the drag in between shapes the closing curve.
    bool _closing = false;

    /// Where the cursor is, for the rubber band from the last anchor.
    QPointF _cursor;

    /// Whether the cursor is close enough to the first anchor that clicking
    /// would close the path, so the overlay can say so.
    bool _overFirstAnchor = false;

    /// Pushes the cursor to the view when what it should show has changed.
    void refreshCursor(PhoenixView& view);

    int _cursorKind = -1;

    /// With no path being drawn, the pen hovers the selected shape: over an
    /// anchor it removes, over a segment it adds.
    EdgeBender::Probe probeSelection(PhoenixView& view, const QPointF& documentPos) const;

    /// Carries out the add or remove the hover promised. Returns false when
    /// there was nothing to do.
    bool editSelectedShape(PhoenixView& view, const QPointF& documentPos);

    EdgeBender::Probe _hover;

    /// An end of an open path on the selected shape that a press would pick
    /// up and continue.
public:
    /// While a path is being drawn, Ctrl-click ends it, so the key is the
    /// pen's rather than the Selection tool's.
    bool usesControlKey() const override { return _active; }

private:
    struct ResumePoint
    {
        bool valid = false;
        fla::Shape* shape = nullptr;
        fla::Edge* edge = nullptr;
        size_t pathIndex = 0;
        /// True at the path's first anchor, where the path has to be turned
        /// round before it can be continued.
        bool atStart = false;
    };

    /// An end of an open path near a point: of the selected shape when
    /// there is one, else of any shape on the frame being drawn on.
    ResumePoint resumePointAt(PhoenixView& view, const QPointF& documentPos) const;

    /// Join Paths: the path being drawn ends on an end of another open
    /// path, and the two become one, written into that path's edge.
    bool joinAt(PhoenixView& view, const ResumePoint& target);

    void beginResume(PhoenixView& view, const ResumePoint& point);

    ResumePoint _hoverResume;

    /// The path being continued, if any: the edge to write back to, its
    /// geometry before, and the shape's transform so the pen can work in
    /// document space.
    bool _resuming = false;
    fla::Shape* _resumeShape = nullptr;
    fla::Edge* _resumeEdge = nullptr;
    size_t _resumePathIndex = 0;
    fla::EditablePath _resumeBefore;
    QTransform _resumeToDocument;
};
