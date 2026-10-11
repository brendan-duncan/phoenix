#include "lasso_tool.h"

#include "phoenix_view.h"

#include "../data/element.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>

#include <cmath>

namespace {

/// How close, in screen pixels, a click must be to the polygon's first
/// corner to close it there.
constexpr double kCloseDistance = 6.0;

} // namespace

bool LassoTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (_polygon && _drawing)
    {
        // Another corner, or the first one again, which closes the polygon.
        const double reach = kCloseDistance / qMax(view.zoom(), 1.0e-6);
        const QPointF first = _loop.first();
        if (_loop.size() >= 3 && std::hypot(documentPos.x() - first.x(), documentPos.y() - first.y()) <= reach)
        {
            close(view);
            return true;
        }
        _loop << documentPos;
        _hover = documentPos;
        view.update();
        return true;
    }

    _drawing = true;
    _additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    _loop.clear();
    _loop << documentPos;
    _hover = documentPos;
    view.update();
    return true;
}

bool LassoTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    if (!_drawing)
        return false;

    if (_polygon)
    {
        // The side to the next corner follows the cursor.
        _hover = documentPos;
        view.update();
        return true;
    }

    // Points closer than a screen pixel add nothing to the loop.
    const double minimum = 1.0 / qMax(view.zoom(), 1.0e-6);
    const QPointF last = _loop.last();
    if (std::hypot(documentPos.x() - last.x(), documentPos.y() - last.y()) >= minimum)
        _loop << documentPos;

    view.update();
    return true;
}

bool LassoTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_drawing)
        return false;

    // The polygon goes on until it is closed.
    if (_polygon)
        return true;

    _loop << documentPos;
    close(view);
    return true;
}

bool LassoTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    if (!_polygon || !_drawing)
        return false;
    // The double-click's first press already added its corner, and its
    // second press another on top of it.
    if (_loop.size() >= 2 && _loop[_loop.size() - 1] == _loop[_loop.size() - 2])
        _loop.removeLast();
    close(view);
    return true;
}

void LassoTool::close(PhoenixView& view)
{
    _drawing = false;

    if (_loop.size() < 3)
    {
        if (!_additive)
            _selection.clear();
        _loop.clear();
        view.update();
        return;
    }

    QPainterPath loop;
    loop.addPolygon(_loop);
    loop.closeSubpath();

    // The loop cuts merge artwork along itself and takes what is inside, and
    // picks objects it touches whole.
    view.selectRegionsIn(loop, _additive);

    _loop.clear();
    view.update();
}

bool LassoTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_drawing)
        return false;

    if (event->key() == Qt::Key_Escape)
    {
        _drawing = false;
        _loop.clear();
        view.update();
        return true;
    }
    if (_polygon && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
    {
        close(view);
        return true;
    }
    return false;
}

void LassoTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    if (!_drawing || _loop.isEmpty())
        return;

    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    if (!_polygon)
    {
        if (_loop.size() >= 2)
            painter.drawPolyline(_loop);
        return;
    }

    // The sides so far, the one to the cursor, and the corners.
    QPolygonF sides = _loop;
    sides << _hover;
    painter.drawPolyline(sides);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale));
    painter.setBrush(Qt::white);
    const double r = 2.5 * scale;
    for (const QPointF& corner : _loop)
        painter.drawRect(QRectF(corner.x() - r, corner.y() - r, 2.0 * r, 2.0 * r));
    // The first corner is where the polygon closes; it shows when the
    // cursor is on it.
    const QPointF first = _loop.first();
    const double reach = kCloseDistance / qMax(view.zoom(), 1.0e-6);
    if (_loop.size() >= 3 && std::hypot(_hover.x() - first.x(), _hover.y() - first.y()) <= reach)
    {
        painter.setBrush(QColor(41, 128, 238));
        painter.drawEllipse(first, 2.0 * r, 2.0 * r);
    }
}

void LassoTool::deactivate(PhoenixView& view)
{
    _drawing = false;
    _loop.clear();
    view.update();
}
