// flash.events: EventDispatcher and the events.

#include "native.h"

namespace fla {
namespace play {
namespace script {

namespace {

EventHost& eventHost(VM& vm, const Val& self)
{
    EventHost& host = hostOf<EventHost>(vm, self, "Event");
    if (!host.live && !host.owned)
        vm.throwError("TypeError", "Error #1034: Type Coercion failed: Event expected.", 1034);
    return host;
}

display::Event& eventOf(VM& vm, const Val& self)
{
    return eventHost(vm, self).event();
}

/// The event as a mouse event, or a TypeError.
template <class E>
E& eventAs(VM& vm, const Val& self, const char* what)
{
    auto* event = dynamic_cast<E*>(&eventOf(vm, self));
    if (!event)
        vm.throwError("TypeError", std::string("Error #1034: Type Coercion failed: ") + what + " expected.", 1034);
    return *event;
}

/// Gives a new event object its engine event, of type \a E, unless the
/// engine made it (it has one).
template <class E>
E* attachEvent(VM& vm, const Val& self, const Args& args, bool bubblesByDefault)
{
    if (self.host<EventHost>())
        return nullptr;
    auto host = std::make_shared<EventHost>();
    auto event = std::make_shared<E>(stringArg(vm, args, 0), booleanArg(args, 1, bubblesByDefault), booleanArg(args, 2));
    host->owned = event;
    self->host = host;
    return event.get();
}

std::string quoted(VM& vm, const Val& value)
{
    if (value.isString())
        return "\"" + value.str() + "\"";
    return vm.toString(value);
}

std::string describe(VM& vm, const Val& self, const std::string& name, std::initializer_list<const char*> fields)
{
    std::string out = "[" + name;
    for (const char* field : fields)
        out += std::string(" ") + field + "=" + quoted(vm, vm.getProperty(self, field));
    return out + "]";
}

} // namespace

Val Bindings::makeEvent(const std::string& cls, const std::string& type, bool bubbles, bool cancelable)
{
    auto host = std::make_shared<EventHost>();
    if (cls == "flash.events.TextEvent")
        host->owned = std::make_shared<display::TextEvent>(type, bubbles, cancelable);
    else
        host->owned = std::make_shared<display::Event>(type, bubbles, cancelable);
    return instance(cls, host);
}

void Bindings::defineEvents()
{
    vm.defineNativeClass("flash.events.IEventDispatcher").interface().define();

    vm.defineNativeClass("flash.events.EventPhase")
        .constant("CAPTURING_PHASE", number(1))
        .constant("AT_TARGET", number(2))
        .constant("BUBBLING_PHASE", number(3))
        .define();

    // Event.
    NativeClassBuilder event = vm.defineNativeClass("flash.events.Event");
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, const char*>>{{"ACTIVATE", "activate"},
             {"ADDED", "added"}, {"ADDED_TO_STAGE", "addedToStage"}, {"CANCEL", "cancel"}, {"CHANGE", "change"},
             {"CLEAR", "clear"}, {"CLOSE", "close"}, {"COMPLETE", "complete"}, {"CONNECT", "connect"}, {"COPY", "copy"},
             {"CUT", "cut"}, {"DEACTIVATE", "deactivate"}, {"ENTER_FRAME", "enterFrame"}, {"EXIT_FRAME", "exitFrame"},
             {"FRAME_CONSTRUCTED", "frameConstructed"}, {"FRAME_LABEL", "frameLabel"}, {"FULLSCREEN", "fullScreen"},
             {"ID3", "id3"}, {"INIT", "init"}, {"MOUSE_LEAVE", "mouseLeave"}, {"OPEN", "open"}, {"PASTE", "paste"},
             {"REMOVED", "removed"}, {"REMOVED_FROM_STAGE", "removedFromStage"}, {"RENDER", "render"},
             {"RESIZE", "resize"}, {"SCROLL", "scroll"}, {"SELECT", "select"}, {"SELECT_ALL", "selectAll"},
             {"SOUND_COMPLETE", "soundComplete"}, {"TAB_CHILDREN_CHANGE", "tabChildrenChange"},
             {"TAB_ENABLED_CHANGE", "tabEnabledChange"}, {"TAB_INDEX_CHANGE", "tabIndexChange"}, {"UNLOAD", "unload"},
             {"TEXT_INTERACTION_MODE_CHANGE", "textInteractionModeChange"}, {"VIDEO_FRAME", "videoFrame"},
             {"SUSPEND", "suspend"}, {"CONTEXT3D_CREATE", "context3DCreate"}})
        event.constant(name, text(value));
    event
        .constructor(NATIVE() {
            attachEvent<display::Event>(vm, self, args, false);
            return Val();
        })
        .getter("type", NATIVE() { return text(eventOf(vm, self).type); })
        .getter("bubbles", NATIVE() { return boolean(eventOf(vm, self).bubbles); })
        .getter("cancelable", NATIVE() { return boolean(eventOf(vm, self).cancelable); })
        .getter("eventPhase", NATIVE() { return number(static_cast<int>(eventOf(vm, self).eventPhase)); })
        .getter("target", NATIVE() {
            const EventHost& host = eventHost(vm, self);
            return host.target.isUndefined() ? Val::null() : host.target;
        })
        .getter("currentTarget", NATIVE() {
            const EventHost& host = eventHost(vm, self);
            return host.currentTarget.isUndefined() ? Val::null() : host.currentTarget;
        })
        .method("clone", NATIVE(this) {
            const display::Event& e = eventOf(vm, self);
            return makeEvent("flash.events.Event", e.type, e.bubbles, e.cancelable);
        })
        .method("isDefaultPrevented", NATIVE() { return boolean(eventOf(vm, self).isDefaultPrevented()); })
        .method("preventDefault", NATIVE() {
            eventOf(vm, self).preventDefault();
            return Val();
        })
        .method("stopPropagation", NATIVE() {
            eventOf(vm, self).stopPropagation();
            return Val();
        })
        .method("stopImmediatePropagation", NATIVE() {
            eventOf(vm, self).stopImmediatePropagation();
            return Val();
        })
        .method("formatToString", NATIVE() {
            std::string out = "[" + stringArg(vm, args, 0);
            for (size_t i = 1; i < args.size(); ++i)
            {
                const std::string field = vm.toString(args[i]);
                out += " " + field + "=" + quoted(vm, vm.getProperty(self, field));
            }
            return text(out + "]");
        })
        .method("toString", NATIVE() {
            return text(describe(vm, self, "Event", {"type", "bubbles", "cancelable", "eventPhase"}));
        })
        .define();

