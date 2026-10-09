#include "lasso_tool.h"

#include "phoenix_view.h"

#include "../data/element.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>

#include <cmath>

bool LassoTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    _drawing = true;
    _additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    _loop.clear();
    _loop << documentPos;
    view.update();
    return true;
}

bool LassoTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    if (!_drawing)
        return false;

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

    _drawing = false;
    _loop << documentPos;

    if (_loop.size() < 3)
    {
        if (!_additive)
            _selection.clear();
        _loop.clear();
        view.update();
        return true;
    }

    QPainterPath loop;
    loop.addPolygon(_loop);
    loop.closeSubpath();

    // The loop cuts merge artwork along itself and takes what is inside, and
    // picks objects it touches whole.
    view.selectRegionsIn(loop, _additive);

    _loop.clear();
    view.update();
    return true;
}

bool LassoTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() != Qt::Key_Escape || !_drawing)
        return false;

    _drawing = false;
    _loop.clear();
    view.update();
    return true;
}

void LassoTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_drawing || _loop.size() < 2)
        return;

    painter.setPen(QPen(QColor(0, 0, 0), 1.0 * scale, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(_loop);
}

void LassoTool::deactivate(PhoenixView& view)
{
    _drawing = false;
    _loop.clear();
    view.update();
}
