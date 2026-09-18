#include "selection_tool.h"

#include "phoenix_view.h"

#include "../data/element.h"
#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/snapping.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>

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
        // Dragging something already selected moves the whole selection, so a
        // press on it must not collapse the selection down to that one object.
        const bool alreadySelected = _selection.contains(hit.element);

        if (_additive)
            _selection.toggle(hit.element);
        else if (!alreadySelected)
            _selection.select(hit.element);

        if (!_additive && _selection.contains(hit.element))
            beginMove(view, documentPos);

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

    if (_moveActive)
    {
        updateMove(view, documentPos);
        return true;
    }

    if (!_marqueeActive)
        return false;

    _marqueeEnd = documentPos;
    view.update();
    return true;
}

bool SelectionTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (_moveActive)
    {
        updateMove(view, documentPos);
        commitMove(view);
        return true;
    }

    if (!_marqueeActive)
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
        if (_moveActive)
        {
            // Put everything back where the drag started and abandon it.
            for (const Target& target : _targets)
            {
                if (target.element)
                    target.element->transform = target.startTransform;
            }
            _targets.clear();
            _moveActive = false;
            view.invalidateBounds();
            view.update();
            return true;
        }

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
    if (_moveActive)
    {
        for (const Target& target : _targets)
        {
            if (target.element)
                target.element->transform = target.startTransform;
        }
        _targets.clear();
        _moveActive = false;
        view.invalidateBounds();
    }

    if (_marqueeActive)
    {
        // Abandon a marquee in progress rather than leaving it drawn over the
        // stage.
        _marqueeActive = false;
    }

    view.update();
}

void SelectionTool::beginMove(PhoenixView& view, const QPointF& documentPos)
{
    _targets.clear();

    QRectF bounds;
    bool first = true;

    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element)
            continue;

        _targets.push_back({element, element->transform});

        const QRectF elementBounds = view.elementBounds(element);
        if (elementBounds.isValid())
        {
            bounds = first ? elementBounds : bounds.united(elementBounds);
            first = false;
        }
    }

    if (_targets.empty())
        return;

    _moveActive = true;
    _moveDragging = false;
    _moveStart = documentPos;
    _moveBounds = bounds;

    // Gather the things this drag can line up with once, at the start: they do
    // not move while it runs.
    if (fla::Snapper* snapper = view.snapper())
    {
        std::vector<fla::Element*> moving;
        moving.reserve(_targets.size());
        for (const Target& target : _targets)
            moving.push_back(target.element);
        view.gatherSnapCandidates(*snapper, moving);
    }
}

QPointF SelectionTool::snapMove(PhoenixView& view, const QPointF& delta) const
{
    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled() || !_moveBounds.isValid())
        return delta;

    // Snapping is about where the box lands, so the candidates are its edges and
    // centre at the proposed position.
    const QRectF moved = _moveBounds.translated(delta);

    snapper->setTolerance(view.pickTolerance() * 2.0);

    const fla::SnapResult x = snapper->snapX(
        {moved.left(), moved.center().x(), moved.right()});
    const fla::SnapResult y = snapper->snapY(
        {moved.top(), moved.center().y(), moved.bottom()});

    return QPointF(delta.x() + x.adjustment, delta.y() + y.adjustment);
}

void SelectionTool::updateMove(PhoenixView& view, const QPointF& documentPos)
{
    const QPointF raw = documentPos - _moveStart;

    // Until the press has travelled, it is still just a selection click, and
    // clicking something must not move it. The same threshold the marquee uses
    // decides when a press becomes a drag.
    if (!_moveDragging)
    {
        const double threshold = kDragThresholdPixels * view.pickTolerance() / 4.0;
        if (std::abs(raw.x()) <= threshold && std::abs(raw.y()) <= threshold)
            return;

        _moveDragging = true;
    }

    const QPointF delta = snapMove(view, raw);

    for (const Target& target : _targets)
    {
        if (!target.element)
            continue;

        // Measured from the starting transform every time, so a long drag does
        // not accumulate rounding error.
        target.element->transform = target.startTransform;
        target.element->transform.tx += delta.x();
        target.element->transform.ty += delta.y();
    }

    // Only positions changed, so the path cache stays warm.
    view.invalidateBounds();
    view.update();
}

void SelectionTool::commitMove(PhoenixView& view)
{
    std::vector<fla::CommandPtr> commands;

    for (const Target& target : _targets)
    {
        if (!target.element)
            continue;

        const fla::Transform& now = target.element->transform;
        if (now.tx == target.startTransform.tx && now.ty == target.startTransform.ty)
            continue;

        commands.push_back(fla::CommandPtr(new fla::SetElementTransformCommand(
            target.element, target.startTransform, now, "Move")));

        // A shape put down somewhere else belongs to the artwork it now sits
        // on, so letting go of it drops it in as a drawn one would be.
        if (target.element->elementType() == fla::Element::Type::Shape)
            view.markShapeEdited(static_cast<fla::Shape*>(target.element));
    }

    // A click that did not actually move anything is not an edit.
    if (!commands.empty())
    {
        const bool needsMacro = commands.size() > 1;
        if (needsMacro)
            _commandStack.beginMacro("Move");

        for (fla::CommandPtr& command : commands)
            _commandStack.push(std::move(command));

        if (needsMacro)
            _commandStack.endMacro();

        _commandStack.breakMergeChain();
    }

    _moveActive = false;
    _moveDragging = false;
    _targets.clear();
    view.update();
}