    // EventDispatcher.
    vm.defineNativeClass("flash.events.EventDispatcher")
        .implements("flash.events.IEventDispatcher")
        .constructor(NATIVE() {
            if (!self.host<DispatcherHost>())
            {
                auto host = std::make_shared<DispatcherHost>();
                host->dispatcher = std::make_shared<display::EventDispatcher>();
                host->dispatcher->script = self.ref;
                self->host = host;
            }
            return Val();
        })
        .method("addEventListener", NATIVE(this) {
            display::EventDispatcher* dispatcher = dispatcherOf(self);
            if (!dispatcher)
                vm.throwError("TypeError", "Error #1034: Type Coercion failed: EventDispatcher expected.", 1034);
            const Val listener = args[1];
            if (!listener.isFunction())
                vm.throwError("TypeError", "Error #2007: Parameter listener must be non-null.", 2007);
            dispatcher->addEventListener(stringArg(vm, args, 0), listenerCallback(listener), booleanArg(args, 2),
                intArg(args, 3), listener.get(), std::make_shared<Val>(listener));
            return Val();
        }, 2)
        .method("removeEventListener", NATIVE(this) {
            display::EventDispatcher* dispatcher = dispatcherOf(self);
            if (dispatcher && args[1].isObject())
                dispatcher->removeEventListener(stringArg(vm, args, 0), args[1].get(), booleanArg(args, 2));
            return Val();
        }, 2)
        .method("hasEventListener", NATIVE(this) {
            display::EventDispatcher* dispatcher = dispatcherOf(self);
            return boolean(dispatcher && dispatcher->hasEventListener(stringArg(vm, args, 0)));
        }, 1)
        .method("willTrigger", NATIVE(this) {
            display::EventDispatcher* dispatcher = dispatcherOf(self);
            return boolean(dispatcher && dispatcher->willTrigger(stringArg(vm, args, 0)));
        }, 1)
        .method("dispatchEvent", NATIVE(this) {
            display::EventDispatcher* dispatcher = dispatcherOf(self);
            if (!dispatcher)
                vm.throwError("TypeError", "Error #1034: Type Coercion failed: EventDispatcher expected.", 1034);
            if (!args[0].host<EventHost>())
                vm.throwError("TypeError", "Error #2007: Parameter event must be non-null.", 2007);
            return boolean(dispatchScriptEvent(*dispatcher, args[0]));
        }, 1)
        .define();

