#include "scripts.h"

namespace fla::play::canvas {

/// CreateJS as an HTML5 Canvas movie's scripts see it, over __phoenix (see
/// Native in scripts.cpp): EventDispatcher and the events, MovieClip over
/// the runtime's clips (one object per clip, its instances by name), what
/// scripts make themselves (Shape and Graphics, Text, Bitmap, Sprite,
/// Container, MovieClip with its own timeline), filters, shadows, masks and
/// caching, the stage, exportRoot and lib, Ticker, Tween and Ease, Sound,
/// and the browser's setTimeout, console and alert. What it does not have says so
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

var DEG = Math.PI / 180;
function appendAll(out, list) {
    for (var i = 0; i < list.length; ++i)
        out.push(list[i]);
}

// Matrix2D, as Graphics and the display objects use it.

function Matrix2D(a, b, c, d, tx, ty) { this.setValues(a, b, c, d, tx, ty); }
var M2 = Matrix2D.prototype;
M2.setValues = function (a, b, c, d, tx, ty) {
    this.a = a == null ? 1 : a; this.b = b || 0; this.c = c || 0;
    this.d = d == null ? 1 : d; this.tx = tx || 0; this.ty = ty || 0;
    return this;
};
M2.append = function (a, b, c, d, tx, ty) {
    var a1 = this.a, b1 = this.b, c1 = this.c, d1 = this.d;
    if (a !== 1 || b !== 0 || c !== 0 || d !== 1) {
        this.a = a1 * a + c1 * b; this.b = b1 * a + d1 * b;
        this.c = a1 * c + c1 * d; this.d = b1 * c + d1 * d;
    }
    this.tx = a1 * tx + c1 * ty + this.tx;
    this.ty = b1 * tx + d1 * ty + this.ty;
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
    var cos = 1, sin = 0;
    if (rotation % 360) {
        cos = Math.cos(rotation * DEG); sin = Math.sin(rotation * DEG);
    }
    if (skewX || skewY) {
        skewX *= DEG; skewY *= DEG;
        this.append(Math.cos(skewY), Math.sin(skewY), -Math.sin(skewX), Math.cos(skewX), x, y);
        this.append(cos * scaleX, sin * scaleX, -sin * scaleY, cos * scaleY, 0, 0);
    } else
        this.append(cos * scaleX, sin * scaleX, -sin * scaleY, cos * scaleY, x, y);
    if (regX || regY) {
        this.tx -= regX * this.a + regY * this.c;
        this.ty -= regX * this.b + regY * this.d;
    }
    return this;
};
M2.prependTransform = function (x, y, scaleX, scaleY, rotation, skewX, skewY, regX, regY) {
    return this.prependMatrix(new Matrix2D().appendTransform(x, y, scaleX, scaleY, rotation, skewX, skewY, regX, regY));
};
M2.rotate = function (angle) {
    angle *= DEG;
    return this.append(Math.cos(angle), Math.sin(angle), -Math.sin(angle), Math.cos(angle), 0, 0);
};
M2.skew = function (skewX, skewY) {
    skewX *= DEG; skewY *= DEG;
    return this.append(Math.cos(skewY), Math.sin(skewY), -Math.sin(skewX), Math.cos(skewX), 0, 0);
};
M2.scale = function (x, y) { this.a *= x; this.b *= x; this.c *= y; this.d *= y; return this; };
M2.translate = function (x, y) {
    this.tx += this.a * x + this.c * y; this.ty += this.b * x + this.d * y;
    return this;
};
M2.identity = function () { return this.setValues(); };
M2.invert = function () {
    var a1 = this.a, b1 = this.b, c1 = this.c, d1 = this.d, tx1 = this.tx, n = a1 * d1 - b1 * c1;
    this.a = d1 / n; this.b = -b1 / n; this.c = -c1 / n; this.d = a1 / n;
    this.tx = (c1 * this.ty - d1 * tx1) / n;
    this.ty = -(a1 * this.ty - b1 * tx1) / n;
    return this;
};
M2.isIdentity = function () {
    return this.tx === 0 && this.ty === 0 && this.a === 1 && this.b === 0 && this.c === 0 && this.d === 1;
};
M2.equals = function (m) {
    return this.tx === m.tx && this.ty === m.ty && this.a === m.a && this.b === m.b && this.c === m.c && this.d === m.d;
};
M2.transformPoint = function (x, y, pt) {
    pt = pt || new Point();
    pt.x = x * this.a + y * this.c + this.tx;
    pt.y = x * this.b + y * this.d + this.ty;
    return pt;
};
M2.decompose = function (target) {
    target = target || {};
    target.x = this.tx; target.y = this.ty;
    target.scaleX = Math.sqrt(this.a * this.a + this.b * this.b);
    target.scaleY = Math.sqrt(this.c * this.c + this.d * this.d);
    var skewX = Math.atan2(-this.c, this.d), skewY = Math.atan2(this.b, this.a);
    if (Math.abs(1 - skewX / skewY) < 0.00001) {
        target.rotation = skewY / DEG;
        if (this.a < 0 && this.d >= 0)
            target.rotation += target.rotation <= 0 ? 180 : -180;
        target.skewX = target.skewY = 0;
    } else {
        target.skewX = skewX / DEG; target.skewY = skewY / DEG;
    }
    return target;
};
M2.copy = function (m) { return this.setValues(m.a, m.b, m.c, m.d, m.tx, m.ty); };
M2.clone = function () { return new Matrix2D(this.a, this.b, this.c, this.d, this.tx, this.ty); };
M2.toString = function () {
    return "[Matrix2D (a=" + this.a + " b=" + this.b + " c=" + this.c + " d=" + this.d + " tx=" + this.tx + " ty=" + this.ty + ")]";
};
Matrix2D.DEG_TO_RAD = DEG;
Matrix2D.identity = new Matrix2D();

// Colors and fonts as CSS writes them.

var NAMED_COLORS = {};
("aliceblue:f0f8ff,antiquewhite:faebd7,aqua:00ffff,aquamarine:7fffd4,azure:f0ffff,beige:f5f5dc,bisque:ffe4c4," +
"black:000000,blanchedalmond:ffebcd,blue:0000ff,blueviolet:8a2be2,brown:a52a2a,burlywood:deb887,cadetblue:5f9ea0," +
"chartreuse:7fff00,chocolate:d2691e,coral:ff7f50,cornflowerblue:6495ed,cornsilk:fff8dc,crimson:dc143c,cyan:00ffff," +
"darkblue:00008b,darkcyan:008b8b,darkgoldenrod:b8860b,darkgray:a9a9a9,darkgreen:006400,darkgrey:a9a9a9," +
"darkkhaki:bdb76b,darkmagenta:8b008b,darkolivegreen:556b2f,darkorange:ff8c00,darkorchid:9932cc,darkred:8b0000," +
"darksalmon:e9967a,darkseagreen:8fbc8f,darkslateblue:483d8b,darkslategray:2f4f4f,darkslategrey:2f4f4f," +
"darkturquoise:00ced1,darkviolet:9400d3,deeppink:ff1493,deepskyblue:00bfff,dimgray:696969,dimgrey:696969," +
"dodgerblue:1e90ff,firebrick:b22222,floralwhite:fffaf0,forestgreen:228b22,fuchsia:ff00ff,gainsboro:dcdcdc," +
"ghostwhite:f8f8ff,gold:ffd700,goldenrod:daa520,gray:808080,green:008000,greenyellow:adff2f,grey:808080," +
"honeydew:f0fff0,hotpink:ff69b4,indianred:cd5c5c,indigo:4b0082,ivory:fffff0,khaki:f0e68c,lavender:e6e6fa," +
"lavenderblush:fff0f5,lawngreen:7cfc00,lemonchiffon:fffacd,lightblue:add8e6,lightcoral:f08080,lightcyan:e0ffff," +
"lightgoldenrodyellow:fafad2,lightgray:d3d3d3,lightgreen:90ee90,lightgrey:d3d3d3,lightpink:ffb6c1," +
"lightsalmon:ffa07a,lightseagreen:20b2aa,lightskyblue:87cefa,lightslategray:778899,lightslategrey:778899," +
"lightsteelblue:b0c4de,lightyellow:ffffe0,lime:00ff00,limegreen:32cd32,linen:faf0e6,magenta:ff00ff,maroon:800000," +
"mediumaquamarine:66cdaa,mediumblue:0000cd,mediumorchid:ba55d3,mediumpurple:9370db,mediumseagreen:3cb371," +
"mediumslateblue:7b68ee,mediumspringgreen:00fa9a,mediumturquoise:48d1cc,mediumvioletred:c71585," +
"midnightblue:191970,mintcream:f5fffa,mistyrose:ffe4e1,moccasin:ffe4b5,navajowhite:ffdead,navy:000080," +
"oldlace:fdf5e6,olive:808000,olivedrab:6b8e23,orange:ffa500,orangered:ff4500,orchid:da70d6,palegoldenrod:eee8aa," +
"palegreen:98fb98,paleturquoise:afeeee,palevioletred:db7093,papayawhip:ffefd5,peachpuff:ffdab9,peru:cd853f," +
"pink:ffc0cb,plum:dda0dd,powderblue:b0e0e6,purple:800080,rebeccapurple:663399,red:ff0000,rosybrown:bc8f8f," +
"royalblue:4169e1,saddlebrown:8b4513,salmon:fa8072,sandybrown:f4a460,seagreen:2e8b57,seashell:fff5ee," +
"sienna:a0522d,silver:c0c0c0,skyblue:87ceeb,slateblue:6a5acd,slategray:708090,slategrey:708090,snow:fffafa," +
"springgreen:00ff7f,steelblue:4682b4,tan:d2b48c,teal:008080,thistle:d8bfd8,tomato:ff6347,turquoise:40e0d0," +
"violet:ee82ee,wheat:f5deb3,white:ffffff,whitesmoke:f5f5f5,yellow:ffff00,yellowgreen:9acd32").split(",")
    .forEach(function (entry) { var at = entry.indexOf(":"); NAMED_COLORS[entry.slice(0, at)] = parseInt(entry.slice(at + 1), 16); });

function hslToRgb(h, s, l) {
    h = ((h % 360) + 360) % 360 / 360;
    function channel(t) {
        t = (t + 1) % 1;
        var q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        if (t < 1 / 6) return p + (q - p) * 6 * t;
        if (t < 1 / 2) return q;
        if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
        return p;
    }
    return (Math.round(channel(h + 1 / 3) * 255) << 16) | (Math.round(channel(h) * 255) << 8) | Math.round(channel(h - 1 / 3) * 255);
}
/// [RGB, alpha] of a CSS color; null for none.
function parseColor(value) {
    if (value === null || value === undefined || value === "")
        return null;
    if (typeof value === "number")
        return [value & 0xffffff, 1];
    var text = String(value).trim().toLowerCase(), m;
    if (text === "transparent")
        return [0, 0];
    if (text.charAt(0) === "#") {
        var hex = text.slice(1);
        if (hex.length === 3 || hex.length === 4)
            hex = hex.split("").map(function (c) { return c + c; }).join("");
        if (/^[0-9a-f]{6}([0-9a-f]{2})?$/.test(hex))
            return [parseInt(hex.slice(0, 6), 16), hex.length === 8 ? parseInt(hex.slice(6), 16) / 255 : 1];
        return null;
    }
    if ((m = /^(rgb|hsl)a?\(([^)]*)\)$/.exec(text))) {
        var parts = m[2].split(/[\s,\/]+/).filter(function (p) { return p.length; });
        var alpha = parts.length > 3 ? parseFloat(parts[3]) * (/%$/.test(parts[3]) ? 0.01 : 1) : 1;
        if (m[1] === "rgb") {
            var rgb = parts.slice(0, 3).map(function (p) {
                var n = parseFloat(p) * (/%$/.test(p) ? 2.55 : 1);
                return Math.max(0, Math.min(255, Math.round(n || 0)));
            });
            return [(rgb[0] << 16) | (rgb[1] << 8) | rgb[2], isNaN(alpha) ? 1 : alpha];
        }
        return [hslToRgb(parseFloat(parts[0]) || 0, (parseFloat(parts[1]) || 0) / 100, (parseFloat(parts[2]) || 0) / 100),
            isNaN(alpha) ? 1 : alpha];
    }
    if (NAMED_COLORS.hasOwnProperty(text))
        return [NAMED_COLORS[text], 1];
    return null;
}
/// A CSS font ("bold 24px Arial"): its family, size in pixels, bold and
/// italic.
function parseFont(value) {
    var text = String(value || "10px sans-serif");
    var font = { family: "_sans", size: 10, bold: /\bbold(er)?\b|\b[6-9]00\b/i.test(text), italic: /\b(italic|oblique)\b/i.test(text) };
    var m = /(\d*\.?\d+)(px|pt|em|rem|%)?(\s*\/\s*\S+)?\s+(.+)$/i.exec(text);
    if (m) {
        var size = parseFloat(m[1]), unit = (m[2] || "px").toLowerCase();
        font.size = unit === "pt" ? size * 4 / 3 : unit === "em" || unit === "rem" ? size * 16 : unit === "%" ? size * 0.16 : size;
        var family = m[4].split(",")[0].trim().replace(/^["']|["']$/g, "");
        var generic = { "sans-serif": "_sans", "serif": "_serif", "monospace": "_typewriter", "system-ui": "_sans" };
        font.family = generic[family.toLowerCase()] || family;
    }
    return font;
}

)JS" R"JS(
// Images: library bitmaps by linkage or name, or files beside the movie.

function PhoenixImage() {
    hidden(this, "__image", -1);
    this.width = this.height = this.naturalWidth = this.naturalHeight = 0;
    this.complete = false;
    this.onload = this.onerror = null;
    hidden(this, "__src", "");
}
Object.defineProperty(PhoenixImage.prototype, "src", {
    get: function () { return this.__src; },
    set: function (value) {
        var self = this;
        this.__src = String(value);
        this.__image = P.image(this.__src);
        var size = this.__image >= 0 ? P.imageSize(this.__image) : null;
        this.width = this.naturalWidth = size ? size[0] : 0;
        this.height = this.naturalHeight = size ? size[1] : 0;
        this.complete = !!size;
        if (!size)
            P.print("Warning: the image \"" + this.__src + "\" is not in the library or beside the movie");
        addTimer(function () {
            var on = size ? self.onload : self.onerror;
            if (typeof on === "function")
                on.call(self, { type: size ? "load" : "error", target: self });
            (self.__listeners && self.__listeners[size ? "load" : "error"] || []).forEach(function (f) {
                f.call(self, { type: size ? "load" : "error", target: self });
            });
        }, 0, [], false);
    }
});
PhoenixImage.prototype.addEventListener = function (type, listener) {
    if (!this.__listeners)
        hidden(this, "__listeners", {});
    (this.__listeners[type] = this.__listeners[type] || []).push(listener);
};
PhoenixImage.prototype.removeEventListener = function () {};
var namedImages = {};
/// The image \a source is (an image, a string naming one, a cached
/// object's canvas); null for none.
function imageOf(source) {
    if (!source)
        return null;
    if (typeof source === "string") {
        if (!namedImages[source]) {
            var made = new PhoenixImage();
            made.src = source;
            namedImages[source] = made;
        }
        source = namedImages[source];
    }
    if (typeof source.__image === "number" && source.__image >= 0)
        return { id: source.__image, width: source.width, height: source.height };
    return null;
}
function libraryImage(name) {
    var id = P.image(name);
    if (id < 0)
        return undefined;
    if (!namedImages[name]) {
        var made = new PhoenixImage();
        made.__image = id;
        made.__src = name;
        var size = P.imageSize(id);
        made.width = made.naturalWidth = size[0];
        made.height = made.naturalHeight = size[1];
        made.complete = true;
        namedImages[name] = made;
    }
    return namedImages[name];
}

// The display objects. Each has the number the native side knows it by
// (__id): a clip's (one object per clip, made by wrap), or one the scripts
// made (new createjs.Shape()...), let go of once the scripts let go of it
// and it is on no display list.

var MOUSE = {click: 1, dblclick: 1, mousedown: 1, mouseup: 1, mouseover: 1, mouseout: 1, rollover: 1, rollout: 1};
var ticking = new Set();
var notLooping = new Set();
var clips = new Map();
var made = new Map();
var held = new Map();
var effected = new Set();
var codeClips = new Set();
var sprites = new Set();
var warned = {};
function warnOnce(what) {
    if (!warned[what]) {
        warned[what] = true;
        P.print("Warning: " + what + " is not available in Test Movie");
    }
}
var finalizer = typeof FinalizationRegistry === "function"
    ? new FinalizationRegistry(function (id) { made.delete(id); held.delete(id); P.release(id); }) : null;
function own(object, kind) {
    if (Object.prototype.hasOwnProperty.call(object, "__id"))
        return object.__id;
    var id = P.create(kind);
    hidden(object, "__id", id);
    made.set(id, typeof WeakRef === "function" ? new WeakRef(object) : { deref: function () { return object; } });
    if (finalizer)
        finalizer.register(object, id);
    return id;
}
function hold(child) {
    if (made.has(child.__id))
        held.set(child.__id, child);
}
function isDisplay(value) {
    return !!value && typeof value === "object" && typeof value.__id === "number";
}
function displayArgument(child, how) {
    if (!isDisplay(child) || child === stage)
        throw new TypeError(how + ": " + show(child) + " is not a display object");
    return child;
}

function DisplayObject() { own(this, "sprite"); }
var DO = DisplayObject.prototype = Object.create(ED);
DO.constructor = DisplayObject;
DO._off = false;
DO.tickEnabled = true;
DO.snapToPixel = true;
DO.hitArea = null;
DO.cacheID = 0;

/// What CreateJS keeps of a transform that the display list's does not
/// (skew, the registration point): once a script sets them, the whole
/// transform is made here.
function pose(object) {
    return Object.prototype.hasOwnProperty.call(object, "__pose") ? object.__pose : null;
}
function posed(object) {
    var p = pose(object);
    if (!p) {
        p = { x: P.get(object.__id, "x"), y: P.get(object.__id, "y"), scaleX: P.get(object.__id, "scaleX"),
            scaleY: P.get(object.__id, "scaleY"), rotation: P.get(object.__id, "rotation"),
            skewX: 0, skewY: 0, regX: 0, regY: 0 };
        hidden(object, "__pose", p);
    }
    return p;
}
function place(object) {
    var p = object.__pose;
    var m = new Matrix2D().appendTransform(p.x, p.y, p.scaleX, p.scaleY, p.rotation, p.skewX, p.skewY, p.regX, p.regY);
    P.matrix(object.__id, m.a, m.b, m.c, m.d, m.tx, m.ty);
}
["x", "y", "rotation", "scaleX", "scaleY"].forEach(function (name) {
    Object.defineProperty(DO, name, {
        get: function () { var p = pose(this); return p ? p[name] : P.get(this.__id, name); },
        set: function (value) {
            var p = pose(this);
            if (!p) {
                P.set(this.__id, name, value);
                return;
            }
            value = Number(value);
            if (isFinite(value)) {
                p[name] = value;
                place(this);
            }
        },
        configurable: true
    });
});
["skewX", "skewY", "regX", "regY"].forEach(function (name) {
    Object.defineProperty(DO, name, {
        get: function () { var p = pose(this); return p ? p[name] : 0; },
        set: function (value) {
            value = Number(value) || 0;
            if (!value && !pose(this))
                return;
            posed(this)[name] = value;
            place(this);
        },
        configurable: true
    });
});
["alpha", "visible", "name", "mouseEnabled", "cursor"].forEach(function (name) {
    Object.defineProperty(DO, name, {
        get: function () { return P.get(this.__id, name); },
        set: function (value) { P.set(this.__id, name, value); },
        configurable: true
    });
});
["mouseX", "mouseY"].forEach(function (name) {
    Object.defineProperty(DO, name, { get: function () { return P.get(this.__id, name); }, set: function () {}, configurable: true });
});
Object.defineProperty(DO, "parent", {
    get: function () { return wrap(P.parent(this.__id)); },
    configurable: true
});
Object.defineProperty(DO, "stage", {
    get: function () {
        for (var at = this; at; at = at.parent)
            if (at === stage)
                return stage;
        return null;
    },
    configurable: true
});
Object.defineProperty(DO, "id", { get: function () { return this.__id; }, configurable: true });
DO._listening = function (type, on) {
    if (MOUSE[type])
        P.listen(this.__id, type, on);
    else if (type === "tick") {
        if (on)
            ticking.add(this);
        else
            ticking.delete(this);
    }
};
DO.isVisible = function () { return !!this.visible && this.alpha > 0; };
DO.set = function (props) {
    for (var key in props)
        this[key] = props[key];
    return this;
};
DO.setTransform = function (x, y, scaleX, scaleY, rotation, skewX, skewY, regX, regY) {
    var values = { x: x || 0, y: y || 0, scaleX: scaleX == null ? 1 : scaleX, scaleY: scaleY == null ? 1 : scaleY,
        rotation: rotation || 0, skewX: skewX || 0, skewY: skewY || 0, regX: regX || 0, regY: regY || 0 };
    if (pose(this) || values.skewX || values.skewY || values.regX || values.regY) {
        var p = posed(this);
        for (var key in values)
            p[key] = values[key];
        place(this);
    } else {
        this.x = values.x; this.y = values.y;
        this.scaleX = values.scaleX; this.scaleY = values.scaleY;
        this.rotation = values.rotation;
    }
    return this;
};
DO.getMatrix = function (matrix) {
    return (matrix ? matrix.identity() : new Matrix2D()).appendTransform(this.x, this.y, this.scaleX, this.scaleY,
        this.rotation, this.skewX, this.skewY, this.regX, this.regY);
};
DO.getConcatenatedMatrix = function (matrix) {
    matrix = matrix ? matrix.identity() : new Matrix2D();
    for (var at = this; at && at !== stage; at = at.parent)
        matrix.prependMatrix(at.getMatrix());
    return matrix;
};
DO.localToGlobal = function (x, y, pt) {
    this._sync && this._sync();
    var at = P.local(this.__id, x, y, true);
    return (pt || new Point()).setValues(at[0], at[1]);
};
DO.globalToLocal = function (x, y, pt) {
    this._sync && this._sync();
    var at = P.local(this.__id, x, y, false);
    return (pt || new Point()).setValues(at[0], at[1]);
};
DO.localToLocal = function (x, y, target, pt) {
    var at = this.localToGlobal(x, y);
    return target.globalToLocal(at.x, at.y, pt);
};
DO.hitTest = function (x, y) {
    this._sync && this._sync();
    return P.hit(this.__id, x, y);
};
DO.setBounds = function (x, y, width, height) {
    hidden(this, "__bounds", x == null ? null : new Rectangle(x, y, width, height));
};
DO.getBounds = function () {
    if (this.__bounds)
        return this.__bounds.clone();
    this._sync && this._sync();
    var box = P.bounds(this.__id);
    return box ? new Rectangle(box[0], box[1], box[2], box[3]) : null;
};
DO.getTransformedBounds = function () {
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
DO.draw = function () { return false; };
DO.updateContext = function () { missing("DisplayObject.updateContext()"); };
DO.getCacheDataURL = function () { missing("getCacheDataURL()"); };
DO.clone = function () { missing(this.toString() + ".clone()"); };
DO.toString = function () { return "[DisplayObject (name=" + this.name + ")]"; };

)JS" R"JS(
// Filters, the shadow, compositeOperation, the mask and the cache: what a
// display object is drawn with. Filters show once it is cached, as in
// CreateJS.

var BLEND = { "source-over": "normal", "lighter": "add", "multiply": "multiply", "screen": "screen",
    "overlay": "overlay", "darken": "darken", "lighten": "lighten", "difference": "difference",
    "hard-light": "hardlight", "destination-out": "erase", "destination-in": "alpha" };
function fx(object) {
    if (!Object.prototype.hasOwnProperty.call(object, "__fx"))
        hidden(object, "__fx", { filters: null, shadow: null, composite: null, mask: null, cache: null, sent: "",
            maskSent: "", canvas: null });
    return object.__fx;
}
["filters", "shadow", "compositeOperation", "mask"].forEach(function (name) {
    var key = name === "compositeOperation" ? "composite" : name;
    Object.defineProperty(DO, name, {
        get: function () { return Object.prototype.hasOwnProperty.call(this, "__fx") ? this.__fx[key] : null; },
        set: function (value) {
            if (value === undefined)
                value = null;
            if (!Object.prototype.hasOwnProperty.call(this, "__fx") && value === null)
                return;
            fx(this)[key] = value;
            applyEffects(this);
        },
        configurable: true
    });
});
function matrixOf(filter) {
    var m = filter.matrix && typeof filter.matrix.toArray === "function" ? filter.matrix.toArray() : filter.matrix || [];
    var out = [];
    for (var i = 0; i < 20; ++i)
        out.push(typeof m[i] === "number" ? m[i] : ColorMatrix.IDENTITY_MATRIX[i]);
    return out;
}
function applyEffects(object) {
    var state = fx(object);
    effected.add(object.__id);
    var cache = state.cache, list = [], alphaMask = null;
    (cache && Array.isArray(state.filters) ? state.filters : []).forEach(function (f) {
        if (f instanceof BlurFilter)
            list.push(1, Number(f.blurX) || 0, Number(f.blurY) || 0, f.quality === undefined ? 1 : f.quality | 0);
        else if (f instanceof ColorMatrixFilter)
            list.push.apply(list, [2].concat(matrixOf(f)));
        else if (f instanceof ColorFilter)
            list.push(2, f.redMultiplier, 0, 0, 0, f.redOffset, 0, f.greenMultiplier, 0, 0, f.greenOffset,
                0, 0, f.blueMultiplier, 0, f.blueOffset, 0, 0, 0, f.alphaMultiplier, f.alphaOffset);
        else if (f instanceof AlphaMaskFilter)
            alphaMask = f;
        else if (f)
            warnOnce((f.constructor && f.constructor.name ? f.constructor.name : "A custom filter"));
    });
    var shadow = [];
    if (state.shadow) {
        var color = parseColor(state.shadow.color) || [0, 1];
        shadow = [color[0], color[1], Number(state.shadow.offsetX) || 0, Number(state.shadow.offsetY) || 0,
            Number(state.shadow.blur) || 0];
    }
    var composite = state.composite ? BLEND[state.composite] : "normal";
    var key = [!!cache, list.join(","), shadow.join(","), state.composite].join("|");
    if (key !== state.sent) {
        state.sent = key;
        P.effects(object.__id, !!cache, list, shadow, composite || "normal");
        if (state.composite && !composite)
            warnOnce("compositeOperation \"" + state.composite + "\"");
    }
    var mask = isDisplay(state.mask) ? state.mask : null, image = alphaMask ? imageOf(alphaMask.mask) : null;
    var maskKey = mask ? "shape" + mask.__id : image ? "image" + image.id + ":" + cache.x + ":" + cache.y : "";
    if (maskKey !== state.maskSent) {
        state.maskSent = maskKey;
        P.mask(object.__id, mask ? mask.__id : -1, image ? image.id : -1, cache ? cache.x : 0, cache ? cache.y : 0);
    }
}
DO.cache = function (x, y, width, height, scale) {
    var state = fx(this);
    state.cache = { x: Number(x) || 0, y: Number(y) || 0, width: Number(width) || 0, height: Number(height) || 0,
        scale: Number(scale) || 1 };
    this.cacheID = (this.cacheID || 0) + 1;
    this.updateCache();
};
DO.updateCache = function (compositeOperation) {
    var state = Object.prototype.hasOwnProperty.call(this, "__fx") ? this.__fx : null;
    if (!state || !state.cache)
        throw new Error("cache() must be called before updateCache()");
    if (compositeOperation)
        warnOnce("updateCache(\"" + compositeOperation + "\") drawing over what is cached");
    if (state.canvas) {
        P.releaseImage(state.canvas.__image);
        state.canvas = null;
    }
    applyEffects(this);
};
DO.uncache = function () {
    if (!Object.prototype.hasOwnProperty.call(this, "__fx") || !this.__fx.cache)
        return;
    if (this.__fx.canvas)
        P.releaseImage(this.__fx.canvas.__image);
    this.__fx.canvas = null;
    this.__fx.cache = null;
    applyEffects(this);
};
Object.defineProperty(DO, "cacheCanvas", {
    get: function () {
        var state = Object.prototype.hasOwnProperty.call(this, "__fx") ? this.__fx : null;
        if (!state || !state.cache)
            return null;
        if (!state.canvas) {
            this._sync && this._sync();
            var c = state.cache, image = new PhoenixImage();
            image.__image = P.snapshot(this.__id, c.x, c.y, c.width, c.height, c.scale);
            image.width = Math.ceil(c.width * c.scale);
            image.height = Math.ceil(c.height * c.scale);
            image.complete = true;
            image.getContext = function () { missing("cacheCanvas.getContext()"); };
            state.canvas = image;
        }
        return state.canvas;
    },
    configurable: true
});

function Shadow(color, offsetX, offsetY, blur) {
    this.color = color || "black";
    this.offsetX = offsetX || 0;
    this.offsetY = offsetY || 0;
    this.blur = blur || 0;
}
Shadow.prototype.clone = function () { return new Shadow(this.color, this.offsetX, this.offsetY, this.blur); };
Shadow.prototype.toString = function () { return "[Shadow]"; };
Shadow.identity = new Shadow("transparent", 0, 0, 0);

function Filter() { this.usesContext = false; }
Filter.prototype.getBounds = function (rect) { return rect || null; };
Filter.prototype.applyFilter = function () { missing("Filter.applyFilter()"); };
Filter.prototype.shaderParamSetup = function () {};
Filter.prototype.toString = function () { return "[Filter]"; };
function subclass(made, base) {
    made.prototype = Object.create(base.prototype);
    made.prototype.constructor = made;
    return made.prototype;
}
function BlurFilter(blurX, blurY, quality) {
    Filter.call(this);
    this.blurX = blurX || 0;
    this.blurY = blurY || 0;
    this.quality = quality === undefined || isNaN(quality) ? 1 : quality | 0;
}
subclass(BlurFilter, Filter).clone = function () { return new BlurFilter(this.blurX, this.blurY, this.quality); };
BlurFilter.prototype.getBounds = function (rect) {
    var x = Math.ceil(Math.abs(this.blurX)), y = Math.ceil(Math.abs(this.blurY));
    var r = rect || new Rectangle();
    return r.setValues(r.x - x, r.y - y, r.width + 2 * x, r.height + 2 * y);
};
BlurFilter.prototype.toString = function () { return "[BlurFilter]"; };
function ColorFilter(redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier, redOffset, greenOffset, blueOffset, alphaOffset) {
    Filter.call(this);
    this.redMultiplier = redMultiplier != null ? redMultiplier : 1;
    this.greenMultiplier = greenMultiplier != null ? greenMultiplier : 1;
    this.blueMultiplier = blueMultiplier != null ? blueMultiplier : 1;
    this.alphaMultiplier = alphaMultiplier != null ? alphaMultiplier : 1;
    this.redOffset = redOffset || 0;
    this.greenOffset = greenOffset || 0;
    this.blueOffset = blueOffset || 0;
    this.alphaOffset = alphaOffset || 0;
}
subclass(ColorFilter, Filter).clone = function () {
    return new ColorFilter(this.redMultiplier, this.greenMultiplier, this.blueMultiplier, this.alphaMultiplier,
        this.redOffset, this.greenOffset, this.blueOffset, this.alphaOffset);
};
ColorFilter.prototype.toString = function () { return "[ColorFilter]"; };
function ColorMatrixFilter(matrix) { Filter.call(this); this.matrix = matrix; }
subclass(ColorMatrixFilter, Filter).clone = function () { return new ColorMatrixFilter(this.matrix); };
ColorMatrixFilter.prototype.toString = function () { return "[ColorMatrixFilter]"; };
function AlphaMaskFilter(mask) { Filter.call(this); this.mask = mask; }
subclass(AlphaMaskFilter, Filter).clone = function () { return new AlphaMaskFilter(this.mask); };
AlphaMaskFilter.prototype.toString = function () { return "[AlphaMaskFilter]"; };
function AlphaMapFilter(alphaMap) { Filter.call(this); this.alphaMap = alphaMap; }
subclass(AlphaMapFilter, Filter).toString = function () { return "[AlphaMapFilter]"; };

/// ColorMatrix: five rows of five, as CreateJS builds them.
function ColorMatrix(brightness, contrast, saturation, hue) { this.setColor(brightness, contrast, saturation, hue); }
var CM = ColorMatrix.prototype;
ColorMatrix.DELTA_INDEX = [0, 0.01, 0.02, 0.04, 0.05, 0.06, 0.07, 0.08, 0.1, 0.11, 0.12, 0.14, 0.15, 0.16, 0.17, 0.18,
    0.20, 0.21, 0.22, 0.24, 0.25, 0.27, 0.28, 0.30, 0.32, 0.34, 0.36, 0.38, 0.40, 0.42, 0.44, 0.46, 0.48, 0.5, 0.53, 0.56,
    0.59, 0.62, 0.65, 0.68, 0.71, 0.74, 0.77, 0.80, 0.83, 0.86, 0.89, 0.92, 0.95, 0.98, 1.0, 1.06, 1.12, 1.18, 1.24, 1.30,
    1.36, 1.42, 1.48, 1.54, 1.60, 1.66, 1.72, 1.78, 1.84, 1.90, 1.96, 2.0, 2.12, 2.25, 2.37, 2.50, 2.62, 2.75, 2.87, 3.0,
    3.2, 3.4, 3.6, 3.8, 4.0, 4.3, 4.7, 4.9, 5.0, 5.5, 6.0, 6.5, 6.8, 7.0, 7.3, 7.5, 7.8, 8.0, 8.4, 8.7, 9.0, 9.4, 9.6, 9.8,
    10.0];
ColorMatrix.IDENTITY_MATRIX = [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1];
ColorMatrix.LENGTH = 25;
CM.setColor = function (brightness, contrast, saturation, hue) {
    return this.reset().adjustColor(brightness, contrast, saturation, hue);
};
CM.reset = function () { return this.copy(ColorMatrix.IDENTITY_MATRIX); };
CM.adjustColor = function (brightness, contrast, saturation, hue) {
    this.adjustHue(hue);
    this.adjustContrast(contrast);
    this.adjustBrightness(brightness);
    return this.adjustSaturation(saturation);
};
CM.adjustBrightness = function (value) {
    if (!value)
        return this;
    value = this._cleanValue(value, 255);
    this._multiplyMatrix([1, 0, 0, 0, value, 0, 1, 0, 0, value, 0, 0, 1, 0, value, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
    return this;
};
CM.adjustContrast = function (value) {
    if (!value)
        return this;
    value = this._cleanValue(value, 100);
    var x;
    if (value < 0)
        x = 127 + value / 100 * 127;
    else {
        x = value % 1;
        if (x === 0)
            x = ColorMatrix.DELTA_INDEX[value];
        else
            x = ColorMatrix.DELTA_INDEX[value << 0] * (1 - x) + ColorMatrix.DELTA_INDEX[(value << 0) + 1] * x;
        x = x * 127 + 127;
    }
    this._multiplyMatrix([x / 127, 0, 0, 0, 0.5 * (127 - x), 0, x / 127, 0, 0, 0.5 * (127 - x), 0, 0, x / 127, 0,
        0.5 * (127 - x), 0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
    return this;
};
CM.adjustSaturation = function (value) {
    if (!value)
        return this;
    value = this._cleanValue(value, 100);
    var x = 1 + (value > 0 ? 3 * value / 100 : value / 100);
    var lumR = 0.3086, lumG = 0.6094, lumB = 0.0820;
    this._multiplyMatrix([lumR * (1 - x) + x, lumG * (1 - x), lumB * (1 - x), 0, 0,
        lumR * (1 - x), lumG * (1 - x) + x, lumB * (1 - x), 0, 0,
        lumR * (1 - x), lumG * (1 - x), lumB * (1 - x) + x, 0, 0,
        0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
    return this;
};
CM.adjustHue = function (value) {
    if (!value)
        return this;
    value = this._cleanValue(value, 180) / 180 * Math.PI;
    var cos = Math.cos(value), sin = Math.sin(value), lumR = 0.213, lumG = 0.715, lumB = 0.072;
    this._multiplyMatrix([
        lumR + cos * (1 - lumR) + sin * (-lumR), lumG + cos * (-lumG) + sin * (-lumG), lumB + cos * (-lumB) + sin * (1 - lumB), 0, 0,
        lumR + cos * (-lumR) + sin * 0.143, lumG + cos * (1 - lumG) + sin * 0.140, lumB + cos * (-lumB) + sin * (-0.283), 0, 0,
        lumR + cos * (-lumR) + sin * (-(1 - lumR)), lumG + cos * (-lumG) + sin * lumG, lumB + cos * (1 - lumB) + sin * lumB, 0, 0,
        0, 0, 0, 1, 0, 0, 0, 0, 0, 1]);
    return this;
};
CM.concat = function (matrix) {
    matrix = this._fixMatrix(matrix);
    if (matrix.length === ColorMatrix.LENGTH)
        this._multiplyMatrix(matrix);
    return this;
};
CM.clone = function () { return new ColorMatrix().copy(this); };
CM.toArray = function () {
    var out = [];
    for (var i = 0; i < ColorMatrix.LENGTH; ++i)
        out[i] = this[i];
    return out;
};
CM.copy = function (matrix) {
    for (var i = 0; i < ColorMatrix.LENGTH; ++i)
        this[i] = matrix[i];
    return this;
};
CM.toString = function () { return "[ColorMatrix]"; };
CM._multiplyMatrix = function (matrix) {
    var col = [];
    for (var i = 0; i < 5; ++i) {
        for (var j = 0; j < 5; ++j)
            col[j] = this[j + i * 5];
        for (j = 0; j < 5; ++j) {
            var value = 0;
            for (var k = 0; k < 5; ++k)
                value += matrix[j + k * 5] * col[k];
            this[j + i * 5] = value;
        }
    }
};
CM._cleanValue = function (value, limit) { return Math.min(limit, Math.max(-limit, value)); };
CM._fixMatrix = function (matrix) {
    if (matrix instanceof ColorMatrix)
        matrix = matrix.toArray();
    if (matrix.length < ColorMatrix.LENGTH)
        matrix = matrix.slice(0).concat(ColorMatrix.IDENTITY_MATRIX.slice(matrix.length, ColorMatrix.LENGTH));
    else if (matrix.length > ColorMatrix.LENGTH)
        matrix = matrix.slice(0, ColorMatrix.LENGTH);
    return matrix;
};

)JS" R"JS(
// Graphics: CreateJS's list of drawing commands, kept as its commands
// (graphics.command can be changed to animate), given to the display list
// as paths each frame.

function Graphics() {
    this.command = null;
    hidden(this, "_instructions", []);
    hidden(this, "_activeInstructions", []);
    hidden(this, "_commitIndex", 0);
    hidden(this, "_storeIndex", 0);
    this.clear();
}
var GP = Graphics.prototype;
var G = Graphics;
function command(made, exec) {
    made.prototype._path = exec || function () {};
    return made.prototype;
}
command(G.MoveTo = function (x, y) { this.x = x; this.y = y; }, function (b) { b.moveTo(this.x, this.y); });
command(G.LineTo = function (x, y) { this.x = x; this.y = y; }, function (b) { b.lineTo(this.x, this.y); });
command(G.ArcTo = function (x1, y1, x2, y2, radius) {
    this.x1 = x1; this.y1 = y1; this.x2 = x2; this.y2 = y2; this.radius = radius;
}, function (b) { b.arcTo(this.x1, this.y1, this.x2, this.y2, this.radius); });
command(G.Arc = function (x, y, radius, startAngle, endAngle, anticlockwise) {
    this.x = x; this.y = y; this.radius = radius; this.startAngle = startAngle; this.endAngle = endAngle;
    this.anticlockwise = !!anticlockwise;
}, function (b) { b.arc(this.x, this.y, this.radius, this.startAngle, this.endAngle, this.anticlockwise); });
command(G.QuadraticCurveTo = function (cpx, cpy, x, y) { this.cpx = cpx; this.cpy = cpy; this.x = x; this.y = y; },
    function (b) { b.quadraticCurveTo(this.cpx, this.cpy, this.x, this.y); });
command(G.BezierCurveTo = function (cp1x, cp1y, cp2x, cp2y, x, y) {
    this.cp1x = cp1x; this.cp1y = cp1y; this.cp2x = cp2x; this.cp2y = cp2y; this.x = x; this.y = y;
}, function (b) { b.bezierCurveTo(this.cp1x, this.cp1y, this.cp2x, this.cp2y, this.x, this.y); });
command(G.Rect = function (x, y, w, h) { this.x = x; this.y = y; this.w = w; this.h = h; },
    function (b) { b.rect(this.x, this.y, this.w, this.h); });
command(G.ClosePath = function () {}, function (b) { b.closePath(); });
command(G.BeginPath = function () {});
command(G.Circle = function (x, y, radius) { this.x = x; this.y = y; this.radius = radius; },
    function (b) { b.arc(this.x, this.y, this.radius, 0, Math.PI * 2, false); });
command(G.Ellipse = function (x, y, w, h) { this.x = x; this.y = y; this.w = w; this.h = h; }, function (b) {
    var x = this.x, y = this.y, w = this.w, h = this.h, k = 0.5522848;
    var ox = w / 2 * k, oy = h / 2 * k, xe = x + w, ye = y + h, xm = x + w / 2, ym = y + h / 2;
    b.moveTo(x, ym);
    b.bezierCurveTo(x, ym - oy, xm - ox, y, xm, y);
    b.bezierCurveTo(xm + ox, y, xe, ym - oy, xe, ym);
    b.bezierCurveTo(xe, ym + oy, xm + ox, ye, xm, ye);
    b.bezierCurveTo(xm - ox, ye, x, ym + oy, x, ym);
});
command(G.RoundRect = function (x, y, w, h, radiusTL, radiusTR, radiusBR, radiusBL) {
    this.x = x; this.y = y; this.w = w; this.h = h;
    this.radiusTL = radiusTL; this.radiusTR = radiusTR; this.radiusBR = radiusBR; this.radiusBL = radiusBL;
}, function (b) {
    var x = this.x, y = this.y, w = this.w, h = this.h, max = (w < h ? w : h) / 2;
    var r = [this.radiusTL, this.radiusTR, this.radiusBR, this.radiusBL], m = [0, 0, 0, 0];
    for (var i = 0; i < 4; ++i) {
        r[i] = Number(r[i]) || 0;
        if (r[i] < 0) { r[i] = -r[i]; m[i] = -1; }
        if (r[i] > max) r[i] = max;
    }
    b.moveTo(x + w - r[1], y);
    b.arcTo(x + w + r[1] * m[1], y - r[1] * m[1], x + w, y + r[1], r[1]);
    b.lineTo(x + w, y + h - r[2]);
    b.arcTo(x + w + r[2] * m[2], y + h + r[2] * m[2], x + w - r[2], y + h, r[2]);
    b.lineTo(x + r[3], y + h);
    b.arcTo(x - r[3] * m[3], y + h + r[3] * m[3], x, y + h - r[3], r[3]);
    b.lineTo(x, y + r[0]);
    b.arcTo(x - r[0] * m[0], y - r[0] * m[0], x + r[0], y, r[0]);
    b.closePath();
});
command(G.PolyStar = function (x, y, radius, sides, pointSize, angle) {
    this.x = x; this.y = y; this.radius = radius; this.sides = sides; this.pointSize = pointSize; this.angle = angle;
}, function (b) {
    var x = this.x, y = this.y, radius = this.radius, angle = (this.angle || 0) / 180 * Math.PI;
    var sides = this.sides, ps = 1 - (this.pointSize || 0), a = Math.PI / sides;
    b.moveTo(x + Math.cos(angle) * radius, y + Math.sin(angle) * radius);
    for (var i = 0; i < sides; ++i) {
        angle += a;
        if (ps !== 1)
            b.lineTo(x + Math.cos(angle) * radius * ps, y + Math.sin(angle) * radius * ps);
        angle += a;
        b.lineTo(x + Math.cos(angle) * radius, y + Math.sin(angle) * radius);
    }
    b.closePath();
});
function paintMethods(proto) {
    proto.linearGradient = function (colors, ratios, x0, y0, x1, y1) {
        this.style = { type: "linear", colors: colors, ratios: ratios, x0: x0, y0: y0, x1: x1, y1: y1 };
        return this;
    };
    proto.radialGradient = function (colors, ratios, x0, y0, r0, x1, y1, r1) {
        this.style = { type: "radial", colors: colors, ratios: ratios, x0: x0, y0: y0, r0: r0, x1: x1, y1: y1, r1: r1 };
        return this;
    };
    proto.bitmap = function (image, repetition) {
        this.style = { type: "bitmap", image: image, repetition: repetition || "" };
        return this;
    };
}
paintMethods(command(G.Fill = function (style, matrix) { this.style = style; this.matrix = matrix; }));
paintMethods(command(G.Stroke = function (style, ignoreScale) { this.style = style; this.ignoreScale = ignoreScale; }));
command(G.StrokeStyle = function (width, caps, joints, miterLimit, ignoreScale) {
    this.width = width; this.caps = caps; this.joints = joints; this.miterLimit = miterLimit; this.ignoreScale = ignoreScale;
});
command(G.StrokeDash = function (segments, offset) { this.segments = segments; this.offset = offset || 0; });
G.beginCmd = new G.BeginPath();
G.STROKE_CAPS_MAP = ["butt", "round", "square"];
G.STROKE_JOINTS_MAP = ["miter", "round", "bevel"];
G.BASE_64 = {};
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".split("").forEach(function (c, i) { G.BASE_64[c] = i; });
G.getRGB = function (r, g, b, alpha) {
    if (r != null && b == null) {
        alpha = g;
        b = r & 0xff; g = r >> 8 & 0xff; r = r >> 16 & 0xff;
    }
    return alpha == null ? "rgb(" + r + "," + g + "," + b + ")" : "rgba(" + r + "," + g + "," + b + "," + alpha + ")";
};
G.getHSL = function (hue, saturation, lightness, alpha) {
    return alpha == null ? "hsl(" + hue % 360 + "," + saturation + "%," + lightness + "%)"
        : "hsla(" + hue % 360 + "," + saturation + "%," + lightness + "%," + alpha + ")";
};

Object.defineProperty(GP, "instructions", {
    get: function () { this._updateInstructions(); return this._instructions; },
    configurable: true
});
GP.getInstructions = function () { return this.instructions; };
GP.isEmpty = function () { return !(this._instructions.length || this._activeInstructions.length); };
GP.draw = GP.drawAsPath = function () { missing("Graphics.draw() (drawing to a canvas context)"); };
GP.append = function (cmd, clean) {
    this._activeInstructions.push(cmd);
    this.command = cmd;
    if (!clean)
        this._dirty = true;
    return this;
};
GP.moveTo = function (x, y) { return this.append(new G.MoveTo(x, y), true); };
GP.lineTo = function (x, y) { return this.append(new G.LineTo(x, y)); };
GP.arcTo = function (x1, y1, x2, y2, radius) { return this.append(new G.ArcTo(x1, y1, x2, y2, radius)); };
GP.arc = function (x, y, radius, startAngle, endAngle, anticlockwise) {
    return this.append(new G.Arc(x, y, radius, startAngle, endAngle, anticlockwise));
};
GP.quadraticCurveTo = GP.curveTo = function (cpx, cpy, x, y) { return this.append(new G.QuadraticCurveTo(cpx, cpy, x, y)); };
GP.bezierCurveTo = function (cp1x, cp1y, cp2x, cp2y, x, y) {
    return this.append(new G.BezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y));
};
GP.rect = GP.drawRect = function (x, y, w, h) { return this.append(new G.Rect(x, y, w, h)); };
GP.closePath = function () { return this._activeInstructions.length ? this.append(new G.ClosePath()) : this; };
GP.clear = function () {
    this._instructions.length = this._activeInstructions.length = this._commitIndex = 0;
    this._strokeStyle = this._oldStrokeStyle = this._stroke = this._fill = this._strokeDash = this._oldStrokeDash = null;
    this._dirty = this._strokeIgnoreScale = false;
    return this;
};
GP.beginFill = function (color) { return this._setFill(color ? new G.Fill(color) : null); };
GP.beginLinearGradientFill = function (colors, ratios, x0, y0, x1, y1) {
    return this._setFill(new G.Fill().linearGradient(colors, ratios, x0, y0, x1, y1));
};
GP.beginRadialGradientFill = function (colors, ratios, x0, y0, r0, x1, y1, r1) {
    return this._setFill(new G.Fill().radialGradient(colors, ratios, x0, y0, r0, x1, y1, r1));
};
GP.beginBitmapFill = function (image, repetition, matrix) {
    return this._setFill(new G.Fill(null, matrix).bitmap(image, repetition));
};
GP.endFill = function () { return this.beginFill(); };
GP.setStrokeStyle = function (thickness, caps, joints, miterLimit, ignoreScale) {
    this._updateInstructions(true);
    this._strokeStyle = this.command = new G.StrokeStyle(thickness, caps, joints, miterLimit, ignoreScale);
    if (this._stroke)
        this._stroke.ignoreScale = ignoreScale;
    this._strokeIgnoreScale = ignoreScale;
    return this;
};
GP.setStrokeDash = function (segments, offset) {
    if (segments && segments.length)
        warnOnce("setStrokeDash (dashed lines)");
    this._updateInstructions(true);
    this._strokeDash = this.command = new G.StrokeDash(segments, offset);
    return this;
};
GP.beginStroke = function (color) { return this._setStroke(color ? new G.Stroke(color) : null); };
GP.beginLinearGradientStroke = function (colors, ratios, x0, y0, x1, y1) {
    return this._setStroke(new G.Stroke().linearGradient(colors, ratios, x0, y0, x1, y1));
};
GP.beginRadialGradientStroke = function (colors, ratios, x0, y0, r0, x1, y1, r1) {
    return this._setStroke(new G.Stroke().radialGradient(colors, ratios, x0, y0, r0, x1, y1, r1));
};
GP.beginBitmapStroke = function (image, repetition) {
    return this._setStroke(new G.Stroke().bitmap(image, repetition));
};
GP.endStroke = function () { return this.beginStroke(); };
GP.drawRoundRect = function (x, y, w, h, radius) { return this.drawRoundRectComplex(x, y, w, h, radius, radius, radius, radius); };
GP.drawRoundRectComplex = function (x, y, w, h, radiusTL, radiusTR, radiusBR, radiusBL) {
    return this.append(new G.RoundRect(x, y, w, h, radiusTL, radiusTR, radiusBR, radiusBL));
};
GP.drawCircle = function (x, y, radius) { return this.append(new G.Circle(x, y, radius)); };
GP.drawEllipse = function (x, y, w, h) { return this.append(new G.Ellipse(x, y, w, h)); };
GP.drawPolyStar = function (x, y, radius, sides, pointSize, angle) {
    return this.append(new G.PolyStar(x, y, radius, sides, pointSize, angle));
};
/// The compact paths the authoring tool writes (graphics.p("...")).
GP.decodePath = function (str) {
    var ops = [this.moveTo, this.lineTo, this.quadraticCurveTo, this.bezierCurveTo, this.closePath];
    var counts = [2, 2, 4, 6, 0], base64 = G.BASE_64, i = 0, l = str.length, params = [], x = 0, y = 0;
    while (i < l) {
        var c = str.charAt(i), n = base64[c], fi = n >> 3, f = ops[fi];
        if (!f || (n & 3))
            throw new Error("bad path data (@" + i + "): " + c);
        var pl = counts[fi];
        if (!fi)
            x = y = 0;
        params.length = 0;
        ++i;
        var charCount = (n >> 2 & 1) + 2;
        for (var p = 0; p < pl; ++p) {
            var num = base64[str.charAt(i)], sign = num >> 5 ? -1 : 1;
            num = ((num & 31) << 6) | base64[str.charAt(i + 1)];
            if (charCount === 3)
                num = (num << 6) | base64[str.charAt(i + 2)];
            num = sign * num / 10;
            if (p % 2)
                y = (num += y);
            else
                x = (num += x);
            params[p] = num;
            i += charCount;
        }
        f.apply(this, params);
    }
    return this;
};
GP.store = function () {
    this._updateInstructions(true);
    this._storeIndex = this._instructions.length;
    return this;
};
GP.unstore = function () { this._storeIndex = 0; return this; };
GP.clone = function () {
    var o = new Graphics();
    o.command = this.command;
    o._stroke = this._stroke; o._strokeStyle = this._strokeStyle; o._strokeDash = this._strokeDash;
    o._strokeIgnoreScale = this._strokeIgnoreScale; o._fill = this._fill;
    o._instructions = this._instructions.slice();
    o._commitIndex = this._commitIndex;
    o._activeInstructions = this._activeInstructions.slice();
    o._dirty = this._dirty;
    o._storeIndex = this._storeIndex;
    return o;
};
GP.toString = function () { return "[Graphics]"; };
GP._updateInstructions = function (commit) {
    var instr = this._instructions, active = this._activeInstructions;
    if (this._dirty && active.length) {
        instr.length = this._commitIndex;
        instr.push(G.beginCmd);
        appendAll(instr, active);
        if (this._fill)
            instr.push(this._fill);
        if (this._stroke) {
            if (this._strokeDash !== this._oldStrokeDash)
                instr.push(this._strokeDash);
            if (this._strokeStyle !== this._oldStrokeStyle)
                instr.push(this._strokeStyle);
            if (commit) {
                this._oldStrokeStyle = this._strokeStyle;
                this._oldStrokeDash = this._strokeDash;
            }
            instr.push(this._stroke);
        }
        this._dirty = false;
    }
    if (commit) {
        active.length = 0;
        this._commitIndex = instr.length;
    }
};
GP._setFill = function (fill) {
    this._updateInstructions(true);
    this.command = this._fill = fill;
    return this;
};
GP._setStroke = function (stroke) {
    this._updateInstructions(true);
    if ((this.command = this._stroke = stroke))
        stroke.ignoreScale = this._strokeIgnoreScale;
    return this;
};
G.prototype.mt = GP.moveTo; GP.lt = GP.lineTo; GP.at = GP.arcTo; GP.bt = GP.bezierCurveTo;
GP.qt = GP.quadraticCurveTo; GP.a = GP.arc; GP.r = GP.rect; GP.cp = GP.closePath; GP.c = GP.clear;
GP.f = GP.beginFill; GP.lf = GP.beginLinearGradientFill; GP.rf = GP.beginRadialGradientFill;
GP.bf = GP.beginBitmapFill; GP.ef = GP.endFill; GP.ss = GP.setStrokeStyle; GP.sd = GP.setStrokeDash;
GP.s = GP.beginStroke; GP.ls = GP.beginLinearGradientStroke; GP.rs = GP.beginRadialGradientStroke;
GP.bs = GP.beginBitmapStroke; GP.es = GP.endStroke; GP.dr = GP.drawRect; GP.rr = GP.drawRoundRect;
GP.rc = GP.drawRoundRectComplex; GP.dc = GP.drawCircle; GP.de = GP.drawEllipse; GP.dp = GP.drawPolyStar;
GP.p = GP.decodePath;

)JS" R"JS(
/// Path steps as the canvas makes them, in numbers for the native side
/// (see Native::replay): arcs and the rest made of lines and curves.
function PathBuilder(out) { this.out = out; this.has = false; this.x = this.y = this.sx = this.sy = 0; this.drew = false; }
var PB = PathBuilder.prototype;
PB.moveTo = function (x, y) {
    this.out.push(2, +x || 0, +y || 0);
    this.x = this.sx = +x || 0; this.y = this.sy = +y || 0;
    this.has = true;
};
PB.lineTo = function (x, y) {
    if (!this.has)
        return this.moveTo(x, y);
    this.out.push(3, +x || 0, +y || 0);
    this.x = +x || 0; this.y = +y || 0;
    this.drew = true;
};
PB.quadraticCurveTo = function (cpx, cpy, x, y) {
    if (!this.has)
        this.moveTo(cpx, cpy);
    this.out.push(4, +cpx || 0, +cpy || 0, +x || 0, +y || 0);
    this.x = +x || 0; this.y = +y || 0;
    this.drew = true;
};
PB.bezierCurveTo = function (c1x, c1y, c2x, c2y, x, y) {
    if (!this.has)
        this.moveTo(c1x, c1y);
    this.out.push(5, +c1x || 0, +c1y || 0, +c2x || 0, +c2y || 0, +x || 0, +y || 0);
    this.x = +x || 0; this.y = +y || 0;
    this.drew = true;
};
PB.closePath = function () {
    if (!this.has)
        return;
    this.out.push(6);
    this.x = this.sx; this.y = this.sy;
};
PB.rect = function (x, y, w, h) {
    this.moveTo(x, y);
    this.lineTo(x + w, y); this.lineTo(x + w, y + h); this.lineTo(x, y + h);
    this.closePath();
};
/// An arc of the circle at (cx, cy), from angle a0 through sweep, in
/// curves of at most a quarter turn; joined to the path by a line, as the
/// canvas joins it.
PB.sweep = function (cx, cy, r, a0, sweep) {
    var sx = cx + Math.cos(a0) * r, sy = cy + Math.sin(a0) * r;
    if (this.has)
        this.lineTo(sx, sy);
    else
        this.moveTo(sx, sy);
    var pieces = Math.max(1, Math.ceil(Math.abs(sweep) / (Math.PI / 2) - 1e-9));
    var step = sweep / pieces, k = 4 / 3 * Math.tan(step / 4);
    for (var i = 0; i < pieces; ++i) {
        var t1 = a0 + step * i, t2 = t1 + step;
        var c1 = Math.cos(t1), s1 = Math.sin(t1), c2 = Math.cos(t2), s2 = Math.sin(t2);
        this.bezierCurveTo(cx + r * (c1 - k * s1), cy + r * (s1 + k * c1), cx + r * (c2 + k * s2), cy + r * (s2 - k * c2),
            cx + r * c2, cy + r * s2);
    }
};
PB.arc = function (x, y, r, a0, a1, anticlockwise) {
    r = Math.abs(+r || 0);
    var full = Math.PI * 2, sweep;
    if (!anticlockwise)
        sweep = a1 - a0 >= full ? full : (((a1 - a0) % full) + full) % full;
    else
        sweep = a0 - a1 >= full ? -full : -((((a0 - a1) % full) + full) % full);
    this.sweep(+x || 0, +y || 0, r, +a0 || 0, sweep);
};
PB.arcTo = function (x1, y1, x2, y2, r) {
    if (!this.has)
        this.moveTo(x1, y1);
    var x0 = this.x, y0 = this.y;
    var ax = x0 - x1, ay = y0 - y1, bx = x2 - x1, by = y2 - y1;
    var la = Math.sqrt(ax * ax + ay * ay), lb = Math.sqrt(bx * bx + by * by);
    var cross = ax * by - ay * bx;
    if (!(r > 0) || la === 0 || lb === 0 || Math.abs(cross) < 1e-9) {
        this.lineTo(x1, y1);
        return;
    }
    ax /= la; ay /= la; bx /= lb; by /= lb;
    var angle = Math.acos(Math.max(-1, Math.min(1, ax * bx + ay * by)));
    var reach = r / Math.tan(angle / 2), out = r / Math.sin(angle / 2);
    var mx = ax + bx, my = ay + by, ml = Math.sqrt(mx * mx + my * my);
    var cx = x1 + mx / ml * out, cy = y1 + my / ml * out;
    var t1x = x1 + ax * reach, t1y = y1 + ay * reach, t2x = x1 + bx * reach, t2y = y1 + by * reach;
    var a0 = Math.atan2(t1y - cy, t1x - cx), a1 = Math.atan2(t2y - cy, t2x - cx);
    var sweep = a1 - a0;
    while (sweep > Math.PI) sweep -= Math.PI * 2;
    while (sweep < -Math.PI) sweep += Math.PI * 2;
    this.sweep(cx, cy, r, a0, sweep);
};
var CAPS = { butt: 0, round: 1, square: 2 }, JOINTS = { miter: 0, round: 1, bevel: 2 };
function paintOf(holder, out) {
    var style = holder ? holder.style : null;
    if (style && typeof style === "object" && (style.type === "linear" || style.type === "radial")) {
        var colors = style.colors || [], ratios = style.ratios || [], n = Math.min(colors.length, ratios.length);
        out.push(style.type === "linear" ? 2 : 3, n);
        for (var i = 0; i < n; ++i) {
            var c = parseColor(colors[i]) || [0, 0];
            out.push(c[0], c[1], Number(ratios[i]) || 0);
        }
        if (style.type === "linear")
            out.push(+style.x0 || 0, +style.y0 || 0, +style.x1 || 0, +style.y1 || 0);
        else
            out.push(+style.x0 || 0, +style.y0 || 0, +style.r0 || 0, +style.x1 || 0, +style.y1 || 0, +style.r1 || 0);
        return true;
    }
    if (style && typeof style === "object" && style.type === "bitmap") {
        var image = imageOf(style.image), m = holder.matrix || Matrix2D.identity;
        if (!image) {
            out.push(0);
            return false;
        }
        out.push(4, image.id, style.repetition === "no-repeat" ? 0 : 1, m.a, m.b, m.c, m.d, m.tx, m.ty);
        return true;
    }
    var color = parseColor(style);
    if (!color) {
        out.push(0);
        return false;
    }
    out.push(1, color[0], color[1]);
    return true;
}
/// The paths a Graphics draws, as the native side takes them.
GP._list = function () {
    var instr = this.instructions, out = [], path = [], builder = new PathBuilder(path), style = null;
    var from = this._storeIndex || 0;
    for (var i = from; i < instr.length; ++i) {
        var cmd = instr[i];
        if (cmd instanceof G.BeginPath) {
            path.length = 0;
            builder = new PathBuilder(path);
        } else if (cmd instanceof G.StrokeStyle)
            style = cmd;
        else if (cmd instanceof G.Fill || cmd instanceof G.Stroke) {
            if (!builder.drew)
                continue;
            var stroke = cmd instanceof G.Stroke, group = [1];
            var filled = paintOf(stroke ? null : cmd, group), stroked = paintOf(stroke ? cmd : null, group);
            if (!filled && !stroked)
                continue;
            var width = style && style.width != null ? Number(style.width) : 1;
            var caps = style ? (typeof style.caps === "number" ? style.caps : CAPS[style.caps] || 0) : 0;
            var joints = style ? (typeof style.joints === "number" ? style.joints : JOINTS[style.joints] || 0) : 0;
            group.push(isFinite(width) ? width : 1, caps, joints,
                style && style.miterLimit != null ? Number(style.miterLimit) || 10 : 10, stroke && cmd.ignoreScale ? 1 : 0);
            appendAll(out, group);
            appendAll(out, path);
        } else if (cmd && cmd._path)
            cmd._path(builder);
    }
    return out;
};

)JS" R"JS(
// Shape, Text, Bitmap and Sprite: display objects the scripts make, what
// they show given to the native side as they change (_sync).

function Shape(graphics) {
    own(this, "sprite");
    this.graphics = graphics || new Graphics();
}
var SH = subclass(Shape, DisplayObject);
SH._sync = function () {
    var drawn = this.graphics instanceof Graphics ? this.graphics._list() : [];
    var key = drawn.join(",");
    if (key !== this.__drawn) {
        hidden(this, "__drawn", key);
        P.graphics(this.__id, drawn);
    }
};
SH.isVisible = function () { return DO.isVisible.call(this) && !!this.graphics && !this.graphics.isEmpty(); };
SH.clone = function (recursive) {
    var copy = new Shape(recursive && this.graphics ? this.graphics.clone() : this.graphics);
    copyDisplay(this, copy);
    return copy;
};
SH.toString = function () { return "[Shape (name=" + this.name + ")]"; };
function copyDisplay(from, to) {
    ["x", "y", "scaleX", "scaleY", "rotation", "skewX", "skewY", "regX", "regY", "alpha", "visible", "name",
        "compositeOperation", "shadow", "filters", "mask", "cursor"].forEach(function (key) {
        var value = from[key];
        if (value !== null && value !== undefined)
            to[key] = Array.isArray(value) ? value.slice() : value;
    });
}

var ALIGN = { left: 0, start: 0, center: 1, right: 2, end: 2 };
var BASELINE = { top: 0, hanging: 1, middle: 2, alphabetic: 3, ideographic: 4, bottom: 5 };
function Text(text, font, color) {
    own(this, "text");
    this.text = text;
    this.font = font;
    this.color = color;
    this.textAlign = "left";
    this.textBaseline = "top";
    this.maxWidth = null;
    this.outline = 0;
    this.lineHeight = 0;
    this.lineWidth = null;
}
var TX = subclass(Text, DisplayObject);
TX._sync = function () {
    var font = parseFont(this.font), color = parseColor(this.color || "#000") || [0, 1];
    var args = [this.__id, this.text === null || this.text === undefined ? "" : String(this.text), font.family, font.size,
        font.bold, font.italic, color[0], ALIGN[this.textAlign] || 0,
        BASELINE.hasOwnProperty(this.textBaseline) ? BASELINE[this.textBaseline] : 0, Number(this.lineWidth) || 0,
        Number(this.lineHeight) || 0, Number(this.maxWidth) || 0];
    var key = args.join("\u0001");
    if (key !== this.__styled) {
        hidden(this, "__styled", key);
        hidden(this, "__metrics", P.textStyle.apply(null, args) || [0, 0, 0]);
        if (this.outline)
            warnOnce("Text.outline (text drawn as an outline)");
    }
};
TX.getMeasuredWidth = function () { this._sync(); return this.__metrics[0]; };
TX.getMeasuredHeight = function () { this._sync(); return this.__metrics[1]; };
TX.getMeasuredLineHeight = function () { this._sync(); return this.__metrics[2]; };
TX.isVisible = function () {
    return DO.isVisible.call(this) && this.text !== null && this.text !== undefined && this.text !== "";
};
TX.clone = function () {
    var copy = new Text(this.text, this.font, this.color);
    ["textAlign", "textBaseline", "maxWidth", "outline", "lineHeight", "lineWidth"].forEach(function (key) { copy[key] = this[key]; }, this);
    copyDisplay(this, copy);
    return copy;
};
TX.toString = function () {
    var text = String(this.text);
    return "[Text (text=" + (text.length > 20 ? text.substr(0, 17) + "..." : text) + ")]";
};

/// The drawing of part of an image, its corner at (dx, dy).
function imageDrawing(image, sx, sy, sw, sh, dx, dy) {
    return [1, 4, image.id, 0, 1, 0, 0, 1, dx - sx, dy - sy, 0, 0, 0, 0, 10, 0,
        2, dx, dy, 3, dx + sw, dy, 3, dx + sw, dy + sh, 3, dx, dy + sh, 6];
}
function Bitmap(imageOrUri) {
    own(this, "sprite");
    this.image = typeof imageOrUri === "string" ? (libraryImage(imageOrUri) || imageOrUri) : imageOrUri;
    this.sourceRect = null;
}
var BM = subclass(Bitmap, DisplayObject);
BM._sync = function () {
    var image = imageOf(this.image), r = this.sourceRect, drawn = [];
    if (image)
        drawn = r ? imageDrawing(image, r.x, r.y, r.width, r.height, 0, 0) : imageDrawing(image, 0, 0, image.width, image.height, 0, 0);
    var key = drawn.join(",");
    if (key !== this.__drawn) {
        hidden(this, "__drawn", key);
        P.graphics(this.__id, drawn);
    }
};
BM.isVisible = function () { return DO.isVisible.call(this) && !!imageOf(this.image); };
BM.getBounds = function () {
    if (this.__bounds)
        return this.__bounds.clone();
    var image = imageOf(this.image), r = this.sourceRect;
    return image ? new Rectangle(0, 0, r ? r.width : image.width, r ? r.height : image.height) : null;
};
BM.clone = function () {
    var copy = new Bitmap(this.image);
    if (this.sourceRect)
        copy.sourceRect = this.sourceRect.clone();
    copyDisplay(this, copy);
    return copy;
};
BM.toString = function () { return "[Bitmap (name=" + this.name + ")]"; };

/// SpriteSheet: frames cut from images, and animations of them.
function SpriteSheet(data) {
    EventDispatcher.call(this);
    this.complete = true;
    this.framerate = data && data.framerate || 0;
    hidden(this, "_frames", []);
    hidden(this, "_animations", []);
    hidden(this, "_data", {});
    if (!data)
        return;
    var images = (data.images || []).map(function (image) {
        return typeof image === "string" ? (libraryImage(image) || image) : image;
    });
    var frames = data.frames, self = this;
    if (Array.isArray(frames)) {
        frames.forEach(function (f) {
            self._frames.push({ image: images[f[4] | 0], rect: new Rectangle(f[0], f[1], f[2], f[3]), regX: f[5] || 0, regY: f[6] || 0 });
        });
    } else if (frames) {
        var w = frames.width, h = frames.height, spacing = frames.spacing || 0, margin = frames.margin || 0;
        var count = frames.count === undefined ? Infinity : frames.count;
        images.forEach(function (image) {
            var size = imageOf(image);
            if (!size || !(w > 0) || !(h > 0))
                return;
            for (var y = margin; y + h <= size.height - margin + 1e-9 && self._frames.length < count; y += h + spacing)
                for (var x = margin; x + w <= size.width - margin + 1e-9 && self._frames.length < count; x += w + spacing)
                    self._frames.push({ image: image, rect: new Rectangle(x, y, w, h), regX: frames.regX || 0, regY: frames.regY || 0 });
        });
    }
    var animations = data.animations || {};
    Object.keys(animations).forEach(function (name) {
        var o = animations[name], anim = { name: name, frames: [], speed: 1, next: undefined };
        if (typeof o === "number")
            anim.frames = [o];
        else if (Array.isArray(o)) {
            if (o.length === 1)
                anim.frames = [o[0]];
            else {
                anim.speed = o[3] || 1;
                anim.next = o[2];
                for (var i = o[0]; i <= o[1]; ++i)
                    anim.frames.push(i);
            }
        } else {
            anim.speed = o.speed || 1;
            anim.next = o.next;
            anim.frames = Array.isArray(o.frames) ? o.frames.slice() : [o.frames];
        }
        if (anim.next === true || anim.next === undefined)
            anim.next = name;
        if (anim.next === false || (anim.frames.length < 2 && anim.next === name))
            anim.next = null;
        self._animations.push(name);
        self._data[name] = anim;
    });
}
var SS = subclass(SpriteSheet, EventDispatcher);
Object.defineProperty(SS, "animations", { get: function () { return this._animations.slice(); }, configurable: true });
SS.getNumFrames = function (animation) {
    if (animation == null)
        return this._frames.length;
    var data = this._data[animation];
    return data ? data.frames.length : 0;
};
SS.getAnimations = function () { return this._animations.slice(); };
SS.getAnimation = function (name) { return this._data[name] || null; };
SS.getFrame = function (index) { return this._frames[index] || null; };
SS.getFrameBounds = function (index, rect) {
    var frame = this._frames[index];
    return frame ? (rect || new Rectangle()).setValues(-frame.regX, -frame.regY, frame.rect.width, frame.rect.height) : null;
};
SS.clone = function () { missing("SpriteSheet.clone()"); };
SS.toString = function () { return "[SpriteSheet]"; };

/// Sprite: a SpriteSheet's frames, played as the stage ticks.
function Sprite(spriteSheet, frameOrAnimation) {
    own(this, "sprite");
    this.currentFrame = 0;
    this.currentAnimation = null;
    this.paused = true;
    this.spriteSheet = spriteSheet;
    this.currentAnimationFrame = 0;
    this.framerate = 0;
    hidden(this, "_animation", null);
    hidden(this, "_currentFrame", null);
    hidden(this, "_skipAdvance", false);
    sprites.add(this.__id);
    if (frameOrAnimation != null)
        this.gotoAndPlay(frameOrAnimation);
}
var SP = subclass(Sprite, DisplayObject);
SP._sync = function () {
    var sheet = this.spriteSheet, frame = sheet && sheet.getFrame ? sheet.getFrame(this._currentFrame | 0) : null;
    var image = frame ? imageOf(frame.image) : null;
    var drawn = image ? imageDrawing(image, frame.rect.x, frame.rect.y, frame.rect.width, frame.rect.height, -frame.regX, -frame.regY) : [];
    var key = drawn.join(",");
    if (key !== this.__drawn) {
        hidden(this, "__drawn", key);
        P.graphics(this.__id, drawn);
    }
};
SP.play = function () { this.paused = false; };
SP.stop = function () { this.paused = true; };
SP.gotoAndPlay = function (frameOrAnimation) { this.paused = false; this._skipAdvance = true; this._goto(frameOrAnimation); };
SP.gotoAndStop = function (frameOrAnimation) { this.paused = true; this._goto(frameOrAnimation); };
SP.advance = function (time) {
    var fps = this.framerate || (this.spriteSheet && this.spriteSheet.framerate);
    this._normalizeFrame(fps && time != null ? time / (1000 / fps) : 1);
};
SP.getBounds = function () {
    if (this.__bounds)
        return this.__bounds.clone();
    return this.spriteSheet ? this.spriteSheet.getFrameBounds(this.currentFrame) : null;
};
SP.clone = function () { missing("Sprite.clone()"); };
SP.toString = function () { return "[Sprite (name=" + this.name + ")]"; };
SP._tick = function (delta) {
    if (!this.paused) {
        if (!this._skipAdvance)
            this.advance(delta);
        this._skipAdvance = false;
    }
};
SP._goto = function (frameOrAnimation, frame) {
    this.currentAnimationFrame = 0;
    if (isNaN(frameOrAnimation)) {
        var data = this.spriteSheet && this.spriteSheet.getAnimation(frameOrAnimation);
        if (data) {
            this._animation = data;
            this.currentAnimation = frameOrAnimation;
            this._normalizeFrame(frame);
        }
    } else {
        this.currentAnimation = this._animation = null;
        this._currentFrame = frameOrAnimation;
        this._normalizeFrame();
    }
};
SP._normalizeFrame = function (frameDelta) {
    frameDelta = frameDelta || 0;
    var animation = this._animation, paused = this.paused, frame = this._currentFrame, l;
    if (animation) {
        var speed = animation.speed || 1, animFrame = this.currentAnimationFrame;
        l = animation.frames.length;
        if (animFrame + frameDelta * speed >= l) {
            var next = animation.next;
            if (this._dispatchAnimationEnd(animation, frame, paused, next, l - 1))
                return;
            else if (next)
                return this._goto(next, frameDelta - (l - animFrame) / speed);
            else {
                this.paused = true;
                animFrame = animation.frames.length - 1;
            }
        } else
            animFrame += frameDelta * speed;
        this.currentAnimationFrame = animFrame;
        this._currentFrame = animation.frames[animFrame | 0];
    } else {
        frame = (this._currentFrame += frameDelta);
        l = this.spriteSheet ? this.spriteSheet.getNumFrames() : 0;
        if (frame >= l && l > 0) {
            if (!this._dispatchAnimationEnd(animation, frame, paused, l - 1))
                if ((this._currentFrame -= l) >= l)
                    return this._normalizeFrame();
        }
    }
    frame = this._currentFrame | 0;
    if (this.currentFrame !== frame) {
        this.currentFrame = frame;
        this.dispatchEvent("change");
    }
};
SP._dispatchAnimationEnd = function (animation, frame, paused, next, end) {
    var name = animation ? animation.name : null;
    if (this.hasEventListener("animationend")) {
        var event = new Event("animationend");
        event.name = name;
        event.next = next;
        this.dispatchEvent(event);
    }
    var changed = this._animation !== animation || this._currentFrame !== frame;
    if (!changed && !paused && this.paused) {
        this.currentAnimationFrame = end;
        changed = true;
    }
    return changed;
};

)JS" R"JS(
// Container: what holds display objects, as MovieClip and the stage do.

function Container() { own(this, "sprite"); }
var CT = subclass(Container, DisplayObject);
Object.defineProperty(CT, "children", {
    get: function () { return P.children(this.__id).map(wrap).filter(function (o) { return o; }); },
    configurable: true
});
Object.defineProperty(CT, "numChildren", { get: function () { return P.get(this.__id, "numChildren"); }, configurable: true });
Object.defineProperty(CT, "mouseChildren", {
    get: function () { return P.get(this.__id, "mouseChildren"); },
    set: function (value) { P.set(this.__id, "mouseChildren", value); },
    configurable: true
});
CT.tickChildren = true;
CT.addChild = function () {
    for (var i = 0; i < arguments.length; ++i) {
        var child = displayArgument(arguments[i], "addChild");
        P.add(this.__id, child.__id);
        hold(child);
    }
    return arguments[arguments.length - 1];
};
CT.addChildAt = function () {
    var index = arguments[arguments.length - 1], child = null;
    for (var i = 0; i < arguments.length - 1; ++i) {
        child = displayArgument(arguments[i], "addChildAt");
        P.add(this.__id, child.__id, Math.max(0, index + i));
        hold(child);
    }
    return child;
};
CT.removeChild = function () {
    var removed = false;
    for (var i = 0; i < arguments.length; ++i)
        removed = P.remove(this.__id, displayArgument(arguments[i], "removeChild").__id) || removed;
    return removed;
};
CT.removeChildAt = function () {
    var children = this.children, removed = false;
    for (var i = 0; i < arguments.length; ++i)
        if (children[arguments[i]])
            removed = P.remove(this.__id, children[arguments[i]].__id) || removed;
    return removed;
};
CT.removeAllChildren = function () {
    var self = this;
    P.children(this.__id).forEach(function (id) { P.remove(self.__id, id); });
};
CT.getChildAt = function (index) {
    var ids = P.children(this.__id);
    return index >= 0 && index < ids.length ? wrap(ids[index]) : null;
};
CT.getChildByName = function (name) {
    var children = this.children;
    for (var i = 0; i < children.length; ++i)
        if (children[i].name === name)
            return children[i];
    return null;
};
CT.getChildIndex = function (child) { return P.children(this.__id).indexOf(child && child.__id); };
CT.setChildIndex = function (child, index) {
    if (child && child.parent === this)
        P.add(this.__id, displayArgument(child, "setChildIndex").__id, index);
};
CT.swapChildren = function (a, b) {
    var ia = this.getChildIndex(a), ib = this.getChildIndex(b);
    if (ia < 0 || ib < 0 || ia === ib)
        return;
    var low = ia < ib ? a : b, high = ia < ib ? b : a, lowAt = Math.min(ia, ib), highAt = Math.max(ia, ib);
    P.add(this.__id, high.__id, lowAt);
    P.add(this.__id, low.__id, highAt);
};
CT.swapChildrenAt = function (i, j) {
    var children = this.children;
    if (children[i] && children[j])
        this.swapChildren(children[i], children[j]);
};
CT.sortChildren = function (sortFunction) {
    var sorted = this.children.slice().sort(sortFunction), self = this;
    sorted.forEach(function (child, i) { P.add(self.__id, child.__id, i); });
};
CT.contains = function (child) {
    for (var at = child; at; at = at.parent)
        if (at === this)
            return true;
    return false;
};
function under(container, x, y, all, out) {
    var children = container.children;
    for (var i = children.length - 1; i >= 0; --i) {
        var child = children[i];
        if (!child.visible)
            continue;
        if (child instanceof Container || child.__clip) {
            var deeper = under(child, x, y, all, out);
            if (deeper && !all)
                return deeper;
        }
        var local = child.globalToLocal(x, y);
        if (child.hitTest(local.x, local.y) && !(child instanceof Container || child.__clip)) {
            if (!all)
                return child;
            out.push(child);
        }
    }
    return null;
}
CT.getObjectUnderPoint = function (x, y) {
    var g = this.localToGlobal(x, y);
    return under(this, g.x, g.y, false, []);
};
CT.getObjectsUnderPoint = function (x, y) {
    var g = this.localToGlobal(x, y), out = [];
    under(this, g.x, g.y, true, out);
    return out;
};
CT.clone = function () { missing("Container.clone()"); };
CT.toString = function () { return "[Container (name=" + this.name + ")]"; };

// MovieClip: one a timeline placed or the library made (its frames the
// runtime's, __clip), or one a script made, whose timeline is its tweens
// (timeline.addTween), played here.

function MovieClip(mode, startPosition, loop, labels) {
    var props = mode && typeof mode === "object" ? mode : null;
    if (props) {
        mode = props.mode; startPosition = props.startPosition; loop = props.loop; labels = props.labels;
    }
    own(this, "sprite");
    hidden(this, "__tl", { raw: -1, position: -1, managed: new Set(), loop: loop === undefined ? true : !!loop, paused: false });
    this.mode = mode || MovieClip.INDEPENDENT;
    this.startPosition = startPosition || 0;
    this.actionsEnabled = true;
    this.autoReset = true;
    this.framerate = null;
    this.frameBounds = null;
    this.timeline = new Timeline({ paused: true, useTicks: true });
    if (labels)
        this.timeline.setLabels(labels);
    codeClips.add(this.__id);
}
var MC = subclass(MovieClip, Container);
MovieClip.INDEPENDENT = "independent";
MovieClip.SINGLE_FRAME = "single";
MovieClip.SYNCHED = "synched";
MC.initialize = function () { MovieClip.apply(this, arguments); };
function scripted(clip) { return !clip.__clip && Object.prototype.hasOwnProperty.call(clip, "__tl"); }
function clipLabels(clip) { return scripted(clip) ? clip.timeline.getLabels() : P.get(clip.__id, "labels") || []; }
Object.defineProperty(MC, "paused", {
    get: function () { return scripted(this) ? this.__tl.paused : P.get(this.__id, "paused"); },
    set: function (value) {
        if (scripted(this))
            this.__tl.paused = !!value;
        else
            P.set(this.__id, "paused", value);
    },
    configurable: true
});
Object.defineProperty(MC, "currentFrame", {
    get: function () { return scripted(this) ? Math.max(0, this.__tl.position) : P.get(this.__id, "currentFrame"); },
    set: function () {},
    configurable: true
});
Object.defineProperty(MC, "totalFrames", {
    get: function () { return scripted(this) ? Math.max(1, this.timeline.duration) : P.get(this.__id, "totalFrames"); },
    set: function () {},
    configurable: true
});
Object.defineProperty(MC, "duration", { get: function () { return this.totalFrames; }, configurable: true });
Object.defineProperty(MC, "labels", { get: function () { return clipLabels(this); }, set: function () {}, configurable: true });
Object.defineProperty(MC, "currentLabel", {
    get: function () {
        if (!scripted(this))
            return P.get(this.__id, "currentLabel");
        var label = null, at = this.currentFrame;
        clipLabels(this).forEach(function (l) { if (l.position <= at) label = l.label; });
        return label;
    },
    set: function () {},
    configurable: true
});
Object.defineProperty(MC, "loop", {
    get: function () { return scripted(this) ? this.__tl.loop : !notLooping.has(this); },
    set: function (value) {
        var on = !(value === false || value === 0);
        if (scripted(this))
            this.__tl.loop = on;
        else if (on)
            notLooping.delete(this);
        else
            notLooping.add(this);
    },
    configurable: true
});
MC.play = function () {
    if (scripted(this))
        this.__tl.paused = false;
    else
        P.play(this.__id, true);
};
MC.stop = function () {
    if (scripted(this))
        this.__tl.paused = true;
    else
        P.play(this.__id, false);
};
function go(clip, target, play, how) {
    if (typeof target === "number")
        target = Math.floor(target);
    else if (typeof target !== "string")
        throw new TypeError(how + ": a frame number (from 0) or a label, not " + show(target));
    if (scripted(clip)) {
        var at = clip.timeline.resolve(target);
        if (at == null) {
            P.print("Warning: " + how + "(" + JSON.stringify(target) + "): there is no such label" + (clip.name ? " in " + clip.name : ""));
            return;
        }
        clip.__tl.paused = !play;
        clip._updateTimeline(Math.max(0, at), true);
        return;
    }
    if (!P.go(clip.__id, target, play))
        P.print("Warning: " + how + "(" + JSON.stringify(target) + "): there is no such " +
            (typeof target === "string" ? "label" : "frame") + (clip.name ? " in " + clip.name : ""));
}
MC.gotoAndPlay = function (positionOrLabel) { go(this, positionOrLabel, true, "gotoAndPlay"); };
MC.gotoAndStop = function (positionOrLabel) { go(this, positionOrLabel, false, "gotoAndStop"); };
MC.advance = function () {
    if (scripted(this)) {
        if (!this.__tl.paused)
            this._updateTimeline(this.__tl.raw + 1, false);
        return;
    }
    var next = this.currentFrame + 1;
    go(this, next >= this.totalFrames ? 0 : next, !this.paused, "advance");
};
MC.getBounds = function () {
    if (this.__bounds)
        return this.__bounds.clone();
    if (this.nominalBounds && scripted(this))
        return this.nominalBounds.clone ? this.nominalBounds.clone() : this.nominalBounds;
    return DO.getBounds.call(this);
};
MC.cache = DO.cache;
MC.clone = function () { missing("MovieClip.clone()"); };
MC.toString = function () { return "[MovieClip (name=" + this.name + ")]"; };
MC._reset = function () {
    this.__tl.raw = this.__tl.position = -1;
    this.__tl.paused = false;
};
/// The clip's timeline at \a raw (from 0): its tweens' values set, its
/// children those its tweens place (the first tween's on top), then the
/// actions passed (or only those there, after a jump).
MC._updateTimeline = function (raw, jump) {
    var s = this.__tl, tl = this.timeline, length = Math.max(1, tl.duration);
    var was = s.position;
    var position = raw >= length ? (s.loop ? raw % length : length - 1) : raw;
    s.raw = position;
    s.position = position;
    tl.position = position;
    tl.tweens.forEach(function (tween) { tween._applyAt(position); });
    this._manage();
    if (!this.actionsEnabled || was === position)
        return;
    var self = this;
    tl.tweens.slice().forEach(function (tween) { tween._runActions(was, position, jump || was < 0, self); });
};
MC._manage = function () {
    var s = this.__tl, want = [], seen = new Set(), self = this;
    function take(child) {
        if (!isDisplay(child) || child === self || child._off || seen.has(child))
            return;
        seen.add(child);
        want.push(child);
    }
    this.timeline.tweens.forEach(function (tween) {
        var target = tween.target;
        if (target === self || tween.passive)
            return;
        if (isDisplay(target))
            take(target);
        else if (target && Array.isArray(target.state))
            target.state.forEach(function (entry) {
                if (!entry || !entry.t)
                    return;
                for (var key in entry.p || {})
                    entry.t[key] = entry.p[key];
                take(entry.t);
            });
    });
    // The first tween's on top: from the bottom, the last first.
    var managed = new Set();
    for (var i = want.length - 1, at = 0; i >= 0; --i, ++at) {
        var child = want[i];
        managed.add(child.__id);
        if (P.children(this.__id)[at] !== child.__id) {
            P.add(this.__id, child.__id, at);
            hold(child);
        }
        if (!s.managed.has(child.__id) && scripted(child) && child.autoReset && child.mode === MovieClip.INDEPENDENT)
            child._reset();
    }
    s.managed.forEach(function (id) {
        if (!managed.has(id))
            P.remove(self.__id, id);
    });
    s.managed = managed;
};

/// Timeline: the tweens a script-made MovieClip plays, and its labels.
function Timeline(props) {
    EventDispatcher.call(this);
    this.tweens = [];
    this.duration = 0;
    this.position = 0;
    this.loop = !!(props && props.loop);
    this.useTicks = true;
    hidden(this, "_labels", {});
}
var TL = subclass(Timeline, EventDispatcher);
TL.addTween = function () {
    for (var i = 0; i < arguments.length; ++i) {
        var tween = arguments[i];
        if (!(tween instanceof Tween))
            continue;
        tween.paused = true;
        if (this.tweens.indexOf(tween) < 0)
            this.tweens.push(tween);
        if (tween.duration > this.duration)
            this.duration = tween.duration;
    }
    return arguments[arguments.length - 1];
};
TL.removeTween = function (tween) {
    var at = this.tweens.indexOf(tween);
    if (at < 0)
        return false;
    this.tweens.splice(at, 1);
    this.duration = this.tweens.reduce(function (d, t) { return Math.max(d, t.duration); }, 0);
    return true;
};
TL.addLabel = function (label, position) { this._labels[label] = position; };
TL.setLabels = function (labels) { hidden(this, "_labels", Object.assign({}, labels || {})); };
TL.getLabels = function () {
    var labels = this._labels;
    return Object.keys(labels).map(function (label) { return { label: label, position: labels[label] }; })
        .sort(function (a, b) { return a.position - b.position; });
};
TL.resolve = function (positionOrLabel) {
    var position = Number(positionOrLabel);
    if (isNaN(position))
        position = this._labels[positionOrLabel];
    return position == null || isNaN(position) ? null : position;
};
TL.getCurrentLabel = function () {
    var label = null, at = this.position;
    this.getLabels().forEach(function (l) { if (l.position <= at) label = l.label; });
    return label;
};
TL.toString = function () { return "[Timeline]"; };

// The clips a timeline placed or the library made: one object each, the
// instances in it found by name.

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
    if (id === -1)
        return stage;
    if (typeof id !== "number" || id < 0)
        return null;
    var ref = made.get(id);
    if (ref)
        return ref.deref() || null;
    var clip = clips.get(id);
    if (!clip) {
        var target = Object.create(MC);
        hidden(target, "__id", id);
        hidden(target, "__clip", true);
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
// The stage: the display list's, the main timeline its first child.

function Stage() { missing("new createjs.Stage()"); }
subclass(Stage, Container);
var stage = Object.create(Stage.prototype);
hidden(stage, "__id", -1);
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
hidden(stage, "canvas", { get width() { return P.stage("width"); }, get height() { return P.stage("height"); },
    style: {}, addEventListener: function () {}, removeEventListener: function () {},
    getContext: function () { missing("stage.canvas.getContext()"); } });
hidden(stage, "parent", null);
hidden(stage, "stage", stage);
hidden(stage, "name", "stage");
hidden(stage, "mouseInBounds", true);
hidden(stage, "autoClear", true);
hidden(stage, "tickOnUpdate", true);
hidden(stage, "mouseMoveOutside", false);
hidden(stage, "snapToPixelEnabled", false);
hidden(stage, "preventSelection", true);
["enableMouseOver", "enableDOMEvents", "update", "clear", "tick", "setBounds", "cache", "uncache", "updateCache"]
    .forEach(function (name) { hidden(stage, name, function () {}); });
hidden(stage, "localToGlobal", function (x, y, pt) { return (pt || new Point()).setValues(x, y); });
hidden(stage, "globalToLocal", function (x, y, pt) { return (pt || new Point()).setValues(x, y); });
hidden(stage, "toDataURL", function () { missing("stage.toDataURL()"); });
hidden(stage, "toString", function () { return "[Stage]"; });
var exportRoot = wrap(P.root());

function DOMElement() { missing("new createjs.DOMElement() (HTML elements over the canvas)"); }

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
    createElement: function (name) {
        name = String(name).toLowerCase();
        if (name === "img")
            return new global.Image();
        if (name === "canvas")
            return { width: 300, height: 150, style: {}, getContext: function () { missing("canvas.getContext()"); },
                addEventListener: function () {}, removeEventListener: function () {} };
        missing("document.createElement(\"" + name + "\")");
    },
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
    hidden(this, "_init", {});
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
/// What the target has of each property as a step first names it, for a
/// timeline to go back to.
TW._remember = function (props) {
    for (var key in props)
        if (!(key in this._init))
            this._init[key] = this.target ? this.target[key] : undefined;
};
TW.to = function (props, duration, ease) {
    this._remember(props || {});
    return this._add({ type: "to", props: props || {}, duration: Math.max(0, Number(duration) || 0), ease: ease || Ease.linear });
};
TW.wait = function (duration, passive) { return this._add({ type: "wait", duration: Math.max(0, Number(duration) || 0) }); };
TW.call = function (callback, params, scope) {
    return this._add({ type: "call", callback: callback, params: params || [this], scope: scope });
};
TW.set = function (props, target) {
    if (!target || target === this.target)
        this._remember(props || {});
    return this._add({ type: "set", props: props || {}, target: target }); };
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
/// In a script-made clip's timeline: the target as it is at \a position
/// (in frames), its actions left for _runActions.
TW._applyAt = function (position) {
    var target = this.target, values = {}, key;
    if (!target)
        return;
    for (key in this._init)
        values[key] = this._init[key];
    for (var i = 0; i < this._steps.length; ++i) {
        var step = this._steps[i];
        if (step.start > position)
            break;
        if (step.type === "set" && (!step.target || step.target === target)) {
            for (key in step.props)
                values[key] = step.props[key];
        } else if (step.type === "to") {
            var t = step.duration > 0 ? (position - step.start) / step.duration : 1;
            if (t >= 1) {
                for (key in step.props)
                    values[key] = step.props[key];
                continue;
            }
            var ratio = step.ease(t);
            for (key in step.props) {
                var from = values[key], to = step.props[key];
                values[key] = typeof from === "number" && typeof to === "number" ? from + (to - from) * ratio : from;
            }
            break;
        }
    }
    for (key in values)
        if (target[key] !== values[key])
            target[key] = values[key];
};
/// Its actions passed going from \a from to \a to (round the end when
/// \a to is before it), or only those at \a to when it jumped there.
TW._runActions = function (from, to, exact, clip) {
    for (var i = 0; i < this._steps.length; ++i) {
        var step = this._steps[i];
        if (step.type !== "call")
            continue;
        var at = step.start;
        var due = exact ? at === to : from < to ? at > from && at <= to : at > from || at <= to;
        if (due)
            step.callback.apply(step.scope || this.target, step.params);
    }
};

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
    Rectangle: Rectangle, Matrix2D: Matrix2D, Sound: strict(Sound, "createjs.Sound"), Stage: Stage,
    Shape: Shape, Graphics: Graphics, Text: Text, Bitmap: Bitmap, Sprite: Sprite, SpriteSheet: SpriteSheet,
    Shadow: Shadow, Filter: Filter, BlurFilter: BlurFilter, ColorFilter: ColorFilter,
    ColorMatrixFilter: ColorMatrixFilter, ColorMatrix: ColorMatrix, AlphaMaskFilter: AlphaMaskFilter,
    AlphaMapFilter: AlphaMapFilter, Timeline: Timeline, DOMElement: DOMElement,
    ButtonHelper: function () {},
    extend: function (subclass, superclass) {
        function o() { this.constructor = subclass; }
        o.prototype = superclass.prototype;
        return (subclass.prototype = new o());
    },
    promote: function (subclass, prefix) {
        var subP = subclass.prototype, supP = Object.getPrototypeOf(subP);
        if (supP) {
            subP[(prefix += "_") + "constructor"] = supP.constructor;
            for (var n in supP) {
                var described = Object.getOwnPropertyDescriptor(subP, n), inherited = null;
                for (var at = supP; at && !inherited; at = Object.getPrototypeOf(at))
                    inherited = Object.getOwnPropertyDescriptor(at, n);
                if (described && inherited && typeof inherited.value === "function")
                    subP[prefix + n] = inherited.value;
            }
        }
        return subclass;
    },
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
/// named) Name, or a Bitmap of the library bitmap.
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
                if (id >= 0)
                    return wrap(id);
                var image = libraryImage(key);
                if (image)
                    return new Bitmap(image);
                missing("lib." + key + " (no library symbol or bitmap is linked as or named " + key + ")");
            };
            constructors[key].prototype = MC;
        }
        return constructors[key];
    },
    set: function (target, key, value) { target[key] = value; return true; }
});
/// images and img: the library's bitmaps by name, as a published page
/// loads them.
function imageTable(existing) {
    return new Proxy(existing || {}, {
        get: function (target, key) {
            if (typeof key !== "string" || key in target)
                return target[key];
            return libraryImage(key);
        },
        set: function (target, key, value) { target[key] = value; return true; }
    });
}
global.images = imageTable(global.images);
global.img = imageTable();
global.ss = global.ss || {};
global.Image = function Image(width, height) {
    PhoenixImage.call(this);
    if (width !== undefined)
        this.width = width;
    if (height !== undefined)
        this.height = height;
};
global.Image.prototype = PhoenixImage.prototype;

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
    effected.delete(id);
    texts.forEach(function (field, key) {
        if (key.indexOf(id + "/") === 0)
            texts.delete(key);
    });
};
/// What the scripts drew and set goes to the display list; what is on no
/// display list any more is the scripts' to keep or let go of.
global.__phoenixSync = function () {
    made.forEach(function (ref, id) {
        var object = ref.deref();
        if (!object)
            made.delete(id);
        else if (object._sync)
            object._sync();
    });
    effected.forEach(function (id) {
        var object = wrap(id);
        if (object)
            applyEffects(object);
        else
            effected.delete(id);
    });
    held.forEach(function (object, id) {
        if (P.parent(id) === -2)
            held.delete(id);
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
    // As the stage ticks what is on it: the clips scripts made and the
    // sprites move on, then tick goes to every object listening, then the
    // stage.
    codeClips.forEach(function (id) {
        var clip = wrap(id);
        if (!clip) {
            codeClips.delete(id);
            return;
        }
        if (clip.stage && (!clip.__tl.paused || clip.__tl.raw < 0))
            clip._updateTimeline(clip.__tl.raw + 1, false);
    });
    sprites.forEach(function (id) {
        var sprite = wrap(id);
        if (!sprite)
            sprites.delete(id);
        else if (sprite.stage)
            sprite._tick(delta);
    });
    var event = function () {
        var made = new Event("tick");
        made.delta = delta;
        made.paused = Ticker.paused;
        made.time = now;
        made.runTime = now - pausedTime;
        return made;
    };
    Array.from(ticking).forEach(function (object) {
        if (object === stage)
            return;
        if (object.__clip && !P.alive(object.__id)) {
            ticking.delete(object);
            return;
        }
        if (object.stage)
            object.dispatchEvent(event());
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
