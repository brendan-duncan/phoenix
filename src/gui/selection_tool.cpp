#include "selection_tool.h"
#include "theme.h"
#include "../edit/element_clone.h"

#include "phoenix_view.h"
#include "selection_targets.h"

#include "../data/element.h"
#include "../data/frame.h"
#include "../data/shape.h"
#include "../data/symbol_instance.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/selection.h"
#include "../edit/shape_merge.h"
#include "../edit/snapping.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>

#include <cmath>
#include <cstdlib>

namespace {

/// A press within this many screen pixels of where it started counts as a click
/// rather than a drag, so a slightly shaky click still selects.
constexpr double kDragThresholdPixels = 3.0;

constexpr double kPi = 3.14159265358979323846;

/// Shift constrains a move to the eight compass directions.
QPointF constrainToCompass(const QPointF& delta)
{
    const double length = std::hypot(delta.x(), delta.y());
    if (length < 1.0e-9)
        return delta;

    const double step = kPi / 4.0;
    const double angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
    return QPointF(length * std::cos(angle), length * std::sin(angle));
}

const fla::Frame* frameOf(const fla::Element* element)
{
    for (const fla::DOMElement* p = element ? element->parent : nullptr; p; p = p->parent)
    {
        if (p->domType() == fla::DOMElement::DOMType::Frame)
            return static_cast<const fla::Frame*>(p);
    }
    return nullptr;
}

} // namespace

QRectF SelectionTool::marqueeRect() const
{
    return QRectF(_marqueeStart, _marqueeEnd).normalized();
}

Cursors::Kind SelectionTool::hoverKindAt(PhoenixView& view, const QPointF& documentPos) const
{
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (!hit)
        return Cursors::Kind::Arrow;

    // An unselected shape's outline can be bent or its corner moved; anything
    // else, and anything selected, moves as a whole.
    if (!_selection.contains(hit.element))
    {
        const EdgeBender::Probe probe = EdgeBender::probe(view, hit, documentPos);
        if (probe.grab == EdgeBender::Grab::Corner)
            return Cursors::Kind::ArrowCorner;
        if (probe.grab == EdgeBender::Grab::Curve)
            return Cursors::Kind::ArrowCurve;
    }

    return Cursors::Kind::ArrowMove;
}

bool SelectionTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    _additive = (event->modifiers() & Qt::ShiftModifier) != 0;
    _pressedElement = nullptr;
    _removeOnRelease = false;
    _duplicated = false;

    // A shape hint, or a selected motion tween's path, is dragged where it
    // is grabbed.
    if (!_additive && view.beginHintDrag(documentPos))
        return true;
    if (!_additive && view.beginMotionPathDrag(documentPos))
        return true;

    // A bone of an armature on show poses it, as on Animate's armature
    // layers, rather than picking what is under it.
    if (!_additive && _poser.begin(view, documentPos))
        return true;

    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());

    if (hit)
    {
        const bool alreadySelected = _selection.contains(hit.element);

        // The outline of something not yet selected bends under the arrow
        // rather than moving. Ctrl here, with this tool active in its own
        // right, adds a corner; from another tool ctrl is only the temporary
        // arrow.
        if (!alreadySelected && !_additive)
        {
            const EdgeBender::Probe probe = EdgeBender::probe(view, hit, documentPos);
            if (probe.grab != EdgeBender::Grab::None)
            {
                const bool insertCorner = (event->modifiers() & Qt::ControlModifier) != 0 &&
                    view.activeTool() == this;
                _bender.begin(view, probe, documentPos, insertCorner);
                if (_bender.active())
                {
                    _bendHit = hit;
                    _bendPress = documentPos;
                    return true;
                }
            }
        }

        // Merge-drawn artwork is selected by region: the fill or stroke under
        // the cursor is lifted out and becomes the selection, and dragging it
        // moves just that.
        if (!alreadySelected && view.selectRegionAt(hit, documentPos, false, _additive))
        {
            beginMove(view, documentPos);
            view.update();
            return true;
        }

        // Shift adds to the selection on the press, so a shift-drag moves the
        // lot; taking something away waits for a release that did not drag.
        if (_additive)
        {
            if (alreadySelected)
                _removeOnRelease = true;
            else
                _selection.add(hit.element);
            _pressedElement = hit.element;
        }
        else if (!alreadySelected)
        {
            _selection.select(hit.element);
        }

        if (_selection.contains(hit.element))
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
    if (view.motionPathDragging())
    {
        if (event->buttons() & Qt::LeftButton)
            view.dragMotionPath(documentPos);
        return true;
    }
    if (view.hintDragging())
    {
        if (event->buttons() & Qt::LeftButton)
            view.dragHint(documentPos);
        return true;
    }
    if (_poser.active())
    {
        if (event->buttons() & Qt::LeftButton)
            _poser.drag(view, documentPos);
        return true;
    }

    if (_bender.active())
    {
        _bender.update(view, documentPos);
        return true;
    }

    if (_moveActive)
    {
        updateMove(view, documentPos, event->modifiers());
        return true;
    }

    if (!_marqueeActive)
    {
        // Hovering: the arrow says what a press would do.
        const Cursors::Kind kind = hoverKindAt(view, documentPos);
        if (kind != _hoverKind)
        {
            _hoverKind = kind;
            view.setCursor(cursor());
        }
        return false;
    }

    _marqueeEnd = documentPos;
    view.update();
    return true;
}

