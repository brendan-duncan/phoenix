// CreateJS over the Phoenix runtime, for HTML5 Canvas documents: their
// frame scripts are JavaScript written against CreateJS's API, run with
// this as the timeline's MovieClip. The display objects are the runtime's
// own (flash.display), given CreateJS's ways where the two differ: frames
// counted from 0, on() and CreateJS's event names, positions as numbers,
// cursor, paused and loop. createjs.Ticker, Tween, Ease, Sound, Shape,
// Graphics, Text, Bitmap, Container, MovieClip and ButtonHelper are here,
// with the stage, exportRoot and lib as Animate's published pages have
// them. Load it after phoenix.js and before the content.

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
    cache: function () {}, uncache: function () {}, updateCache: function () {},
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

// Drawing from scripts: CreateJS's Graphics, chained, over the runtime's.
function Graphics(target) {
    this._g = target || new display.Graphics(null);
    this._stroke = null;
    this._strokeWidth = 1;
    this._caps = "butt";
    this._joints = "miter";
    this._miter = 10;
}
var GR = Graphics.prototype;
GR._line = function () {
    if (!this._stroke)
        this._g.lineStyle();
    else
        this._g.lineStyle(this._strokeWidth, this._stroke.rgb, this._stroke.alpha, false, "normal",
            this._caps === "butt" ? "none" : this._caps, this._joints, this._miter);
};
GR.beginFill = function (color) {
    if (!color)
        this._g.endFill();
    else {
        var c = cssColor(color);
        this._g.beginFill(c.rgb, c.alpha);
    }
    return this;
};
GR.endFill = function () { this._g.endFill(); return this; };
GR.beginStroke = function (color) { this._stroke = color ? cssColor(color) : null; this._line(); return this; };
GR.endStroke = function () { this._stroke = null; this._line(); return this; };
GR.setStrokeStyle = function (thickness, caps, joints, miterLimit) {
    this._strokeWidth = thickness;
    var capNames = ["butt", "round", "square"], jointNames = ["miter", "round", "bevel"];
    this._caps = typeof caps === "number" ? capNames[caps] : caps || "butt";
    this._joints = typeof joints === "number" ? jointNames[joints] : joints || "miter";
    this._miter = miterLimit || 10;
    this._line();
    return this;
};
GR.beginLinearGradientFill = function (colors, ratios, x0, y0, x1, y1) {
    var parsed = colors.map(cssColor);
    var m = new flash.geom.Matrix();
    var dx = x1 - x0, dy = y1 - y0, length = Math.sqrt(dx * dx + dy * dy) || 1;
    m.createGradientBox(length, length, Math.atan2(dy, dx), (x0 + x1) / 2 - length / 2, (y0 + y1) / 2 - length / 2);
    this._g.beginGradientFill("linear", parsed.map(function (c) { return c.rgb; }), parsed.map(function (c) { return c.alpha; }),
        ratios.map(function (r) { return r * 255; }), m);
    return this;
};
GR.beginRadialGradientFill = function (colors, ratios, x0, y0, r0, x1, y1, r1) {
    var parsed = colors.map(cssColor);
    var m = new flash.geom.Matrix();
    m.createGradientBox(r1 * 2, r1 * 2, 0, x1 - r1, y1 - r1);
    this._g.beginGradientFill("radial", parsed.map(function (c) { return c.rgb; }), parsed.map(function (c) { return c.alpha; }),
        ratios.map(function (r) { return r * 255; }), m);
    return this;
};
GR.moveTo = function (x, y) { this._g.moveTo(x, y); return this; };
GR.lineTo = function (x, y) { this._g.lineTo(x, y); return this; };
GR.quadraticCurveTo = GR.curveTo = function (cx, cy, x, y) { this._g.curveTo(cx, cy, x, y); return this; };
GR.bezierCurveTo = function (c1x, c1y, c2x, c2y, x, y) { this._g.cubicCurveTo(c1x, c1y, c2x, c2y, x, y); return this; };
GR.rect = GR.drawRect = function (x, y, w, h) { this._g.drawRect(x, y, w, h); return this; };
GR.drawRoundRect = function (x, y, w, h, r) { this._g.drawRoundRect(x, y, w, h, r * 2, r * 2); return this; };
GR.drawCircle = function (x, y, r) { this._g.drawCircle(x, y, r); return this; };
GR.drawEllipse = function (x, y, w, h) { this._g.drawEllipse(x, y, w, h); return this; };
GR.closePath = function () { return this; };
GR.clear = function () { this._g.clear(); this._line(); return this; };
GR.f = GR.beginFill; GR.ef = GR.endFill; GR.s = GR.beginStroke; GR.es = GR.endStroke; GR.ss = GR.setStrokeStyle;
GR.mt = GR.moveTo; GR.lt = GR.lineTo; GR.qt = GR.quadraticCurveTo; GR.bt = GR.bezierCurveTo; GR.r = GR.rect;
GR.dr = GR.drawRect; GR.rr = GR.drawRoundRect; GR.dc = GR.drawCircle; GR.de = GR.drawEllipse; GR.cp = GR.closePath;
GR.lf = GR.beginLinearGradientFill; GR.rf = GR.beginRadialGradientFill;

