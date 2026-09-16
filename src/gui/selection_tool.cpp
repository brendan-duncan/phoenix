#include "selection_tool.h"

#include "phoenix_view.h"

#include "../data/element.h"
#include "../edit/selection.h"

#include <QKeyEvent>
#include <QMouseEvent>

namespace {

/// A press within this many screen pixels of where it started counts as a click
/// rather than a drag, so a slightly shaky click still selects.
constexpr double kDragThresholdPixels = 3.0;

} // namespace

QRectF SelectionTool::marqueeRect() const
{
    return QRectF(_marqueeStart, _marqueeEnd).normalized();
}

bool SelectionTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    _additive = (event->modifiers() & Qt::ShiftModifier) != 0;

    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());

    if (hit)
    {
        if (_additive)
            _selection.toggle(hit.element);
        else
            _selection.select(hit.element);

        view.update();
        return true;
    }

    // Nothing under the cursor, so this is either a marquee or a click on empty
    // stage. Which one it is is not known until the mouse moves.
    _marqueeActive = true;
    _marqueeStart = documentPos;
    _marqueeEnd = documentPos;
    return true;
}

bool SelectionTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;

    if (!_marqueeActive)
        return false;

    _marqueeEnd = documentPos;
    view.update();
    return true;
}

bool SelectionTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_marqueeActive)
        return false;

    _marqueeActive = false;
    _marqueeEnd = documentPos;

    const QRectF rect = marqueeRect();
    const double threshold = kDragThresholdPixels * view.pickTolerance() / 4.0;

    if (rect.width() <= threshold && rect.height() <= threshold)
    {
        // A click on empty stage, which clears unless shift is deliberately
        // holding on to what is already selected.
        if (!_additive)
            _selection.clear();
    }
    else
    {
        std::vector<fla::Element*> swept = view.elementsIn(rect);

        std::vector<fla::DOMElement*> picked;
        picked.reserve(swept.size());
        for (fla::Element* element : swept)
            picked.push_back(element);

        if (_additive)
        {
            for (fla::DOMElement* element : picked)
                _selection.add(element);
        }
        else
        {
            _selection.select(picked);
        }
    }

    view.update();
    return true;
}

bool SelectionTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        if (_marqueeActive)
        {
            _marqueeActive = false;
            view.update();
            return true;
        }

        if (!_selection.isEmpty())
        {
            _selection.clear();
            view.update();
            return true;
        }
    }

    return false;
}

void SelectionTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;

    if (!_marqueeActive)
        return;

    QPen pen(QColor(0, 170, 255), 1.0 * scale, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(QColor(0, 170, 255, 40));
    painter.drawRect(marqueeRect());
}

void SelectionTool::deactivate(PhoenixView& view)
{
    if (!_marqueeActive)
        return;

    // Abandon a marquee in progress rather than leaving it drawn over the stage.
    _marqueeActive = false;
    view.update();
}
