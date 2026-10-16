#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fla {
namespace display {

class EventDispatcher;

/// The event types the display engine dispatches itself (Event.ADDED and
/// the rest), by their ActionScript string values.
namespace events {
constexpr const char* kAdded = "added";
constexpr const char* kAddedToStage = "addedToStage";
constexpr const char* kRemoved = "removed";
constexpr const char* kRemovedFromStage = "removedFromStage";
constexpr const char* kEnterFrame = "enterFrame";
constexpr const char* kExitFrame = "exitFrame";
constexpr const char* kFrameConstructed = "frameConstructed";
constexpr const char* kRender = "render";
constexpr const char* kActivate = "activate";
constexpr const char* kDeactivate = "deactivate";
constexpr const char* kChange = "change";
constexpr const char* kScroll = "scroll";
constexpr const char* kResize = "resize";
constexpr const char* kClick = "click";
constexpr const char* kDoubleClick = "doubleClick";
constexpr const char* kMouseDown = "mouseDown";
constexpr const char* kMouseUp = "mouseUp";
constexpr const char* kMouseMove = "mouseMove";
constexpr const char* kMouseOver = "mouseOver";
constexpr const char* kMouseOut = "mouseOut";
constexpr const char* kRollOver = "rollOver";
constexpr const char* kRollOut = "rollOut";
constexpr const char* kMouseWheel = "mouseWheel";
constexpr const char* kMouseLeave = "mouseLeave";
constexpr const char* kKeyDown = "keyDown";
constexpr const char* kKeyUp = "keyUp";
constexpr const char* kFocusIn = "focusIn";
constexpr const char* kFocusOut = "focusOut";
constexpr const char* kTextInput = "textInput";
/// Whether \a type goes to every dispatcher listening for it, on the
/// display list or not (enterFrame, exitFrame, frameConstructed, render,
/// activate, deactivate), rather than down a path to a target.
bool broadcast(const std::string& type);
} // namespace events

/// flash.events.Event: what is dispatched, and where it is on its way.
class Event
{
public:
    enum class Phase
    {
        None = 0,
        Capturing = 1,
        AtTarget = 2,
        Bubbling = 3
    };
    /// What kind of event it is, for the bindings to wrap the right class.
    enum class Class
    {
        Event,
        Mouse,
        Keyboard,
        Focus,
        Text
    };

    Event(std::string type, bool bubbles = false, bool cancelable = false)
        : type(std::move(type)), bubbles(bubbles), cancelable(cancelable)
    {}
    virtual ~Event() = default;
    virtual Class eventClass() const { return Class::Event; }
    /// A copy as Event.clone makes it: what it carries, not where it went.
    virtual std::unique_ptr<Event> clone() const;

    std::string type;
    bool bubbles = false;
    bool cancelable = false;
    /// Set as it is dispatched; null before. Held while it is on its way.
    EventDispatcher* target = nullptr;
    EventDispatcher* currentTarget = nullptr;
    Phase eventPhase = Phase::None;

    void stopPropagation() { propagationStopped = true; }
    void stopImmediatePropagation()
    {
        propagationStopped = true;
        immediateStopped = true;
    }
    /// Only a cancelable event can be.
    void preventDefault()
    {
        if (cancelable)
            defaultPrevented = true;
    }
    bool isDefaultPrevented() const { return defaultPrevented; }

    bool propagationStopped = false;
    bool immediateStopped = false;
    bool defaultPrevented = false;

    /// Whatever the script bindings keep for it: the script object wrapping
    /// it, so every listener sees the same one. Opaque here.
    std::shared_ptr<void> script;
};

/// flash.events.MouseEvent.
class MouseEvent : public Event
{
public:
    using Event::Event;
    Class eventClass() const override { return Class::Mouse; }
    std::unique_ptr<Event> clone() const override;

