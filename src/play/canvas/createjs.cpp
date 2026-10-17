#include "scripts.h"

namespace fla::play::canvas {

/// CreateJS as an HTML5 Canvas movie's scripts see it, over __phoenix (see
/// Native in scripts.cpp): EventDispatcher and the events, MovieClip over
/// the runtime's clips (one object per clip, its instances by name),
/// the stage, exportRoot and lib, Ticker, Tween and Ease, Sound, and the
/// browser's setTimeout, console and alert. What it does not have says so
/// when a script asks for it. In pieces, as MSVC takes no string literal
/// longer than 16K.
const char* const kPrelude = R"JS(
(function (global) {
"use strict";
var P = __phoenix;
delete global.__phoenix;

function hidden(object, name, value) {
    Object.defineProperty(object, name, {value: value, writable: true, configurable: true, enumerable: false});
}
function missing(what) {
    throw new TypeError(what + " is not available in Test Movie");
}
/// An object whose unknown members say they are not there, rather than
/// being undefined.
function strict(object, name) {
    return new Proxy(object, {
        get: function (target, key, receiver) {
            if (typeof key !== "string" || key in target || key === "toJSON" || key === "then")
                return Reflect.get(target, key, receiver);
            missing(name + "." + key);
        }
    });
}
function show(value) {
    if (typeof value === "string")
        return value;
    if (value instanceof Error)
        return String(value);
    if (value && typeof value === "object" && !Array.isArray(value) && value.toString === Object.prototype.toString) {
        try { return JSON.stringify(value); } catch (e) { return String(value); }
    }
    return String(value);
}
function print(args, prefix) {
    var parts = [];
    for (var i = 0; i < args.length; ++i)
        parts.push(show(args[i]));
    P.print((prefix || "") + parts.join(" "));
}

// Events.

function Event(type, bubbles, cancelable) {
    this.type = type;
    this.target = null;
    this.currentTarget = null;
    this.eventPhase = 0;
    this.bubbles = !!bubbles;
    this.cancelable = !!cancelable;
    this.timeStamp = P.time();
    this.defaultPrevented = false;
    this.propagationStopped = false;
    this.immediatePropagationStopped = false;
    this.removed = false;
}
Event.prototype.preventDefault = function () { this.defaultPrevented = this.cancelable; };
Event.prototype.stopPropagation = function () { this.propagationStopped = true; };
Event.prototype.stopImmediatePropagation = function () {
    this.immediatePropagationStopped = this.propagationStopped = true;
};
Event.prototype.remove = function () { this.removed = true; };
Event.prototype.clone = function () {
    var copy = new Event(this.type, this.bubbles, this.cancelable);
    for (var key in this)
        if (this.hasOwnProperty(key))
            copy[key] = this[key];
    return copy;
};
Event.prototype.set = function (props) {
    for (var key in props)
        this[key] = props[key];
    return this;
};
Event.prototype.toString = function () { return "[Event (type=" + this.type + ")]"; };

function MouseEvent(type, bubbles, cancelable, stageX, stageY, nativeEvent, pointerID, primary, rawX, rawY, relatedTarget) {
    Event.call(this, type, bubbles, cancelable);
    this.stageX = stageX || 0;
    this.stageY = stageY || 0;
    this.rawX = rawX === undefined ? this.stageX : rawX;
    this.rawY = rawY === undefined ? this.stageY : rawY;
    this.nativeEvent = nativeEvent || null;
    this.pointerID = pointerID || 0;
    this.primary = primary === undefined ? true : !!primary;
    this.relatedTarget = relatedTarget || null;
}
MouseEvent.prototype = Object.create(Event.prototype);
MouseEvent.prototype.constructor = MouseEvent;
Object.defineProperty(MouseEvent.prototype, "localX", {
    get: function () { return this.currentTarget && this.currentTarget.globalToLocal ? this.currentTarget.globalToLocal(this.rawX, this.rawY).x : this.rawX; }
});
Object.defineProperty(MouseEvent.prototype, "localY", {
    get: function () { return this.currentTarget && this.currentTarget.globalToLocal ? this.currentTarget.globalToLocal(this.rawX, this.rawY).y : this.rawY; }
});
MouseEvent.prototype.toString = function () {
    return "[MouseEvent (type=" + this.type + " stageX=" + this.stageX + " stageY=" + this.stageY + ")]";
};

// EventDispatcher: listeners by type, in the capture phase or the target
// and bubble phases, as CreateJS keeps them.

function EventDispatcher() {}
var ED = EventDispatcher.prototype;
EventDispatcher.initialize = function (target) {
    ["addEventListener", "on", "removeEventListener", "off", "removeAllEventListeners", "hasEventListener",
        "dispatchEvent", "_dispatchEvent", "willTrigger"].forEach(function (name) { target[name] = ED[name]; });
};
ED._listenersOf = function (capture, make) {
    var key = capture ? "__captureListeners" : "__listeners";
    if (!Object.prototype.hasOwnProperty.call(this, key)) {
        if (!make)
            return null;
        hidden(this, key, {});
    }
    return this[key];
};
/// A type's first listener added, or its last taken off: what the object
/// does about it (a clip asks the display list for its mouse events).
ED._listening = function () {};
ED.addEventListener = function (type, listener, useCapture) {
    if (typeof listener !== "function" && !(listener && typeof listener.handleEvent === "function"))
        throw new TypeError("addEventListener(\"" + type + "\"): the listener is not a function");
    var listeners = this._listenersOf(useCapture, true);
    var list = listeners[type];
    if (list) {
        var at = list.indexOf(listener);
        if (at >= 0)
            list.splice(at, 1);
    }
    list = listeners[type];
    if (!list || !list.length) {
        listeners[type] = [listener];
        this._listening(type, true);
    } else
        list.push(listener);
    return listener;
};
ED.on = function (type, listener, scope, once, data, useCapture) {
    if (listener && listener.handleEvent) {
        scope = scope || listener;
        listener = listener.handleEvent;
    }
    scope = scope || this;
    return this.addEventListener(type, function (event) {
        listener.call(scope, event, data);
        if (once)
            event.remove();
    }, useCapture);
};
ED.removeEventListener = function (type, listener, useCapture) {
    var listeners = this._listenersOf(useCapture, false);
    var list = listeners && listeners[type];
    if (!list)
        return;
    var at = list.indexOf(listener);
    if (at < 0)
        return;
    if (list.length === 1) {
        delete listeners[type];
        if (!this.hasEventListener(type))
            this._listening(type, false);
    } else
        list.splice(at, 1);
};
ED.off = ED.removeEventListener;
ED.removeAllEventListeners = function (type) {
    var self = this, types = [];
    [false, true].forEach(function (capture) {
        var listeners = self._listenersOf(capture, false);
        if (!listeners)
            return;
        Object.keys(listeners).forEach(function (key) {
            if (!type || key === type) {
                delete listeners[key];
                types.push(key);
            }
        });
    });
    types.forEach(function (t) { self._listening(t, false); });
};
ED.hasEventListener = function (type) {
    var listeners = this._listenersOf(false, false), captures = this._listenersOf(true, false);
    return !!((listeners && listeners[type] && listeners[type].length) || (captures && captures[type] && captures[type].length));
};
ED.willTrigger = function (type) {
    for (var o = this; o; o = o.parent)
        if (o.hasEventListener(type))
            return true;
    return false;
};
ED.dispatchEvent = function (eventObj, bubbles, cancelable) {
    if (typeof eventObj === "string") {
        if (!bubbles && !this.hasEventListener(eventObj))
            return true;
        eventObj = new Event(eventObj, bubbles, cancelable);
    } else if (eventObj.target && eventObj.clone)
        eventObj = eventObj.clone();
    try { eventObj.target = this; } catch (e) {}
    if (!eventObj.bubbles || !this.parent)
        this._dispatchEvent(eventObj, 2);
    else {
        var path = [];
        for (var top = this; top; top = top.parent)
            path.push(top);
        var i;
        for (i = path.length - 1; i >= 0 && !eventObj.propagationStopped; --i)
            path[i]._dispatchEvent(eventObj, 1 + (i === 0 ? 1 : 0));
        for (i = 1; i < path.length && !eventObj.propagationStopped; ++i)
            path[i]._dispatchEvent(eventObj, 3);
    }
    return !eventObj.defaultPrevented;
};
ED._dispatchEvent = function (eventObj, phase) {
    var listeners = this._listenersOf(phase === 1, false);
    var list = listeners && listeners[eventObj.type];
    if (!list || !list.length)
        return;
    try { eventObj.currentTarget = this; } catch (e) {}
    try { eventObj.eventPhase = phase | 0; } catch (e) {}
    eventObj.removed = false;
    list = list.slice();
    for (var i = 0; i < list.length && !eventObj.immediatePropagationStopped; ++i) {
        var o = list[i];
        if (o.handleEvent)
            o.handleEvent(eventObj);
        else
            o(eventObj);
        if (eventObj.removed) {
            this.off(eventObj.type, o, phase === 1);
            eventObj.removed = false;
        }
    }
};

)JS" R"JS(
// Geometry, as scripts make and read it.

function Point(x, y) { this.x = x || 0; this.y = y || 0; }
Point.prototype.setValues = function (x, y) { this.x = x || 0; this.y = y || 0; return this; };
Point.prototype.clone = function () { return new Point(this.x, this.y); };
Point.prototype.copy = function (p) { this.x = p.x; this.y = p.y; return this; };
Point.prototype.toString = function () { return "[Point (x=" + this.x + " y=" + this.y + ")]"; };
function Rectangle(x, y, width, height) { this.setValues(x, y, width, height); }
Rectangle.prototype.setValues = function (x, y, width, height) {
    this.x = x || 0; this.y = y || 0; this.width = width || 0; this.height = height || 0;
    return this;
};
Rectangle.prototype.clone = function () { return new Rectangle(this.x, this.y, this.width, this.height); };
Rectangle.prototype.contains = function (x, y, width, height) {
    width = width || 0; height = height || 0;
    return x >= this.x && x + width <= this.x + this.width && y >= this.y && y + height <= this.y + this.height;
};
Rectangle.prototype.intersects = function (r) {
    return r.x <= this.x + this.width && this.x <= r.x + r.width && r.y <= this.y + this.height && this.y <= r.y + r.height;
};
Rectangle.prototype.isEmpty = function () { return this.width <= 0 || this.height <= 0; };
Rectangle.prototype.toString = function () {
    return "[Rectangle (x=" + this.x + " y=" + this.y + " width=" + this.width + " height=" + this.height + ")]";
};

// The display objects: MovieClip over the runtime's clips, one object per
// clip. What a script sets that a clip does not have is kept on it.

var MOUSE = {click: 1, dblclick: 1, mousedown: 1, mouseup: 1, mouseover: 1, mouseout: 1, rollover: 1, rollout: 1};
var ticking = new Set();
var notLooping = new Set();

function DisplayObject() { missing("new createjs.DisplayObject()"); }
DisplayObject.prototype = Object.create(ED);
DisplayObject.prototype.constructor = DisplayObject;
function Container() { missing("new createjs.Container()"); }
Container.prototype = Object.create(DisplayObject.prototype);
Container.prototype.constructor = Container;
function MovieClip() {
    missing("new createjs.MovieClip() (make a library symbol's with new lib.Name())");
}
MovieClip.prototype = Object.create(Container.prototype);
MovieClip.prototype.constructor = MovieClip;
MovieClip.INDEPENDENT = "independent";
MovieClip.SINGLE_FRAME = "single";
MovieClip.SYNCHED = "synched";
var MC = MovieClip.prototype;

["x", "y", "rotation", "scaleX", "scaleY", "alpha", "visible", "paused", "name", "mouseEnabled", "mouseChildren",
    "cursor"].forEach(function (name) {
    Object.defineProperty(MC, name, {
        get: function () { return P.get(this.__id, name); },
        set: function (value) { P.set(this.__id, name, value); },
        configurable: true
    });
});
["currentFrame", "totalFrames", "currentLabel", "labels", "numChildren", "mouseX", "mouseY"].forEach(function (name) {
    Object.defineProperty(MC, name, {
        get: function () { return P.get(this.__id, name); },
        set: function () {},
        configurable: true
    });
});
Object.defineProperty(MC, "duration", { get: function () { return this.totalFrames; }, configurable: true });
Object.defineProperty(MC, "frameBounds", { value: null, writable: true, configurable: true });
Object.defineProperty(MC, "loop", {
    get: function () { return !notLooping.has(this); },
    set: function (value) {
        if (value === false || value === 0)
            notLooping.add(this);
        else
            notLooping.delete(this);
    },
    configurable: true
});
Object.defineProperty(MC, "parent", {
    get: function () {
        var id = P.parent(this.__id);
        return id === -1 ? stage : id >= 0 ? wrap(id) : null;
    },
    configurable: true
});
Object.defineProperty(MC, "stage", {
    get: function () {
        for (var at = this; at; at = at.parent)
            if (at === stage)
                return stage;
        return null;
    },
    configurable: true
});
Object.defineProperty(MC, "children", {
    get: function () { return P.children(this.__id).map(wrap); },
    configurable: true
});
Object.defineProperty(MC, "id", { get: function () { return this.__id; }, configurable: true });
MC._listening = function (type, on) {
    if (MOUSE[type])
        P.listen(this.__id, type, on);
    else if (type === "tick") {
        if (on)
            ticking.add(this);
        else
            ticking.delete(this);
    }
};
MC.play = function () { P.play(this.__id, true); };
MC.stop = function () { P.play(this.__id, false); };
function go(clip, target, play, how) {
    if (typeof target === "number")
        target = Math.floor(target);
    else if (typeof target !== "string")
        throw new TypeError(how + ": a frame number (from 0) or a label, not " + show(target));
    if (!P.go(clip.__id, target, play))
        P.print("Warning: " + how + "(" + JSON.stringify(target) + "): there is no such " +
            (typeof target === "string" ? "label" : "frame") + (clip.name ? " in " + clip.name : ""));
}
MC.gotoAndPlay = function (positionOrLabel) { go(this, positionOrLabel, true, "gotoAndPlay"); };
MC.gotoAndStop = function (positionOrLabel) { go(this, positionOrLabel, false, "gotoAndStop"); };
MC.advance = function () {
    var next = this.currentFrame + 1;
    go(this, next >= this.totalFrames ? 0 : next, !this.paused, "advance");
};
MC.isVisible = function () { return !!this.visible && this.alpha > 0; };
MC.getChildByName = function (name) {
    var ids = P.children(this.__id);
    for (var i = 0; i < ids.length; ++i)
        if (wrap(ids[i]).name === name)
            return wrap(ids[i]);
    return null;
};
MC.getChildAt = function (index) { var ids = P.children(this.__id); return index >= 0 && index < ids.length ? wrap(ids[index]) : null; };
MC.getChildIndex = function (child) { return P.children(this.__id).indexOf(child && child.__id); };
MC.contains = function (child) {
    for (var at = child; at; at = at.parent)
        if (at === this)
            return true;
    return false;
};
function clipArgument(child, how) {
    if (!child || child.__id === undefined)
        throw new TypeError(how + ": " + show(child) + " is not a movie clip (only library symbols' clips can be added)");
    return child;
}
MC.addChild = function () {
    for (var i = 0; i < arguments.length; ++i)
        P.add(this.__id, clipArgument(arguments[i], "addChild").__id);
    return arguments[arguments.length - 1];
};
MC.addChildAt = function (child, index) {
    P.add(this.__id, clipArgument(child, "addChildAt").__id, index);
    return child;
};
MC.setChildIndex = function (child, index) { P.add(this.__id, clipArgument(child, "setChildIndex").__id, index); };
MC.removeChild = function () {
    var removed = false;
    for (var i = 0; i < arguments.length; ++i)
        removed = P.remove(this.__id, clipArgument(arguments[i], "removeChild").__id) || removed;
    return removed;
};
MC.removeChildAt = function (index) {
    var child = this.getChildAt(index);
    return child ? this.removeChild(child) : false;
};
MC.removeAllChildren = function () {
    var self = this;
    P.children(this.__id).forEach(function (id) { P.remove(self.__id, id); });
};
MC.localToGlobal = function (x, y, pt) {
    var at = P.local(this.__id, x, y, true);
    return (pt || new Point()).setValues(at[0], at[1]);
};
MC.globalToLocal = function (x, y, pt) {
    var at = P.local(this.__id, x, y, false);
    return (pt || new Point()).setValues(at[0], at[1]);
};
MC.localToLocal = function (x, y, target, pt) {
    var at = this.localToGlobal(x, y);
    return target.globalToLocal(at.x, at.y, pt);
};
MC.hitTest = function (x, y) { return P.hit(this.__id, x, y); };
MC.getBounds = function () {
    var box = P.bounds(this.__id);
    return box ? new Rectangle(box[0], box[1], box[2], box[3]) : null;
};
MC.getTransformedBounds = function () {
    var box = this.getBounds();
    if (!box)
        return null;
    var corners = [[box.x, box.y], [box.x + box.width, box.y], [box.x, box.y + box.height], [box.x + box.width, box.y + box.height]];
    var parent = this.parent, self = this;
    var points = corners.map(function (c) {
        var g = self.localToGlobal(c[0], c[1]);
        return parent && parent !== stage ? parent.globalToLocal(g.x, g.y) : g;
    });
    var xs = points.map(function (p) { return p.x; }), ys = points.map(function (p) { return p.y; });
    var left = Math.min.apply(null, xs), top = Math.min.apply(null, ys);
    return new Rectangle(left, top, Math.max.apply(null, xs) - left, Math.max.apply(null, ys) - top);
};
MC.setTransform = function (x, y, scaleX, scaleY, rotation, skewX, skewY, regX, regY) {
    this.x = x || 0; this.y = y || 0;
    this.scaleX = scaleX == null ? 1 : scaleX; this.scaleY = scaleY == null ? 1 : scaleY;
    this.rotation = rotation || 0;
    return this;
};
MC.set = function (props) {
    for (var key in props)
        this[key] = props[key];
    return this;
};
MC.cache = MC.uncache = MC.updateCache = function () {};
MC.toString = function () { return "[MovieClip (name=" + this.name + ")]"; };
MC.clone = function () { missing("MovieClip.clone()"); };

/// The object scripts see for clip \a id: made once, the instances in it
/// found by name.
var clips = new Map();
var texts = new Map();
var handler = {
    get: function (target, key, receiver) {
        if (typeof key !== "string" || key in target)
            return Reflect.get(target, key, receiver);
        var found = P.child(target.__id, key);
        if (found >= 0)
            return wrap(found);
        if (found === -3)
            return textOf(target.__id, key);
        return undefined;
    }
};
function wrap(id) {
    if (id < 0)
        return null;
    var clip = clips.get(id);
    if (!clip) {
        var target = Object.create(MC);
        hidden(target, "__id", id);
        clip = new Proxy(target, handler);
        clips.set(id, clip);
    }
    return clip;
}
/// A text field the timeline placed in clip \a id: its text.
function TextField() {}
TextField.prototype.toString = function () { return "[Text (text=" + this.text + ")]"; };
function textOf(id, name) {
    var key = id + "/" + name;
    var field = texts.get(key);
    if (!field) {
        field = Object.create(TextField.prototype);
        Object.defineProperty(field, "text", {
            get: function () { return P.text(id, name); },
            set: function (value) { P.text(id, name, value === undefined || value === null ? "" : String(value)); },
            enumerable: true
        });
        hidden(field, "name", name);
        texts.set(key, field);
    }
    return field;
}

)JS" R"JS(
// The stage.

var stage = new EventDispatcher();
stage._listening = function (type, on) {
    if (MOUSE[type])
        P.listen(-1, type, on);
    else if (type === "tick") {
        if (on)
            ticking.add(stage);
        else
            ticking.delete(stage);
    }
};
Object.defineProperty(stage, "mouseX", { get: function () { return P.stage("mouseX"); } });
Object.defineProperty(stage, "mouseY", { get: function () { return P.stage("mouseY"); } });
Object.defineProperty(stage, "children", { get: function () { return [exportRoot]; } });
Object.defineProperty(stage, "numChildren", { get: function () { return 1; } });
hidden(stage, "canvas", { get width() { return P.stage("width"); }, get height() { return P.stage("height"); },
    style: {}, addEventListener: function () {}, removeEventListener: function () {} });
hidden(stage, "parent", null);
hidden(stage, "stage", stage);
hidden(stage, "name", "stage");
hidden(stage, "mouseInBounds", true);
hidden(stage, "autoClear", true);
hidden(stage, "tickOnUpdate", true);
hidden(stage, "mouseMoveOutside", false);
hidden(stage, "snapToPixelEnabled", false);
["enableMouseOver", "enableDOMEvents", "update", "clear", "tick", "setBounds", "cache", "uncache", "updateCache"]
    .forEach(function (name) { hidden(stage, name, function () {}); });
hidden(stage, "getChildAt", function (i) { return i === 0 ? exportRoot : null; });
hidden(stage, "contains", function (child) { return MC.contains.call(stage, child); });
hidden(stage, "localToGlobal", function (x, y, pt) { return (pt || new Point()).setValues(x, y); });
hidden(stage, "globalToLocal", function (x, y, pt) { return (pt || new Point()).setValues(x, y); });
hidden(stage, "addChild", function () { missing("stage.addChild (add it to exportRoot or another clip)"); });
hidden(stage, "removeChild", function () { missing("stage.removeChild"); });
hidden(stage, "toString", function () { return "[Stage]"; });
var exportRoot = wrap(P.root());

// The pointer the stage hears: stagemousedown and the rest, and pressmove
// and pressup for the clip pressed.
var pressed = null;
/// The pressup to send once the release's click has gone.
var releasing = null;
function pointer(type, x, y, target, bubbles) {
    var event = new MouseEvent(type, bubbles, false, x, y);
    event.target = target;
    return event;
}
function released() {
    var was = releasing;
    releasing = null;
    if (was && P.alive(was.clip.__id))
        was.clip.dispatchEvent(pointer("pressup", was.x, was.y, was.clip, true));
}
global.__phoenixReleased = released;
global.__phoenixStage = function (type, x, y, targetId) {
    var target = targetId >= 0 ? wrap(targetId) : stage;
    released();
    if (type === "mouseDown") {
        pressed = target === stage ? null : target;
        stage.dispatchEvent(pointer("stagemousedown", x, y, stage, false));
    } else if (type === "mouseMove") {
        if (pressed && P.alive(pressed.__id))
            pressed.dispatchEvent(pointer("pressmove", x, y, pressed, true));
        stage.dispatchEvent(pointer("stagemousemove", x, y, stage, false));
    } else if (type === "mouseUp") {
        if (pressed)
            releasing = { clip: pressed, x: x, y: y };
        pressed = null;
        stage.dispatchEvent(pointer("stagemouseup", x, y, stage, false));
    }
};

// The display list's mouse events on clips, and keys at the stage (as the
// browser's keydown and keyup).
var last = { serial: -1, event: null };
var keyListeners = { keydown: [], keyup: [] };
var KEY_NAMES = { 8: "Backspace", 9: "Tab", 13: "Enter", 16: "Shift", 17: "Control", 18: "Alt", 27: "Escape",
    32: " ", 37: "ArrowLeft", 38: "ArrowUp", 39: "ArrowRight", 40: "ArrowDown", 46: "Delete" };
global.__phoenixEvent = function (id, type, targetId, serial, phase, a, b, modifiers) {
    if (type === "keydown" || type === "keyup") {
        var key = { type: type, keyCode: a, which: a, charCode: b,
            key: KEY_NAMES[a] || (b ? String.fromCharCode(b) : String.fromCharCode(a)),
            code: a >= 65 && a <= 90 ? "Key" + String.fromCharCode(a) : (KEY_NAMES[a] || ""),
            shiftKey: !!(modifiers & 1), ctrlKey: !!(modifiers & 2), altKey: !!(modifiers & 4), metaKey: false,
            repeat: false, preventDefault: function () {}, stopPropagation: function () {} };
        keyListeners[type].slice().forEach(function (f) {
            if (typeof f === "function")
                f.call(global, key);
            else if (f && f.handleEvent)
                f.handleEvent(key);
        });
        var on = global["on" + type];
        if (typeof on === "function")
            on.call(global, key);
        on = documentObject["on" + type];
        if (typeof on === "function")
            on.call(documentObject, key);
        return false;
    }
    var at = id >= 0 ? wrap(id) : stage;
    if (!at)
        return false;
    var event;
    if (last.serial === serial && last.event && last.event.type === type)
        event = last.event;
    else {
        event = new MouseEvent(type, type !== "rollover" && type !== "rollout", false, a, b);
        event.target = targetId >= 0 ? wrap(targetId) : stage;
        last.serial = serial;
        last.event = event;
    }
    at._dispatchEvent(event, phase === 1 ? 1 : phase === 2 ? 2 : 3);
    return event.propagationStopped;
};

/// The window and document, for what canvas scripts use of them.
function keyListen(type, listener, on) {
    if (type !== "keydown" && type !== "keyup")
        return;
    var list = keyListeners[type];
    var at = list.indexOf(listener);
    if (on && at < 0)
        list.push(listener);
    else if (!on && at >= 0)
        list.splice(at, 1);
}
global.addEventListener = function (type, listener) { keyListen(type, listener, true); };
global.removeEventListener = function (type, listener) { keyListen(type, listener, false); };
var documentObject = {
    addEventListener: function (type, listener) { keyListen(type, listener, true); },
    removeEventListener: function (type, listener) { keyListen(type, listener, false); },
    getElementById: function () { return null; },
    querySelector: function () { return null; },
    createElement: function (name) { missing("document.createElement(\"" + name + "\")"); },
    body: { style: {} },
    title: ""
};
global.window = global;
global.document = documentObject;
global.location = { href: "", search: "", hash: "", assign: function (url) { P.open(url, "_self"); },
    replace: function (url) { P.open(url, "_self"); }, reload: function () {} };
global.navigator = { userAgent: "Phoenix Test Movie", language: "en-US", platform: "" };
global.open = function (url, target) { P.open(String(url), target === undefined ? "_blank" : String(target)); return null; };
global.alert = function (message) { print([message === undefined ? "" : message]); };
global.confirm = function (message) { print([message]); return true; };
global.prompt = function (message, value) { print([message]); return value === undefined ? null : value; };
global.console = {
    log: function () { print(arguments); },
    info: function () { print(arguments); },
    debug: function () { print(arguments); },
    warn: function () { print(arguments, "Warning: "); },
    error: function () { print(arguments, "Error: "); },
    trace: function () { print(arguments); },
    dir: function (value) { print([value]); },
    table: function (value) { print([value]); },
    assert: function (condition) {
        if (!condition)
            print(Array.prototype.slice.call(arguments, 1), "Assertion failed: ");
    },
    clear: function () {},
    time: function () {},
    timeEnd: function () {}
};
// Keys come in once the stage has the window's focus; asked for now.
P.listen(-1, "keydown", true);
P.listen(-1, "keyup", true);

)JS" R"JS(
// Timers, in movie time.
var timers = new Map();
var nextTimer = 1;
var now = 0;
function addTimer(callback, delay, args, repeat) {
    if (typeof callback !== "function")
        throw new TypeError((repeat ? "setInterval" : "setTimeout") + ": the callback is not a function");
    var id = nextTimer++;
    delay = Math.max(0, Number(delay) || 0);
    timers.set(id, { callback: callback, delay: delay, due: now + delay, args: args, repeat: repeat });
    return id;
}
global.setTimeout = function (callback, delay) {
    return addTimer(callback, delay, Array.prototype.slice.call(arguments, 2), false);
};
global.setInterval = function (callback, delay) {
    return addTimer(callback, delay, Array.prototype.slice.call(arguments, 2), true);
};
global.clearTimeout = global.clearInterval = function (id) { timers.delete(id); };
global.requestAnimationFrame = function (callback) { return addTimer(function () { callback(now); }, 0, [], false); };
global.cancelAnimationFrame = global.clearTimeout;
function runTimers() {
    var due = [];
    timers.forEach(function (timer, id) {
        if (timer.due <= now + 1e-6)
            due.push([id, timer]);
    });
    due.sort(function (a, b) { return a[1].due - b[1].due || a[0] - b[0]; });
    due.forEach(function (entry) {
        var id = entry[0], timer = entry[1];
        if (!timers.has(id))
            return;
        if (timer.repeat)
            timer.due += Math.max(timer.delay, 1);
        else
            timers.delete(id);
        timer.callback.apply(global, timer.args);
    });
}

// Ticker: the movie's frames.

var Ticker = new EventDispatcher();
var ticks = 0, startTime = 0, pausedTime = 0;
Ticker.RAF_SYNCHED = "synched";
Ticker.RAF = "raf";
Ticker.TIMEOUT = "timeout";
Ticker.timingMode = Ticker.TIMEOUT;
Ticker.maxDelta = 0;
Ticker.paused = false;
Object.defineProperty(Ticker, "framerate", {
    get: function () { return P.frameRate(); },
    set: function (value) { P.frameRate(value); }
});
Object.defineProperty(Ticker, "interval", {
    get: function () { return 1000 / P.frameRate(); },
    set: function (value) { if (value > 0) P.frameRate(1000 / value); }
});
Ticker.getFPS = function () { return P.frameRate(); };
Ticker.setFPS = function (value) { P.frameRate(value); };
Ticker.getInterval = function () { return 1000 / P.frameRate(); };
Ticker.setInterval = function (value) { Ticker.interval = value; };
Ticker.getMeasuredFPS = function () { return P.frameRate(); };
Ticker.getMeasuredTickTime = function () { return 0; };
Ticker.getTime = function (runTime) { return runTime ? now - pausedTime : now; };
Ticker.getEventTime = Ticker.getTime;
Ticker.getTicks = function (pauseable) { return ticks; };
Ticker.reset = Ticker.init = function () {};
Ticker.setPaused = function (value) { Ticker.paused = !!value; };
Ticker.getPaused = function () { return Ticker.paused; };

)JS" R"JS(
// Tween and Ease.

var Ease = {
    linear: function (t) { return t; },
    get: function (amount) {
        if (amount < -1) amount = -1;
        else if (amount > 1) amount = 1;
        return function (t) {
            if (amount === 0) return t;
            if (amount < 0) return t * (t * -amount + 1 + amount);
            return t * ((2 - t) * amount + (1 - amount));
        };
    },
    getPowIn: function (pow) { return function (t) { return Math.pow(t, pow); }; },
    getPowOut: function (pow) { return function (t) { return 1 - Math.pow(1 - t, pow); }; },
    getPowInOut: function (pow) {
        return function (t) {
            if ((t *= 2) < 1) return 0.5 * Math.pow(t, pow);
            return 1 - 0.5 * Math.abs(Math.pow(2 - t, pow));
        };
    },
    getBackIn: function (amount) { return function (t) { return t * t * ((amount + 1) * t - amount); }; },
    getBackOut: function (amount) { return function (t) { return --t * t * ((amount + 1) * t + amount) + 1; }; },
    getBackInOut: function (amount) {
        amount *= 1.525;
        return function (t) {
            if ((t *= 2) < 1) return 0.5 * (t * t * ((amount + 1) * t - amount));
            return 0.5 * ((t -= 2) * t * ((amount + 1) * t + amount) + 2);
        };
    },
    getElasticIn: function (amplitude, period) {
        var pi2 = Math.PI * 2;
        return function (t) {
            if (t === 0 || t === 1) return t;
            var s = period / pi2 * Math.asin(1 / amplitude);
            return -(amplitude * Math.pow(2, 10 * (t -= 1)) * Math.sin((t - s) * pi2 / period));
        };
    },
    getElasticOut: function (amplitude, period) {
        var pi2 = Math.PI * 2;
        return function (t) {
            if (t === 0 || t === 1) return t;
            var s = period / pi2 * Math.asin(1 / amplitude);
            return amplitude * Math.pow(2, -10 * t) * Math.sin((t - s) * pi2 / period) + 1;
        };
    },
    getElasticInOut: function (amplitude, period) {
        var pi2 = Math.PI * 2;
        return function (t) {
            var s = period / pi2 * Math.asin(1 / amplitude);
            if ((t *= 2) < 1) return -0.5 * (amplitude * Math.pow(2, 10 * (t -= 1)) * Math.sin((t - s) * pi2 / period));
            return amplitude * Math.pow(2, -10 * (t -= 1)) * Math.sin((t - s) * pi2 / period) * 0.5 + 1;
        };
    },
    sineIn: function (t) { return 1 - Math.cos(t * Math.PI / 2); },
    sineOut: function (t) { return Math.sin(t * Math.PI / 2); },
    sineInOut: function (t) { return -0.5 * (Math.cos(Math.PI * t) - 1); },
    circIn: function (t) { return -(Math.sqrt(1 - t * t) - 1); },
    circOut: function (t) { return Math.sqrt(1 - (--t) * t); },
    circInOut: function (t) {
        if ((t *= 2) < 1) return -0.5 * (Math.sqrt(1 - t * t) - 1);
        return 0.5 * (Math.sqrt(1 - (t -= 2) * t) + 1);
    },
    bounceOut: function (t) {
        if (t < 1 / 2.75) return 7.5625 * t * t;
        if (t < 2 / 2.75) return 7.5625 * (t -= 1.5 / 2.75) * t + 0.75;
        if (t < 2.5 / 2.75) return 7.5625 * (t -= 2.25 / 2.75) * t + 0.9375;
        return 7.5625 * (t -= 2.625 / 2.75) * t + 0.984375;
    }
};
Ease.none = Ease.linear;
Ease.quadIn = Ease.getPowIn(2); Ease.quadOut = Ease.getPowOut(2); Ease.quadInOut = Ease.getPowInOut(2);
Ease.cubicIn = Ease.getPowIn(3); Ease.cubicOut = Ease.getPowOut(3); Ease.cubicInOut = Ease.getPowInOut(3);
Ease.quartIn = Ease.getPowIn(4); Ease.quartOut = Ease.getPowOut(4); Ease.quartInOut = Ease.getPowInOut(4);
Ease.quintIn = Ease.getPowIn(5); Ease.quintOut = Ease.getPowOut(5); Ease.quintInOut = Ease.getPowInOut(5);
Ease.backIn = Ease.getBackIn(1.7); Ease.backOut = Ease.getBackOut(1.7); Ease.backInOut = Ease.getBackInOut(1.7);
Ease.elasticIn = Ease.getElasticIn(1, 0.3); Ease.elasticOut = Ease.getElasticOut(1, 0.3);
Ease.elasticInOut = Ease.getElasticInOut(1, 0.3 * 1.5);
Ease.bounceIn = function (t) { return 1 - Ease.bounceOut(1 - t); };
Ease.bounceInOut = function (t) {
    if (t < 0.5) return Ease.bounceIn(t * 2) * 0.5;
    return Ease.bounceOut(t * 2 - 1) * 0.5 + 0.5;
};

var tweens = [];
function Tween(target, props) {
    props = props || {};
    this.target = target;
    this.loop = props.loop === true ? -1 : (props.loop | 0);
    this.useTicks = !!props.useTicks;
    this.ignoreGlobalPause = !!props.ignoreGlobalPause;
    this.reversed = !!props.reversed;
    this.bounce = !!props.bounce;
    this.timeScale = props.timeScale || 1;
    this.passive = false;
    this.duration = 0;
    this.position = 0;
    this.rawPosition = 0;
    hidden(this, "_steps", []);
    hidden(this, "_step", 0);
    hidden(this, "_time", 0);
    hidden(this, "_paused", false);
    if (props.onChange)
        this.addEventListener("change", props.onChange);
    if (props.onComplete)
        this.addEventListener("complete", props.onComplete);
    if (props.override)
        Tween.removeTweens(target);
    if (props.position)
        this._time = props.position;
    if (props.paused)
        this._paused = true;
    else
        tweens.push(this);
}
Tween.prototype = Object.create(ED);
Tween.prototype.constructor = Tween;
Tween.get = function (target, props) { return new Tween(target, props); };
Tween.removeTweens = function (target) {
    tweens = tweens.filter(function (tween) { return tween.target !== target; });
};
Tween.removeAllTweens = function () { tweens = []; };
Tween.hasActiveTweens = function (target) {
    return target ? tweens.some(function (tween) { return tween.target === target; }) : tweens.length > 0;
};
Tween.tick = function () {};
Tween.IGNORE = {};
var TW = Tween.prototype;
TW._add = function (step) {
    step.start = this.duration;
    this.duration += step.duration || 0;
    this._steps.push(step);
    return this;
};
TW.to = function (props, duration, ease) {
    return this._add({ type: "to", props: props || {}, duration: Math.max(0, Number(duration) || 0), ease: ease || Ease.linear });
};
TW.wait = function (duration, passive) { return this._add({ type: "wait", duration: Math.max(0, Number(duration) || 0) }); };
TW.call = function (callback, params, scope) {
    return this._add({ type: "call", callback: callback, params: params || [this], scope: scope });
};
TW.set = function (props, target) { return this._add({ type: "set", props: props || {}, target: target }); };
TW.label = function () { return this; };
TW.setPaused = function (paused) {
    this.paused = paused;
    return this;
};
Object.defineProperty(TW, "paused", {
    get: function () { return this._paused; },
    set: function (value) {
        value = !!value;
        if (value === this._paused)
            return;
        this._paused = value;
        var at = tweens.indexOf(this);
        if (value && at >= 0)
            tweens.splice(at, 1);
        else if (!value && at < 0)
            tweens.push(this);
    }
});
TW.play = function (tween) {
    if (tween)
        tween.paused = false;
    else
        this.paused = false;
    return this;
};
TW.pause = function (tween) {
    if (tween)
        tween.paused = true;
    else
        this.paused = true;
    return this;
};
TW.setPosition = function (position) {
    this._time = Math.max(0, position);
    this._step = 0;
    this._advance(0);
    return this;
};
TW._advance = function (delta) {
    this._time += delta * this.timeScale;
    var target = this.target;
    while (this._step < this._steps.length) {
        var step = this._steps[this._step];
        if (step.type === "to") {
            if (!step.from) {
                step.from = {};
                for (var key in step.props)
                    step.from[key] = target[key];
            }
            // A frame's milliseconds add up to a little less than they
            // should: what is within a hair of the end is the end.
            var t = step.duration > 0 ? Math.min(1, (this._time - step.start) / step.duration + 1e-9) : 1;
            var ratio = step.ease(Math.max(0, t));
            for (var name in step.props) {
                var from = step.from[name], to = step.props[name];
                target[name] = typeof from === "number" && typeof to === "number" ? from + (to - from) * ratio
                    : (t >= 1 ? to : from);
            }
            if (t < 1)
                break;
        } else if (step.type === "wait") {
            if (this._time + 1e-6 < step.start + step.duration)
                break;
        } else if (step.type === "call") {
            step.callback.apply(step.scope || target, step.params);
        } else if (step.type === "set") {
            var to2 = step.target || target;
            for (var k in step.props)
                to2[k] = step.props[k];
        }
        ++this._step;
    }
    this.position = this.rawPosition = Math.min(this._time, this.duration);
    this.dispatchEvent("change");
    if (this._step < this._steps.length)
        return;
    if (this.loop !== 0 && this.duration > 0) {
        if (this.loop > 0)
            --this.loop;
        this._time -= this.duration;
        this._step = 0;
        return;
    }
    var at = tweens.indexOf(this);
    if (at >= 0)
        tweens.splice(at, 1);
    this.dispatchEvent("complete");
};
TW.toString = function () { return "[Tween]"; };

// Sound: the library's sounds, by their linkage.
var Sound = {
    INTERRUPT_ANY: "any", INTERRUPT_EARLY: "early", INTERRUPT_LATE: "late", INTERRUPT_NONE: "none",
    PLAY_SUCCEEDED: "playSucceeded", PLAY_FAILED: "playFailed",
    volume: 1, muted: false,
    play: function (src, interrupt, delay, offset, loop, volume) {
        var options = interrupt && typeof interrupt === "object" ? interrupt : { delay: delay, offset: offset, loop: loop, volume: volume };
        var loops = options.loop === -1 ? 9999 : (options.loop | 0) + 1;
        var channel = Sound.muted ? -1 : P.sound(String(src), loops, (options.volume === undefined ? 1 : options.volume) * Sound.volume, options.offset);
        return {
            src: src, playState: channel >= 0 ? Sound.PLAY_SUCCEEDED : Sound.PLAY_FAILED,
            stop: function () { if (channel >= 0) P.stopSound(channel); return this; },
            paused: false, volume: options.volume === undefined ? 1 : options.volume, loop: options.loop | 0,
            addEventListener: function () {}, on: function () {}, removeEventListener: function () {}, off: function () {}
        };
    },
    stop: function () { P.stopSound(-1); },
    registerSound: function () { return true; },
    registerSounds: function () { return true; },
    registerManifest: function () { return true; },
    removeSound: function () { return true; },
    loadComplete: function () { return true; },
    initializeDefaultPlugins: function () { return true; },
    setVolume: function (value) { Sound.volume = value; },
    getVolume: function () { return Sound.volume; },
    setMute: function (value) { Sound.muted = !!value; return true; },
    getMute: function () { return Sound.muted; },
    addEventListener: function () {}, on: function () {}, removeEventListener: function () {}, off: function () {}
};

)JS" R"JS(
// The namespaces.

var createjs = {
    EventDispatcher: EventDispatcher, Event: Event, MouseEvent: MouseEvent, Ticker: Ticker, Tween: Tween,
    Ease: Ease, MovieClip: MovieClip, Container: Container, DisplayObject: DisplayObject, Point: Point,
    Rectangle: Rectangle, Sound: strict(Sound, "createjs.Sound"), Stage: function () { missing("new createjs.Stage()"); },
    Shape: function () { missing("new createjs.Shape() (drawing from scripts)"); },
    Graphics: function () { missing("createjs.Graphics (drawing from scripts)"); },
    Text: function () { missing("new createjs.Text()"); },
    Bitmap: function () { missing("new createjs.Bitmap()"); },
    Sprite: function () { missing("new createjs.Sprite()"); },
    ButtonHelper: function () {},
    Timeline: function () { missing("new createjs.Timeline()"); },
    extend: function (subclass, superclass) {
        function o() { this.constructor = subclass; }
        o.prototype = superclass.prototype;
        return (subclass.prototype = new o());
    },
    promote: function (subclass) { return subclass; },
    indexOf: function (array, item) { return array.indexOf(item); },
    proxy: function (method, scope) {
        var args = Array.prototype.slice.call(arguments, 2);
        return function () { return method.apply(scope, Array.prototype.slice.call(arguments, 0).concat(args)); };
    }
};
createjs.Ticker = strict(Ticker, "createjs.Ticker");
createjs.Tween = Tween;
global.createjs = strict(createjs, "createjs");
global.cjs = global.createjs;
global.stage = stage;
global.exportRoot = exportRoot;
/// lib: new lib.Name() makes a clip of the library symbol linked as (or
/// named) Name.
var libProperties = { get width() { return P.stage("width"); }, get height() { return P.stage("height"); },
    get fps() { return P.frameRate(); }, get color() { return "#" + ("000000" + P.stage("color").toString(16)).slice(-6); },
    opacity: 1, manifest: [], preloads: [] };
var constructors = {};
global.lib = new Proxy({}, {
    get: function (target, key) {
        if (typeof key !== "string")
            return undefined;
        if (key in target)
            return target[key];
        if (key === "properties")
            return libProperties;
        if (key === "ssMetadata")
            return [];
        if (!constructors[key]) {
            constructors[key] = function () {
                var id = P.make(key);
                if (id < 0)
                    missing("lib." + key + " (no library symbol is linked as or named " + key + ")");
                return wrap(id);
            };
            constructors[key].prototype = MC;
        }
        return constructors[key];
    },
    set: function (target, key, value) { target[key] = value; return true; }
});
global.images = global.images || {};
global.ss = global.ss || {};

// What the runtime calls.

global.__phoenixRun = function (index, id) {
    __phoenixFrames[index].call(wrap(id));
};
global.__phoenixForget = function (id) {
    var clip = clips.get(id);
    if (clip) {
        ticking.delete(clip);
        notLooping.delete(clip);
        Tween.removeTweens(clip);
        if (pressed === clip)
            pressed = null;
    }
    clips.delete(id);
    texts.forEach(function (field, key) {
        if (key.indexOf(id + "/") === 0)
            texts.delete(key);
    });
};
global.__phoenixTick = function (delta, time) {
    released();
    now = time;
    ++ticks;
    if (Ticker.paused)
        pausedTime += delta;
    // A clip that does not loop stops on its last frame.
    notLooping.forEach(function (clip) {
        if (!clip.paused && clip.currentFrame >= clip.totalFrames - 1)
            clip.stop();
    });
    runTimers();
    var event = function () {
        var made = new Event("tick");
        made.delta = delta;
        made.paused = Ticker.paused;
        made.time = now;
        made.runTime = now - pausedTime;
        return made;
    };
    // As the stage updates: every clip listening, then the stage.
    Array.from(ticking).forEach(function (clip) {
        if (clip !== stage && !P.alive(clip.__id)) {
            ticking.delete(clip);
            return;
        }
        if (clip !== stage)
            clip.dispatchEvent(event());
    });
    if (ticking.has(stage))
        stage.dispatchEvent(event());
    tweens.slice().forEach(function (tween) {
        if (!Ticker.paused || tween.ignoreGlobalPause)
            tween._advance(tween.useTicks ? 1 : delta);
    });
    Ticker.dispatchEvent(event());
};
})(globalThis);
)JS";

} // namespace fla::play::canvas