    // MouseEvent.
    vm.defineNativeClass("flash.events.MouseEvent")
        .extends("flash.events.Event")
        .constant("CLICK", text("click"))
        .constant("CONTEXT_MENU", text("contextMenu"))
        .constant("DOUBLE_CLICK", text("doubleClick"))
        .constant("MIDDLE_CLICK", text("middleClick"))
        .constant("MIDDLE_MOUSE_DOWN", text("middleMouseDown"))
        .constant("MIDDLE_MOUSE_UP", text("middleMouseUp"))
        .constant("MOUSE_DOWN", text("mouseDown"))
        .constant("MOUSE_MOVE", text("mouseMove"))
        .constant("MOUSE_OUT", text("mouseOut"))
        .constant("MOUSE_OVER", text("mouseOver"))
        .constant("MOUSE_UP", text("mouseUp"))
        .constant("MOUSE_WHEEL", text("mouseWheel"))
        .constant("RELEASE_OUTSIDE", text("releaseOutside"))
        .constant("RIGHT_CLICK", text("rightClick"))
        .constant("RIGHT_MOUSE_DOWN", text("rightMouseDown"))
        .constant("RIGHT_MOUSE_UP", text("rightMouseUp"))
        .constant("ROLL_OUT", text("rollOut"))
        .constant("ROLL_OVER", text("rollOver"))
        .constructor(NATIVE(this) {
            if (display::MouseEvent* e = attachEvent<display::MouseEvent>(vm, self, args, true))
            {
                e->localX = numberArg(vm, args, 3, std::nan(""));
                e->localY = numberArg(vm, args, 4, std::nan(""));
                e->relatedObject = dispatcherOf(args[5]);
                e->ctrlKey = booleanArg(args, 6);
                e->altKey = booleanArg(args, 7);
                e->shiftKey = booleanArg(args, 8);
                e->buttonDown = booleanArg(args, 9);
                e->delta = intArg(args, 10);
            }
            return Val();
        })
        .property("localX", NATIVE() { return number(eventAs<display::MouseEvent>(vm, self, "MouseEvent").localX); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").localX = vm.toNumber(args[0]);
                return Val();
            })
        .property("localY", NATIVE() { return number(eventAs<display::MouseEvent>(vm, self, "MouseEvent").localY); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").localY = vm.toNumber(args[0]);
                return Val();
            })
        .getter("stageX", NATIVE() { return number(eventAs<display::MouseEvent>(vm, self, "MouseEvent").stageX); })
        .getter("stageY", NATIVE() { return number(eventAs<display::MouseEvent>(vm, self, "MouseEvent").stageY); })
        .property("buttonDown",
            NATIVE() { return boolean(eventAs<display::MouseEvent>(vm, self, "MouseEvent").buttonDown); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").buttonDown = args[0].toBoolean();
                return Val();
            })
        .property("shiftKey", NATIVE() { return boolean(eventAs<display::MouseEvent>(vm, self, "MouseEvent").shiftKey); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").shiftKey = args[0].toBoolean();
                return Val();
            })
        .property("ctrlKey", NATIVE() { return boolean(eventAs<display::MouseEvent>(vm, self, "MouseEvent").ctrlKey); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").ctrlKey = args[0].toBoolean();
                return Val();
            })
        .property("altKey", NATIVE() { return boolean(eventAs<display::MouseEvent>(vm, self, "MouseEvent").altKey); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").altKey = args[0].toBoolean();
                return Val();
            })
        .property("commandKey", NATIVE() { return boolean(false); }, NATIVE() { return Val(); })
        .property("controlKey",
            NATIVE() { return boolean(eventAs<display::MouseEvent>(vm, self, "MouseEvent").ctrlKey); },
            NATIVE() { return Val(); })
        .property("delta", NATIVE() { return number(eventAs<display::MouseEvent>(vm, self, "MouseEvent").delta); },
            NATIVE() {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").delta = args[0].toInt32();
                return Val();
            })
        .property("relatedObject",
            NATIVE(this) { return wrap(eventAs<display::MouseEvent>(vm, self, "MouseEvent").relatedObject); },
            NATIVE(this) {
                eventAs<display::MouseEvent>(vm, self, "MouseEvent").relatedObject = dispatcherOf(args[0]);
                return Val();
            })
        .getter("isRelatedObjectInaccessible", NATIVE() { return boolean(false); })
        .method("updateAfterEvent", NATIVE(this) {
            if (runtime.changed)
                runtime.changed();
            return Val();
        })
        .method("clone", NATIVE(this) {
            const auto& e = eventAs<display::MouseEvent>(vm, self, "MouseEvent");
            Val copy = makeEvent("flash.events.MouseEvent", e.type, e.bubbles, e.cancelable);
            copy.host<EventHost>()->owned = std::shared_ptr<display::Event>(e.clone().release());
            return copy;
        })
        .method("toString", NATIVE() {
            return text(describe(vm, self, "MouseEvent",
                {"type", "bubbles", "cancelable", "eventPhase", "localX", "localY", "stageX", "stageY", "relatedObject",
                    "ctrlKey", "altKey", "shiftKey", "buttonDown", "delta"}));
        })
        .define();