    /// Where the pointer is on the stage, and in the target's own space.
    double stageX = 0.0;
    double stageY = 0.0;
    double localX = 0.0;
    double localY = 0.0;
    bool buttonDown = false;
    bool shiftKey = false;
    bool ctrlKey = false;
    bool altKey = false;
    /// MOUSE_WHEEL: lines to scroll, positive away from the user.
    int delta = 0;
    /// MOUSE_OVER and the rest: the object the pointer came from or went to.
    EventDispatcher* relatedObject = nullptr;
};

/// flash.events.KeyboardEvent.
class KeyboardEvent : public Event
{
public:
    using Event::Event;
    Class eventClass() const override { return Class::Keyboard; }
    std::unique_ptr<Event> clone() const override;

    /// The character typed (0 for none) and the key's code (Keyboard.LEFT
    /// 37, "A" 65...).
    uint32_t charCode = 0;
    uint32_t keyCode = 0;
    uint32_t keyLocation = 0;
    bool shiftKey = false;
    bool ctrlKey = false;
    bool altKey = false;
};

/// flash.events.FocusEvent.
class FocusEvent : public Event
{
public:
    using Event::Event;
    Class eventClass() const override { return Class::Focus; }
    std::unique_ptr<Event> clone() const override;

    EventDispatcher* relatedObject = nullptr;
    bool shiftKey = false;
    uint32_t keyCode = 0;
};

/// flash.events.TextEvent.
class TextEvent : public Event
{
public:
    using Event::Event;
    Class eventClass() const override { return Class::Text; }
    std::unique_ptr<Event> clone() const override;

    std::string text;
};

/// flash.events.EventDispatcher: listeners by type, called by priority (the
/// higher first, then in the order added), in the capture phase or the
/// target and bubble phases. Display objects pass events down from and up
/// to the stage through their parents (eventParent).
class EventDispatcher : public std::enable_shared_from_this<EventDispatcher>
{
public:
    using Callback = std::function<void(Event&)>;

    EventDispatcher() = default;
    virtual ~EventDispatcher();
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    /// Adds \a callback for \a type; its id, for taking it off again.
    /// \a key is what tells listeners apart, as a function does in
    /// ActionScript: one added again with the same key, type and phase is
    /// not added twice (0 is returned). \a handle is kept with it for as
    /// long as it is on (a script function, say). A null \a key never
    /// matches another.
    uint64_t addEventListener(const std::string& type, Callback callback, bool useCapture = false, int priority = 0,
        const void* key = nullptr, std::shared_ptr<void> handle = {});
    /// Takes off the listener \a key added for \a type in that phase.
    void removeEventListener(const std::string& type, const void* key, bool useCapture = false);
    /// Takes off the listener with this id.
    void removeEventListener(uint64_t id);
    /// Takes off every listener.
    void removeAllEventListeners();
    bool hasEventListener(const std::string& type) const;
    /// Whether this or anything it passes events up to listens for \a type.
    bool willTrigger(const std::string& type) const;
    /// Sends \a event: down from the stage to this (capture), to this, and
    /// back up when it bubbles. An event already sent goes as a clone.
    /// False when a listener prevented its default.
    bool dispatchEvent(Event& event);

    /// What events go up to from here: a display object's parent.
    virtual EventDispatcher* eventParent() const { return nullptr; }

    /// Whatever the script bindings keep for this object: the script object
    /// wrapping it. Opaque here, and held weakly so the two do not keep each
    /// other alive.
    std::weak_ptr<void> script;

    /// Sends \a event of a broadcast type (enterFrame...) to every
    /// dispatcher listening for it, in the order they started listening.
    static void broadcast(Event& event);

protected:
    /// A listener for \a type was added or the last one taken off.
    virtual void listenersChanged(const std::string& /*type*/) {}

private:
    struct Listener
    {
        Callback callback;
        bool useCapture = false;
        int priority = 0;
        const void* key = nullptr;
        std::shared_ptr<void> handle;
        uint64_t id = 0;
    };
    /// Calls this one's listeners for \a event in \a capture's phase.
    void deliver(Event& event, bool capture);

    std::map<std::string, std::vector<std::shared_ptr<Listener>>> _listeners;
};

} // namespace display
} // namespace fla