bool SelectionTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    if (view.motionPathDragging())
    {
        view.dragMotionPath(documentPos);
        view.endMotionPathDrag(true);
        return true;
    }
    if (view.hintDragging())
    {
        view.dragHint(documentPos);
        view.endHintDrag(true);
        return true;
    }

    if (_poser.active())
    {
        _poser.finish(view, _commandStack);
        return true;
    }

    if (_bender.active())
    {
        _bender.update(view, documentPos);
        const bool bent = _bender.moved();
        _bender.commit(view, _commandStack);

        // A press on an outline that never moved is a click, and a click on
        // merge artwork selects the stroke or fill it landed on.
        if (!bent)
        {
            const HitResult hit = view.hitTest(_bendPress, view.pickTolerance());
            if (hit && !view.selectRegionAt(hit, _bendPress, false, false))
                _selection.select(hit.element);
            view.update();
        }
        return true;
    }

    if (_moveActive)
    {
        updateMove(view, documentPos, event->modifiers());
        const bool dragged = _moveDragging;
        commitMove(view);

        // A shift-click, not a shift-drag, on something already selected takes
        // it out of the selection.
        if (!dragged && _removeOnRelease && _pressedElement)
            _selection.remove(_pressedElement);
        _pressedElement = nullptr;
        _removeOnRelease = false;
        view.update();
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
        // A marquee cuts merge artwork along its edge and takes what is
        // inside, and picks objects it touches whole.
        QPainterPath area;
        area.addRect(rect);
        view.selectRegionsIn(area, _additive);
    }

    view.update();
    return true;
}

bool SelectionTool::mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;

    // The press of the double-click already selected the region and may have
    // begun a move; that move is abandoned in favor of the wider selection.
    if (_moveActive)
        cancelMove(view);

    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (!hit)
    {
        // Double-clicking empty stage steps back out of the symbol being
        // edited, as in Animate.
        if (view.scopeDepth() > 0)
        {
            view.exitScope();
            return true;
        }
        return false;
    }

    // Double-clicking a symbol instance edits the symbol in place; a group,
    // the group.
    if (hit.element->elementType() == fla::Element::Type::SymbolInstance)
        return view.enterSymbol(static_cast<fla::SymbolInstance*>(hit.element));
    if (hit.element->elementType() == fla::Element::Type::Group)
        return view.enterGroup(static_cast<fla::Group*>(hit.element));

    return view.selectRegionAt(hit, documentPos, true, false);
}

