// CreateJS over the Phoenix runtime, for HTML5 Canvas documents: their
// frame scripts are JavaScript written against CreateJS's API, run with
// this as the timeline's MovieClip. The display objects are the runtime's
// own (flash.display), given CreateJS's ways where the two differ: frames
// counted from 0, on() and CreateJS's event names, positions as numbers,
// cursor, paused and loop, skew and a registration point, filters shown
// once cached, shadows, masks and composite operations. createjs.Ticker,
// Tween, Ease, Sound, Shape, Graphics (its whole API, the tiny one and
// encoded paths, its commands kept for scripts to change), Text, Bitmap,
// SpriteSheet, Sprite, Container, MovieClip (a script's own, played from
// its tweens), the filters, ColorMatrix, Shadow, Matrix2D and ButtonHelper
// are here, with the stage, exportRoot, lib and images as published
// pages have them. Load it after phoenix.js and before the content.

(function (global) {
"use strict";
var phoenix = global.phoenix;
var flash = phoenix.root.flash;
var display = flash.display;
var DO = display.DisplayObject.prototype;
var MC = display.MovieClip.prototype;

function show(value) {
    return typeof value === "string" ? value : JSON.stringify(value);
}

// Events: CreateJS's names onto the runtime's, and CreateJS's event objects.

var names = {click: "click", dblclick: "doubleClick", mousedown: "mouseDown", mouseup: "mouseUp",
    mouseover: "mouseOver", mouseout: "mouseOut", rollover: "rollOver", rollout: "rollOut", tick: "enterFrame",
    added: "added", removed: "removed", stagemousedown: "mouseDown", stagemouseup: "mouseUp",
    stagemousemove: "mouseMove", mouseleave: "mouseLeave"};

function CJSEvent(type, bubbles, cancelable) {
    this.type = type;
    this.target = null;
    this.currentTarget = null;
    this.eventPhase = 0;
    this.bubbles = !!bubbles;
    this.cancelable = !!cancelable;
    this.timeStamp = Date.now();
    this.defaultPrevented = false;
    this.propagationStopped = false;
    this.immediatePropagationStopped = false;
    this.removed = false;
}
CJSEvent.prototype.preventDefault = function () { this.defaultPrevented = this.cancelable; };
CJSEvent.prototype.stopPropagation = function () {
    this.propagationStopped = true;
    if (this.nativeEvent && this.nativeEvent.stopPropagation)
        this.nativeEvent.stopPropagation();
};
CJSEvent.prototype.stopImmediatePropagation = function () {
    this.immediatePropagationStopped = this.propagationStopped = true;
    if (this.nativeEvent && this.nativeEvent.stopImmediatePropagation)
        this.nativeEvent.stopImmediatePropagation();
};
CJSEvent.prototype.remove = function () { this.removed = true; };
CJSEvent.prototype.clone = function () { return Object.assign(new CJSEvent(this.type, this.bubbles, this.cancelable), this); };
CJSEvent.prototype.set = function (props) { return Object.assign(this, props); };
CJSEvent.prototype.toString = function () { return "[Event (type=" + this.type + ")]"; };

/// A runtime event as CreateJS hands it to a listener.
function eventFor(type, e) {
    var made = new CJSEvent(type, e.bubbles, e.cancelable);
    made.target = e.target;
    made.currentTarget = e.currentTarget;
    made.eventPhase = e.eventPhase;
    made.nativeEvent = e;
    if (e.stageX !== undefined) {
        made.stageX = made.rawX = e.stageX;
        made.stageY = made.rawY = e.stageY;
        made.pointerID = 0;
        made.primary = true;
        Object.defineProperty(made, "localX", {get: function () { return e.localX; }});
        Object.defineProperty(made, "localY", {get: function () { return e.localY; }});
    }
    if (type === "tick") {
        made.delta = 1000 / phoenix.player.stage.frameRate;
        made.paused = Ticker.paused;
        made.time = now;
        made.runTime = now;
    }
    return made;
}

/// CreateJS's addEventListener: a function or an object with handleEvent,
/// by CreateJS's type names; returns the listener.
function listen(target, type, listener, useCapture) {
    if (typeof listener !== "function" && !(listener && typeof listener.handleEvent === "function"))
        throw new TypeError("addEventListener(\"" + type + "\"): the listener is not a function");
    var flashType = names[type] || type;
    var map = target._cjsListeners || (target._cjsListeners = []);
    for (var i = 0; i < map.length; ++i)
        if (map[i].type === type && map[i].listener === listener && map[i].capture === !!useCapture)
            return listener;
    var entry = {type: type, listener: listener, capture: !!useCapture};
    entry.wrapped = function (e) {
        var event = eventFor(type, e);
        if (typeof listener === "function")
            listener.call(target, event);
        else
            listener.handleEvent(event);
        if (event.removed)
            unlisten(target, type, listener, useCapture);
        if (event.propagationStopped)
            e.stopPropagation();
        if (event.immediatePropagationStopped)
            e.stopImmediatePropagation();
    };
    map.push(entry);
    if (type === "pressmove" || type === "pressup")
        presses(target, type, entry);
    else
        flash.events.EventDispatcher.prototype.addEventListener.call(target, flashType, entry.wrapped, !!useCapture);
    return listener;
}
function unlisten(target, type, listener, useCapture) {
    var map = target._cjsListeners || [];
    for (var i = 0; i < map.length; ++i) {
        var entry = map[i];
        if (entry.type !== type || entry.listener !== listener || entry.capture !== !!useCapture)
            continue;
        map.splice(i, 1);
        if (type === "pressmove" || type === "pressup")
            entry.off && entry.off();
        else
            flash.events.EventDispatcher.prototype.removeEventListener.call(target, names[type] || type, entry.wrapped, !!useCapture);
        return;
    }
}
/// pressmove and pressup: the pointer moving and let go after a press on
/// the object (or something in it).
var pressed = null;
function presses(target, type, entry) {
    entry.off = function () { entry.dead = true; };
    target._cjsPress = target._cjsPress || [];
    target._cjsPress.push(entry);
}
function deliverPress(type, e) {
    if (!pressed)
        return;
    var chain = [];
    for (var at = pressed; at && !(at instanceof display.Stage); at = at._parent)
        chain.push(at);
    for (var i = 0; i < chain.length; ++i) {
        var list = (chain[i]._cjsPress || []).filter(function (p) { return p.type === type && !p.dead; });
        list.forEach(function (p) {
            var fake = {target: pressed, currentTarget: chain[i], eventPhase: chain[i] === pressed ? 2 : 3, bubbles: true,
                cancelable: false, stageX: e.stageX, stageY: e.stageY, localX: chain[i]._globalToLocal(e.stageX, e.stageY)[0],
                localY: chain[i]._globalToLocal(e.stageX, e.stageY)[1], stopPropagation: function () {},
                stopImmediatePropagation: function () {}};
            p.wrapped(fake);
        });
    }
}

function on(type, listener, scope, once, data, useCapture) {
    if (listener && listener.handleEvent) {
        scope = scope || listener;
        listener = listener.handleEvent;
    }
    scope = scope || this;
    var self = this;
    var made = function (event) {
        listener.call(scope, event, data);
        if (once)
            event.remove();
    };
    return listen(self, type, made, useCapture);
}

// The display objects, as CreateJS has them.

var cjs = {
    addEventListener: function (type, listener, useCapture) { return listen(this, type, listener, useCapture); },
    removeEventListener: function (type, listener, useCapture) { unlisten(this, type, listener, useCapture); },
    on: on,
    off: function (type, listener, useCapture) { unlisten(this, type, listener, useCapture); },
    removeAllEventListeners: function (type) {
        var self = this;
        (this._cjsListeners || []).slice().forEach(function (entry) {
            if (!type || entry.type === type)
                unlisten(self, entry.type, entry.listener, entry.capture);
        });
    },
    hasEventListener: function (type) {
        return (this._cjsListeners || []).some(function (entry) { return entry.type === type; });
    },
    dispatchEvent: function (event, bubbles, cancelable) {
        // The runtime's own events go as they are.
        if (event instanceof flash.events.Event)
            return flash.events.EventDispatcher.prototype.dispatchEvent.call(this, event);
        var type = typeof event === "string" ? event : event.type;
        var flashEvent = new flash.events.Event(names[type] || type, typeof event === "string" ? !!bubbles : !!event.bubbles,
            !!cancelable);
        return flash.events.EventDispatcher.prototype.dispatchEvent.call(this, flashEvent);
    },
    localToGlobal: function (x, y, pt) {
        var at = this._localToGlobal(+x, +y);
        return (pt || new Point()).setValues(at[0], at[1]);
    },
    globalToLocal: function (x, y, pt) {
        var at = this._globalToLocal(+x, +y);
        return (pt || new Point()).setValues(at[0], at[1]);
    },
    localToLocal: function (x, y, target, pt) {
        var at = this._localToGlobal(+x, +y);
        var back = target._globalToLocal(at[0], at[1]);
        return (pt || new Point()).setValues(back[0], back[1]);
    },
    hitTest: function (x, y) { return this._hitContent(+x, +y, true); },
    getBounds: function () {
        var box = this._localBounds(true);
        return box ? new Rectangle(box[0], box[1], box[2] - box[0], box[3] - box[1]) : null;
    },
    getTransformedBounds: function () {
        var box = this._boundsIn(this._parent, true, true);
        return box ? new Rectangle(box[0], box[1], box[2] - box[0], box[3] - box[1]) : null;
    },
    setTransform: function (x, y, scaleX, scaleY, rotation) {
        this.x = x || 0;
        this.y = y || 0;
        this.scaleX = scaleX === undefined || scaleX === null ? 1 : scaleX;
        this.scaleY = scaleY === undefined || scaleY === null ? 1 : scaleY;
        this.rotation = rotation || 0;
        return this;
    },
    set: function (props) { return Object.assign(this, props); },
    isVisible: function () { return !!this.visible && this.alpha > 0; },
    removeAllChildren: function () { if (this.removeChildren) this.removeChildren(); },
    clone: function () { throw new TypeError("clone() is not available in a published Phoenix movie"); }
};
Object.keys(cjs).forEach(function (key) {
    Object.defineProperty(DO, key, {value: cjs[key], writable: true, configurable: true});
});
Object.defineProperty(display.Stage.prototype, "on", {value: on, writable: true, configurable: true});
// A name the timeline gave may be changed by a script, as CreateJS lets it.
Object.defineProperty(DO, "name", {
    get: function () { return this._name; },
    set: function (value) { this._name = value === null || value === undefined ? null : String(value); },
    configurable: true
});
Object.defineProperty(DO, "cursor", {
    get: function () { return this._cssCursor || null; },
    set: function (value) { this._cssCursor = value ? String(value) : null; },
    configurable: true
});
Object.defineProperty(DO, "id", {
    get: function () { return this._cjsId || (this._cjsId = ++ids); },
    configurable: true
});
var ids = 0;
Object.defineProperty(display.DisplayObjectContainer.prototype, "children", {
    get: function () { return this._children.slice(); },
    configurable: true
});
// Frames from 0, as CreateJS counts them.
Object.defineProperty(MC, "currentFrame", {
    get: function () { return this._frame; },
    configurable: true
});
Object.defineProperty(MC, "duration", {get: function () { return this.totalFrames; }, configurable: true});
Object.defineProperty(MC, "paused", {
    get: function () { return !this._playing; },
    set: function (value) { this._playing = !value && this.totalFrames > 1; },
    configurable: true
});
Object.defineProperty(MC, "loop", {
    get: function () { return !this._noLoop; },
    set: function (value) { this._noLoop = value === false || value === 0; },
    configurable: true
});
Object.defineProperty(MC, "labels", {
    get: function () {
        return (this._timeline && this._timeline.labels || []).map(function (l) { return {label: l[1], position: l[0]}; });
    },
    configurable: true
});
function frameOf(clip, target, how) {
    if (typeof target === "number")
        return Math.max(0, Math.min(clip.totalFrames - 1, Math.floor(target)));
    var labels = clip._timeline && clip._timeline.labels || [];
    for (var i = 0; i < labels.length; ++i)
        if (labels[i][1] === target)
            return labels[i][0];
    console.warn(how + "(" + show(target) + "): there is no such label" + (clip.name ? " in " + clip.name : ""));
    return -1;
}
MC.gotoAndPlay = function (positionOrLabel) {
    var frame = frameOf(this, positionOrLabel, "gotoAndPlay");
    if (frame < 0)
        return;
    this._playing = true;
    this._goTo(frame);
};
MC.gotoAndStop = function (positionOrLabel) {
    var frame = frameOf(this, positionOrLabel, "gotoAndStop");
    if (frame < 0)
        return;
    this._playing = false;
    this._goTo(frame);
};
MC.advance = function () {
    var next = this._frame + 1;
    this._goTo(next >= this.totalFrames ? 0 : next);
};
// A clip that does not loop stops on its last frame.
var advance = MC._advance;
MC._advance = function () {
    if (this._noLoop && this._timeline && this._frame + 1 >= this._timeline.n) {
        this._playing = false;
        return false;
    }
    return advance.call(this);
};
// Text fields: color as CSS.
Object.defineProperty(flash.text.TextField.prototype, "color", {
    get: function () { return "#" + ("000000" + this.textColor.toString(16)).slice(-6); },
    set: function (value) { this.textColor = cssColor(value).rgb; },
    configurable: true
});

/// A CSS color as RGB and alpha.
var colorContext = null;
function cssColor(value) {
    if (typeof value === "number")
        return {rgb: value & 0xffffff, alpha: 1};
    var text = String(value || "#000").trim();
    var m = /^#([0-9a-f]{3,8})$/i.exec(text);
    if (m) {
        var hex = m[1];
        if (hex.length === 3 || hex.length === 4)
            hex = hex.split("").map(function (c) { return c + c; }).join("");
        return {rgb: parseInt(hex.slice(0, 6), 16), alpha: hex.length === 8 ? parseInt(hex.slice(6), 16) / 255 : 1};
    }
    m = /^rgba?\(([^)]*)\)$/i.exec(text);
    if (m) {
        var parts = m[1].split(",").map(function (p) { return parseFloat(p); });
        return {rgb: ((parts[0] & 255) << 16) | ((parts[1] & 255) << 8) | (parts[2] & 255), alpha: parts.length > 3 ? parts[3] : 1};
    }
    if (!colorContext && typeof document !== "undefined")
        colorContext = document.createElement("canvas").getContext("2d");
    if (colorContext) {
        colorContext.fillStyle = "#000";
        colorContext.fillStyle = text;
        if (colorContext.fillStyle !== text && colorContext.fillStyle[0] === "#")
            return cssColor(colorContext.fillStyle);
        if (/^rgba/.test(colorContext.fillStyle))
            return cssColor(colorContext.fillStyle);
    }
    return {rgb: 0, alpha: 1};
}

// Geometry.
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

// EventDispatcher, for scripts' own objects.
function EventDispatcher() {}
var ED = EventDispatcher.prototype;
EventDispatcher.initialize = function (target) {
    ["addEventListener", "on", "removeEventListener", "off", "removeAllEventListeners", "hasEventListener", "dispatchEvent"]
        .forEach(function (name) { target[name] = ED[name]; });
};
ED.addEventListener = function (type, listener) {
    var all = this._own || (Object.defineProperty(this, "_own", {value: {}, writable: true}), this._own);
    var list = all[type] || (all[type] = []);
    if (list.indexOf(listener) < 0)
        list.push(listener);
    return listener;
};
ED.on = function (type, listener, scope, once, data) {
    if (listener && listener.handleEvent) {
        scope = scope || listener;
        listener = listener.handleEvent;
    }
    scope = scope || this;
    return this.addEventListener(type, function (event) {
        listener.call(scope, event, data);
        if (once)
            event.remove();
    });
};
ED.removeEventListener = ED.off = function (type, listener) {
    var list = this._own && this._own[type];
    var at = list ? list.indexOf(listener) : -1;
    if (at >= 0)
        list.splice(at, 1);
};
ED.removeAllEventListeners = function (type) {
    if (!this._own)
        return;
    if (type)
        delete this._own[type];
    else
        this._own = {};
};
ED.hasEventListener = function (type) { return !!(this._own && this._own[type] && this._own[type].length); };
ED.dispatchEvent = function (event) {
    if (typeof event === "string")
        event = new CJSEvent(event);
    event.target = event.currentTarget = this;
    var list = (this._own && this._own[event.type] || []).slice();
    for (var i = 0; i < list.length && !event.immediatePropagationStopped; ++i) {
        var listener = list[i];
        if (typeof listener === "function")
            listener.call(this, event);
        else if (listener && listener.handleEvent)
            listener.handleEvent(event);
        if (event.removed) {
            this.off(event.type, listener);
            event.removed = false;
        }
    }
    return !event.defaultPrevented;
};

// Ticker: the movie's frames.
var Ticker = new EventDispatcher();
var ticks = 0, now = 0;
Ticker.RAF_SYNCHED = "synched";
Ticker.RAF = "raf";
Ticker.TIMEOUT = "timeout";
Ticker.timingMode = Ticker.TIMEOUT;
Ticker.maxDelta = 0;
Ticker.paused = false;
Object.defineProperty(Ticker, "framerate", {
    get: function () { return phoenix.player.stage ? phoenix.player.stage.frameRate : 24; },
    set: function (value) { if (phoenix.player.stage) phoenix.player.stage.frameRate = value; }
});
Object.defineProperty(Ticker, "interval", {
    get: function () { return 1000 / Ticker.framerate; },
    set: function (value) { if (value > 0) Ticker.framerate = 1000 / value; }
});
Ticker.getFPS = function () { return Ticker.framerate; };
Ticker.setFPS = function (value) { Ticker.framerate = value; };
Ticker.getInterval = function () { return Ticker.interval; };
Ticker.setInterval = function (value) { Ticker.interval = value; };
Ticker.getMeasuredFPS = function () { return Ticker.framerate; };
Ticker.getMeasuredTickTime = function () { return 0; };
Ticker.getTime = function () { return now; };
Ticker.getEventTime = Ticker.getTime;
Ticker.getTicks = function () { return ticks; };
Ticker.reset = Ticker.init = function () {};
Ticker.setPaused = function (value) { Ticker.paused = !!value; };
Ticker.getPaused = function () { return Ticker.paused; };

// Tween and Ease, in movie time: a frame's worth of milliseconds each frame.
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
    Object.defineProperty(this, "_steps", {value: [], writable: true});
    Object.defineProperty(this, "_step", {value: 0, writable: true});
    Object.defineProperty(this, "_time", {value: 0, writable: true});
    Object.defineProperty(this, "_paused", {value: false, writable: true});
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
    return this._add({type: "to", props: props || {}, duration: Math.max(0, Number(duration) || 0), ease: ease || Ease.linear});
};
TW.wait = function (duration) { return this._add({type: "wait", duration: Math.max(0, Number(duration) || 0)}); };
TW.call = function (callback, params, scope) {
    return this._add({type: "call", callback: callback, params: params || [this], scope: scope});
};
TW.set = function (props, target) { return this._add({type: "set", props: props || {}, target: target}); };
TW.label = function () { return this; };
TW.setPaused = function (paused) { this.paused = paused; return this; };
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
TW.play = function (tween) { (tween || this).paused = false; return this; };
TW.pause = function (tween) { (tween || this).paused = true; return this; };
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
                target[name] = typeof from === "number" && typeof to === "number" ? from + (to - from) * ratio : (t >= 1 ? to : from);
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
        this._steps.forEach(function (step) { step.from = null; });
        return;
    }
    var at = tweens.indexOf(this);
    if (at >= 0)
        tweens.splice(at, 1);
    this.dispatchEvent("complete");
};
TW.toString = function () { return "[Tween]"; };

