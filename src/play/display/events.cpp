#include "events.h"

#include <algorithm>

namespace fla {
namespace display {

namespace events {
bool broadcast(const std::string& type)
{
    return type == kEnterFrame || type == kExitFrame || type == kFrameConstructed || type == kRender ||
        type == kActivate || type == kDeactivate;
}
} // namespace events

namespace {

uint64_t nextListenerId = 0;

/// The dispatchers listening for each broadcast type, in the order they
/// started. A dispatcher takes itself off when it stops listening or goes.
std::map<std::string, std::vector<EventDispatcher*>>& broadcasters()
{
    static std::map<std::string, std::vector<EventDispatcher*>> listening;
    return listening;
}

template <typename T>
std::unique_ptr<Event> copyOf(const T& event)
{
    auto copy = std::make_unique<T>(event);
    copy->target = nullptr;
    copy->currentTarget = nullptr;
    copy->eventPhase = Event::Phase::None;
    copy->propagationStopped = false;
    copy->immediateStopped = false;
    copy->defaultPrevented = false;
    copy->script.reset();
    return copy;
}

} // namespace

std::unique_ptr<Event> Event::clone() const
{
    return copyOf(*this);
}

std::unique_ptr<Event> MouseEvent::clone() const
{
    return copyOf(*this);
}

std::unique_ptr<Event> KeyboardEvent::clone() const
{
    return copyOf(*this);
}

std::unique_ptr<Event> FocusEvent::clone() const
{
    return copyOf(*this);
}

std::unique_ptr<Event> TextEvent::clone() const
{
    return copyOf(*this);
}

EventDispatcher::~EventDispatcher()
{
    for (auto& [type, list] : broadcasters())
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
}

uint64_t EventDispatcher::addEventListener(const std::string& type, Callback callback, bool useCapture, int priority,
    const void* key, std::shared_ptr<void> handle)
{
    if (!callback)
        return 0;
    std::vector<std::shared_ptr<Listener>>& list = _listeners[type];
    if (key)
        for (const auto& listener : list)
            if (listener->key == key && listener->useCapture == useCapture)
                return 0;
    auto listener = std::make_shared<Listener>();
    listener->callback = std::move(callback);
    listener->useCapture = useCapture;
    listener->priority = priority;
    listener->key = key;
    listener->handle = std::move(handle);
    listener->id = ++nextListenerId;
    // After those of the same or a higher priority.
    auto at = list.begin();
    while (at != list.end() && (*at)->priority >= priority)
        ++at;
    list.insert(at, listener);
    if (events::broadcast(type))
    {
        std::vector<EventDispatcher*>& listening = broadcasters()[type];
        if (std::find(listening.begin(), listening.end(), this) == listening.end())
            listening.push_back(this);
    }
    if (list.size() == 1)
        listenersChanged(type);
    return listener->id;
}

void EventDispatcher::removeEventListener(const std::string& type, const void* key, bool useCapture)
{
    const auto found = _listeners.find(type);
    if (found == _listeners.end() || !key)
        return;
    std::vector<std::shared_ptr<Listener>>& list = found->second;
    list.erase(std::remove_if(list.begin(), list.end(),
                   [&](const std::shared_ptr<Listener>& l) { return l->key == key && l->useCapture == useCapture; }),
        list.end());
    if (list.empty())
    {
        _listeners.erase(found);
        if (events::broadcast(type))
        {
            std::vector<EventDispatcher*>& listening = broadcasters()[type];
            listening.erase(std::remove(listening.begin(), listening.end(), this), listening.end());
        }
        listenersChanged(type);
    }
}

void EventDispatcher::removeEventListener(uint64_t id)
{
    for (auto& [type, list] : _listeners)
    {
        const auto found =
            std::find_if(list.begin(), list.end(), [id](const std::shared_ptr<Listener>& l) { return l->id == id; });
        if (found == list.end())
            continue;
        const std::string which = type;
        list.erase(found);
        if (list.empty())
        {
            _listeners.erase(which);
            if (events::broadcast(which))
            {
                std::vector<EventDispatcher*>& listening = broadcasters()[which];
                listening.erase(std::remove(listening.begin(), listening.end(), this), listening.end());
            }
            listenersChanged(which);
        }
        return;
    }
}

void EventDispatcher::removeAllEventListeners()
{
    std::vector<std::string> types;
    for (const auto& entry : _listeners)
        types.push_back(entry.first);
    _listeners.clear();
    for (auto& [type, list] : broadcasters())
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    for (const std::string& type : types)
        listenersChanged(type);
}

bool EventDispatcher::hasEventListener(const std::string& type) const
{
    const auto found = _listeners.find(type);
    return found != _listeners.end() && !found->second.empty();
}

bool EventDispatcher::willTrigger(const std::string& type) const
{
    for (const EventDispatcher* at = this; at; at = at->eventParent())
        if (at->hasEventListener(type))
            return true;
    return false;
}

void EventDispatcher::deliver(Event& event, bool capture)
{
    const auto found = _listeners.find(event.type);
    if (found == _listeners.end())
        return;
    // Those there are as it arrives: one added on the way waits for the
    // next event, and one taken off on the way is still called.
    const std::vector<std::shared_ptr<Listener>> listeners = found->second;
    event.currentTarget = this;
    for (const std::shared_ptr<Listener>& listener : listeners)
    {
        if (listener->useCapture != capture)
            continue;
        listener->callback(event);
        if (event.immediateStopped)
            break;
    }
}

bool EventDispatcher::dispatchEvent(Event& event)
{
    // One already sent goes again as a copy, as Flash Player does it.
    if (event.target)
    {
        std::unique_ptr<Event> copy = event.clone();
        return dispatchEvent(*copy);
    }
    // Kept alive while it is on its way: a listener may take any of them
    // off the display list and let them go.
    std::vector<std::shared_ptr<EventDispatcher>> held;
    std::vector<EventDispatcher*> path;
    for (EventDispatcher* at = eventParent(); at; at = at->eventParent())
    {
        path.push_back(at);
        if (std::shared_ptr<EventDispatcher> keep = at->weak_from_this().lock())
            held.push_back(std::move(keep));
    }
    if (std::shared_ptr<EventDispatcher> keep = weak_from_this().lock())
        held.push_back(std::move(keep));

    event.target = this;
    event.eventPhase = Event::Phase::Capturing;
    for (auto at = path.rbegin(); at != path.rend() && !event.propagationStopped; ++at)
        if ((*at)->hasEventListener(event.type))
            (*at)->deliver(event, true);
    if (!event.propagationStopped)
    {
        event.eventPhase = Event::Phase::AtTarget;
        deliver(event, false);
    }
    if (event.bubbles)
    {
        event.eventPhase = Event::Phase::Bubbling;
        for (auto at = path.begin(); at != path.end() && !event.propagationStopped; ++at)
            if ((*at)->hasEventListener(event.type))
                (*at)->deliver(event, false);
    }
    event.currentTarget = nullptr;
    event.eventPhase = Event::Phase::None;
    return !event.defaultPrevented;
}

void EventDispatcher::broadcast(Event& event)
{
    const auto found = broadcasters().find(event.type);
    if (found == broadcasters().end())
        return;
    // Those listening as it starts, each still listening when its turn
    // comes.
    const std::vector<EventDispatcher*> listening = found->second;
    for (EventDispatcher* dispatcher : listening)
    {
        const std::vector<EventDispatcher*>& now = broadcasters()[event.type];
        if (std::find(now.begin(), now.end(), dispatcher) == now.end())
            continue;
        std::shared_ptr<EventDispatcher> keep = dispatcher->weak_from_this().lock();
        event.target = dispatcher;
        event.eventPhase = Event::Phase::AtTarget;
        event.propagationStopped = false;
        event.immediateStopped = false;
        dispatcher->deliver(event, false);
    }
    event.target = nullptr;
    event.currentTarget = nullptr;
    event.eventPhase = Event::Phase::None;
}

} // namespace display
} // namespace fla
