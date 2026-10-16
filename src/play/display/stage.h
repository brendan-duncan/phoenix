#pragma once

#include "display_object.h"

#include <functional>
#include <memory>
#include <string>

namespace fla {
namespace display {

class TextField;

/// The keys held with a pointer or key event.
struct Modifiers
{
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

/// flash.display.Stage: the top of the display list (the main timeline is
/// its first child), and where the pointer and the keys come in: it finds
/// what the pointer is over and sends the mouse events Flash Player sends,
/// sends keys to the focus, drags what startDrag drags, and broadcasts the
/// frame events.
class Stage : public DisplayObjectContainer
{
public:
    Stage(double width = 550.0, double height = 400.0, double frameRate = 24.0);
    ~Stage() override;

    double stageWidth() const { return _width; }
    double stageHeight() const { return _height; }
    /// The stage was resized: resize is dispatched.
    void setSize(double width, double height);
    double frameRate() const { return _frameRate; }
    void setFrameRate(double rate);
    uint32_t color = 0xffffff;
    std::string scaleMode = "showAll";
    std::string align;
    std::string quality = "high";
    std::string displayState = "normal";
    bool showDefaultContextMenu = true;
    bool stageFocusRect = true;

    /// The main timeline, or null.
    DisplayObject* rootObject() const { return _root; }
    void setRootObject(DisplayObject* root) { _root = root; }

    /// stage.focus: the object keys go to; null for the stage. Setting it
    /// sends focusOut and focusIn.
    InteractiveObject* focus() const { return _focus.lock().get(); }
    void setFocus(InteractiveObject* object);
    /// stage.invalidate: render is broadcast before the next frame shows.
    void invalidate() { _renderWanted = true; }

    // The frame's events, in the order Flash Player sends them: enterFrame
    // as the frame starts, frameConstructed once new children are made,
    // exitFrame after the frame scripts, and render (when invalidate was
    // called) just before it shows.
    void enterFrame();
    void frameConstructed();
    void exitFrame();
    void render();

    // The pointer, in stage coordinates; \a time in seconds, for double
    // clicks.
    void pointerMove(double x, double y, const Modifiers& modifiers = {});
    void pointerDown(double x, double y, const Modifiers& modifiers = {}, double time = 0.0);
    void pointerUp(double x, double y, const Modifiers& modifiers = {});
    void wheel(double x, double y, int delta, const Modifiers& modifiers = {});
    /// The pointer left the stage: mouseLeave, and out of what it was over.
    void pointerLeave();
    double pointerX() const { return _mouseX; }
    double pointerY() const { return _mouseY; }
    bool buttonDown() const { return _buttonDown; }
    /// A key went down or up: keyDown or keyUp to the focus (bubbling to
    /// the stage), and into the focused input field.
    void keyDown(uint32_t keyCode, uint32_t charCode, const Modifiers& modifiers = {});
    void keyUp(uint32_t keyCode, uint32_t charCode, const Modifiers& modifiers = {});

    /// The interactive object the pointer would find at (\a x, \a y): the
    /// deepest under it that takes the mouse, or its nearest container
    /// that does; null over nothing.
    InteractiveObject* hitTarget(double x, double y) const;
    /// What the pointer is over now.
    InteractiveObject* hovered() const { return _over.get(); }
    /// Whether the pointer shows as a hand: over a button, or a sprite
    /// with buttonMode and useHandCursor.
    bool handCursor() const;

    /// startDrag: \a object follows the pointer, at its center with
    /// \a lockCenter, within \a bounds (in its parent's space) when given.
    void startDrag(DisplayObject& object, bool lockCenter = false, const Rect* bounds = nullptr);
    void stopDrag();
    DisplayObject* dragging() const { return _drag.object.lock().get(); }
    /// What the dragged object is over (dropTarget), or null.
    DisplayObject* dropTarget() const;

    /// Seconds between two clicks that make a double click.
    double doubleClickTime = 0.5;

    /// Something shown changed: called (from invalidate) so the window
    /// can draw again.
    std::function<void()> changed;

private:
    /// Sends a mouse event of \a type to \a target at the pointer.
    void mouseEvent(InteractiveObject* target, const char* type, bool bubbles, InteractiveObject* related = nullptr,
        int delta = 0);
    /// The pointer is over \a target now: out, roll out, roll over and over
    /// sent as it moves from what it was over.
    void moveOver(InteractiveObject* target);
    void updateButtons();
    void dragNow();

    double _width;
    double _height;
    double _frameRate;
    DisplayObject* _root = nullptr;
    std::weak_ptr<InteractiveObject> _focus;
    bool _renderWanted = false;
    double _mouseX = 0.0;
    double _mouseY = 0.0;
    bool _buttonDown = false;
    Modifiers _modifiers;
    std::shared_ptr<InteractiveObject> _over;
    std::shared_ptr<InteractiveObject> _pressed;
    std::shared_ptr<InteractiveObject> _lastClick;
    double _lastClickTime = -1.0;
    double _pressTime = 0.0;
    struct Drag
    {
        std::weak_ptr<DisplayObject> object;
        bool lock = false;
        double dx = 0.0, dy = 0.0;
        bool bounded = false;
        Rect bounds;
    };
    Drag _drag;
};

} // namespace display
} // namespace fla