// Sound: the library's sounds by their linkage or name.
var Sound = {
    INTERRUPT_ANY: "any", INTERRUPT_EARLY: "early", INTERRUPT_LATE: "late", INTERRUPT_NONE: "none",
    PLAY_SUCCEEDED: "playSucceeded", PLAY_FAILED: "playFailed",
    volume: 1, muted: false,
    play: function (src, interrupt, delay, offset, loop, volume) {
        var options = interrupt && typeof interrupt === "object" ? interrupt : {delay: delay, offset: offset, loop: loop, volume: volume};
        var sounds = phoenix._.librarySounds();
        var index = -1;
        for (var i = 0; i < sounds.length; ++i)
            if (sounds[i].desc.linkage === src || sounds[i].desc.name === src || sounds[i].desc.src === src)
                index = i;
        var loops = options.loop === -1 ? 9999 : (options.loop | 0) + 1;
        var channel = index >= 0 && !Sound.muted ? phoenix._.playLibrarySound(index, loops, options.offset || 0) : null;
        if (channel)
            channel.soundTransform = new flash.media.SoundTransform((options.volume === undefined ? 1 : options.volume) * Sound.volume);
        return {
            src: src, playState: channel ? Sound.PLAY_SUCCEEDED : Sound.PLAY_FAILED,
            stop: function () { if (channel) channel.stop(); return this; },
            paused: false, volume: options.volume === undefined ? 1 : options.volume, loop: options.loop | 0,
            addEventListener: function () {}, on: function () {}, removeEventListener: function () {}, off: function () {}
        };
    },
    stop: function () { phoenix._.stopAllSounds(); },
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

// Drawing from scripts: CreateJS's Graphics, a list of commands (each an
// object a script may keep and change, as .command gives it) drawn into
// the runtime's Graphics; drawn again each frame once a script holds one.
var DEG = Math.PI / 180;
function Matrix2D(a, b, c, d, tx, ty) { this.setValues(a, b, c, d, tx, ty); }
var M2 = Matrix2D.prototype;
M2.setValues = function (a, b, c, d, tx, ty) {
    this.a = a === undefined || a === null ? 1 : a; this.b = b || 0; this.c = c || 0;
    this.d = d === undefined || d === null ? 1 : d; this.tx = tx || 0; this.ty = ty || 0;
    return this;
};
M2.append = function (a, b, c, d, tx, ty) {
    var a1 = this.a, b1 = this.b, c1 = this.c, d1 = this.d;
    if (a !== 1 || b !== 0 || c !== 0 || d !== 1) {
        this.a = a1 * a + c1 * b; this.b = b1 * a + d1 * b;
        this.c = a1 * c + c1 * d; this.d = b1 * c + d1 * d;
    }
    this.tx = a1 * tx + c1 * ty + this.tx; this.ty = b1 * tx + d1 * ty + this.ty;
    return this;
};
M2.prepend = function (a, b, c, d, tx, ty) {
    var a1 = this.a, c1 = this.c, tx1 = this.tx;
    this.a = a * a1 + c * this.b; this.b = b * a1 + d * this.b;
    this.c = a * c1 + c * this.d; this.d = b * c1 + d * this.d;
    this.tx = a * tx1 + c * this.ty + tx; this.ty = b * tx1 + d * this.ty + ty;
    return this;
};
M2.appendMatrix = function (m) { return this.append(m.a, m.b, m.c, m.d, m.tx, m.ty); };
M2.prependMatrix = function (m) { return this.prepend(m.a, m.b, m.c, m.d, m.tx, m.ty); };
M2.appendTransform = function (x, y, scaleX, scaleY, rotation, skewX, skewY, regX, regY) {
    var r = (rotation || 0) % 360 ? rotation * DEG : 0, cos = Math.cos(r), sin = Math.sin(r);
    scaleX = scaleX === undefined ? 1 : scaleX;
    scaleY = scaleY === undefined ? 1 : scaleY;
    if (skewX || skewY) {
        this.append(Math.cos(skewY * DEG), Math.sin(skewY * DEG), -Math.sin(skewX * DEG), Math.cos(skewX * DEG), x, y);
        this.append(cos * scaleX, sin * scaleX, -sin * scaleY, cos * scaleY, 0, 0);
    } else
        this.append(cos * scaleX, sin * scaleX, -sin * scaleY, cos * scaleY, x, y);
    if (regX || regY) {
        this.tx -= regX * this.a + regY * this.c;
        this.ty -= regX * this.b + regY * this.d;
    }
    return this;
};
M2.rotate = function (angle) {
    angle *= DEG;
    var cos = Math.cos(angle), sin = Math.sin(angle);
    return this.append(cos, sin, -sin, cos, 0, 0);
};
M2.scale = function (x, y) { this.a *= x; this.b *= x; this.c *= y; this.d *= y; return this; };
M2.translate = function (x, y) { this.tx += this.a * x + this.c * y; this.ty += this.b * x + this.d * y; return this; };
M2.skew = function (skewX, skewY) {
    return this.append(Math.cos(skewY * DEG), Math.sin(skewY * DEG), -Math.sin(skewX * DEG), Math.cos(skewX * DEG), 0, 0);
};
M2.identity = function () { return this.setValues(); };
M2.isIdentity = function () { return this.a === 1 && !this.b && !this.c && this.d === 1 && !this.tx && !this.ty; };
M2.invert = function () {
    var a1 = this.a, b1 = this.b, c1 = this.c, d1 = this.d, tx1 = this.tx, n = a1 * d1 - b1 * c1;
    this.a = d1 / n; this.b = -b1 / n; this.c = -c1 / n; this.d = a1 / n;
    this.tx = (c1 * this.ty - d1 * tx1) / n; this.ty = -(a1 * this.ty - b1 * tx1) / n;
    return this;
};
M2.transformPoint = function (x, y, pt) {
    pt = pt || new Point();
    pt.x = x * this.a + y * this.c + this.tx;
    pt.y = x * this.b + y * this.d + this.ty;
    return pt;
};
M2.copy = function (m) { return this.setValues(m.a, m.b, m.c, m.d, m.tx, m.ty); };
M2.clone = function () { return new Matrix2D(this.a, this.b, this.c, this.d, this.tx, this.ty); };
M2.equals = function (m) { return this.a === m.a && this.b === m.b && this.c === m.c && this.d === m.d && this.tx === m.tx && this.ty === m.ty; };
M2.toString = function () { return "[Matrix2D (a=" + this.a + " b=" + this.b + " c=" + this.c + " d=" + this.d + " tx=" + this.tx + " ty=" + this.ty + ")]"; };
function flashMatrix(m) {
    return m ? new flash.geom.Matrix(m.a, m.b, m.c, m.d, m.tx, m.ty) : null;
}

/// A picture a script names (an image, a canvas, a BitmapData, a library
/// image's name) as a BitmapData, made once.
function bitmapDataOf(source) {
    if (!source)
        return null;
    if (source instanceof display.BitmapData)
        return source;
    if (typeof source === "string") {
        var index = imageIndex(source);
        return index >= 0 ? phoenix._.libraryBitmapData(index) : null;
    }
    if (source._cjsData)
        return source._cjsData;
    var picture = phoenix._.drawable(source);
    if (!picture || !picture.width)
        return null;
    var data = new display.BitmapData(picture.width, picture.height, true, 0);
    data._ctx().drawImage(picture, 0, 0);
    try {
        Object.defineProperty(source, "_cjsData", {value: data, configurable: true});
    } catch (error) {}
    return data;
}
/// The library image a name stands for: its linkage, its name, its name
/// without the extension, or its file.
function imageIndex(name) {
    var list = phoenix._.library && phoenix._.library.movie.images || [];
    var plain = function (text) { return String(text || "").split("/").pop().replace(/\.[a-z0-9]+$/i, ""); };
    for (var pass = 0; pass < 2; ++pass)
        for (var i = 0; i < list.length; ++i) {
            var desc = list[i];
            if (desc.sheet || desc.in !== undefined)
                continue;
            if (pass === 0 ? desc.linkage === name || desc.name === name || desc.src === name
                    : plain(desc.name) === plain(name) || plain(desc.linkage) === plain(name) || plain(desc.src) === plain(name))
                return i;
        }
    return -1;
}

function Graphics() {
    hidden(this, "_g", new display.Graphics(null));
    hidden(this, "_instructions", []);
    hidden(this, "_command", null);
    hidden(this, "_live", false);
    hidden(this, "_stored", -1);
    hidden(this, "_dirty", false);
    hidden(this, "_shapes", []);
}
var GR = Graphics.prototype;
function hidden(object, name, value) {
    Object.defineProperty(object, name, {value: value, writable: true, configurable: true, enumerable: false});
}
/// What a script holds of the list it may change: the list is drawn again
/// each frame from then on.
Object.defineProperty(GR, "command", {get: function () { liveGraphics(this); return this._command; }});
Object.defineProperty(GR, "instructions", {get: function () { liveGraphics(this); return this._instructions; }});
var live = [];
function liveGraphics(g) {
    if (!g._live) {
        g._live = true;
        live.push(g);
    }
}
GR._append = function (command) {
    this._instructions.push(command);
    this._command = command;
    this._changed();
    return this;
};
GR._changed = function () {
    this._dirty = true;
    this._shapes.forEach(function (shape) { shape._changed(); });
};
GR.append = function (command) { return this._append(command); };
GR.getInstructions = function () { return this.instructions; };
GR.isEmpty = function () { return !this._instructions.length; };
GR.clear = function () { this._instructions.length = 0; this._command = null; this._stored = -1; this._changed(); return this; };
GR.store = function () { this._stored = this._instructions.length; return this; };
GR.unstore = function () { this._stored = -1; return this; };
GR.clone = function () {
    var copy = new Graphics();
    this._instructions.forEach(function (c) { copy._append(Object.assign(Object.create(Object.getPrototypeOf(c)), c)); });
    return copy;
};
GR.inject = function (callback, data) { return this._append({type: "inject", callback: callback, data: data}); };
GR.toString = function () { return "[Graphics]"; };
/// The list drawn into the runtime's Graphics.
GR._draw = function () {
    var g = this._g;
    g.clear();
    var state = {x: 0, y: 0, sx: 0, sy: 0, started: false, stroke: null, width: 1, caps: "butt", joints: "miter",
        miter: 10, ignoreScale: false};
    var line = function () {
        var s = state.stroke;
        if (!s)
            g.lineStyle();
        else if (s.kind === "color") {
            var c = cssColor(s.color);
            g.lineStyle(state.width, c.rgb, c.alpha, false, state.ignoreScale ? "none" : "normal",
                state.caps === "butt" ? "none" : state.caps, state.joints, state.miter);
        } else {
            g.lineStyle(state.width, 0, 1, false, state.ignoreScale ? "none" : "normal",
                state.caps === "butt" ? "none" : state.caps, state.joints, state.miter);
            if (s.kind === "bitmap") {
                var data = bitmapDataOf(s.image);
                if (data)
                    g.lineBitmapStyle(data, flashMatrix(s.matrix), s.repetition !== "no-repeat");
            } else
                gradient(g, s, true);
        }
    };
    var to = function (x, y) {
        if (!state.started) {
            g.moveTo(x, y);
            state.sx = x;
            state.sy = y;
            state.started = true;
        } else
            g.lineTo(x, y);
        state.x = x;
        state.y = y;
    };
    var arc = function (cx, cy, r, from, end, anticlockwise) {
        var sweep = end - from;
        if (anticlockwise ? sweep > 0 : sweep < 0)
            sweep += anticlockwise ? -Math.PI * 2 * Math.ceil(sweep / (Math.PI * 2)) : Math.PI * 2 * Math.ceil(-sweep / (Math.PI * 2));
        if (Math.abs(end - from) >= Math.PI * 2)
            sweep = anticlockwise ? -Math.PI * 2 : Math.PI * 2;
        to(cx + r * Math.cos(from), cy + r * Math.sin(from));
        var parts = Math.max(1, Math.ceil(Math.abs(sweep) / (Math.PI / 2)));
        var step = sweep / parts, k = 4 / 3 * Math.tan(step / 4);
        for (var i = 0; i < parts; ++i) {
            var a0 = from + step * i, a1 = a0 + step;
            var x0 = Math.cos(a0), y0 = Math.sin(a0), x1 = Math.cos(a1), y1 = Math.sin(a1);
            g.cubicCurveTo(cx + r * (x0 - k * y0), cy + r * (y0 + k * x0), cx + r * (x1 + k * y1), cy + r * (y1 - k * x1),
                cx + r * x1, cy + r * y1);
        }
        state.x = cx + r * Math.cos(from + sweep);
        state.y = cy + r * Math.sin(from + sweep);
    };
    var list = this._instructions;
    for (var i = 0; i < list.length; ++i) {
        var c = list[i];
        switch (c.type) {
        case "fill":
            state.started = false;
            if (!c.style)
                g.endFill();
            else if (c.style.kind === "color") {
                var color = cssColor(c.style.color);
                g.beginFill(color.rgb, color.alpha);
            } else if (c.style.kind === "bitmap") {
                var data = bitmapDataOf(c.style.image);
                if (data)
                    g.beginBitmapFill(data, flashMatrix(c.matrix || c.style.matrix), c.style.repetition !== "no-repeat");
            } else
                gradient(g, c.style, false);
            break;
        case "stroke":
            state.stroke = c.style;
            state.ignoreScale = !!c.ignoreScale;
            line();
            break;
        case "strokeStyle":
            state.width = c.width;
            state.caps = c.caps;
            state.joints = c.joints;
            state.miter = c.miterLimit;
            state.ignoreScale = !!c.ignoreScale;
            line();
            break;
        case "strokeDash":
            break;
        case "beginPath":
            state.started = false;
            break;
        case "moveTo":
            g.moveTo(c.x, c.y);
            state.x = state.sx = c.x;
            state.y = state.sy = c.y;
            state.started = true;
            break;
        case "lineTo":
            to(c.x, c.y);
            break;
        case "quadraticCurveTo":
            if (!state.started)
                to(c.cpx, c.cpy);
            g.curveTo(c.cpx, c.cpy, c.x, c.y);
            state.x = c.x;
            state.y = c.y;
            break;
        case "bezierCurveTo":
            if (!state.started)
                to(c.cp1x, c.cp1y);
            g.cubicCurveTo(c.cp1x, c.cp1y, c.cp2x, c.cp2y, c.x, c.y);
            state.x = c.x;
            state.y = c.y;
            break;
        case "arc":
            arc(c.x, c.y, c.radius, c.startAngle, c.endAngle, c.anticlockwise);
            break;
        case "arcTo": {
            // The canvas's arcTo: a line toward the corner, then the arc of
            // the radius touching both of its sides.
            if (!state.started)
                to(c.x1, c.y1);
            var x0 = state.x, y0 = state.y;
            var ax = x0 - c.x1, ay = y0 - c.y1, bx = c.x2 - c.x1, by = c.y2 - c.y1;
            var la = Math.sqrt(ax * ax + ay * ay), lb = Math.sqrt(bx * bx + by * by);
            var cross = ax * by - ay * bx;
            if (!c.radius || !la || !lb || Math.abs(cross) < 1e-9) {
                to(c.x1, c.y1);
                break;
            }
            var angle = Math.acos(Math.max(-1, Math.min(1, (ax * bx + ay * by) / (la * lb))));
            var reach = c.radius / Math.tan(angle / 2);
            var t0x = c.x1 + ax / la * reach, t0y = c.y1 + ay / la * reach;
            var t1x = c.x1 + bx / lb * reach, t1y = c.y1 + by / lb * reach;
            var bisx = ax / la + bx / lb, bisy = ay / la + by / lb, bl = Math.sqrt(bisx * bisx + bisy * bisy);
            var centerDistance = c.radius / Math.sin(angle / 2);
            var cx = c.x1 + bisx / bl * centerDistance, cy = c.y1 + bisy / bl * centerDistance;
            to(t0x, t0y);
            arc(cx, cy, c.radius, Math.atan2(t0y - cy, t0x - cx), Math.atan2(t1y - cy, t1x - cx), cross > 0);
            break;
        }
        case "closePath":
            if (state.started) {
                g.lineTo(state.sx, state.sy);
                state.x = state.sx;
                state.y = state.sy;
            }
            break;
        case "rect":
            g.drawRect(c.x, c.y, c.w, c.h);
            state.started = false;
            break;
        case "roundRect":
            g.drawRoundRectComplex(c.x, c.y, c.w, c.h, c.radiusTL, c.radiusTR, c.radiusBL, c.radiusBR);
            state.started = false;
            break;
        case "circle":
            g.drawCircle(c.x, c.y, c.radius);
            state.started = false;
            break;
        case "ellipse":
            g.drawEllipse(c.x, c.y, c.w, c.h);
            state.started = false;
            break;
        case "polyStar": {
            var sides = Math.max(3, c.sides | 0), turn = (c.angle || 0) * DEG, ps = 1 - (c.pointSize || 0);
            var a = Math.PI / sides;
            g.moveTo(c.x + Math.cos(turn) * c.radius, c.y + Math.sin(turn) * c.radius);
            for (var k = 0; k < sides; ++k) {
                turn += a;
                if (ps !== 1)
                    g.lineTo(c.x + Math.cos(turn) * c.radius * ps, c.y + Math.sin(turn) * c.radius * ps);
                turn += a;
                g.lineTo(c.x + Math.cos(turn) * c.radius, c.y + Math.sin(turn) * c.radius);
            }
            state.started = false;
            break;
        }
        case "inject":
            try {
                c.callback.call(this, c.data);
            } catch (error) {
                phoenix._.reportError(error, "Graphics.inject");
            }
            break;
        }
    }
    this._dirty = false;
};
/// A gradient fill or stroke: CreateJS's two points (and radii) as the
/// runtime's gradient box.
function gradient(g, style, stroke) {
    var parsed = style.colors.map(cssColor);
    var ratios = style.ratios.map(function (r) { return Math.max(0, Math.min(255, r * 255)); });
    var m = new flash.geom.Matrix();
    if (style.kind === "linear") {
        var dx = style.x1 - style.x0, dy = style.y1 - style.y0, length = Math.sqrt(dx * dx + dy * dy) || 1;
        m.createGradientBox(length, length, Math.atan2(dy, dx), (style.x0 + style.x1) / 2 - length / 2,
            (style.y0 + style.y1) / 2 - length / 2);
    } else
        m.createGradientBox(style.r1 * 2, style.r1 * 2, 0, style.x1 - style.r1, style.y1 - style.r1);
    var focal = 0;
    if (style.kind === "radial" && style.r1) {
        var fx = style.x0 - style.x1, fy = style.y0 - style.y1;
        focal = Math.max(-0.99, Math.min(0.99, Math.sqrt(fx * fx + fy * fy) / style.r1));
    }
    var args = [style.kind, parsed.map(function (c) { return c.rgb; }), parsed.map(function (c) { return c.alpha; }), ratios, m,
        "pad", "rgb", focal];
    if (stroke)
        g.lineGradientStyle.apply(g, args);
    else
        g.beginGradientFill.apply(g, args);
}
function command(type, props) {
    var c = Object.assign(Object.create(commandProto), props);
    hidden(c, "type", type);
    return c;
}
var commandProto = {};
var capNames = ["butt", "round", "square"], jointNames = ["miter", "round", "bevel"];
GR.beginFill = function (color) { return this._append(command("fill", {style: color ? {kind: "color", color: color} : null})); };
GR.beginLinearGradientFill = function (colors, ratios, x0, y0, x1, y1) {
    return this._append(command("fill", {style: {kind: "linear", colors: colors, ratios: ratios, x0: x0, y0: y0, x1: x1, y1: y1}}));
};
GR.beginRadialGradientFill = function (colors, ratios, x0, y0, r0, x1, y1, r1) {
    return this._append(command("fill", {style: {kind: "radial", colors: colors, ratios: ratios, x0: x0, y0: y0, r0: r0,
        x1: x1, y1: y1, r1: r1}}));
};
GR.beginBitmapFill = function (image, repetition, matrix) {
    return this._append(command("fill", {style: {kind: "bitmap", image: image, repetition: repetition || "repeat"}, matrix: matrix}));
};
GR.endFill = function () { return this.beginFill(); };
GR.beginStroke = function (color) {
    return this._append(command("stroke", {style: color ? {kind: "color", color: color} : null}));
};
GR.beginLinearGradientStroke = function (colors, ratios, x0, y0, x1, y1) {
    return this._append(command("stroke", {style: {kind: "linear", colors: colors, ratios: ratios, x0: x0, y0: y0, x1: x1, y1: y1}}));
};
GR.beginRadialGradientStroke = function (colors, ratios, x0, y0, r0, x1, y1, r1) {
    return this._append(command("stroke", {style: {kind: "radial", colors: colors, ratios: ratios, x0: x0, y0: y0, r0: r0,
        x1: x1, y1: y1, r1: r1}}));
};
GR.beginBitmapStroke = function (image, repetition) {
    return this._append(command("stroke", {style: {kind: "bitmap", image: image, repetition: repetition || "repeat"}}));
};
GR.endStroke = function () { return this.beginStroke(); };
GR.setStrokeStyle = function (thickness, caps, joints, miterLimit, ignoreScale) {
    return this._append(command("strokeStyle", {width: thickness === undefined ? 1 : thickness,
        caps: typeof caps === "number" ? capNames[caps] : caps || "butt",
        joints: typeof joints === "number" ? jointNames[joints] : joints || "miter",
        miterLimit: miterLimit || 10, ignoreScale: !!ignoreScale}));
};
// Dashes are kept; the line is drawn solid.
GR.setStrokeDash = function (segments, offset) { return this._append(command("strokeDash", {segments: segments, offset: offset || 0})); };
GR.beginPath = function () { return this._append(command("beginPath", {})); };
GR.moveTo = function (x, y) { return this._append(command("moveTo", {x: x, y: y})); };
GR.lineTo = function (x, y) { return this._append(command("lineTo", {x: x, y: y})); };
GR.quadraticCurveTo = GR.curveTo = function (cpx, cpy, x, y) {
    return this._append(command("quadraticCurveTo", {cpx: cpx, cpy: cpy, x: x, y: y}));
};
GR.bezierCurveTo = function (cp1x, cp1y, cp2x, cp2y, x, y) {
    return this._append(command("bezierCurveTo", {cp1x: cp1x, cp1y: cp1y, cp2x: cp2x, cp2y: cp2y, x: x, y: y}));
};
GR.arc = function (x, y, radius, startAngle, endAngle, anticlockwise) {
    return this._append(command("arc", {x: x, y: y, radius: radius, startAngle: startAngle, endAngle: endAngle,
        anticlockwise: !!anticlockwise}));
};
GR.arcTo = function (x1, y1, x2, y2, radius) {
    return this._append(command("arcTo", {x1: x1, y1: y1, x2: x2, y2: y2, radius: radius}));
};
GR.closePath = function () { return this._append(command("closePath", {})); };
GR.rect = GR.drawRect = function (x, y, w, h) { return this._append(command("rect", {x: x, y: y, w: w, h: h})); };
GR.drawRoundRect = function (x, y, w, h, radius) { return this.drawRoundRectComplex(x, y, w, h, radius, radius, radius, radius); };
GR.drawRoundRectComplex = function (x, y, w, h, radiusTL, radiusTR, radiusBR, radiusBL) {
    return this._append(command("roundRect", {x: x, y: y, w: w, h: h, radiusTL: radiusTL, radiusTR: radiusTR,
        radiusBR: radiusBR, radiusBL: radiusBL}));
};
GR.drawCircle = function (x, y, radius) { return this._append(command("circle", {x: x, y: y, radius: radius})); };
GR.drawEllipse = function (x, y, w, h) { return this._append(command("ellipse", {x: x, y: y, w: w, h: h})); };
GR.drawPolyStar = function (x, y, radius, sides, pointSize, angle) {
    return this._append(command("polyStar", {x: x, y: y, radius: radius, sides: sides, pointSize: pointSize || 0, angle: angle || 0}));
};
/// Paths in CreateJS's compact encoding (Graphics.decodePath).
var base64 = {};
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".split("").forEach(function (c, i) { base64[c] = i; });
GR.decodePath = function (text) {
    var ops = [this.moveTo, this.lineTo, this.quadraticCurveTo, this.bezierCurveTo, this.closePath];
    var counts = [2, 2, 4, 6, 0];
    var i = 0, x = 0, y = 0;
    text = String(text);
    while (i < text.length) {
        var n = base64[text.charAt(i)];
        var fi = n >> 3, op = ops[fi];
        if (!op || (n & 3))
            throw new Error("bad path data (@" + i + "): " + text.charAt(i));
        if (!fi)
            x = y = 0;
        ++i;
        var size = (n >> 2 & 1) + 2, params = [];
        for (var p = 0; p < counts[fi]; ++p) {
            var num = base64[text.charAt(i)];
            var sign = num >> 5 ? -1 : 1;
            num = ((num & 31) << 6) | base64[text.charAt(i + 1)];
            if (size === 3)
                num = (num << 6) | base64[text.charAt(i + 2)];
            num = sign * num / 10;
            if (p % 2)
                y = (num += y);
            else
                x = (num += x);
            params.push(num);
            i += size;
        }
        op.apply(this, params);
    }
    return this;
};
GR.f = GR.beginFill; GR.lf = GR.beginLinearGradientFill; GR.rf = GR.beginRadialGradientFill; GR.bf = GR.beginBitmapFill;
GR.ef = GR.endFill; GR.s = GR.beginStroke; GR.ls = GR.beginLinearGradientStroke; GR.rs = GR.beginRadialGradientStroke;
GR.bs = GR.beginBitmapStroke; GR.es = GR.endStroke; GR.ss = GR.setStrokeStyle; GR.sd = GR.setStrokeDash;
GR.mt = GR.moveTo; GR.lt = GR.lineTo; GR.qt = GR.quadraticCurveTo; GR.bt = GR.bezierCurveTo; GR.a = GR.arc;
GR.at = GR.arcTo; GR.cp = GR.closePath; GR.bp = GR.beginPath; GR.r = GR.rect; GR.dr = GR.drawRect;
GR.rr = GR.drawRoundRect; GR.rc = GR.drawRoundRectComplex; GR.dc = GR.drawCircle; GR.de = GR.drawEllipse;
GR.dp = GR.drawPolyStar; GR.p = GR.decodePath;
Graphics.getRGB = function (r, g, b, alpha) {
    if (r !== undefined && b === undefined) {
        alpha = g;
        b = r & 0xff;
        g = r >> 8 & 0xff;
        r = r >> 16 & 0xff;
    }
    return alpha === undefined || alpha === null ? "rgb(" + r + "," + g + "," + b + ")" : "rgba(" + r + "," + g + "," + b + "," + alpha + ")";
};
Graphics.getHSL = function (hue, saturation, lightness, alpha) {
    return alpha === undefined || alpha === null ? "hsl(" + hue % 360 + "," + saturation + "%," + lightness + "%)"
        : "hsla(" + hue % 360 + "," + saturation + "%," + lightness + "%," + alpha + ")";
};
Graphics.BASE_64 = base64;
Graphics.STROKE_CAPS_MAP = capNames;
Graphics.STROKE_JOINTS_MAP = jointNames;
function Shape(graphics) {
    var shape = new display.Shape();
    hidden(shape, "_cjsGraphics", null);
    // The list drawn when the shape is next drawn, measured or hit.
    var sync = function () {
        var g = shape._cjsGraphics;
        if (g && g._dirty)
            g._draw();
        shape._graphics = g ? g._g : null;
    };
    ["_draw", "_contentBounds", "_hitContent"].forEach(function (name) {
        var base = shape[name];
        if (typeof base === "function")
            hidden(shape, name, function () { sync(); return base.apply(shape, arguments); });
    });
    Object.defineProperty(shape, "graphics", {
        get: function () { return shape._cjsGraphics; },
        set: function (value) {
            var g = value || new Graphics();
            shape._cjsGraphics = g;
            if (g._shapes.indexOf(shape) < 0)
                g._shapes.push(shape);
            g._dirty = true;
            shape._changed();
        },
        configurable: true
    });
    shape.graphics = graphics;
    return shape;
}

function Container() { return new display.Sprite(); }

// Text: the runtime's text field set out as CreateJS sets text: from its
// point by textAlign and textBaseline, wrapped at lineWidth.
var TF = flash.text.TextField.prototype;
var fieldText = Object.getOwnPropertyDescriptor(TF, "text");
function fontOf(font) {
    var format = new flash.text.TextFormat();
    var m = /(italic\s+|oblique\s+)?(normal\s+)?(bold\s+|[6-9]00\s+)?([\d.]+)px\s+(.+)/i.exec(font || "10px sans-serif");
    format.size = m ? parseFloat(m[4]) : 10;
    format.italic = !!(m && m[1]);
    format.bold = !!(m && m[3]);
    var family = m ? m[5].split(",")[0].replace(/["']/g, "").trim() : "sans-serif";
    format.font = family === "sans-serif" ? "_sans" : family === "serif" ? "_serif" : family === "monospace" ? "_typewriter" : family;
    return format;
}
function layText(field) {
    var t = field._cjsText;
    var format = fontOf(t.font);
    format.color = cssColor(t.color).rgb;
    format.align = t.textAlign === "center" ? "center" : t.textAlign === "right" || t.textAlign === "end" ? "right" : "left";
    format.leading = t.lineHeight ? Math.max(0, t.lineHeight - format.size * 1.15) : 0;
    field.wordWrap = !!t.lineWidth;
    field.multiline = true;
    field.defaultTextFormat = format;
    field.setTextFormat(format);
    if (t.lineWidth)
        field._box[2] = t.lineWidth + 4;
    field._layout = null;
    field._lines();
    var w = field._box[2], h = field._box[3];
    field._box[0] = format.align === "center" ? -w / 2 : format.align === "right" ? -w + 2 : -2;
    var first = field._layout.lines[0];
    var ascent = first ? first.ascent : format.size * 0.8;
    field._box[1] = t.textBaseline === "alphabetic" ? -2 - ascent : t.textBaseline === "middle" ? -h / 2
        : t.textBaseline === "bottom" || t.textBaseline === "ideographic" ? -h + 2 : -2;
    field._changed();
}
function Text(text, font, color) {
    var field = new flash.text.TextField();
    field.selectable = false;
    field.autoSize = "left";
    hidden(field, "_cjsText", {font: font || "10px sans-serif", color: color || "#000", textAlign: "left",
        textBaseline: "top", lineWidth: null, lineHeight: 0, maxWidth: null, outline: 0});
    ["font", "color", "textAlign", "textBaseline", "lineWidth", "lineHeight", "maxWidth", "outline"].forEach(function (name) {
        Object.defineProperty(field, name, {
            get: function () { return field._cjsText[name]; },
            set: function (value) { field._cjsText[name] = value; layText(field); },
            configurable: true
        });
    });
    Object.defineProperty(field, "text", {
        get: function () { return fieldText.get.call(field); },
        set: function (value) {
            fieldText.set.call(field, value === undefined || value === null ? "" : String(value));
            layText(field);
        },
        configurable: true
    });
    field.text = text;
    field.getMeasuredWidth = function () { return field.textWidth; };
    field.getMeasuredHeight = function () { return field.textHeight; };
    field.getMeasuredLineHeight = function () {
        return field._cjsText.lineHeight || fontOf(field._cjsText.font).size * 1.2;
    };
    field.getMetrics = function () {
        var lines = field._lines().lines;
        return {width: field.textWidth, height: field.textHeight, lineHeight: field.getMeasuredLineHeight(),
            lines: lines.map(function (l) { return field.text.slice(l.start, l.end); }), vOffset: 0};
    };
    return field;
}

/// A Bitmap of a picture (an image, a canvas, a library image's name), a
/// part of it when its sourceRect says.
function Bitmap(source) {
    var whole = bitmapDataOf(source);
    var bitmap = new display.Bitmap(whole);
    var rect = null;
    Object.defineProperty(bitmap, "image", {
        get: function () { return source; },
        set: function (value) { source = value; whole = bitmapDataOf(value); bitmap.sourceRect = rect; },
        configurable: true
    });
    Object.defineProperty(bitmap, "sourceRect", {
        get: function () { return rect; },
        set: function (value) {
            rect = value || null;
            if (!rect || !whole) {
                bitmap.bitmapData = whole;
                return;
            }
            var part = new display.BitmapData(Math.max(1, Math.round(rect.width)), Math.max(1, Math.round(rect.height)), true, 0);
            part._ctx().drawImage(whole._canvasForDraw(), rect.x, rect.y, rect.width, rect.height, 0, 0, rect.width, rect.height);
            bitmap.bitmapData = part;
        },
        configurable: true
    });
    return bitmap;
}

// Sprite sheets and sprites: frames cut from pictures, played as their
// animations say.
function SpriteSheet(data) {
    data = data || {};
    this.framerate = data.framerate || 0;
    this.complete = true;
    var pictures = (data.images || []).map(function (image) { return typeof image === "string" ? image : image; });
    var frames = [];
    if (Array.isArray(data.frames))
        data.frames.forEach(function (f) {
            frames.push({image: pictures[f[4] || 0], rect: new Rectangle(f[0], f[1], f[2], f[3]), regX: f[5] || 0, regY: f[6] || 0});
        });
    else if (data.frames) {
        var o = data.frames, w = o.width, h = o.height, spacing = o.spacing || 0, margin = o.margin || 0;
        var count = o.count || Infinity;
        pictures.forEach(function (image) {
            var picture = bitmapDataOf(image);
            if (!picture || !w || !h)
                return;
            for (var y = margin; y + h <= picture.height - margin + 0.01 && frames.length < count; y += h + spacing)
                for (var x = margin; x + w <= picture.width - margin + 0.01 && frames.length < count; x += w + spacing)
                    frames.push({image: image, rect: new Rectangle(x, y, w, h), regX: o.regX || 0, regY: o.regY || 0});
        });
    }
    this._frames = frames;
    var animations = {};
    Object.keys(data.animations || {}).forEach(function (name) {
        var a = data.animations[name], anim = {name: name, speed: 1, next: name};
        if (typeof a === "number")
            anim.frames = [a];
        else if (Array.isArray(a)) {
            anim.frames = [];
            for (var i = a[0]; i <= (a[1] === undefined ? a[0] : a[1]); ++i)
                anim.frames.push(i);
            if (a.length > 2)
                anim.next = a[2];
            if (a[3])
                anim.speed = a[3];
        } else {
            anim.frames = (a.frames || []).slice();
            if (a.next !== undefined)
                anim.next = a.next;
            if (a.speed)
                anim.speed = a.speed;
        }
        if (anim.frames.length < 2 && anim.next === name)
            anim.next = false;
        animations[name] = anim;
    });
    this._animations = animations;
    this.animations = Object.keys(animations);
    hidden(this, "_cut", []);
}
SpriteSheet.prototype = Object.create(ED);
SpriteSheet.prototype.constructor = SpriteSheet;
SpriteSheet.prototype.getNumFrames = function (animation) {
    if (animation === undefined || animation === null)
        return this._frames.length;
    var a = this._animations[animation];
    return a ? a.frames.length : 0;
};
SpriteSheet.prototype.getAnimation = function (name) { return this._animations[name] || null; };
SpriteSheet.prototype.getFrame = function (index) { return this._frames[index] || null; };
SpriteSheet.prototype.getFrameBounds = function (index) {
    var f = this._frames[index];
    return f ? new Rectangle(-f.regX, -f.regY, f.rect.width, f.rect.height) : null;
};
/// Frame \a index's picture, cut once.
SpriteSheet.prototype._data = function (index) {
    if (this._cut[index])
        return this._cut[index];
    var f = this._frames[index], whole = f && bitmapDataOf(f.image);
    if (!whole)
        return null;
    var part = new display.BitmapData(Math.max(1, f.rect.width), Math.max(1, f.rect.height), true, 0);
    part._ctx().drawImage(whole._canvasForDraw(), f.rect.x, f.rect.y, f.rect.width, f.rect.height, 0, 0, f.rect.width, f.rect.height);
    return (this._cut[index] = part);
};
var sprites = new Set();
function Sprite(spriteSheet, frameOrAnimation) {
    var sprite = new display.Sprite();
    var picture = new display.Bitmap(null);
    picture.smoothing = true;
    sprite.addChild(picture);
    var at = 0, animation = null, position = 0;
    sprite.spriteSheet = spriteSheet;
    sprite.paused = true;
    sprite.framerate = 0;
    var show = function () {
        var index = animation ? animation.frames[Math.min(position | 0, animation.frames.length - 1)] : at;
        at = index;
        var f = spriteSheet.getFrame(index);
        picture.bitmapData = f ? spriteSheet._data(index) : null;
        picture.x = f ? -f.regX : 0;
        picture.y = f ? -f.regY : 0;
    };
    var go = function (target) {
        if (typeof target === "number") {
            animation = null;
            at = target;
        } else {
            animation = spriteSheet.getAnimation(target);
            if (!animation)
                return;
        }
        position = 0;
        show();
    };
    Object.defineProperty(sprite, "currentFrame", {get: function () { return at; }, configurable: true});
    Object.defineProperty(sprite, "currentAnimation", {get: function () { return animation ? animation.name : null; }, configurable: true});
    Object.defineProperty(sprite, "currentAnimationFrame", {get: function () { return position | 0; }, configurable: true});
    sprite.play = function () { sprite.paused = false; };
    sprite.stop = function () { sprite.paused = true; };
    sprite.gotoAndPlay = function (target) { sprite.paused = false; go(target); };
    sprite.gotoAndStop = function (target) { sprite.paused = true; go(target); };
    sprite.advance = function (time) {
        var stageRate = phoenix.player.stage ? phoenix.player.stage.frameRate : 24;
        var rate = sprite.framerate || spriteSheet.framerate;
        var step = (rate ? (time ? time / (1000 / rate) : rate / stageRate) : 1) * (animation ? animation.speed : 1);
        if (!animation) {
            at = (at + step) % Math.max(1, spriteSheet.getNumFrames());
            show();
            return;
        }
        position += step;
        if (position >= animation.frames.length) {
            var ended = animation.name, next = animation.next;
            sprite.dispatchEvent(new flash.events.Event("animationend"));
            if (next) {
                if (animation.name === ended && animation.name === next)
                    position -= animation.frames.length;
                else
                    go(next);
            } else {
                sprite.paused = true;
                position = animation.frames.length - 1;
            }
        }
        show();
    };
    sprite._cjsTick = function () {
        if (!sprite.paused)
            sprite.advance();
    };
    sprites.add(sprite);
    if (frameOrAnimation !== undefined && frameOrAnimation !== null)
        sprite.gotoAndPlay(frameOrAnimation);
    else
        show();
    return sprite;
}

// A movie clip a script makes: its timeline is the tweens added to it,
// counted in frames, which it plays as the movie's frames go by.
var madeClips = new Set();
function MovieClip(mode, startPosition, loop, labels) {
    var props = mode && typeof mode === "object" ? mode : {mode: mode, startPosition: startPosition, loop: loop, labels: labels};
    var clip = new display.MovieClip();
    var tweensOf = [], labelList = [];
    var position = props.startPosition || 0;
    clip.mode = props.mode || "independent";
    clip.startPosition = props.startPosition || 0;
    clip.loop = props.loop === undefined || props.loop === null ? true : props.loop;
    var playing = !props.paused;
    var timeline = {
        get duration() {
            return tweensOf.reduce(function (most, t) { return Math.max(most, t.duration); }, 1);
        },
        get position() { return position; },
        tweens: tweensOf,
        addTween: function () {
            for (var i = 0; i < arguments.length; ++i) {
                var tween = arguments[i];
                tween.paused = true;
                tweensOf.push(tween);
                if (tween.target && tween.target._isDisplayObject && tween.target.parent !== clip)
                    clip.addChild(tween.target);
            }
            seek(position);
            return arguments[0];
        },
        removeTween: function (tween) {
            var at = tweensOf.indexOf(tween);
            if (at >= 0)
                tweensOf.splice(at, 1);
            return at >= 0;
        },
        addLabel: function (label, at) { labelList.push({label: label, position: at}); },
        setLabels: function (o) { labelList = Object.keys(o || {}).map(function (k) { return {label: k, position: o[k]}; }); },
        getLabels: function () { return labelList.slice(); },
        resolve: function (target) {
            if (typeof target === "number")
                return target;
            var found = labelList.filter(function (l) { return l.label === target; })[0];
            return found ? found.position : null;
        }
    };
    var seek = function (at) {
        position = at;
        tweensOf.forEach(function (tween) { tween.setPosition(at); });
    };
    Object.defineProperty(clip, "timeline", {value: timeline, configurable: true});
    Object.defineProperty(clip, "currentFrame", {get: function () { return position; }, configurable: true});
    Object.defineProperty(clip, "totalFrames", {get: function () { return timeline.duration; }, configurable: true});
    Object.defineProperty(clip, "duration", {get: function () { return timeline.duration; }, configurable: true});
    Object.defineProperty(clip, "paused", {
        get: function () { return !playing; }, set: function (value) { playing = !value; }, configurable: true
    });
    Object.defineProperty(clip, "labels", {get: function () { return timeline.getLabels(); }, configurable: true});
    clip.play = function () { playing = true; };
    clip.stop = function () { playing = false; };
    clip.gotoAndPlay = function (target) {
        var at = timeline.resolve(target);
        if (at !== null) {
            playing = true;
            seek(at);
        }
    };
    clip.gotoAndStop = function (target) {
        var at = timeline.resolve(target);
        if (at !== null) {
            playing = false;
            seek(at);
        }
    };
    clip.advance = function () {
        var next = position + 1;
        if (next >= timeline.duration) {
            if (!clip.loop) {
                playing = false;
                return;
            }
            next = 0;
        }
        seek(next);
    };
    clip._cjsTick = function () {
        if (playing && clip.stage)
            clip.advance();
    };
    madeClips.add(clip);
    return clip;
}

// Filters, which CreateJS shows once the object is cached; shadows, which
// it shows always. Both become the runtime's filters.
function BlurFilter(blurX, blurY, quality) {
    this.blurX = blurX || 0;
    this.blurY = blurY || 0;
    this.quality = quality || 1;
}
BlurFilter.prototype._flash = function () {
    return new flash.filters.BlurFilter(this.blurX * 2, this.blurY * 2, Math.max(1, Math.min(3, this.quality | 0)));
};
function ColorMatrixFilter(matrix) { this.matrix = matrix; }
ColorMatrixFilter.prototype._flash = function () {
    var m = this.matrix && this.matrix.toArray ? this.matrix.toArray() : (this.matrix || []).slice();
    return new flash.filters.ColorMatrixFilter(m.slice(0, 20));
};
function ColorFilter(redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier, redOffset, greenOffset, blueOffset,
    alphaOffset) {
    var one = function (v) { return v === undefined || v === null ? 1 : v; };
    this.redMultiplier = one(redMultiplier); this.greenMultiplier = one(greenMultiplier);
    this.blueMultiplier = one(blueMultiplier); this.alphaMultiplier = one(alphaMultiplier);
    this.redOffset = redOffset || 0; this.greenOffset = greenOffset || 0;
    this.blueOffset = blueOffset || 0; this.alphaOffset = alphaOffset || 0;
}
ColorFilter.prototype._flash = function () {
    return new flash.filters.ColorMatrixFilter([this.redMultiplier, 0, 0, 0, this.redOffset, 0, this.greenMultiplier, 0, 0,
        this.greenOffset, 0, 0, this.blueMultiplier, 0, this.blueOffset, 0, 0, 0, this.alphaMultiplier, this.alphaOffset]);
};
/// CreateJS's ColorMatrix: brightness, contrast, saturation and hue as a
/// matrix of five rows of five.
function ColorMatrix(brightness, contrast, saturation, hue) {
    this.setColor(brightness, contrast, saturation, hue);
}
var CM = ColorMatrix.prototype;
ColorMatrix.IDENTITY = [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1];
CM.reset = function () {
    this.length = 25;
    for (var i = 0; i < 25; ++i)
        this[i] = ColorMatrix.IDENTITY[i];
    return this;
};
CM.setColor = function (brightness, contrast, saturation, hue) {
    return this.reset().adjustColor(brightness, contrast, saturation, hue);
};
CM.adjustColor = function (brightness, contrast, saturation, hue) {
    return this.adjustHue(hue).adjustContrast(contrast).adjustBrightness(brightness).adjustSaturation(saturation);
};
CM.toArray = function () { var out = []; for (var i = 0; i < 25; ++i) out.push(this[i]); return out; };
CM._multiply = function (m) {
    var out = [];
    for (var i = 0; i < 5; ++i)
        for (var j = 0; j < 5; ++j) {
            var sum = 0;
            for (var k = 0; k < 5; ++k)
                sum += m[j + k * 5] * this[k + i * 5];
            out[j + i * 5] = sum;
        }
    for (var n = 0; n < 25; ++n)
        this[n] = out[n];
    return this;
};
CM.concat = function (m) {
    var a = m && m.toArray ? m.toArray() : m;
    if (a && a.length === 20)
        a = a.concat([0, 0, 0, 0, 1]);
    return a && a.length === 25 ? this._multiply(a) : this;
};
CM.adjustBrightness = function (value) {
    value = Math.max(-255, Math.min(255, value || 0));
    return value ? this._multiply([1, 0, 0, 0, value, 0, 1, 0, 0, value, 0, 0, 1, 0, value, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1]) : this;
};
CM.adjustContrast = function (value) {
    value = Math.max(-100, Math.min(100, value || 0));
    if (!value)
        return this;
    // CreateJS's own table rises steeply toward 100; this follows it.
    var x = value < 0 ? 127 + value / 100 * 127 : 127 + Math.pow(value / 100, 2) * 9 * 127 + value / 100 * 127;
    return this._multiply([x / 127, 0, 0, 0, 0.5 * (127 - x), 0, x / 127, 0, 0, 0.5 * (127 - x), 0, 0, x / 127, 0,
        0.5 * (127 - x), 0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
};
CM.adjustSaturation = function (value) {
    value = Math.max(-100, Math.min(100, value || 0));
    if (!value)
        return this;
    var x = 1 + (value > 0 ? 3 * value / 100 : value / 100);
    var r = 0.3086, g = 0.6094, b = 0.082;
    return this._multiply([r * (1 - x) + x, g * (1 - x), b * (1 - x), 0, 0, r * (1 - x), g * (1 - x) + x, b * (1 - x), 0, 0,
        r * (1 - x), g * (1 - x), b * (1 - x) + x, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
};
CM.adjustHue = function (value) {
    value = Math.max(-180, Math.min(180, value || 0)) / 180 * Math.PI;
    if (!value)
        return this;
    var cos = Math.cos(value), sin = Math.sin(value), r = 0.213, g = 0.715, b = 0.072;
    return this._multiply([r + cos * (1 - r) + sin * -r, g + cos * -g + sin * -g, b + cos * -b + sin * (1 - b), 0, 0,
        r + cos * -r + sin * 0.143, g + cos * (1 - g) + sin * 0.14, b + cos * -b + sin * -0.283, 0, 0,
        r + cos * -r + sin * -(1 - r), g + cos * -g + sin * g, b + cos * (1 - b) + sin * b, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
};
CM.clone = function () { var copy = new ColorMatrix(); for (var i = 0; i < 25; ++i) copy[i] = this[i]; return copy; };
CM.toString = function () { return "[ColorMatrix]"; };
function Shadow(color, offsetX, offsetY, blur) {
    this.color = color || "black";
    this.offsetX = offsetX || 0;
    this.offsetY = offsetY || 0;
    this.blur = blur || 0;
}
Shadow.identity = new Shadow("transparent", 0, 0, 0);
Shadow.prototype.clone = function () { return new Shadow(this.color, this.offsetX, this.offsetY, this.blur); };
Shadow.prototype.toString = function () { return "[Shadow]"; };
Shadow.prototype._flash = function () {
    var c = cssColor(this.color);
    return new flash.filters.DropShadowFilter(Math.sqrt(this.offsetX * this.offsetX + this.offsetY * this.offsetY),
        Math.atan2(this.offsetY, this.offsetX) / DEG, c.rgb, c.alpha, this.blur, this.blur, 1, 2);
};
/// The runtime's filters for what CreateJS shows: the cached object's
/// filters, then its shadow.
function syncFilters(object) {
    var list = object._cjsCached ? (object._cjsFilters || []) : [];
    var made = list.map(function (f) { return f && f._flash ? f._flash() : f; }).filter(function (f) { return !!f; });
    if (object._cjsShadow && object._cjsShadow !== Shadow.identity)
        made.push(object._cjsShadow._flash());
    object._filters = made;
    object._changed();
}

/// Button behavior from a clip's frames: out, over and down labels.
function ButtonHelper(target, outLabel, overLabel, downLabel, play, hitArea, hitLabel) {
    outLabel = outLabel || "out";
    overLabel = overLabel || "over";
    downLabel = downLabel || "down";
    var over = false, down = false;
    var go = function () {
        var label = down ? downLabel : over ? overLabel : outLabel;
        if (play)
            target.gotoAndPlay(label);
        else
            target.gotoAndStop(label);
    };
    target.cursor = "pointer";
    target.addEventListener("rollover", function () { over = true; go(); });
    target.addEventListener("rollout", function () { over = false; go(); });
    target.addEventListener("mousedown", function () { down = true; go(); });
    target.addEventListener("pressup", function () { down = false; go(); });
    if (hitArea)
        target.hitArea = hitArea;
    go();
    this.target = target;
    this.enabled = true;
}

// What CreateJS's display objects have besides: filters shown once cached,
// a shadow, a mask in the space of what it masks, the canvas's composite
// operations, skew and a registration point.
var dop = function (name, get, set) {
    Object.defineProperty(DO, name, {get: get, set: set, configurable: true});
};
dop("filters", function () { return this._cjsFilters || null; },
    function (value) { this._cjsFilters = value ? Array.prototype.slice.call(value) : null; syncFilters(this); });
dop("shadow", function () { return this._cjsShadow || null; },
    function (value) { this._cjsShadow = value || null; syncFilters(this); });
dop("cacheCanvas", function () { return this._cjsCached ? {width: this._cjsCache[2], height: this._cjsCache[3]} : null; },
    function () {});
dop("bitmapCache", function () { return this._cjsCached ? this._cjsCache : null; }, function () {});
Object.defineProperty(DO, "cache", {value: function (x, y, width, height, scale) {
    this._cjsCached = true;
    this._cjsCache = [x || 0, y || 0, width || 0, height || 0, scale || 1];
    syncFilters(this);
}, writable: true, configurable: true});
Object.defineProperty(DO, "updateCache", {value: function () { syncFilters(this); }, writable: true, configurable: true});
Object.defineProperty(DO, "uncache", {value: function () { this._cjsCached = false; syncFilters(this); }, writable: true,
    configurable: true});
Object.defineProperty(DO, "getCacheDataURL", {value: function () { return null; }, writable: true, configurable: true});
var maskDescriptor = Object.getOwnPropertyDescriptor(DO, "mask");
dop("mask", function () { return maskDescriptor.get.call(this); }, function (value) {
    if (value && !value._parent)
        value._maskInOwnerSpace = true;
    maskDescriptor.set.call(this, value || null);
});
var blendNames = {"source-over": "normal", lighter: "add", "destination-out": "erase", "destination-in": "alpha",
    "hard-light": "hardlight", multiply: "multiply", screen: "screen", darken: "darken", lighten: "lighten",
    difference: "difference", overlay: "overlay"};
dop("compositeOperation", function () { return this._cjsComposite || null; }, function (value) {
    this._cjsComposite = value || null;
    this.blendMode = value ? blendNames[value] || String(value) : "normal";
});
// Skew turns one axis of the object against the other, in degrees.
dop("skewX", function () { return this._cjsSkewX || 0; }, function (value) {
    var parts = Object.assign({}, this._parts());
    parts.ry += ((+value || 0) - (this._cjsSkewX || 0)) * DEG;
    this._cjsSkewX = +value || 0;
    this._compose(parts);
});
dop("skewY", function () { return this._cjsSkewY || 0; }, function (value) {
    var parts = Object.assign({}, this._parts());
    parts.rx += ((+value || 0) - (this._cjsSkewY || 0)) * DEG;
    this._cjsSkewY = +value || 0;
    this._compose(parts);
});
// The registration point: the point of the object's own space that sits at
// its x and y, about which it turns and scales.
function regShift(object) {
    var r = object._cjsReg, m = object._m;
    return r ? [r[0] * m[0] + r[1] * m[2], r[0] * m[1] + r[1] * m[3]] : [0, 0];
}
var xDescriptor = Object.getOwnPropertyDescriptor(DO, "x"), yDescriptor = Object.getOwnPropertyDescriptor(DO, "y");
dop("x", function () { return xDescriptor.get.call(this) + regShift(this)[0]; },
    function (value) { xDescriptor.set.call(this, +value - regShift(this)[0]); });
dop("y", function () { return yDescriptor.get.call(this) + regShift(this)[1]; },
    function (value) { yDescriptor.set.call(this, +value - regShift(this)[1]); });
var compose = DO._compose;
DO._compose = function (parts) {
    if (!this._cjsReg)
        return compose.call(this, parts);
    var x = this.x, y = this.y;
    compose.call(this, parts);
    this.x = x;
    this.y = y;
};
var setReg = function (object, rx, ry) {
    var x = object.x, y = object.y;
    object._cjsReg = rx || ry ? [rx, ry] : null;
    object.x = x;
    object.y = y;
};
dop("regX", function () { return this._cjsReg ? this._cjsReg[0] : 0; },
    function (value) { setReg(this, +value || 0, this._cjsReg ? this._cjsReg[1] : 0); });
dop("regY", function () { return this._cjsReg ? this._cjsReg[1] : 0; },
    function (value) { setReg(this, this._cjsReg ? this._cjsReg[0] : 0, +value || 0); });
Object.defineProperty(DO, "setTransform", {value: function (x, y, scaleX, scaleY, rotation, skewX, skewY, regX, regY) {
    this.regX = regX || 0;
    this.regY = regY || 0;
    this.scaleX = scaleX === undefined || scaleX === null ? 1 : scaleX;
    this.scaleY = scaleY === undefined || scaleY === null ? 1 : scaleY;
    this.rotation = rotation || 0;
    this.skewX = skewX || 0;
    this.skewY = skewY || 0;
    this.x = x || 0;
    this.y = y || 0;
    return this;
}, writable: true, configurable: true});
Object.defineProperty(DO, "getMatrix", {value: function (matrix) {
    var m = this._m;
    return (matrix || new Matrix2D()).setValues(m[0], m[1], m[2], m[3], m[4], m[5]);
}, writable: true, configurable: true});
Object.defineProperty(DO, "getConcatenatedMatrix", {value: function (matrix) {
    var m = this._concatenatedMatrix();
    return (matrix || new Matrix2D()).setValues(m[0], m[1], m[2], m[3], m[4], m[5]);
}, writable: true, configurable: true});

var createjs = {
    EventDispatcher: EventDispatcher, Event: CJSEvent, MouseEvent: CJSEvent, Ticker: Ticker, Tween: Tween, Ease: Ease,
    MovieClip: MovieClip, Container: Container, Shape: Shape, Graphics: Graphics, Text: Text, Bitmap: Bitmap,
    Sprite: Sprite, SpriteSheet: SpriteSheet, Matrix2D: Matrix2D, BlurFilter: BlurFilter,
    ColorMatrixFilter: ColorMatrixFilter, ColorFilter: ColorFilter, ColorMatrix: ColorMatrix, Shadow: Shadow,
    DisplayObject: display.DisplayObject, Point: Point, Rectangle: Rectangle, Sound: Sound, ButtonHelper: ButtonHelper,
    Stage: function () { return phoenix.player.stage; },
    StageGL: function () { return phoenix.player.stage; },
    Touch: {enable: function () { return true; }, isSupported: function () { return true; }},
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
MovieClip.INDEPENDENT = "independent";
MovieClip.SINGLE_FRAME = "single";
MovieClip.SYNCHED = "synched";
global.createjs = global.cjs = createjs;
// images: the library's pictures by their linkage or name, as published
// pages load them.
var imagesSet = global.images || {};
global.images = new Proxy(imagesSet, {
    get: function (target, key) {
        if (typeof key !== "string" || key in target)
            return target[key];
        var index = imageIndex(key);
        return index >= 0 ? phoenix._.images[index] : undefined;
    }
});
// Instances of frames still to come are made with their clip, as published
// pages make them.
phoenix._.premakeNamed = true;
global.ss = global.ss || {};

// Wiring to the movie: the frame scripts, the stage, lib, and the ticks.
phoenix.actionscript(function (as3) {
    var movie = phoenix._.library.movie;
    if (!movie.javaScript)
        return;
    var compile = function (code, where) {
        try {
            return new Function(code);
        } catch (error) {
            phoenix._.reportError(error, where);
            return null;
        }
    };
    var install = function (timeline, symbol, label) {
        if (!timeline || !timeline.js)
            return;
        var scripts = {};
        timeline.js.forEach(function (entry) {
            var body = compile(entry[1], label + ", frame " + entry[0]);
            if (body)
                scripts[entry[0]] = function () {
                    try {
                        body.call(this);
                    } catch (error) {
                        phoenix._.reportError(error, label + ", frame " + entry[0]);
                    }
                };
        });
        as3.frameScripts(symbol, scripts);
    };
    install(movie.root, "", "the main timeline");
    (movie.symbols || []).forEach(function (symbol) { install(symbol.timeline, symbol.linkage || symbol.name, symbol.name); });

    // lib: new lib.Name() makes a library symbol's object.
    var constructors = {};
    global.lib = new Proxy({}, {
        get: function (target, key) {
            if (typeof key !== "string")
                return undefined;
            if (key in target)
                return target[key];
            if (key === "properties")
                return {width: movie.width, height: movie.height, fps: movie.frameRate,
                    color: "#" + ("000000" + (movie.background || 0).toString(16)).slice(-6), opacity: 1, manifest: [], preloads: []};
            if (key === "ssMetadata")
                return [];
            if (!constructors[key]) {
                var symbols = phoenix._.library.symbols;
                var def = symbols.filter(function (s) { return s.linkage === key; })[0] ||
                    symbols.filter(function (s) { return s.name === key || s.name.split("/").pop() === key; })[0];
                if (def)
                    constructors[key] = function () {
                        return phoenix._.makeObject({t: "s", id: def.index});
                    };
                else if (imageIndex(key) >= 0)
                    // A library picture by its class: a Bitmap of it.
                    constructors[key] = function () { return Bitmap(key); };
                else
                    return undefined;
            }
            return constructors[key];
        },
        set: function (target, key, value) { target[key] = value; return true; }
    });
});
phoenix._.onRoot = phoenix._.onRoot || [];
phoenix._.onRoot.push(function (stage, root) {
    if (!phoenix._.library.movie.javaScript)
        return;
    global.stage = stage;
    global.exportRoot = root;
    stage.canvas = typeof document !== "undefined" ? document.querySelector("canvas.phoenix-stage") : null;
    ["enableMouseOver", "enableDOMEvents", "update", "clear", "tick", "setBounds"].forEach(function (name) {
        stage[name] = function () {};
    });
    stage.mouseInBounds = true;
    stage.autoClear = true;
    stage.tickOnUpdate = true;
    stage.snapToPixelEnabled = false;
    // Presses for pressmove and pressup.
    flash.events.EventDispatcher.prototype.addEventListener.call(stage, "mouseDown", function (e) {
        pressed = e.target instanceof display.Stage ? null : e.target;
    }, true);
    flash.events.EventDispatcher.prototype.addEventListener.call(stage, "mouseMove", function (e) {
        deliverPress("pressmove", e);
    });
    flash.events.EventDispatcher.prototype.addEventListener.call(stage, "mouseUp", function (e) {
        deliverPress("pressup", e);
        pressed = null;
    });
    // Each frame: the clock moves on; the clips hear tick; then the tweens
    // move on and the Ticker ticks, as CreateJS orders the Ticker's
    // listeners (the stage's first, then Tween's).
    flash.events.EventDispatcher.prototype.addEventListener.call(stage, "enterFrame", function () {
        now += 1000 / stage.frameRate;
        ++ticks;
        // Sprites and the clips scripts made play on.
        sprites.forEach(function (sprite) { sprite._cjsTick(); });
        madeClips.forEach(function (clip) { clip._cjsTick(); });
    });
    flash.events.EventDispatcher.prototype.addEventListener.call(stage, "exitFrame", function () {
        // Not on the first frame, before the Ticker has ticked.
        if (!ticks)
            return;
        var delta = 1000 / stage.frameRate;
        tweens.slice().forEach(function (tween) {
            if (!Ticker.paused || tween.ignoreGlobalPause)
                tween._advance(tween.useTicks ? 1 : delta);
        });
        var event = new CJSEvent("tick");
        event.delta = delta;
        event.paused = Ticker.paused;
        event.time = event.runTime = now;
        Ticker.dispatchEvent(event);
        // Drawing a script holds commands of is drawn again, as changed.
        live.forEach(function (g) { g._changed(); });
    });
});
})(globalThis);
