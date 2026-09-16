#include "pen_tool.h"

#include "draw_placement.h"
#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/snapping.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>

namespace {

/// Index used for the single fill and stroke on a generated shape; the format
/// numbers styles from one.
constexpr int kStyleIndex = 1;

QPointF toQPointF(const fla::Point& point)
{
    return QPointF(point.x, point.y);
}

fla::Point toPoint(const QPointF& point)
{
    return fla::Point(point.x(), point.y());
}

} // namespace

QPainterPath PenTool::previewPath(bool includeCursor) const
{
    QPainterPath painterPath;
    if (_path.anchors.empty())
        return painterPath;

    painterPath.moveTo(toQPointF(_path.anchors[0].position));

    for (size_t i = 1; i < _path.anchors.size(); ++i)
    {
        const fla::Anchor& from = _path.anchors[i - 1];
        const fla::Anchor& to = _path.anchors[i];

        if (!from.hasOutCurve() && !to.hasInCurve())
            painterPath.lineTo(toQPointF(to.position));
        else
            painterPath.cubicTo(toQPointF(from.outHandle), toQPointF(to.inHandle),
                toQPointF(to.position));
    }

    if (includeCursor && !_draggingHandle)
    {
        // The rubber band shows where the next segment would land, curving if
        // the last anchor has a handle pulled out.
        const fla::Anchor& last = _path.anchors.back();
        if (last.hasOutCurve())
            painterPath.cubicTo(toQPointF(last.outHandle), _cursor, _cursor);
        else
            painterPath.lineTo(_cursor);
    }

    return painterPath;
}

bool PenTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (!view.activeFrame())
        return false;

    QPointF position = documentPos;
    if (fla::Snapper* snapper = view.snapper())
    {
        if (!_active)
            view.gatherSnapCandidates(*snapper, {});

        if (snapper->isEnabled())
        {
            snapper->setTolerance(view.pickTolerance() * 2.0);
            const fla::SnapResult x = snapper->snapX({position.x()});
            const fla::SnapResult y = snapper->snapY({position.y()});
            position = QPointF(position.x() + x.adjustment, position.y() + y.adjustment);
        }
    }

    // Clicking back on the first point closes the path, which is how a pen tool
    // is told the outline is finished.
    if (_active && _path.anchors.size() >= 2)
    {
        const QPointF first = toQPointF(_path.anchors.front().position);
        if (std::hypot(position.x() - first.x(), position.y() - first.y())
                <= view.pickTolerance() * 2.0)
        {
            finish(view, true);
            return true;
        }
    }

    _active = true;
    _path.anchors.push_back(fla::Anchor(toPoint(position)));
    _draggingHandle = true;
    _cursor = position;

    view.update();
    return true;
}

bool PenTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_active)
        return false;

    _cursor = documentPos;

    if (_draggingHandle && !_path.anchors.empty())
    {
        fla::Anchor& anchor = _path.anchors.back();

        // Dragging out of a fresh point makes it smooth; holding alt keeps the
        // two sides independent, which is how a corner with one curved side is
        // drawn.
        anchor.smooth = (event->modifiers() & Qt::AltModifier) == 0;
        anchor.setOutHandle(toPoint(documentPos));
    }
    else if (_path.anchors.size() >= 2)
    {
        const QPointF first = toQPointF(_path.anchors.front().position);
        _overFirstAnchor = std::hypot(documentPos.x() - first.x(),
            documentPos.y() - first.y()) <= view.pickTolerance() * 2.0;
    }

    view.update();
    return true;
}

bool PenTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)documentPos;

    if (event->button() != Qt::LeftButton || !_active)
        return false;

    _draggingHandle = false;
    view.update();
    return true;
}

bool PenTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_active)
        return false;

    if (event->key() == Qt::Key_Escape)
    {
        cancel(view);
        return true;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        finish(view, false);
        return true;
    }

    return false;
}

