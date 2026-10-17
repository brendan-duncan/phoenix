// flash.events: the event classes and EventDispatcher, with the capture,
// target and bubble phases and listener priorities.

(function (phoenix) {
"use strict";

var EventPhase = {CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3};

class Event {
    constructor(type, bubbles, cancelable) {
        this._type = String(type);
        this._bubbles = !!bubbles;
        this._cancelable = !!cancelable;
        this._target = null;
        this._currentTarget = null;
        this._eventPhase = EventPhase.AT_TARGET;
        this._stopped = false;
        this._stoppedNow = false;
        this._prevented = false;
    }
    get type() { return this._type; }
    get bubbles() { return this._bubbles; }
    get cancelable() { return this._cancelable; }
    get target() { return this._target; }
    get currentTarget() { return this._currentTarget; }
    get eventPhase() { return this._eventPhase; }
    clone() { return new Event(this._type, this._bubbles, this._cancelable); }
    formatToString(className) {
        var parts = ["[" + className];
        for (var i = 1; i < arguments.length; ++i) {
            var value = this[arguments[i]];
            parts.push(arguments[i] + "=" + (typeof value === "string" ? "\"" + value + "\"" : value));
        }
        return parts.join(" ") + "]";
    }
    isDefaultPrevented() { return this._prevented; }
    preventDefault() {
        if (this._cancelable)
            this._prevented = true;
    }
    stopPropagation() { this._stopped = true; }
    stopImmediatePropagation() { this._stopped = this._stoppedNow = true; }
    toString() { return this.formatToString("Event", "type", "bubbles", "cancelable", "eventPhase"); }
}
Object.assign(Event, {
    ACTIVATE: "activate", ADDED: "added", ADDED_TO_STAGE: "addedToStage", CANCEL: "cancel", CHANGE: "change",
    CLEAR: "clear", CLOSE: "close", COMPLETE: "complete", CONNECT: "connect", COPY: "copy", CUT: "cut",
    DEACTIVATE: "deactivate", ENTER_FRAME: "enterFrame", EXIT_FRAME: "exitFrame",
    FRAME_CONSTRUCTED: "frameConstructed", FULLSCREEN: "fullScreen", ID3: "id3", INIT: "init",
    MOUSE_LEAVE: "mouseLeave", OPEN: "open", PASTE: "paste", REMOVED: "removed",
    REMOVED_FROM_STAGE: "removedFromStage", RENDER: "render", RESIZE: "resize", SCROLL: "scroll",
    SELECT: "select", SELECT_ALL: "selectAll", SOUND_COMPLETE: "soundComplete", TAB_CHILDREN_CHANGE: "tabChildrenChange",
    TAB_ENABLED_CHANGE: "tabEnabledChange", TAB_INDEX_CHANGE: "tabIndexChange", UNLOAD: "unload"
});

class MouseEvent extends Event {
    constructor(type, bubbles, cancelable, localX, localY, relatedObject, ctrlKey, altKey, shiftKey, buttonDown, delta) {
        super(type, bubbles === undefined ? true : bubbles, cancelable);
        this._localX = localX === undefined ? NaN : +localX;
        this._localY = localY === undefined ? NaN : +localY;
        this.relatedObject = relatedObject || null;
        this.ctrlKey = !!ctrlKey;
        this.altKey = !!altKey;
        this.shiftKey = !!shiftKey;
        this.commandKey = false;
        this.controlKey = !!ctrlKey;
        this.buttonDown = !!buttonDown;
        this.delta = delta | 0;
        this.isRelatedObjectInaccessible = false;
        this.movementX = 0;
        this.movementY = 0;
        this._stageX = NaN;
        this._stageY = NaN;
    }
    /// Where the pointer is in the current target's space.
    get localX() {
        if (!isNaN(this._stageX) && this._currentTarget && this._currentTarget.globalToLocal)
            return this._currentTarget._globalToLocal(this._stageX, this._stageY)[0];
        return this._localX;
    }
    set localX(value) { this._localX = +value; }
    get localY() {
        if (!isNaN(this._stageY) && this._currentTarget && this._currentTarget.globalToLocal)
            return this._currentTarget._globalToLocal(this._stageX, this._stageY)[1];
        return this._localY;
    }
    set localY(value) { this._localY = +value; }
    get stageX() { return this._stageX; }
    get stageY() { return this._stageY; }
    updateAfterEvent() { phoenix._.requestDraw && phoenix._.requestDraw(); }
    clone() {
        var copy = new MouseEvent(this._type, this._bubbles, this._cancelable, this._localX, this._localY,
            this.relatedObject, this.ctrlKey, this.altKey, this.shiftKey, this.buttonDown, this.delta);
        copy._stageX = this._stageX;
        copy._stageY = this._stageY;
        return copy;
    }
    toString() {
        return this.formatToString("MouseEvent", "type", "bubbles", "cancelable", "eventPhase", "localX", "localY",
            "stageX", "stageY", "relatedObject", "ctrlKey", "altKey", "shiftKey", "buttonDown", "delta");
    }
}
Object.assign(MouseEvent, {
    CLICK: "click", CONTEXT_MENU: "contextMenu", DOUBLE_CLICK: "doubleClick", MIDDLE_CLICK: "middleClick",
    MIDDLE_MOUSE_DOWN: "middleMouseDown", MIDDLE_MOUSE_UP: "middleMouseUp", MOUSE_DOWN: "mouseDown",
    MOUSE_MOVE: "mouseMove", MOUSE_OUT: "mouseOut", MOUSE_OVER: "mouseOver", MOUSE_UP: "mouseUp",
    MOUSE_WHEEL: "mouseWheel", RELEASE_OUTSIDE: "releaseOutside", RIGHT_CLICK: "rightClick",
    RIGHT_MOUSE_DOWN: "rightMouseDown", RIGHT_MOUSE_UP: "rightMouseUp", ROLL_OUT: "rollOut", ROLL_OVER: "rollOver"
});

class KeyboardEvent extends Event {
    constructor(type, bubbles, cancelable, charCodeValue, keyCodeValue, keyLocationValue, ctrlKeyValue, altKeyValue, shiftKeyValue) {
        super(type, bubbles === undefined ? true : bubbles, cancelable);
        this.charCode = charCodeValue >>> 0;
        this.keyCode = keyCodeValue >>> 0;
        this.keyLocation = keyLocationValue >>> 0;
        this.ctrlKey = !!ctrlKeyValue;
        this.altKey = !!altKeyValue;
        this.shiftKey = !!shiftKeyValue;
        this.commandKey = false;
        this.controlKey = !!ctrlKeyValue;
    }
    updateAfterEvent() { phoenix._.requestDraw && phoenix._.requestDraw(); }
    clone() {
        return new KeyboardEvent(this._type, this._bubbles, this._cancelable, this.charCode, this.keyCode,
            this.keyLocation, this.ctrlKey, this.altKey, this.shiftKey);
    }
    toString() {
        return this.formatToString("KeyboardEvent", "type", "bubbles", "cancelable", "eventPhase", "charCode",
            "keyCode", "keyLocation", "ctrlKey", "altKey", "shiftKey");
    }
}
Object.assign(KeyboardEvent, {KEY_DOWN: "keyDown", KEY_UP: "keyUp"});

class FocusEvent extends Event {
    constructor(type, bubbles, cancelable, relatedObject, shiftKey, keyCode) {
        super(type, bubbles === undefined ? true : bubbles, cancelable);
        this.relatedObject = relatedObject || null;
        this.shiftKey = !!shiftKey;
        this.keyCode = keyCode >>> 0;
        this.isRelatedObjectInaccessible = false;
    }
    clone() { return new FocusEvent(this._type, this._bubbles, this._cancelable, this.relatedObject, this.shiftKey, this.keyCode); }
}
Object.assign(FocusEvent, {FOCUS_IN: "focusIn", FOCUS_OUT: "focusOut", KEY_FOCUS_CHANGE: "keyFocusChange",
    MOUSE_FOCUS_CHANGE: "mouseFocusChange"});

class TextEvent extends Event {
    constructor(type, bubbles, cancelable, text) {
        super(type, bubbles, cancelable);
        this.text = text === undefined ? "" : String(text);
    }
    clone() { return new TextEvent(this._type, this._bubbles, this._cancelable, this.text); }
}
Object.assign(TextEvent, {LINK: "link", TEXT_INPUT: "textInput"});

class ErrorEvent extends TextEvent {
    constructor(type, bubbles, cancelable, text, id) {
        super(type, bubbles, cancelable, text);
        this._errorID = id | 0;
    }
    get errorID() { return this._errorID; }
    clone() { return new ErrorEvent(this._type, this._bubbles, this._cancelable, this.text, this._errorID); }
}
ErrorEvent.ERROR = "error";

class IOErrorEvent extends ErrorEvent {
    clone() { return new IOErrorEvent(this._type, this._bubbles, this._cancelable, this.text, this._errorID); }
}
IOErrorEvent.IO_ERROR = "ioError";

class SecurityErrorEvent extends ErrorEvent {}
SecurityErrorEvent.SECURITY_ERROR = "securityError";

class TimerEvent extends Event {
    clone() { return new TimerEvent(this._type, this._bubbles, this._cancelable); }
    updateAfterEvent() { phoenix._.requestDraw && phoenix._.requestDraw(); }
}
Object.assign(TimerEvent, {TIMER: "timer", TIMER_COMPLETE: "timerComplete"});

class ProgressEvent extends Event {
    constructor(type, bubbles, cancelable, bytesLoaded, bytesTotal) {
        super(type, bubbles, cancelable);
        this.bytesLoaded = +bytesLoaded || 0;
        this.bytesTotal = +bytesTotal || 0;
    }
    clone() { return new ProgressEvent(this._type, this._bubbles, this._cancelable, this.bytesLoaded, this.bytesTotal); }
}
Object.assign(ProgressEvent, {PROGRESS: "progress", SOCKET_DATA: "socketData"});

/// Listeners by type and phase: each an entry of the function, its
/// priority and the order it came in, kept sorted by priority (higher
/// first), then order.
var serial = 0;
class EventDispatcher {
    constructor(target) {
        Object.defineProperty(this, "_listeners", {value: null, writable: true});
        Object.defineProperty(this, "_eventTarget", {value: target || null, writable: true});
    }
    /// The ActionScript constructor, for translated subclasses (core.js,
    /// adoptClass): the root of the display classes' chain.
    $construct(target) {
        if (target)
            this._eventTarget = target;
    }
    _listenersOf(useCapture, make) {
        if (!this._listeners) {
            if (!make)
                return null;
            this._listeners = {capture: {}, bubble: {}};
        }
        return this._listeners[useCapture ? "capture" : "bubble"];
    }
    addEventListener(type, listener, useCapture, priority, useWeakReference) {
        if (typeof listener !== "function")
            throw phoenix.as3Error("TypeError", 2007, "Parameter listener must be non-null.");
        var byType = this._listenersOf(!!useCapture, true);
        var list = byType[type] || (byType[type] = []);
        for (var i = 0; i < list.length; ++i)
            if (list[i].listener === listener)
                return;
        var entry = {listener: listener, priority: priority | 0, order: ++serial};
        var at = list.length;
        while (at > 0 && list[at - 1].priority < entry.priority)
            --at;
        // A copy, so a dispatch already going keeps the list it started with.
        list = list.slice();
        list.splice(at, 0, entry);
        byType[type] = list;
        this._listening(type, true);
    }
    removeEventListener(type, listener, useCapture) {
        var byType = this._listenersOf(!!useCapture, false);
        var list = byType && byType[type];
        if (!list)
            return;
        for (var i = 0; i < list.length; ++i) {
            if (list[i].listener === listener) {
                list = list.slice();
                list.splice(i, 1);
                if (list.length)
                    byType[type] = list;
                else
                    delete byType[type];
                if (!this.hasEventListener(type))
                    this._listening(type, false);
                return;
            }
        }
    }
    hasEventListener(type) {
        if (!this._listeners)
            return false;
        return !!(this._listeners.capture[type] || this._listeners.bubble[type]);
    }
    willTrigger(type) {
        for (var at = this; at; at = at._parent)
            if (at.hasEventListener && at.hasEventListener(type))
                return true;
        return false;
    }
    /// The first listener of a type added or the last removed: what the
    /// object does about it (display objects tell the player).
    _listening(type, on) {}
    dispatchEvent(event) {
        if (!(event instanceof Event))
            throw phoenix.as3Error("TypeError", 2007, "Parameter event must be non-null.");
        // An event already sent goes again as a copy.
        if (event._target)
            event = event.clone();
        var target = this._eventTarget || this;
        event._target = target;
        event._stopped = event._stoppedNow = false;
        // The ancestors, for the capture and bubble phases.
        var path = [];
        if (this._isDisplayObject)
            for (var up = this._parent; up; up = up._parent)
                path.push(up);
        var i;
        event._eventPhase = EventPhase.CAPTURING_PHASE;
        for (i = path.length - 1; i >= 0 && !event._stopped; --i)
            path[i]._deliver(event, true);
        if (!event._stopped) {
            event._eventPhase = EventPhase.AT_TARGET;
            this._deliver(event, false);
        }
        if (event._bubbles) {
            event._eventPhase = EventPhase.BUBBLING_PHASE;
            for (i = 0; i < path.length && !event._stopped; ++i)
                path[i]._deliver(event, false);
        }
        return !event._prevented;
    }
    /// Calls this object's listeners for \a event's current phase: capture
    /// listeners on the way down, the others at the target and on the way
    /// up.
    _deliver(event, capture) {
        var byType = this._listenersOf(capture, false);
        var list = byType && byType[event._type];
        if (!list)
            return;
        event._currentTarget = this._eventTarget || this;
        for (var i = 0; i < list.length && !event._stoppedNow; ++i) {
            try {
                list[i].listener.call(null, event);
            } catch (error) {
                phoenix._.reportError(error, event._type + " listener");
            }
        }
    }
    toString() { return "[object " + phoenix.qualifiedNameOf(this).split("::").pop() + "]"; }
}

phoenix._.reportError = function (error, where) {
    var message = error && error.stack ? error.stack : String(error);
    if (typeof console !== "undefined" && console.error)
        console.error("Error in " + where + ": " + message);
    phoenix._.errors = phoenix._.errors || [];
    phoenix._.errors.push(where + ": " + String(error));
};

phoenix.defineClass("flash.events.Event", Event);
phoenix.defineClass("flash.events.EventPhase", function EventPhaseClass() {});
Object.assign(phoenix.ns("flash.events").EventPhase, EventPhase);
phoenix.defineClass("flash.events.MouseEvent", MouseEvent);
phoenix.defineClass("flash.events.KeyboardEvent", KeyboardEvent);
phoenix.defineClass("flash.events.FocusEvent", FocusEvent);
phoenix.defineClass("flash.events.TextEvent", TextEvent);
phoenix.defineClass("flash.events.ErrorEvent", ErrorEvent);
phoenix.defineClass("flash.events.IOErrorEvent", IOErrorEvent);
phoenix.defineClass("flash.events.SecurityErrorEvent", SecurityErrorEvent);
phoenix.defineClass("flash.events.TimerEvent", TimerEvent);
phoenix.defineClass("flash.events.ProgressEvent", ProgressEvent);
phoenix.defineClass("flash.events.EventDispatcher", EventDispatcher);
phoenix.defineClass("flash.events.IEventDispatcher", function IEventDispatcher() {});
})(phoenix);
