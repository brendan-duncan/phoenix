#include "stage.h"

#include "text_field.h"

#include <algorithm>
#include <cmath>

namespace fla {
namespace display {

namespace {

/// Whether \a outer is \a inner or holds it.
bool holds(const DisplayObject* outer, const DisplayObject* inner)
{
    if (!outer || !inner)
        return false;
    if (outer == inner)
        return true;
    return outer->isContainer() && static_cast<const DisplayObjectContainer*>(outer)->contains(inner);
}

/// Whether a mask lets (x, y) in \a object's space show.
bool maskShows(const DisplayObject& object, double x, double y)
{
    const DisplayObject* mask = object.mask();
    if (!mask)
        return true;
    const Point onStage = object.concatenatedMatrix().map(x, y);
    bool invertible = false;
    const Point inMask = mask->concatenatedMatrix().inverted(&invertible).map(onStage);
    return invertible && mask->hitsLocal(inMask.x, inMask.y);
}

/// Finds what the pointer would find at (x, y) in \a object's own space:
/// true when it is over something there, \a target the interactive object
/// that takes it (null for a plain shape, whose container decides).
bool pick(const DisplayObject& object, double x, double y, InteractiveObject*& target)
{
    target = nullptr;
    if (!object.visible() || object.maskedObject())
        return false;
    if (object.scrollRect() && !object.scrollRect()->contains(x, y))
        return false;
    if (!maskShows(object, x, y))
        return false;
    auto* self = const_cast<DisplayObject*>(&object);
    const bool interactive = object.isInteractive();
    const bool enabled = interactive && static_cast<const InteractiveObject&>(object).mouseEnabled;
    // A sprite's hit area stands for it, children and all.
    if (object.kind() == ObjectKind::Sprite || object.kind() == ObjectKind::MovieClip)
    {
        if (std::shared_ptr<Sprite> area = static_cast<const Sprite&>(object).hitArea())
        {
            const Point onStage = object.concatenatedMatrix().map(x, y);
            bool invertible = false;
            const Point inArea = area->concatenatedMatrix().inverted(&invertible).map(onStage);
            if (!invertible || !area->hitsLocal(inArea.x, inArea.y) || !enabled)
                return false;
            target = static_cast<InteractiveObject*>(self);
            return true;
        }
    }
    if (object.kind() == ObjectKind::SimpleButton)
    {
        if (!enabled || !object.contentHits(x, y))
            return false;
        target = static_cast<InteractiveObject*>(self);
        return true;
    }
    if (!object.isContainer())
    {
        if (!object.contentHits(x, y))
            return false;
        if (interactive)
        {
            if (!enabled)
                return false;
            target = static_cast<InteractiveObject*>(self);
        }
        return true;
    }
    const auto& container = static_cast<const DisplayObjectContainer&>(object);
    // Its children are not looked into: it takes the pointer whole.
    if (!container.mouseChildren)
    {
        if (!enabled || !object.hitsLocal(x, y))
            return false;
        target = static_cast<InteractiveObject*>(self);
        return true;
    }
    const auto& children = container.children();
    for (size_t i = children.size(); i-- > 0;)
    {
        const DisplayObject* child = children[i].get();
        if (!child)
            continue;
        bool invertible = false;
        const Point inChild = child->localToParent().inverted(&invertible).map(x, y);
        if (!invertible)
            continue;
        InteractiveObject* inner = nullptr;
        if (!pick(*child, inChild.x, inChild.y, inner))
            continue;
        if (inner)
        {
            target = inner;
            return true;
        }
        // A plain shape in it: it takes the pointer, unless it does not
        // take the mouse, when the pointer goes on to what is under.
        if (enabled)
        {
            target = static_cast<InteractiveObject*>(self);
            return true;
        }
    }
    if (enabled && object.contentHits(x, y))
    {
        target = static_cast<InteractiveObject*>(self);
        return true;
    }
    return false;
}

} // namespace

Stage::Stage(double width, double height, double frameRate)
    : DisplayObjectContainer(ObjectKind::Stage)
    , _width(width)
    , _height(height)
    , _frameRate(frameRate)
{}

Stage::~Stage() = default;

void Stage::setSize(double width, double height)
{
    if (width == _width && height == _height)
        return;
    _width = width;
    _height = height;
    Event resize(events::kResize);
    dispatchEvent(resize);
}

void Stage::setFrameRate(double rate)
{
    _frameRate = std::max(0.01, std::min(1000.0, rate));
}

void Stage::setFocus(InteractiveObject* object)
{
    InteractiveObject* old = focus();
    if (old == object)
        return;
    std::shared_ptr<DisplayObject> keepOld = old ? old->shared() : nullptr;
    std::shared_ptr<DisplayObject> keepNew = object ? object->shared() : nullptr;
    if (old)
    {
        FocusEvent out(events::kFocusOut, true, true);
        out.relatedObject = object;
        old->dispatchEvent(out);
    }
    _focus = keepNew ? std::static_pointer_cast<InteractiveObject>(keepNew) : std::weak_ptr<InteractiveObject>();
    if (object)
    {
        FocusEvent in(events::kFocusIn, true);
        in.relatedObject = old;
        object->dispatchEvent(in);
    }
    if (old)
        old->invalidate();
    if (object)
        object->invalidate();
}

void Stage::enterFrame()
{
    Event event(events::kEnterFrame);
    EventDispatcher::broadcast(event);
}

void Stage::frameConstructed()
{
    Event event(events::kFrameConstructed);
    EventDispatcher::broadcast(event);
}

void Stage::exitFrame()
{
    Event event(events::kExitFrame);
    EventDispatcher::broadcast(event);
}

void Stage::render()
{
    if (!_renderWanted)
        return;
    _renderWanted = false;
    Event event(events::kRender);
    EventDispatcher::broadcast(event);
}

InteractiveObject* Stage::hitTarget(double x, double y) const
{
    const auto& all = children();
    for (size_t i = all.size(); i-- > 0;)
    {
        const DisplayObject* child = all[i].get();
        if (!child)
            continue;
        bool invertible = false;
        const Point inChild = child->localToParent().inverted(&invertible).map(x, y);
        if (!invertible)
            continue;
        InteractiveObject* target = nullptr;
        if (pick(*child, inChild.x, inChild.y, target) && target)
            return target;
    }
    return nullptr;
}

void Stage::mouseEvent(InteractiveObject* target, const char* type, bool bubbles, InteractiveObject* related, int delta)
{
    EventDispatcher* to = target ? static_cast<EventDispatcher*>(target) : this;
    MouseEvent event(type, bubbles);
    event.stageX = _mouseX;
    event.stageY = _mouseY;
    const Point local = target ? target->globalToLocal({_mouseX, _mouseY}) : Point{_mouseX, _mouseY};
    event.localX = local.x;
    event.localY = local.y;
    event.buttonDown = _buttonDown;
    event.shiftKey = _modifiers.shift;
    event.ctrlKey = _modifiers.ctrl;
    event.altKey = _modifiers.alt;
    event.delta = delta;
    event.relatedObject = related;
    to->dispatchEvent(event);
}

void Stage::moveOver(InteractiveObject* target)
{
    std::shared_ptr<InteractiveObject> next =
        target ? std::static_pointer_cast<InteractiveObject>(target->shared()) : nullptr;
    if (next == _over)
        return;
    std::shared_ptr<InteractiveObject> old = _over;
    _over = next;
    if (old)
    {
        mouseEvent(old.get(), events::kMouseOut, true, next.get());
        // Rolled out of it and its parents that do not hold what it is over
        // now, from the inside out.
        for (DisplayObject* at = old.get(); at && at != this && !holds(at, next.get()); at = at->parent())
        {
            std::shared_ptr<DisplayObject> keep = at->shared();
            MouseEvent out(events::kRollOut);
            out.stageX = _mouseX;
            out.stageY = _mouseY;
            const Point local = at->globalToLocal({_mouseX, _mouseY});
            out.localX = local.x;
            out.localY = local.y;
            out.buttonDown = _buttonDown;
            out.relatedObject = next.get();
            at->dispatchEvent(out);
        }
    }
    if (next)
    {
        std::vector<std::shared_ptr<DisplayObject>> over;
        for (DisplayObject* at = next.get(); at && at != this && !holds(at, old.get()); at = at->parent())
            over.push_back(at->shared());
        for (auto at = over.rbegin(); at != over.rend(); ++at)
        {
            if (!*at)
                continue;
            MouseEvent in(events::kRollOver);
            in.stageX = _mouseX;
            in.stageY = _mouseY;
            const Point local = (*at)->globalToLocal({_mouseX, _mouseY});
            in.localX = local.x;
            in.localY = local.y;
            in.buttonDown = _buttonDown;
            in.relatedObject = old.get();
            (*at)->dispatchEvent(in);
        }
        mouseEvent(next.get(), events::kMouseOver, true, old.get());
    }
}

void Stage::updateButtons()
{
    // Every button on the stage up, but the one the pointer is on.
    std::vector<SimpleButton*> buttons;
    std::vector<const DisplayObject*> stack{this};
    while (!stack.empty())
    {
        const DisplayObject* at = stack.back();
        stack.pop_back();
        if (at->kind() == ObjectKind::SimpleButton)
            buttons.push_back(const_cast<SimpleButton*>(static_cast<const SimpleButton*>(at)));
        if (at->isContainer())
            for (const auto& child : static_cast<const DisplayObjectContainer*>(at)->children())
                if (child)
                    stack.push_back(child.get());
    }
    for (SimpleButton* button : buttons)
    {
        SimpleButton::State state = SimpleButton::State::Up;
        if (button->enabled && _over.get() == button)
            state = _buttonDown ? (_pressed.get() == button ? SimpleButton::State::Down : SimpleButton::State::Up)
                                : SimpleButton::State::Over;
        else if (button->enabled && button->trackAsMenu && _buttonDown && _over.get() == button)
            state = SimpleButton::State::Down;
        button->setState(state);
    }
}

void Stage::dragNow()
{
    std::shared_ptr<DisplayObject> object = _drag.object.lock();
    if (!object)
        return;
    Point at{_mouseX, _mouseY};
    if (object->parent())
        at = object->parent()->globalToLocal(at);
    double x = at.x + _drag.dx;
    double y = at.y + _drag.dy;
    if (_drag.bounded)
    {
        x = std::max(_drag.bounds.left(), std::min(_drag.bounds.right(), x));
        y = std::max(_drag.bounds.top(), std::min(_drag.bounds.bottom(), y));
    }
    if (x != object->x())
        object->setX(x);
    if (y != object->y())
        object->setY(y);
}

void Stage::startDrag(DisplayObject& object, bool lockCenter, const Rect* bounds)
{
    _drag = Drag();
    _drag.object = object.shared();
    _drag.lock = lockCenter;
    if (!lockCenter)
    {
        Point at{_mouseX, _mouseY};
        if (object.parent())
            at = object.parent()->globalToLocal(at);
        _drag.dx = object.x() - at.x;
        _drag.dy = object.y() - at.y;
    }
    if (bounds)
    {
        _drag.bounded = true;
        // A rectangle given with a negative size runs the other way.
        Rect b = *bounds;
        if (b.width < 0)
        {
            b.x += b.width;
            b.width = -b.width;
        }
        if (b.height < 0)
        {
            b.y += b.height;
            b.height = -b.height;
        }
        _drag.bounds = b;
    }
    dragNow();
}

void Stage::stopDrag()
{
    _drag = Drag();
}

DisplayObject* Stage::dropTarget() const
{
    std::shared_ptr<DisplayObject> dragged = _drag.object.lock();
    const std::vector<std::shared_ptr<DisplayObject>> under = getObjectsUnderPoint({_mouseX, _mouseY});
    for (auto at = under.rbegin(); at != under.rend(); ++at)
        if (*at && !holds(dragged.get(), at->get()))
            return at->get();
    return nullptr;
}

bool Stage::handCursor() const
{
    for (const DisplayObject* at = _over.get(); at && at != this; at = at->parent())
    {
        if (at->kind() == ObjectKind::SimpleButton)
        {
            const auto* button = static_cast<const SimpleButton*>(at);
            return button->useHandCursor && button->enabled;
        }
        if (at->kind() == ObjectKind::TextField && static_cast<const TextField*>(at)->type() == TextField::Type::Input)
            return false;
        if (at->kind() == ObjectKind::Sprite || at->kind() == ObjectKind::MovieClip)
        {
            const auto* sprite = static_cast<const Sprite*>(at);
            if (sprite->buttonMode)
                return sprite->useHandCursor;
        }
    }
    return false;
}

void Stage::pointerMove(double x, double y, const Modifiers& modifiers)
{
    std::shared_ptr<DisplayObject> keep = shared();
    _mouseX = x;
    _mouseY = y;
    _modifiers = modifiers;
    dragNow();
    InteractiveObject* target = hitTarget(x, y);
    moveOver(target);
    mouseEvent(_over.get(), events::kMouseMove, true);
    // Dragging across a field selects.
    if (_buttonDown && _pressed && _pressed->kind() == ObjectKind::TextField && !_pressed->timeline)
    {
        auto* field = static_cast<TextField*>(_pressed.get());
        const Point local = field->globalToLocal({x, y});
        field->dragTo(local.x, local.y);
    }
    updateButtons();
}

void Stage::pointerDown(double x, double y, const Modifiers& modifiers, double time)
{
    std::shared_ptr<DisplayObject> keep = shared();
    _mouseX = x;
    _mouseY = y;
    _modifiers = modifiers;
    _buttonDown = true;
    InteractiveObject* target = hitTarget(x, y);
    moveOver(target);
    _pressed = _over;
    _pressTime = time;
    // A click on a field gives it the focus; one elsewhere takes it from a
    // field.
    if (_pressed && _pressed->kind() == ObjectKind::TextField)
    {
        auto* field = static_cast<TextField*>(_pressed.get());
        if (field->type() == TextField::Type::Input || field->selectable)
            setFocus(field);
    }
    else if (focus() && focus()->kind() == ObjectKind::TextField)
        setFocus(nullptr);
    mouseEvent(_pressed.get(), events::kMouseDown, true);
    if (_pressed && _pressed->kind() == ObjectKind::TextField && !_pressed->timeline)
    {
        auto* field = static_cast<TextField*>(_pressed.get());
        const Point local = field->globalToLocal({x, y});
        field->pressAt(local.x, local.y, modifiers.shift);
    }
    updateButtons();
}

void Stage::pointerUp(double x, double y, const Modifiers& modifiers)
{
    std::shared_ptr<DisplayObject> keep = shared();
    _mouseX = x;
    _mouseY = y;
    _modifiers = modifiers;
    _buttonDown = false;
    InteractiveObject* target = hitTarget(x, y);
    moveOver(target);
    std::shared_ptr<InteractiveObject> pressed = _pressed;
    _pressed.reset();
    mouseEvent(_over.get(), events::kMouseUp, true);
    // A click: down and up on the same object. The second of two quick ones
    // on an object that wants them is a double click instead.
    if (pressed == _over)
    {
        InteractiveObject* on = _over.get();
        if (on && on->doubleClickEnabled && _lastClick == _over && _pressTime - _lastClickTime <= doubleClickTime)
        {
            mouseEvent(on, events::kDoubleClick, true);
            _lastClick.reset();
        }
        else
        {
            mouseEvent(on, events::kClick, true);
            _lastClick = _over;
            _lastClickTime = _pressTime;
        }
    }
    updateButtons();
}

void Stage::wheel(double x, double y, int delta, const Modifiers& modifiers)
{
    std::shared_ptr<DisplayObject> keep = shared();
    _mouseX = x;
    _mouseY = y;
    _modifiers = modifiers;
    InteractiveObject* target = hitTarget(x, y);
    moveOver(target);
    mouseEvent(_over.get(), events::kMouseWheel, true, nullptr, delta);
    if (_over && _over->kind() == ObjectKind::TextField)
    {
        auto* field = static_cast<TextField*>(_over.get());
        if (field->mouseWheelEnabled)
        {
            const int before = field->scrollV();
            field->setScrollV(before - delta);
            if (field->scrollV() != before)
            {
                Event scroll(events::kScroll, true);
                field->dispatchEvent(scroll);
            }
        }
    }
}

void Stage::pointerLeave()
{
    std::shared_ptr<DisplayObject> keep = shared();
    moveOver(nullptr);
    Event leave(events::kMouseLeave);
    dispatchEvent(leave);
    updateButtons();
}

void Stage::keyDown(uint32_t keyCode, uint32_t charCode, const Modifiers& modifiers)
{
    std::shared_ptr<DisplayObject> keep = shared();
    InteractiveObject* to = focus();
    std::shared_ptr<DisplayObject> held = to ? to->shared() : nullptr;
    KeyboardEvent event(events::kKeyDown, true);
    event.keyCode = keyCode;
    event.charCode = charCode;
    event.shiftKey = modifiers.shift;
    event.ctrlKey = modifiers.ctrl;
    event.altKey = modifiers.alt;
    (to ? static_cast<EventDispatcher*>(to) : this)->dispatchEvent(event);
    // Typing goes into the field with the focus, if it still has it (a
    // field a timeline placed is the movie runtime's to type into).
    if (to && focus() == to && to->kind() == ObjectKind::TextField && !to->timeline)
        static_cast<TextField*>(to)->keyTyped(keyCode, charCode, modifiers.shift, modifiers.ctrl);
}

void Stage::keyUp(uint32_t keyCode, uint32_t charCode, const Modifiers& modifiers)
{
    std::shared_ptr<DisplayObject> keep = shared();
    InteractiveObject* to = focus();
    std::shared_ptr<DisplayObject> held = to ? to->shared() : nullptr;
    KeyboardEvent event(events::kKeyUp, true);
    event.keyCode = keyCode;
    event.charCode = charCode;
    event.shiftKey = modifiers.shift;
    event.ctrlKey = modifiers.ctrl;
    event.altKey = modifiers.alt;
    (to ? static_cast<EventDispatcher*>(to) : this)->dispatchEvent(event);
}

} // namespace display
} // namespace fla