void PenTool::finish(PhoenixView& view, bool close)
{
    // One anchor is a click, not a path.
    if (_path.anchors.size() < 2)
    {
        cancel(view);
        return;
    }

    _path.closed = close;

    fla::Frame* frame = view.activeFrame();
    if (!frame)
    {
        cancel(view);
        return;
    }

    fla::Element* shape = createShape(frame);
    if (!shape)
    {
        cancel(view);
        return;
    }

    _path = fla::EditablePath();
    _active = false;
    _draggingHandle = false;
    _overFirstAnchor = false;

    placeDrawnElement(view, _commandStack, _selection, frame, shape, "Pen",
        _style.objectDrawing);
}

void PenTool::cancel(PhoenixView& view)
{
    _path = fla::EditablePath();
    _active = false;
    _draggingHandle = false;
    _overFirstAnchor = false;
    view.update();
}

fla::Element* PenTool::createShape(fla::Frame* frame) const
{
    fla::Shape* shape = new fla::Shape(frame);

    // Only a closed outline encloses an area, so an open path is stroke only
    // however the fill is set.
    int fillIndex = -1;
    if (_path.closed)
    {
        if (fla::FillStyle* fill = _style.createFill(shape))
        {
            shape->fills.push_back(fill);
            shape->fillsMap[kStyleIndex] = fill;
            fillIndex = kStyleIndex;
        }
    }

    fla::StrokeStyle* stroke = _style.createStroke(shape);
    if (!stroke && fillIndex == -1)
    {
        // With no fill and no stroke the path would be invisible, so fall back
        // to a hairline rather than adding nothing to the document.
        fla::SolidStroke* fallback = new fla::SolidStroke(shape);
        fallback->weight = 1.0;
        fallback->fill = new fla::SolidColor(fallback);
        stroke = fallback;
    }

    int strokeIndex = -1;
    if (stroke)
    {
        shape->strokes.push_back(stroke);
        shape->strokesMap[kStyleIndex] = stroke;
        strokeIndex = kStyleIndex;
    }

    fla::Edge* edge = new fla::Edge(shape);
    edge->fillStyle1 = fillIndex;
    edge->strokeStyle = strokeIndex;
    _path.applyTo(*edge);
    shape->edges.push_back(edge);

    const QRectF bounds = previewPath(false).boundingRect();
    shape->localBounds = fla::Rect(
        fla::Point(bounds.left(), bounds.top()),
        fla::Point(bounds.right(), bounds.bottom()));
    shape->bounds = shape->localBounds;

    return shape;
}

void PenTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_active || _path.anchors.empty())
        return;

    // The path so far, including the rubber band to the cursor.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 170, 255), 1.0 * scale));
    painter.drawPath(previewPath(true));

    const double anchorSize = 3.0 * scale;
    const double handleSize = 2.5 * scale;

    for (size_t i = 0; i < _path.anchors.size(); ++i)
    {
        const fla::Anchor& anchor = _path.anchors[i];
        const QPointF position = toQPointF(anchor.position);

        // Handles, drawn as a tangent line with a round grip at each end.
        painter.setPen(QPen(QColor(120, 200, 255), 1.0 * scale));
        if (anchor.hasInCurve())
        {
            painter.drawLine(position, toQPointF(anchor.inHandle));
            painter.setBrush(QColor(120, 200, 255));
            painter.drawEllipse(toQPointF(anchor.inHandle), handleSize, handleSize);
        }
        if (anchor.hasOutCurve())
        {
            painter.drawLine(position, toQPointF(anchor.outHandle));
            painter.setBrush(QColor(120, 200, 255));
            painter.drawEllipse(toQPointF(anchor.outHandle), handleSize, handleSize);
        }

        // The first anchor is highlighted once clicking it would close the path.
        const bool closeTarget = (i == 0) && _overFirstAnchor && _path.anchors.size() >= 2;
        painter.setPen(QPen(QColor(0, 120, 200), 1.0 * scale));
        painter.setBrush(closeTarget ? QColor(255, 220, 0) : QColor(255, 255, 255));
        painter.drawRect(QRectF(position.x() - anchorSize, position.y() - anchorSize,
            anchorSize * 2.0, anchorSize * 2.0));
    }
}

void PenTool::deactivate(PhoenixView& view)
{
    if (_active)
        cancel(view);
}