bool SelectionTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    const int key = event->key();
    if (key == Qt::Key_Escape && view.motionPathDragging())
    {
        view.endMotionPathDrag(false);
    view.endHintDrag(false);
        return true;
    }
    if (key == Qt::Key_Escape && view.hintDragging())
    {
        view.endHintDrag(false);
        return true;
    }
    if ((key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) &&
        !(event->modifiers() & Qt::ControlModifier) &&
        !_moveActive && !_marqueeActive && !_bender.active() && !_selection.isEmpty())
    {
        // One pixel, or ten with shift, as in Animate.
        const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
        const double dx = key == Qt::Key_Left ? -step : key == Qt::Key_Right ? step : 0.0;
        const double dy = key == Qt::Key_Up ? -step : key == Qt::Key_Down ? step : 0.0;
        nudge(view, dx, dy);
        return true;
    }

    if (event->key() == Qt::Key_Escape)
    {
        if (_bender.active())
        {
            _bender.cancel(view);
            return true;
        }

        if (_moveActive)
        {
            cancelMove(view);
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
    if (_marqueeActive)
    {
        QPen pen(theme::color("stage.handleLine"), 1.0 * scale, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(marqueeRect());
    }

    const fla::Snapper* snapper = view.snapper();
    if (_moveActive && _moveDragging && snapper && snapper->objectSnapEnabled())
    {
        // The snap ring sits at the grab point and grows when the drag has
        // locked onto something.
        const double radius = (_snapped ? 6.0 : 3.5) * scale;
        painter.setPen(QPen(theme::color("stage.handleLine"), (_snapped ? 1.5 : 1.0) * scale));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(_snapRingPos, radius, radius);
    }
}

void SelectionTool::nudge(PhoenixView& view, double dx, double dy)
{
    std::vector<fla::CommandPtr> commands;

    for (fla::Element* element : transformLeaves(_selection))
    {
        fla::Transform after = element->transform;
        after.tx += dx;
        after.ty += dy;

        commands.push_back(view.transformCommand(element, element->transform, after, "Move"));

        if (element->elementType() == fla::Element::Type::Shape)
            view.markShapeEdited(static_cast<fla::Shape*>(element));
    }

    if (commands.empty())
        return;

    // Pushing applies the move. The merge chain is left open on purpose, so
    // tapping an arrow key ten times is one thing to undo.
    const bool needsMacro = commands.size() > 1;
    if (needsMacro)
        _commandStack.beginMacro("Move");

    for (fla::CommandPtr& command : commands)
        _commandStack.push(std::move(command));

    if (needsMacro)
        _commandStack.endMacro();

    view.invalidateBounds();
    view.update();
}

void SelectionTool::cancelMove(PhoenixView& view)
{
    // Put everything back where the drag started and abandon it.
    for (const Target& target : _targets)
    {
        if (target.element)
            target.element->transform = target.startTransform;
    }
    _targets.clear();
    _moveActive = false;
    _moveDragging = false;
    view.invalidateBounds();
    view.update();
}

void SelectionTool::deactivate(PhoenixView& view)
{
    _hoverKind = Cursors::Kind::Arrow;
    view.endMotionPathDrag(false);

    if (_poser.active())
        _poser.cancel(view);
    if (_bender.active())
        _bender.cancel(view);

    if (_moveActive)
        cancelMove(view);

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

    // A group moves by its members; the box and the snapping cover the lot.
    for (fla::Element* element : transformLeaves(_selection))
        _targets.push_back({element, element->transform});

    const std::vector<fla::Element*> topLevel = topLevelElements(_selection);
    for (fla::Element* element : topLevel)
    {
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
        view.gatherSnapCandidates(*snapper, topLevel);
}

QPointF SelectionTool::snapMove(PhoenixView& view, const QPointF& delta)
{
    _snapped = false;

    fla::Snapper* snapper = view.snapper();
    if (!snapper || !snapper->isEnabled() || !_moveBounds.isValid())
        return delta;

    // Snapping is about where the box lands, so the candidates are its edges and
    // center at the proposed position.
    const QRectF moved = _moveBounds.translated(delta);

    snapper->setTolerance(view.pickTolerance() * 2.0);

    const fla::SnapResult x = snapper->snapX(
        {moved.left(), moved.center().x(), moved.right()});
    const fla::SnapResult y = snapper->snapY(
        {moved.top(), moved.center().y(), moved.bottom()});

    _snapped = x.snapped() || y.snapped();
    return QPointF(delta.x() + x.adjustment, delta.y() + y.adjustment);
}

void SelectionTool::duplicateTargets(PhoenixView& view)
{
    // Whatever sits directly in the frame is copied, of any kind.
    for (const Target& target : _targets)
    {
        if (!target.element || !target.element->parent ||
            target.element->parent->domType() != fla::DOMElement::DOMType::Frame)
        {
            return;
        }
    }

    std::vector<Target> copies;
    _commandStack.beginMacro("Duplicate");

    for (const Target& target : _targets)
    {
        fla::Frame* frame = const_cast<fla::Frame*>(frameOf(target.element));
        fla::Element* original = target.element;

        // The original goes back where it was; the copy takes over the drag.
        original->transform = target.startTransform;

        fla::Element* copy = fla::cloneElement(*original, frame);
        if (!copy)
            continue;
        _commandStack.push(fla::CommandPtr(new fla::AddElementCommand(
            frame, copy, "Duplicate", &_selection)));
        copies.push_back({copy, target.startTransform});
    }

    _commandStack.endMacro();
    _commandStack.breakMergeChain();

    std::vector<fla::DOMElement*> picked;
    for (const Target& copy : copies)
        picked.push_back(copy.element);
    _selection.select(picked);

    _targets = copies;
    _duplicated = true;
    view.clearCaches();
}

void SelectionTool::updateMove(PhoenixView& view, const QPointF& documentPos, Qt::KeyboardModifiers modifiers)
{
    QPointF raw = documentPos - _moveStart;

    // Until the press has traveled, it is still just a selection click, and
    // clicking something must not move it. The same threshold the marquee uses
    // decides when a press becomes a drag.
    if (!_moveDragging)
    {
        const double threshold = kDragThresholdPixels * view.pickTolerance() / 4.0;
        if (std::abs(raw.x()) <= threshold && std::abs(raw.y()) <= threshold)
            return;

        _moveDragging = true;

        // Alt at the moment the drag begins means "move a copy".
        if ((modifiers & Qt::AltModifier) && !_duplicated)
            duplicateTargets(view);
    }

    if (modifiers & Qt::ShiftModifier)
        raw = constrainToCompass(raw);

    const QPointF delta = snapMove(view, raw);
    _snapRingPos = documentPos;

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

        commands.push_back(view.transformCommand(target.element, target.startTransform, now, "Move"));

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
    _duplicated = false;
    _targets.clear();
    view.update();
}