function Shape(graphics) {
    var shape = new display.Shape();
    var g = new Graphics(shape.graphics);
    Object.defineProperty(shape, "graphics", {value: graphics ? graphics : g, writable: true, configurable: true});
    if (graphics)
        shape._graphics = graphics._g;
    return shape;
}
function Container() { return new display.Sprite(); }
function MovieClip() { return new display.MovieClip(); }
function Text(text, font, color) {
    var field = new flash.text.TextField();
    var format = new flash.text.TextFormat();
    var m = /(italic\s+)?(bold\s+)?([\d.]+)px\s+(.+)/i.exec(font || "10px sans-serif");
    if (m) {
        format.italic = !!m[1];
        format.bold = !!m[2];
        format.size = parseFloat(m[3]);
        format.font = m[4].replace(/["']/g, "").split(",")[0].trim();
    }
    format.color = cssColor(color || "#000").rgb;
    field.defaultTextFormat = format;
    field.autoSize = "left";
    field.selectable = false;
    field.text = text === undefined ? "" : String(text);
    Object.defineProperty(field, "textAlign", {
        get: function () { return field.defaultTextFormat.align; },
        set: function (value) {
            var f = new flash.text.TextFormat();
            f.align = value === "center" ? "center" : value === "right" || value === "end" ? "right" : "left";
            field.setTextFormat(f);
            field.defaultTextFormat = f;
        }
    });
    Object.defineProperty(field, "lineWidth", {
        get: function () { return field.wordWrap ? field.width : null; },
        set: function (value) { field.wordWrap = !!value; if (value) field.width = value; }
    });
    return field;
}
function Bitmap(source) {
    var data = null;
    if (typeof source === "string") {
        var images = phoenix._.library && phoenix._.library.movie.images || [];
        for (var i = 0; i < images.length && !data; ++i)
            if (images[i].name === source || images[i].linkage === source || images[i].src === source)
                data = phoenix._.libraryBitmapData(i);
    } else if (source && source.width) {
        data = new display.BitmapData(source.width, source.height, true, 0);
        data._ctx().drawImage(source, 0, 0);
    }
    return new display.Bitmap(data);
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

var createjs = {
    EventDispatcher: EventDispatcher, Event: CJSEvent, MouseEvent: CJSEvent, Ticker: Ticker, Tween: Tween, Ease: Ease,
    MovieClip: MovieClip, Container: Container, Shape: Shape, Graphics: Graphics, Text: Text, Bitmap: Bitmap,
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
global.images = global.images || {};
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
                if (!def)
                    return undefined;
                constructors[key] = function () {
                    return phoenix._.makeObject({t: "s", id: def.index});
                };
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
    });
});
})(globalThis);