    // KeyboardEvent.
    vm.defineNativeClass("flash.events.KeyboardEvent")
        .extends("flash.events.Event")
        .constant("KEY_DOWN", text("keyDown"))
        .constant("KEY_UP", text("keyUp"))
        .constructor(NATIVE() {
            if (display::KeyboardEvent* e = attachEvent<display::KeyboardEvent>(vm, self, args, true))
            {
                e->charCode = uintArg(args, 3);
                e->keyCode = uintArg(args, 4);
                e->keyLocation = uintArg(args, 5);
                e->ctrlKey = booleanArg(args, 6);
                e->altKey = booleanArg(args, 7);
                e->shiftKey = booleanArg(args, 8);
            }
            return Val();
        })
        .property("charCode",
            NATIVE() { return number(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").charCode); },
            NATIVE() {
                eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").charCode = args[0].toUint32();
                return Val();
            })
        .property("keyCode",
            NATIVE() { return number(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").keyCode); },
            NATIVE() {
                eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").keyCode = args[0].toUint32();
                return Val();
            })
        .property("keyLocation",
            NATIVE() { return number(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").keyLocation); },
            NATIVE() {
                eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").keyLocation = args[0].toUint32();
                return Val();
            })
        .property("shiftKey",
            NATIVE() { return boolean(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").shiftKey); },
            NATIVE() {
                eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").shiftKey = args[0].toBoolean();
                return Val();
            })
        .property("ctrlKey",
            NATIVE() { return boolean(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").ctrlKey); },
            NATIVE() {
                eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").ctrlKey = args[0].toBoolean();
                return Val();
            })
        .property("altKey",
            NATIVE() { return boolean(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").altKey); },
            NATIVE() {
                eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").altKey = args[0].toBoolean();
                return Val();
            })
        .property("commandKey", NATIVE() { return boolean(false); }, NATIVE() { return Val(); })
        .property("controlKey",
            NATIVE() { return boolean(eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent").ctrlKey); },
            NATIVE() { return Val(); })
        .method("updateAfterEvent", NATIVE(this) {
            if (runtime.changed)
                runtime.changed();
            return Val();
        })
        .method("clone", NATIVE(this) {
            const auto& e = eventAs<display::KeyboardEvent>(vm, self, "KeyboardEvent");
            Val copy = makeEvent("flash.events.KeyboardEvent", e.type, e.bubbles, e.cancelable);
            copy.host<EventHost>()->owned = std::shared_ptr<display::Event>(e.clone().release());
            return copy;
        })
        .method("toString", NATIVE() {
            return text(describe(vm, self, "KeyboardEvent",
                {"type", "bubbles", "cancelable", "eventPhase", "charCode", "keyCode", "keyLocation", "ctrlKey",
                    "altKey", "shiftKey"}));
        })
        .define();

    // FocusEvent.
    vm.defineNativeClass("flash.events.FocusEvent")
        .extends("flash.events.Event")
        .constant("FOCUS_IN", text("focusIn"))
        .constant("FOCUS_OUT", text("focusOut"))
        .constant("KEY_FOCUS_CHANGE", text("keyFocusChange"))
        .constant("MOUSE_FOCUS_CHANGE", text("mouseFocusChange"))
        .constructor(NATIVE(this) {
            if (display::FocusEvent* e = attachEvent<display::FocusEvent>(vm, self, args, true))
            {
                e->relatedObject = dispatcherOf(args[3]);
                e->shiftKey = booleanArg(args, 4);
                e->keyCode = uintArg(args, 5);
            }
            return Val();
        })
        .property("relatedObject",
            NATIVE(this) { return wrap(eventAs<display::FocusEvent>(vm, self, "FocusEvent").relatedObject); },
            NATIVE(this) {
                eventAs<display::FocusEvent>(vm, self, "FocusEvent").relatedObject = dispatcherOf(args[0]);
                return Val();
            })
        .property("shiftKey", NATIVE() { return boolean(eventAs<display::FocusEvent>(vm, self, "FocusEvent").shiftKey); },
            NATIVE() {
                eventAs<display::FocusEvent>(vm, self, "FocusEvent").shiftKey = args[0].toBoolean();
                return Val();
            })
        .property("keyCode", NATIVE() { return number(eventAs<display::FocusEvent>(vm, self, "FocusEvent").keyCode); },
            NATIVE() {
                eventAs<display::FocusEvent>(vm, self, "FocusEvent").keyCode = args[0].toUint32();
                return Val();
            })
        .getter("isRelatedObjectInaccessible", NATIVE() { return boolean(false); })
        .method("clone", NATIVE(this) {
            const auto& e = eventAs<display::FocusEvent>(vm, self, "FocusEvent");
            Val copy = makeEvent("flash.events.FocusEvent", e.type, e.bubbles, e.cancelable);
            copy.host<EventHost>()->owned = std::shared_ptr<display::Event>(e.clone().release());
            return copy;
        })
        .define();

    // TextEvent, and the error events that are TextEvents.
    const auto textOf = [](VM& vm, const Val& self) -> std::string {
        EventHost& host = eventHost(vm, self);
        if (const auto* e = dynamic_cast<const display::TextEvent*>(&host.event()))
            return e->text;
        return host.text;
    };
    vm.defineNativeClass("flash.events.TextEvent")
        .extends("flash.events.Event")
        .constant("LINK", text("link"))
        .constant("TEXT_INPUT", text("textInput"))
        .constructor(NATIVE() {
            if (display::TextEvent* e = attachEvent<display::TextEvent>(vm, self, args, false))
                e->text = stringArg(vm, args, 3);
            return Val();
        })
        .property("text", NATIVE(textOf) { return text(textOf(vm, self)); }, NATIVE() {
            EventHost& host = eventHost(vm, self);
            if (auto* e = dynamic_cast<display::TextEvent*>(&host.event()))
                e->text = vm.toString(args[0]);
            else
                host.text = vm.toString(args[0]);
            return Val();
        })
        .method("clone", NATIVE(this, textOf) {
            const display::Event& e = eventOf(vm, self);
            Val copy = makeEvent("flash.events.TextEvent", e.type, e.bubbles, e.cancelable);
            static_cast<display::TextEvent&>(copy.host<EventHost>()->event()).text = textOf(vm, self);
            return copy;
        })
        .define();
    vm.defineNativeClass("flash.events.DataEvent")
        .extends("flash.events.TextEvent")
        .constant("DATA", text("data"))
        .constant("UPLOAD_COMPLETE_DATA", text("uploadCompleteData"))
        .constructor(NATIVE() {
            if (display::TextEvent* e = attachEvent<display::TextEvent>(vm, self, args, false))
                e->text = stringArg(vm, args, 3);
            return Val();
        })
        .property("data", NATIVE(textOf) { return text(textOf(vm, self)); }, NATIVE() {
            EventHost& host = eventHost(vm, self);
            if (auto* e = dynamic_cast<display::TextEvent*>(&host.event()))
                e->text = vm.toString(args[0]);
            return Val();
        })
        .define();
    const auto errorEvent = [this, textOf](const char* name, const char* base,
                                std::initializer_list<std::pair<const char*, const char*>> constants) {
        NativeClassBuilder builder = vm.defineNativeClass(name);
        builder.extends(base);
        for (const auto& [constant, value] : constants)
            builder.constant(constant, text(value));
        const std::string cls = name;
        builder
            .constructor(NATIVE() {
                if (display::TextEvent* e = attachEvent<display::TextEvent>(vm, self, args, false))
                {
                    e->text = stringArg(vm, args, 3);
                    self.host<EventHost>()->errorID = intArg(args, 4);
                }
                return Val();
            })
            .getter("errorID", NATIVE() { return number(eventHost(vm, self).errorID); })
            .method("clone", NATIVE(this, textOf, cls) {
                const display::Event& e = eventOf(vm, self);
                Val copy = makeEvent(cls, e.type, e.bubbles, e.cancelable);
                if (auto* t = dynamic_cast<display::TextEvent*>(&copy.host<EventHost>()->event()))
                    t->text = textOf(vm, self);
                else
                    copy.host<EventHost>()->text = textOf(vm, self);
                copy.host<EventHost>()->errorID = eventHost(vm, self).errorID;
                return copy;
            })
            .method("toString", NATIVE(cls) {
                const size_t dot = cls.find_last_of('.');
                return text(describe(vm, self, cls.substr(dot + 1), {"type", "bubbles", "cancelable", "eventPhase", "text"}));
            })
            .define();
    };
    errorEvent("flash.events.ErrorEvent", "flash.events.TextEvent", {{"ERROR", "error"}});
    errorEvent("flash.events.IOErrorEvent", "flash.events.ErrorEvent",
        {{"IO_ERROR", "ioError"}, {"NETWORK_ERROR", "networkError"}, {"DISK_ERROR", "diskError"},
            {"VERIFY_ERROR", "verifyError"}, {"STANDARD_ERROR_IO_ERROR", "standardErrorIoError"},
            {"STANDARD_INPUT_IO_ERROR", "standardInputIoError"}, {"STANDARD_OUTPUT_IO_ERROR", "standardOutputIoError"}});
    errorEvent("flash.events.SecurityErrorEvent", "flash.events.ErrorEvent", {{"SECURITY_ERROR", "securityError"}});
    errorEvent("flash.events.AsyncErrorEvent", "flash.events.ErrorEvent", {{"ASYNC_ERROR", "asyncError"}});

    // TimerEvent, ProgressEvent, HTTPStatusEvent.
    vm.defineNativeClass("flash.events.TimerEvent")
        .extends("flash.events.Event")
        .constant("TIMER", text("timer"))
        .constant("TIMER_COMPLETE", text("timerComplete"))
        .constructor(NATIVE() {
            attachEvent<display::Event>(vm, self, args, false);
            return Val();
        })
        .method("updateAfterEvent", NATIVE(this) {
            if (runtime.changed)
                runtime.changed();
            return Val();
        })
        .method("clone", NATIVE(this) {
            const display::Event& e = eventOf(vm, self);
            return makeEvent("flash.events.TimerEvent", e.type, e.bubbles, e.cancelable);
        })
        .method("toString", NATIVE() {
            return text(describe(vm, self, "TimerEvent", {"type", "bubbles", "cancelable", "eventPhase"}));
        })
        .define();
    vm.defineNativeClass("flash.events.ProgressEvent")
        .extends("flash.events.Event")
        .constant("PROGRESS", text("progress"))
        .constant("SOCKET_DATA", text("socketData"))
        .constructor(NATIVE() {
            if (attachEvent<display::Event>(vm, self, args, false))
            {
                EventHost* host = self.host<EventHost>();
                host->bytesLoaded = numberArg(vm, args, 3);
                host->bytesTotal = numberArg(vm, args, 4);
            }
            return Val();
        })
        .property("bytesLoaded", NATIVE() { return number(eventHost(vm, self).bytesLoaded); }, NATIVE() {
            eventHost(vm, self).bytesLoaded = vm.toNumber(args[0]);
            return Val();
        })
        .property("bytesTotal", NATIVE() { return number(eventHost(vm, self).bytesTotal); }, NATIVE() {
            eventHost(vm, self).bytesTotal = vm.toNumber(args[0]);
            return Val();
        })
        .method("clone", NATIVE(this) {
            const EventHost& host = eventHost(vm, self);
            const display::Event& e = host.event();
            Val copy = makeEvent("flash.events.ProgressEvent", e.type, e.bubbles, e.cancelable);
            copy.host<EventHost>()->bytesLoaded = host.bytesLoaded;
            copy.host<EventHost>()->bytesTotal = host.bytesTotal;
            return copy;
        })
        .method("toString", NATIVE() {
            return text(describe(
                vm, self, "ProgressEvent", {"type", "bubbles", "cancelable", "eventPhase", "bytesLoaded", "bytesTotal"}));
        })
        .define();
    vm.defineNativeClass("flash.events.HTTPStatusEvent")
        .extends("flash.events.Event")
        .constant("HTTP_STATUS", text("httpStatus"))
        .constant("HTTP_RESPONSE_STATUS", text("httpResponseStatus"))
        .constructor(NATIVE() {
            if (attachEvent<display::Event>(vm, self, args, false))
                self.host<EventHost>()->status = intArg(args, 3);
            return Val();
        })
        .getter("status", NATIVE() { return number(eventHost(vm, self).status); })
        .getter("responseURL", NATIVE() { return Val::null(); })
        .getter("responseHeaders", NATIVE() { return vm.newArray(); })
        .getter("redirected", NATIVE() { return boolean(false); })
        .method("clone", NATIVE(this) {
            const EventHost& host = eventHost(vm, self);
            const display::Event& e = host.event();
            Val copy = makeEvent("flash.events.HTTPStatusEvent", e.type, e.bubbles, e.cancelable);
            copy.host<EventHost>()->status = host.status;
            return copy;
        })
        .define();
}

} // namespace script
} // namespace play
} // namespace fla
