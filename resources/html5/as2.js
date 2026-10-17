// ActionScript 1 and 2 over the runtime: the classes and objects such
// scripts know (MovieClip, Button, TextField, Key, Mouse, Stage, Sound,
// Color, TextFormat, LoadVars, XML, SharedObject, MovieClipLoader...) as
// faces over the runtime's display objects, and the host avm1.js (libas3's
// helpers for the language) plugs into. Published beside phoenix.js as
// phoenix-as2.js, avm1.js first, for movies whose scripts are ActionScript
// 1 or 2; their translated scripts (movie.as.js) call avm1.movie(...).
//
// Each display object a script reaches has an ActionScript 2 object: a
// proxy whose own properties are the timeline's variables, whose named
// children are found by name, and whose prototype (MovieClip.prototype, or
// a class Object.registerClass linked to the symbol) has _x, _alpha,
// gotoAndPlay and the rest working on the display object.
(function (phoenix, avm1) {
"use strict";

var display = flash.display, geom = flash.geom, Event = flash.events.Event;
var $ = avm1;
var G = $.global;

var faces = new WeakMap();       // display object -> its ActionScript 2 object
var displays = new WeakMap();    // ActionScript 2 object -> display object
var stage = null;
var lowerCase = function (s) { return String(s).toLowerCase(); };

/// The display object behind an ActionScript 2 object (or a path to one).
function displayOf(v) {
    if (v === null || v === undefined)
        return null;
    var d = displays.get(v);
    if (d)
        return d;
    if (v._isDisplayObject)
        return v;
    return null;
}
phoenix.as2DisplayOf = displayOf;

function childNamed(d, name) {
    var children = d._children;
    if (!children || typeof name !== "string")
        return null;
    var folded = $.version < 7;
    var want = folded ? lowerCase(name) : name;
    for (var i = children.length - 1; i >= 0; --i) {
        var c = children[i];
        if (c._name && (folded ? lowerCase(c._name) : c._name) === want && hasFace(c))
            return c;
    }
    return null;
}
/// Whether scripts reach a display object: clips, buttons, text fields.
function hasFace(d) {
    return d instanceof display.Sprite || d instanceof display.SimpleButton || d instanceof flash.text.TextField;
}

var buttonEvents = ["onPress", "onRelease", "onReleaseOutside", "onRollOver", "onRollOut", "onDragOver", "onDragOut"];

/// The ActionScript 2 object for \a d, made the first time.
function face(d) {
    if (!d || d === stage || !(d._isDisplayObject))
        return undefined;
    var known = faces.get(d);
    if (known)
        return known;
    if (!hasFace(d))
        return undefined;
    var proto = protoFor(d);
    var target = Object.create(proto);
    var proxy = new Proxy(target, {
        get: function (t, k, receiver) {
            if (typeof k === "string" && !Object.prototype.hasOwnProperty.call(t, k)) {
                var c = childNamed(d, k);
                if (c)
                    return face(c);
            }
            return Reflect.get(t, k, receiver);
        },
        set: function (t, k, v, receiver) {
            var done = Reflect.set(t, k, v, receiver);
            if (typeof k === "string" && buttonEvents.indexOf(k) >= 0 && d instanceof display.Sprite)
                d.buttonMode = true;
            return done;
        },
        has: function (t, k) {
            return Reflect.has(t, k) || (typeof k === "string" && !!childNamed(d, k));
        },
        ownKeys: function (t) {
            var keys = Reflect.ownKeys(t);
            (d._children || []).forEach(function (c) {
                if (c._name && hasFace(c) && keys.indexOf(c._name) < 0)
                    keys.push(c._name);
            });
            return keys;
        },
        getOwnPropertyDescriptor: function (t, k) {
            var own = Reflect.getOwnPropertyDescriptor(t, k);
            if (own)
                return own;
            var c = typeof k === "string" ? childNamed(d, k) : null;
            return c ? {value: face(c), writable: true, enumerable: true, configurable: true} : undefined;
        }
    });
    faces.set(d, proxy);
    displays.set(proxy, d);
    displays.set(target, d);
    var init = d._as2Init;
    if (init) {
        d._as2Init = null;
        Object.keys(init).forEach(function (k) { proxy[k] = init[k]; });
    }
    var cls = classFor(d);
    if (cls) {
        try {
            cls.call(proxy);
        } catch (error) {
            phoenix._.reportError(error, "the constructor of " + (d._name || "a clip"));
        }
    }
    return proxy;
}
phoenix.as2Face = face;

function linkageOf(d) {
    var def = d._placement && d._placement.def;
    return def ? def.ident || def.linkage || null : null;
}
function classFor(d) {
    var linkage = linkageOf(d);
    var cls = linkage ? $.registered[linkage] : null;
    return typeof cls === "function" ? cls : null;
}
function protoFor(d) {
    if (d instanceof flash.text.TextField)
        return G.TextField.prototype;
    if (d instanceof display.SimpleButton)
        return G.Button.prototype;
    var cls = classFor(d);
    return cls && cls.prototype ? cls.prototype : G.MovieClip.prototype;
}

// ---- Numbers in, numbers out

function num(v) { return $.n(v); }
function finite(v, then) {
    var n = num(v);
    if (Number.isFinite(n))
        then(n);
}
function bool(v) {
    if (typeof v === "boolean")
        return v;
    var n = num(v);
    return !Number.isNaN(n) && n !== 0;
}
function D(self) { return displays.get(self); }

// ---- Paths

/// "_level0.a.b", as targetPath and a clip's string give it.
function dotPath(d) {
    var path = "";
    for (var at = d; at && at._parent && at._parent !== stage; at = at._parent)
        path = "." + at._name + path;
    return "_level0" + path;
}
/// "/a/b", as _target gives it.
function slashPath(d) {
    var path = "";
    for (var at = d; at && at._parent && at._parent !== stage; at = at._parent)
        path = "/" + at._name + path;
    return path || "/";
}
function rootOf(d) {
    var at = d;
    while (at && at._parent && at._parent !== stage)
        at = at._parent;
    return at;
}

// ---- Depths: the timeline's objects below, those scripts make from 0 up

var SCRIPTED = 1e9;
function depthOf(d) {
    if (d._as2Depth !== undefined)
        return d._as2Depth;
    var parent = d._parent;
    var index = parent ? parent._children.indexOf(d) : 0;
    return -16384 + Math.max(0, index);
}
function childAtDepth(parent, depth) {
    var children = parent._children || [];
    for (var i = 0; i < children.length; ++i)
        if (depthOf(children[i]) === depth)
            return children[i];
    return null;
}
/// Puts \a child in \a parent at \a depth, replacing what was there.
function placeAt(parent, child, depth) {
    var there = childAtDepth(parent, depth);
    if (there && there !== child)
        parent.removeChild(there);
    child._as2Depth = depth;
    child._depth = SCRIPTED + depth;
    if (child._parent === parent)
        parent.removeChild(child);
    var children = parent._children, index = children.length;
    for (var i = 0; i < children.length; ++i)
        if (children[i]._depth !== undefined && children[i]._depth > child._depth) {
            index = i;
            break;
        }
    parent.addChildAt(child, index);
}
function nextDepth(parent) {
    var highest = -1;
    (parent._children || []).forEach(function (c) {
        if (c._as2Depth !== undefined)
            highest = Math.max(highest, c._as2Depth);
    });
    return highest + 1;
}

// ---- Scenes and frames

function absoluteFrame(d, frame) {
    var n = Math.floor(num(frame));
    var total = d.totalFrames || 1;
    return Math.max(0, Math.min(total - 1, n - 1));
}
function goTo(d, frame, play) {
    if (!(d instanceof display.MovieClip))
        return;
    if (typeof frame === "string" && Number.isNaN(Number(frame))) {
        try {
            if (play)
                d.gotoAndPlay(frame);
            else
                d.gotoAndStop(frame);
        } catch (error) {
            // A label it does not have: it stays.
        }
        return;
    }
    if (Number.isNaN(num(frame)))
        return;
    d._playing = !!play;
    d._goTo(absoluteFrame(d, frame));
}

// ---- The classes

function defineClass(name, fn, parent) {
    var cls = fn;
    if (parent)
        $.extend(cls, parent);
    G[name] = cls;
    return cls;
}
function hide(o) { $.hide(o, null); }
/// Properties on a prototype, read and written through the display object.
function properties(proto, list) {
    Object.keys(list).forEach(function (name) {
        var p = list[name];
        Object.defineProperty(proto, name, {
            get: function () { var d = D(this); return d ? p[0].call(this, d) : undefined; },
            set: function (v) { var d = D(this); if (d && p[1]) p[1].call(this, d, v); },
            enumerable: false,
            configurable: true
        });
    });
}
function methods(proto, list) {
    Object.keys(list).forEach(function (name) {
        Object.defineProperty(proto, name, {value: list[name], writable: true, enumerable: false, configurable: true});
    });
}

// What clips, buttons and text fields share.
var common = {
    _x: [function (d) { return d.x; }, function (d, v) { finite(v, function (n) { d.x = n; }); }],
    _y: [function (d) { return d.y; }, function (d, v) { finite(v, function (n) { d.y = n; }); }],
    _xscale: [function (d) { return d.scaleX * 100; }, function (d, v) { finite(v, function (n) { d.scaleX = n / 100; }); }],
    _yscale: [function (d) { return d.scaleY * 100; }, function (d, v) { finite(v, function (n) { d.scaleY = n / 100; }); }],
    _rotation: [function (d) { return d.rotation; }, function (d, v) { finite(v, function (n) { d.rotation = n; }); }],
    _alpha: [function (d) { return d.alpha * 100; }, function (d, v) { finite(v, function (n) { d.alpha = n / 100; }); }],
    _visible: [function (d) { return d.visible; }, function (d, v) {
        if (typeof v === "boolean" || !Number.isNaN(num(v)))
            d.visible = bool(v);
    }],
    _width: [function (d) { return d.width; }, function (d, v) { finite(v, function (n) { d.width = n; }); }],
    _height: [function (d) { return d.height; }, function (d, v) { finite(v, function (n) { d.height = n; }); }],
    _name: [function (d) { return d.name; }, function (d, v) { d.name = $.s(v); }],
    _parent: [function (d) { return d._parent && d._parent !== stage ? face(d._parent) : undefined; }],
    _target: [function (d) { return slashPath(d); }],
    _xmouse: [function (d) { return d.mouseX; }],
    _ymouse: [function (d) { return d.mouseY; }],
    _url: [function () { return typeof location !== "undefined" ? location.href : ""; }],
    _quality: [function () { return G._quality || "HIGH"; }, function (d, v) { G._quality = $.s(v); }],
    _highquality: [function () { return 1; }, function () {}],
    _focusrect: [function (d) { return d._as2Focusrect === undefined ? true : d._as2Focusrect; }, function (d, v) { d._as2Focusrect = v; }],
    _soundbuftime: [function () { return 5; }, function () {}],
    filters: [function (d) { return d.filters; }, function (d, v) { d.filters = Array.isArray(v) ? v : []; }],
    blendMode: [function (d) { return d.blendMode; }, function (d, v) {
        var modes = ["normal", "normal", "layer", "multiply", "screen", "lighten", "darken", "difference", "add",
            "subtract", "invert", "alpha", "erase", "overlay", "hardlight"];
        d.blendMode = typeof v === "number" ? modes[v] || "normal" : $.s(v);
    }],
    cacheAsBitmap: [function (d) { return d.cacheAsBitmap; }, function (d, v) { d.cacheAsBitmap = bool(v); }],
    opaqueBackground: [function (d) { return d.opaqueBackground; }, function (d, v) { d.opaqueBackground = v; }],
    scrollRect: [function (d) { return d.scrollRect; }, function (d, v) { d.scrollRect = v || null; }],
    transform: [function (d) { return d.transform; }, function (d, v) { if (v) d.transform = v; }],
    tabEnabled: [function (d) { return d.tabEnabled; }, function (d, v) { d.tabEnabled = bool(v); }],
    tabIndex: [function (d) { return d.tabIndex; }, function (d, v) { d.tabIndex = num(v); }]
};
var commonMethods = {
    getDepth: function () { var d = D(this); return d ? depthOf(d) : undefined; },
    swapDepths: function (target) {
        var d = D(this);
        if (!d || !d._parent)
            return;
        var parent = d._parent;
        var other = typeof target === "number" ? childAtDepth(parent, target) : displayOf(target);
        var mine = depthOf(d);
        var theirs = typeof target === "number" ? target : other && other._parent === parent ? depthOf(other) : undefined;
        if (theirs === undefined)
            return;
        if (other && other !== d) {
            other._as2Depth = mine;
            other._depth = SCRIPTED + mine;
        }
        d._as2Depth = theirs;
        d._depth = SCRIPTED + theirs;
        var ordered = parent._children.slice().sort(function (a, b) {
            var x = a._as2Depth !== undefined ? SCRIPTED + a._as2Depth : -1, y = b._as2Depth !== undefined ? SCRIPTED + b._as2Depth : -1;
            return x === -1 && y === -1 ? 0 : x - y;
        });
        // The timeline's keep their order below; the scripts' go above.
        var timeline = parent._children.filter(function (c) { return c._as2Depth === undefined; });
        var scripted = ordered.filter(function (c) { return c._as2Depth !== undefined; });
        timeline.concat(scripted).forEach(function (c, i) {
            if (parent._children[i] !== c)
                parent.setChildIndex(c, i);
        });
    },
    toString: function () { var d = D(this); return d ? dotPath(d) : "[object Object]"; }
};

function defineMovieClip() {
    var MovieClip = defineClass("MovieClip", function MovieClip() {});
    var p = MovieClip.prototype;
    Object.defineProperty(p, "$clip", {value: true});
    properties(p, common);
    properties(p, {
        _currentframe: [function (d) { return d instanceof display.MovieClip ? d._frame + 1 : 1; }],
        _totalframes: [function (d) { return d instanceof display.MovieClip ? d.totalFrames : 1; }],
        _framesloaded: [function (d) { return d instanceof display.MovieClip ? d.totalFrames : 1; }],
        _droptarget: [function (d) { return d.dropTarget ? slashPath(d.dropTarget) : ""; }],
        _lockroot: [function (d) { return !!d._as2Lockroot; }, function (d, v) { d._as2Lockroot = bool(v); }],
        enabled: [function (d) { return d._as2Enabled !== false; }, function (d, v) { d._as2Enabled = bool(v); }],
        useHandCursor: [function (d) { return d.useHandCursor; }, function (d, v) { d.useHandCursor = bool(v); }],
        hitArea: [function (d) { return d.hitArea ? face(d.hitArea) : undefined; }, function (d, v) { d.hitArea = displayOf(v); }],
        trackAsMenu: [function (d) { return !!d._as2TrackAsMenu; }, function (d, v) { d._as2TrackAsMenu = bool(v); }],
        focusEnabled: [function (d) { return !!d._as2FocusEnabled; }, function (d, v) { d._as2FocusEnabled = bool(v); }],
        tabChildren: [function (d) { return d.tabChildren; }, function (d, v) { d.tabChildren = bool(v); }],
        scale9Grid: [function (d) { return d.scale9Grid; }, function (d, v) { d.scale9Grid = v || null; }],
        menu: [function (d) { return d._as2Menu; }, function (d, v) { d._as2Menu = v; }]
    });
    methods(p, commonMethods);
    methods(p, {
        play: function () { var d = D(this); if (d instanceof display.MovieClip) d.play(); },
        stop: function () { var d = D(this); if (d instanceof display.MovieClip) d.stop(); },
        nextFrame: function () { var d = D(this); if (d instanceof display.MovieClip) d.nextFrame(); },
        prevFrame: function () { var d = D(this); if (d instanceof display.MovieClip) d.prevFrame(); },
        gotoAndPlay: function (frame) { var d = D(this); if (d) goTo(d, frame, true); },
        gotoAndStop: function (frame) { var d = D(this); if (d) goTo(d, frame, false); },
        nextScene: function () { var d = D(this); if (d instanceof display.MovieClip) d.nextScene(); },
        prevScene: function () { var d = D(this); if (d instanceof display.MovieClip) d.prevScene(); },
        attachMovie: function (id, name, depth, init) {
            var d = D(this);
            var def = symbolByLinkage($.s(id));
            if (!d || !def)
                return undefined;
            return place(d, makeSymbol(def, $.s(name)), num(depth), init);
        },
        createEmptyMovieClip: function (name, depth) {
            var d = D(this);
            if (!d)
                return undefined;
            phoenix._.pending = {name: $.s(name)};
            var clip = new display.MovieClip();
            phoenix._.pending = null;
            clip.name = $.s(name);
            return place(d, clip, num(depth));
        },
        createTextField: function (name, depth, x, y, width, height) {
            var d = D(this);
            if (!d)
                return undefined;
            var field = new flash.text.TextField();
            field.name = $.s(name);
            field.x = num(x) || 0;
            field.y = num(y) || 0;
            field.width = num(width) || 100;
            field.height = num(height) || 100;
            return place(d, field, num(depth));
        },
        duplicateMovieClip: function (name, depth, init) {
            var d = D(this);
            if (!d || !d._parent || d._parent === stage)
                return undefined;
            var def = d._placement && d._placement.def;
            var made = def ? makeSymbol(def, $.s(name)) : new display.MovieClip();
            made.name = $.s(name);
            made.transform.matrix = d.transform.matrix;
            made.transform.colorTransform = d.transform.colorTransform;
            made.filters = d.filters;
            made._as2Handlers = d._as2Handlers;
            return place(d._parent, made, num(depth), init);
        },
        removeMovieClip: function () {
            var d = D(this);
            if (d && d._parent && d._parent !== stage)
                d._parent.removeChild(d);
        },
        getNextHighestDepth: function () { var d = D(this); return d ? nextDepth(d) : 0; },
        getInstanceAtDepth: function (depth) {
            var d = D(this);
            var c = d ? childAtDepth(d, num(depth)) : null;
            return c ? face(c) : undefined;
        },
        getSWFVersion: function () { return $.version; },
        getBytesLoaded: function () { return 1000; },
        getBytesTotal: function () { return 1000; },
        getURL: function (url, window, method) { $.getURL(url, window, method); },
        loadMovie: function (url) { var d = D(this); if (d) loadInto(d, $.s(url), null); },
        unloadMovie: function () {
            var d = D(this);
            if (d && d._as2Loader && d._as2Loader._parent === d)
                d.removeChild(d._as2Loader);
        },
        loadVariables: function (url, method) { var d = D(this); if (d) loadVariables(face(d), $.s(url), method); },
        startDrag: function (lock, left, top, right, bottom) {
            var d = D(this);
            if (!(d instanceof display.Sprite))
                return;
            var bounds = arguments.length >= 5 ? new geom.Rectangle(num(left), num(top), num(right) - num(left), num(bottom) - num(top)) : null;
            dragged = d;
            d.startDrag(bool(lock), bounds);
        },
        stopDrag: function () { stopDrag(); },
        hitTest: function (x, y, shapeFlag) {
            var d = D(this);
            if (!d)
                return false;
            if (arguments.length >= 2 && typeof x !== "object")
                return d.hitTestPoint(num(x), num(y), bool(shapeFlag));
            var other = displayOf(typeof x === "string" ? $.scope(face(d._parent) || face(d)).resolve(x) : x);
            return !!other && d.hitTestObject(other);
        },
        getBounds: function (space) { return bounds(D(this), space, "getBounds"); },
        getRect: function (space) { return bounds(D(this), space, "getRect"); },
        localToGlobal: function (point) {
            var d = D(this);
            if (!d || !point)
                return;
            var p = d.localToGlobal(new geom.Point(num(point.x), num(point.y)));
            point.x = p.x;
            point.y = p.y;
        },
        globalToLocal: function (point) {
            var d = D(this);
            if (!d || !point)
                return;
            var p = d.globalToLocal(new geom.Point(num(point.x), num(point.y)));
            point.x = p.x;
            point.y = p.y;
        },
        setMask: function (mask) { var d = D(this); if (d) d.mask = displayOf(mask); },
        attachBitmap: function (bitmapData, depth, snapping, smoothing) {
            var d = D(this);
            if (!d || !bitmapData)
                return;
            var bitmap = new display.Bitmap(bitmapData);
            bitmap.smoothing = bool(smoothing);
            placeAt(d, bitmap, num(depth));
        },
        attachAudio: function () {},
        getTextSnapshot: function () { return null; },
        // The drawing API: alphas from 0 to 100.
        beginFill: function (color, alpha) {
            var g = graphicsOf(this);
            if (g)
                g.beginFill(num(color), alpha === undefined ? 1 : num(alpha) / 100);
        },
        beginGradientFill: function (type, colors, alphas, ratios, matrix, spread, interpolation, focal) {
            var g = graphicsOf(this);
            if (g)
                g.beginGradientFill($.s(type), colors, (alphas || []).map(function (a) { return num(a) / 100; }), ratios,
                    gradientMatrix(matrix), spread, interpolation, focal === undefined ? 0 : num(focal));
        },
        beginBitmapFill: function (bitmap, matrix, repeat, smoothing) {
            var g = graphicsOf(this);
            if (g)
                g.beginBitmapFill(bitmap, matrix || null, repeat === undefined ? true : bool(repeat), bool(smoothing));
        },
        lineStyle: function (thickness, color, alpha, pixelHinting, noScale, caps, joints, miterLimit) {
            var g = graphicsOf(this);
            if (!g)
                return;
            if (thickness === undefined || thickness === null || Number.isNaN(num(thickness))) {
                g.lineStyle();
                return;
            }
            g.lineStyle(num(thickness), num(color) || 0, alpha === undefined ? 1 : num(alpha) / 100, bool(pixelHinting),
                noScale === undefined ? "normal" : $.s(noScale), caps === undefined ? null : $.s(caps),
                joints === undefined ? null : $.s(joints), miterLimit === undefined ? 3 : num(miterLimit));
        },
        lineGradientStyle: function (type, colors, alphas, ratios, matrix, spread, interpolation, focal) {
            var g = graphicsOf(this);
            if (g)
                g.lineGradientStyle($.s(type), colors, (alphas || []).map(function (a) { return num(a) / 100; }), ratios,
                    gradientMatrix(matrix), spread, interpolation, focal === undefined ? 0 : num(focal));
        },
        moveTo: function (x, y) { var g = graphicsOf(this); if (g) g.moveTo(num(x), num(y)); },
        lineTo: function (x, y) { var g = graphicsOf(this); if (g) g.lineTo(num(x), num(y)); },
        curveTo: function (cx, cy, x, y) { var g = graphicsOf(this); if (g) g.curveTo(num(cx), num(cy), num(x), num(y)); },
        endFill: function () { var g = graphicsOf(this); if (g) g.endFill(); },
        clear: function () { var g = graphicsOf(this); if (g) g.clear(); }
    });
    hide(p);
}

function graphicsOf(self) {
    var d = D(self);
    return d instanceof display.Sprite ? d.graphics : null;
}
/// A gradient's matrix as ActionScript 2 gives it: a box, a 3 by 3, or a
/// flash.geom.Matrix.
function gradientMatrix(m) {
    if (!m)
        return null;
    if (m instanceof geom.Matrix)
        return m;
    if (m.matrixType === "box") {
        var box = new geom.Matrix();
        box.createGradientBox(num(m.w), num(m.h), num(m.r) || 0, num(m.x) || 0, num(m.y) || 0);
        return box;
    }
    return new geom.Matrix(num(m.a), num(m.b), num(m.d), num(m.e), num(m.g), num(m.h));
}
function bounds(d, space, how) {
    if (!d)
        return undefined;
    var other = displayOf(space) || d;
    var r = d[how](other);
    return {xMin: r.x, xMax: r.x + r.width, yMin: r.y, yMax: r.y + r.height};
}

function symbolByLinkage(id) {
    var symbols = phoenix._.library ? phoenix._.library.symbols : [];
    // The identifier attachMovie names; an ActionScript 3 linkage after.
    for (var i = 0; i < symbols.length; ++i)
        if (symbols[i].ident === id || (!symbols[i].ident && symbols[i].linkage === id))
            return symbols[i];
    var lower = lowerCase(id);
    for (var j = 0; j < symbols.length; ++j)
        if (lowerCase(symbols[j].ident || symbols[j].linkage || "") === lower)
            return symbols[j];
    return null;
}
function makeSymbol(def, name) {
    var Cls = phoenix._.classForSymbol.get(def.index) ||
        (def.type === "button" ? display.SimpleButton : display.MovieClip);
    phoenix._.pending = {name: name, def: def};
    var made;
    try {
        made = new Cls();
    } finally {
        phoenix._.pending = null;
    }
    made.name = name;
    return made;
}
function place(parent, child, depth, init) {
    if (init && typeof init === "object")
        child._as2Init = Object.assign({}, init);
    placeAt(parent, child, Number.isFinite(depth) ? depth : nextDepth(parent));
    return face(child);
}

function defineButton() {
    var Button = defineClass("Button", function Button() {});
    var p = Button.prototype;
    Object.defineProperty(p, "$clip", {value: true});
    Object.defineProperty(p, "$button", {value: true});
    properties(p, common);
    properties(p, {
        enabled: [function (d) { return d.enabled; }, function (d, v) { d.enabled = bool(v); }],
        useHandCursor: [function (d) { return d.useHandCursor; }, function (d, v) { d.useHandCursor = bool(v); }],
        trackAsMenu: [function (d) { return d.trackAsMenu; }, function (d, v) { d.trackAsMenu = bool(v); }]
    });
    methods(p, commonMethods);
    hide(p);
}

// ---- Text fields

function defineTextField() {
    var TextField = defineClass("TextField", function TextField() {});
    var p = TextField.prototype;
    properties(p, common);
    var same = ["text", "textColor", "multiline", "wordWrap", "border", "borderColor", "background", "backgroundColor",
        "selectable", "maxChars", "restrict", "condenseWhite", "embedFonts", "antiAliasType", "gridFitType", "sharpness",
        "thickness", "mouseWheelEnabled", "length", "textWidth", "textHeight", "tabIndex"];
    var list = {};
    same.forEach(function (name) {
        list[name] = [function (d) { return d[name]; }, function (d, v) {
            try {
                d[name] = typeof d[name] === "boolean" ? bool(v) : typeof d[name] === "number" ? num(v) : $.s(v);
            } catch (error) {}
        }];
    });
    list.text = [function (d) { return d.text; }, function (d, v) { d.text = $.s(v); }];
    list.htmlText = [function (d) { return d._as2Html ? d.htmlText : d.text; }, function (d, v) {
        if (d._as2Html)
            d.htmlText = $.s(v);
        else
            d.text = $.s(v);
    }];
    list.html = [function (d) { return !!d._as2Html; }, function (d, v) { d._as2Html = bool(v); }];
    list.password = [function (d) { return d.displayAsPassword; }, function (d, v) { d.displayAsPassword = bool(v); }];
    list.type = [function (d) { return d.type; }, function (d, v) { d.type = $.s(v); }];
    list.autoSize = [function (d) { return d.autoSize; }, function (d, v) {
        d.autoSize = v === true ? "left" : v === false || v === undefined || v === null ? "none" : $.s(v);
    }];
    list.scroll = [function (d) { return d.scrollV; }, function (d, v) { d.scrollV = num(v); }];
    list.maxscroll = [function (d) { return d.maxScrollV; }];
    list.bottomScroll = [function (d) { return d.bottomScrollV; }];
    list.hscroll = [function (d) { return d.scrollH; }, function (d, v) { d.scrollH = num(v); }];
    list.maxhscroll = [function (d) { return d.maxScrollH; }];
    list.variable = [function (d) { return d._as2Variable || null; }, function (d, v) { d._as2Variable = v === null || v === undefined ? null : $.s(v); }];
    list.styleSheet = [function (d) { return d._as2StyleSheet; }, function (d, v) { d._as2StyleSheet = v; }];
    properties(p, list);
    methods(p, commonMethods);
    methods(p, {
        setTextFormat: function () {
            var d = D(this);
            if (!d)
                return;
            var args = Array.prototype.slice.call(arguments);
            var format = args.pop();
            var tf = toFormat(format);
            if (args.length === 0)
                d.setTextFormat(tf);
            else if (args.length === 1)
                d.setTextFormat(tf, num(args[0]), num(args[0]) + 1);
            else
                d.setTextFormat(tf, num(args[0]), num(args[1]));
        },
        getTextFormat: function (begin, end) {
            var d = D(this);
            return d ? fromFormat(d.getTextFormat(begin === undefined ? -1 : num(begin), end === undefined ? -1 : num(end))) : undefined;
        },
        setNewTextFormat: function (format) { var d = D(this); if (d) d.defaultTextFormat = toFormat(format); },
        getNewTextFormat: function () { var d = D(this); return d ? fromFormat(d.defaultTextFormat) : undefined; },
        replaceSel: function (text) {
            var d = D(this);
            if (d)
                d.replaceText(d.selectionBeginIndex, d.selectionEndIndex, $.s(text));
        },
        replaceText: function (begin, end, text) { var d = D(this); if (d) d.replaceText(num(begin), num(end), $.s(text)); },
        removeTextField: function () {
            var d = D(this);
            if (d && d._parent)
                d._parent.removeChild(d);
        },
        addListener: function (o) { listen(this, o); return true; },
        removeListener: function (o) { return unlisten(this, o); }
    });
    hide(p);

    var TextFormat = defineClass("TextFormat", function TextFormat(font, size, color, bold, italic, underline, url, target, align,
        leftMargin, rightMargin, indent, leading) {
        var names = ["font", "size", "color", "bold", "italic", "underline", "url", "target", "align", "leftMargin",
            "rightMargin", "indent", "leading"];
        var values = arguments;
        var self = this;
        names.forEach(function (name, i) { self[name] = i < values.length ? values[i] : null; });
        self.blockIndent = null;
        self.bullet = null;
        self.tabStops = null;
        self.kerning = null;
        self.letterSpacing = null;
    });
    methods(TextFormat.prototype, {
        getTextExtent: function (text) {
            var field = new flash.text.TextField();
            field.defaultTextFormat = toFormat(this);
            field.text = $.s(text);
            return {width: field.textWidth, height: field.textHeight, textFieldWidth: field.textWidth + 4, textFieldHeight: field.textHeight + 4};
        }
    });
}
var formatNames = ["font", "size", "color", "bold", "italic", "underline", "url", "target", "align", "leftMargin",
    "rightMargin", "indent", "leading", "blockIndent", "bullet", "tabStops", "kerning", "letterSpacing"];
function toFormat(f) {
    var tf = new flash.text.TextFormat();
    if (!f)
        return tf;
    formatNames.forEach(function (name) {
        if (f[name] !== null && f[name] !== undefined)
            try { tf[name] = f[name]; } catch (error) {}
    });
    return tf;
}
function fromFormat(tf) {
    var f = new G.TextFormat();
    formatNames.forEach(function (name) {
        if (tf && tf[name] !== undefined)
            f[name] = tf[name];
    });
    return f;
}

// ---- Listeners: Key, Mouse, Stage, text fields

function listeners(owner) {
    if (!Object.prototype.hasOwnProperty.call(owner, "_listeners"))
        Object.defineProperty(owner, "_listeners", {value: [], writable: true});
    return owner._listeners;
}
function listen(owner, o) {
    var list = listeners(owner);
    if (o && list.indexOf(o) < 0)
        list.push(o);
}
function unlisten(owner, o) {
    var list = listeners(owner);
    var at = list.indexOf(o);
    if (at < 0)
        return false;
    list.splice(at, 1);
    return true;
}
function broadcast(owner, name, args) {
    listeners(owner).slice().forEach(function (o) {
        $.guard(function () { $.callMethod(o, name, args || []); });
    });
}

var keysDown = new Set();
var lastKey = 0, lastAscii = 0;
function defineKey() {
    var Key = {};
    var codes = {BACKSPACE: 8, CAPSLOCK: 20, CONTROL: 17, DELETEKEY: 46, DOWN: 40, END: 35, ENTER: 13, ESCAPE: 27,
        HOME: 36, INSERT: 45, LEFT: 37, PGDN: 34, PGUP: 33, RIGHT: 39, SHIFT: 16, SPACE: 32, TAB: 9, UP: 38, ALT: 18};
    Object.keys(codes).forEach(function (k) { Key[k] = codes[k]; });
    methods(Key, {
        isDown: function (code) { return keysDown.has(num(code)); },
        isToggled: function () { return false; },
        getCode: function () { return lastKey; },
        getAscii: function () { return lastAscii; },
        addListener: function (o) { listen(Key, o); },
        removeListener: function (o) { return unlisten(Key, o); },
        isAccessible: function () { return false; }
    });
    G.Key = Key;

    var Mouse = {};
    methods(Mouse, {
        show: function () { flash.ui.Mouse.show(); return 0; },
        hide: function () { flash.ui.Mouse.hide(); return 1; },
        addListener: function (o) { listen(Mouse, o); },
        removeListener: function (o) { return unlisten(Mouse, o); }
    });
    G.Mouse = Mouse;

    var Stage = {};
    Object.defineProperty(Stage, "width", {get: function () { return stage ? stage.stageWidth : 0; }});
    Object.defineProperty(Stage, "height", {get: function () { return stage ? stage.stageHeight : 0; }});
    Object.defineProperty(Stage, "scaleMode", {
        get: function () { return stage ? stage.scaleMode : "showAll"; },
        set: function (v) { if (stage) stage.scaleMode = $.s(v); }
    });
    Object.defineProperty(Stage, "align", {
        get: function () { return stage ? stage.align : ""; },
        set: function (v) { if (stage) stage.align = $.s(v); }
    });
    Object.defineProperty(Stage, "displayState", {
        get: function () { return stage ? stage.displayState : "normal"; },
        set: function (v) { if (stage) try { stage.displayState = $.s(v); } catch (error) {} }
    });
    Stage.showMenu = true;
    methods(Stage, {
        addListener: function (o) { listen(Stage, o); },
        removeListener: function (o) { return unlisten(Stage, o); }
    });
    G.Stage = Stage;

    var Selection = {};
    methods(Selection, {
        getFocus: function () { return stage && stage.focus ? dotPath(stage.focus) : null; },
        setFocus: function (target) {
            var d = displayOf(typeof target === "string" ? $.scope(face(root())).eval(target) : target);
            if (phoenix._.setFocus)
                phoenix._.setFocus(d);
            return !!d;
        },
        getBeginIndex: function () { return stage && stage.focus && stage.focus.selectionBeginIndex !== undefined ? stage.focus.selectionBeginIndex : -1; },
        getEndIndex: function () { return stage && stage.focus && stage.focus.selectionEndIndex !== undefined ? stage.focus.selectionEndIndex : -1; },
        getCaretIndex: function () { return stage && stage.focus && stage.focus.caretIndex !== undefined ? stage.focus.caretIndex : -1; },
        setSelection: function (begin, end) { if (stage && stage.focus && stage.focus.setSelection) stage.focus.setSelection(num(begin), num(end)); },
        addListener: function (o) { listen(Selection, o); },
        removeListener: function (o) { return unlisten(Selection, o); }
    });
    G.Selection = Selection;

    G.System = {
        capabilities: {version: "WIN 8,0,0,0", os: "Windows", language: "en", playerType: "PlugIn", hasAudio: true,
            screenResolutionX: typeof screen !== "undefined" ? screen.width : 0,
            screenResolutionY: typeof screen !== "undefined" ? screen.height : 0},
        security: {allowDomain: function () {}, allowInsecureDomain: function () {}, loadPolicyFile: function () {}},
        useCodepage: false,
        showSettings: function () {},
        setClipboard: function (text) {
            if (typeof navigator !== "undefined" && navigator.clipboard)
                navigator.clipboard.writeText($.s(text)).catch(function () {});
        }
    };
    G.getVersion = function () { return "WIN 8,0,0,0"; };
    G.flash = flash;
}
function root() { return phoenix._.rootClip || null; }

// ---- Color, Sound

function defineMedia() {
    var Color = defineClass("Color", function Color(target) {
        Object.defineProperty(this, "_target", {value: target, writable: true});
    });
    function targetOf(c) {
        var t = c._target;
        return displayOf(typeof t === "string" ? $.scope(face(root())).resolve(t) : t);
    }
    methods(Color.prototype, {
        setRGB: function (rgb) {
            var d = targetOf(this);
            if (!d)
                return;
            var n = num(rgb) >>> 0;
            var ct = d.transform.colorTransform;
            d.transform.colorTransform = new geom.ColorTransform(0, 0, 0, ct.alphaMultiplier, (n >> 16) & 255, (n >> 8) & 255, n & 255, ct.alphaOffset);
        },
        getRGB: function () {
            var d = targetOf(this);
            if (!d)
                return 0;
            var ct = d.transform.colorTransform;
            return ((ct.redOffset & 255) << 16) | ((ct.greenOffset & 255) << 8) | (ct.blueOffset & 255);
        },
        setTransform: function (t) {
            var d = targetOf(this);
            if (!d || !t)
                return;
            var ct = d.transform.colorTransform;
            var pick = function (v, was) { return v === undefined ? was : num(v); };
            d.transform.colorTransform = new geom.ColorTransform(
                pick(t.ra, ct.redMultiplier * 100) / 100, pick(t.ga, ct.greenMultiplier * 100) / 100,
                pick(t.ba, ct.blueMultiplier * 100) / 100, pick(t.aa, ct.alphaMultiplier * 100) / 100,
                pick(t.rb, ct.redOffset), pick(t.gb, ct.greenOffset), pick(t.bb, ct.blueOffset), pick(t.ab, ct.alphaOffset));
        },
        getTransform: function () {
            var d = targetOf(this);
            var ct = d ? d.transform.colorTransform : new geom.ColorTransform();
            return {ra: ct.redMultiplier * 100, rb: ct.redOffset, ga: ct.greenMultiplier * 100, gb: ct.greenOffset,
                ba: ct.blueMultiplier * 100, bb: ct.blueOffset, aa: ct.alphaMultiplier * 100, ab: ct.alphaOffset};
        }
    });
    hide(Color.prototype);

    var Sound = defineClass("Sound", function Sound(target) {
        Object.defineProperty(this, "_s", {value: {target: target, sound: null, channels: [], volume: 100, pan: 0}, writable: true});
    });
    function transformOf(s) {
        return new flash.media.SoundTransform(s._s.volume / 100, s._s.pan / 100);
    }
    function apply(s) {
        s._s.channels.forEach(function (c) { c.soundTransform = transformOf(s); });
        if (!s._s.target && !s._s.sound)
            flash.media.SoundMixer.soundTransform = new flash.media.SoundTransform(s._s.volume / 100, 0);
    }
    methods(Sound.prototype, {
        attachSound: function (id) {
            var Cls = phoenix.classByName($.s(id));
            this._s.sound = Cls ? new Cls() : null;
        },
        loadSound: function (url, streaming) {
            var self = this;
            var sound = new flash.media.Sound();
            this._s.sound = sound;
            sound.addEventListener(Event.COMPLETE, function () {
                $.guard(function () { $.callMethod(self, "onLoad", [true]); });
                if (bool(streaming))
                    self.start();
            });
            sound.addEventListener("ioError", function () { $.guard(function () { $.callMethod(self, "onLoad", [false]); }); });
            sound.load(new flash.net.URLRequest($.s(url)));
        },
        start: function (offset, loops) {
            var sound = this._s.sound;
            if (!sound)
                return;
            var self = this;
            var channel = sound.play((num(offset) || 0) * 1000, Math.max(1, num(loops) || 1), transformOf(this));
            if (!channel)
                return;
            this._s.channels.push(channel);
            channel.addEventListener(Event.SOUND_COMPLETE || "soundComplete", function () {
                var at = self._s.channels.indexOf(channel);
                if (at >= 0)
                    self._s.channels.splice(at, 1);
                $.guard(function () { $.callMethod(self, "onSoundComplete", []); });
            });
        },
        stop: function () {
            this._s.channels.forEach(function (c) { c.stop(); });
            this._s.channels = [];
            if (!this._s.sound && !this._s.target && flash.media.SoundMixer.stopAll)
                flash.media.SoundMixer.stopAll();
        },
        setVolume: function (v) { this._s.volume = num(v); apply(this); },
        getVolume: function () { return this._s.volume; },
        setPan: function (v) { this._s.pan = num(v); apply(this); },
        getPan: function () { return this._s.pan; },
        setTransform: function (t) {
            if (t && t.ll !== undefined)
                this._s.volume = num(t.ll);
            apply(this);
        },
        getTransform: function () { return {ll: this._s.volume, lr: 0, rr: this._s.volume, rl: 0}; },
        getBytesLoaded: function () { return 1; },
        getBytesTotal: function () { return 1; }
    });
    Object.defineProperty(Sound.prototype, "duration", {get: function () { return this._s.sound ? this._s.sound.length : 0; }});
    Object.defineProperty(Sound.prototype, "position", {get: function () {
        var c = this._s.channels[this._s.channels.length - 1];
        return c ? c.position : 0;
    }});
    hide(Sound.prototype);
}

// ---- Loading: LoadVars, XML, loadVariables, SharedObject, MovieClipLoader

function decodeVariables(text, into) {
    String(text).replace(/^\s+|\s+$/g, "").split("&").forEach(function (pair) {
        if (!pair)
            return;
        var at = pair.indexOf("=");
        var key = $.unescape((at < 0 ? pair : pair.slice(0, at)).replace(/\+/g, " "));
        var value = at < 0 ? "" : $.unescape(pair.slice(at + 1).replace(/\+/g, " "));
        into[key] = value;
    });
}
function encodeVariables(o) {
    return $.keys(o).filter(function (k) { return typeof o[k] !== "function"; }).map(function (k) {
        return $.escape(k) + "=" + $.escape(o[k]);
    }).join("&");
}
function fetchText(url, method, body) {
    if (typeof fetch === "undefined")
        return Promise.reject(new Error("no fetch"));
    var init = {method: method === "POST" ? "POST" : "GET"};
    if (body !== undefined && init.method === "POST") {
        init.body = body;
        init.headers = {"Content-Type": "application/x-www-form-urlencoded"};
    } else if (body)
        url += (url.indexOf("?") < 0 ? "?" : "&") + body;
    return fetch(url, init).then(function (response) {
        if (!response.ok)
            throw new Error(response.status + " " + response.statusText);
        return response.text();
    });
}
function loadVariables(target, url, method) {
    fetchText(url, method === undefined ? "GET" : $.s(method).toUpperCase()).then(function (text) {
        decodeVariables(text, target);
        var d = displayOf(target);
        if (d)
            clipEvent(d, "data");
        $.guard(function () { $.callMethod(target, "onData", []); });
        phoenix._.afterEvents && phoenix._.afterEvents();
    }, function () {});
}

function defineLoading() {
    var LoadVars = defineClass("LoadVars", function LoadVars() {});
    methods(LoadVars.prototype, {
        load: function (url) {
            var self = this;
            Object.defineProperty(self, "loaded", {value: false, writable: true, configurable: true});
            fetchText($.s(url), "GET").then(function (text) {
                $.guard(function () { $.callMethod(self, "onData", [text]); });
            }, function () {
                $.guard(function () { $.callMethod(self, "onData", [undefined]); });
            });
            return true;
        },
        onData: function (text) {
            if (text === undefined) {
                $.callMethod(this, "onLoad", [false]);
                return;
            }
            this.decode(text);
            this.loaded = true;
            $.callMethod(this, "onLoad", [true]);
        },
        send: function (url, target, method) {
            $.getURL($.s(url) + "?" + this.toString(), target, method);
            return true;
        },
        sendAndLoad: function (url, into, method) {
            var self = this;
            fetchText($.s(url), method === undefined ? "POST" : $.s(method).toUpperCase(), self.toString()).then(function (text) {
                $.guard(function () { $.callMethod(into, "onData", [text]); });
            }, function () {
                $.guard(function () { $.callMethod(into, "onData", [undefined]); });
            });
            return true;
        },
        decode: function (text) { decodeVariables(text, this); },
        toString: function () { return encodeVariables(this); },
        getBytesLoaded: function () { return this.loaded ? 1 : 0; },
        getBytesTotal: function () { return 1; },
        addRequestHeader: function () {}
    });
    hide(LoadVars.prototype);
    LoadVars.prototype.contentType = "application/x-www-form-urlencoded";

    defineXml(fetchText);

    var SharedObject = defineClass("SharedObject", function SharedObject() {});
    var storeKey = function (name) {
        return "phoenix-so:" + (typeof location !== "undefined" ? location.pathname : "") + ":" + name;
    };
    SharedObject.getLocal = function (name) {
        var so = new SharedObject();
        var key = storeKey($.s(name));
        var data = {};
        try {
            var saved = typeof localStorage !== "undefined" ? localStorage.getItem(key) : null;
            if (saved)
                data = JSON.parse(saved) || {};
        } catch (error) {}
        so.data = data;
        Object.defineProperty(so, "_key", {value: key});
        return so;
    };
    methods(SharedObject.prototype, {
        flush: function () {
            try {
                if (typeof localStorage !== "undefined")
                    localStorage.setItem(this._key, JSON.stringify(this.data));
                return true;
            } catch (error) {
                return false;
            }
        },
        clear: function () {
            this.data = {};
            try {
                if (typeof localStorage !== "undefined")
                    localStorage.removeItem(this._key);
            } catch (error) {}
        },
        getSize: function () { return JSON.stringify(this.data || {}).length; },
        close: function () {}
    });
    hide(SharedObject.prototype);

    var MovieClipLoader = defineClass("MovieClipLoader", function MovieClipLoader() {});
    methods(MovieClipLoader.prototype, {
        addListener: function (o) { listen(this, o); return true; },
        removeListener: function (o) { return unlisten(this, o); },
        loadClip: function (url, target) {
            var scope = $.scope(face(root()));
            var t = typeof target === "number" ? (target === 0 ? face(root()) : undefined) : scope.resolve(target);
            var d = displayOf(t);
            if (!d)
                return false;
            loadInto(d, $.s(url), this);
            return true;
        },
        unloadClip: function (target) {
            var d = displayOf(target);
            if (d && d._as2Loader && d._as2Loader._parent === d)
                d.removeChild(d._as2Loader);
            return true;
        },
        getProgress: function (target) { return {bytesLoaded: 1, bytesTotal: 1}; }
    });
    hide(MovieClipLoader.prototype);
}

/// A picture loaded into a clip, by loadMovie or MovieClipLoader: what it
/// had goes, the picture is its content. SWF movies cannot be.
function loadInto(d, url, loader) {
    var target = face(d);
    var tell = function (name, args) {
        if (loader)
            broadcast(loader, name, [target].concat(args || []));
    };
    tell("onLoadStart");
    if (/\.swf(\?|#|$)/i.test(url)) {
        console.warn("loadMovie: " + url + " is an SWF movie, which an HTML5 page cannot load.");
        tell("onLoadError", ["URLNotFound"]);
        return;
    }
    if (d instanceof display.DisplayObjectContainer)
        while (d.numChildren)
            d.removeChildAt(0);
    var holder = new display.Loader();
    d._as2Loader = holder;
    holder.contentLoaderInfo.addEventListener(Event.COMPLETE, function () {
        tell("onLoadProgress", [1, 1]);
        tell("onLoadComplete", [200]);
        tell("onLoadInit");
    });
    holder.contentLoaderInfo.addEventListener("ioError", function () { tell("onLoadError", ["URLNotFound", 404]); });
    d.addChild(holder);
    holder.load(new flash.net.URLRequest(url));
}

// XML as ActionScript 1 and 2 have it: nodes with childNodes, attributes,
// firstChild and the rest, read by a parser of its own.
function defineXml(fetchText) {
    var XMLNode = defineClass("XMLNode", function XMLNode(type, value) {
        Object.defineProperty(this, "_kids", {value: [], writable: true});
        Object.defineProperty(this, "_up", {value: null, writable: true});
        this.nodeType = type === undefined ? 1 : num(type);
        this.nodeName = this.nodeType === 1 ? (value === undefined ? null : $.s(value)) : null;
        this.nodeValue = this.nodeType === 3 ? (value === undefined ? null : $.s(value)) : null;
        this.attributes = {};
    });
    var p = XMLNode.prototype;
    var getter = function (name, fn) { Object.defineProperty(p, name, {get: fn, configurable: true}); };
    getter("childNodes", function () { return this._kids.slice(); });
    getter("firstChild", function () { return this._kids[0] || null; });
    getter("lastChild", function () { return this._kids[this._kids.length - 1] || null; });
    getter("parentNode", function () { return this._up; });
    getter("nextSibling", function () {
        var up = this._up;
        return up ? up._kids[up._kids.indexOf(this) + 1] || null : null;
    });
    getter("previousSibling", function () {
        var up = this._up;
        return up ? up._kids[up._kids.indexOf(this) - 1] || null : null;
    });
    getter("localName", function () { return this.nodeName ? this.nodeName.split(":").pop() : null; });
    getter("prefix", function () { return this.nodeName && this.nodeName.indexOf(":") >= 0 ? this.nodeName.split(":")[0] : ""; });
    var escapeText = function (s) { return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); };
    var escapeAttribute = function (s) { return escapeText(s).replace(/"/g, "&quot;").replace(/'/g, "&apos;"); };
    methods(p, {
        appendChild: function (node) {
            if (!node)
                return;
            if (node._up)
                node.removeNode();
            node._up = this;
            this._kids.push(node);
        },
        insertBefore: function (node, before) {
            if (!node)
                return;
            if (node._up)
                node.removeNode();
            var at = this._kids.indexOf(before);
            node._up = this;
            this._kids.splice(at < 0 ? this._kids.length : at, 0, node);
        },
        removeNode: function () {
            var up = this._up;
            if (up)
                up._kids.splice(up._kids.indexOf(this), 1);
            this._up = null;
        },
        hasChildNodes: function () { return this._kids.length > 0; },
        cloneNode: function (deep) {
            var copy = new XMLNode(this.nodeType, this.nodeType === 1 ? this.nodeName : this.nodeValue);
            copy.attributes = Object.assign({}, this.attributes);
            if (bool(deep))
                this._kids.forEach(function (k) { copy.appendChild(k.cloneNode(true)); });
            return copy;
        },
        toString: function () {
            if (this.nodeType === 3)
                return escapeText(this.nodeValue || "");
            var inner = this._kids.map(function (k) { return k.toString(); }).join("");
            if (!this.nodeName)
                return inner;
            var attributes = this.attributes, text = "<" + this.nodeName;
            $.keys(attributes).reverse().forEach(function (k) { text += " " + k + "=\"" + escapeAttribute($.s(attributes[k])) + "\""; });
            return text + (this._kids.length ? ">" + inner + "</" + this.nodeName + ">" : " />");
        }
    });
    hide(p);

    var XML = defineClass("XML", function XML(text) {
        XMLNode.call(this, 1, null);
        this.ignoreWhite = false;
        this.status = 0;
        this.loaded = false;
        this.xmlDecl = undefined;
        this.docTypeDecl = undefined;
        if (text !== undefined && text !== null)
            this.parseXML($.s(text));
    }, XMLNode);
    var entities = {amp: "&", lt: "<", gt: ">", quot: "\"", apos: "'"};
    var unescapeXml = function (s) {
        return s.replace(/&(#x[0-9a-f]+|#\d+|\w+);/gi, function (all, name) {
            if (name[0] === "#")
                return String.fromCharCode(name[1] === "x" || name[1] === "X" ? parseInt(name.slice(2), 16) : parseInt(name.slice(1), 10));
            return entities[name] !== undefined ? entities[name] : all;
        });
    };
    methods(XML.prototype, {
        createElement: function (name) { return new XMLNode(1, name); },
        createTextNode: function (text) { return new XMLNode(3, text); },
        parseXML: function (source) {
            this._kids = [];
            this.status = 0;
            var text = $.s(source), at = 0, current = this, ignoreWhite = bool(this.ignoreWhite);
            var addText = function (value) {
                if (!value)
                    return;
                if (ignoreWhite && /^\s*$/.test(value))
                    return;
                current.appendChild(new XMLNode(3, unescapeXml(value)));
            };
            while (at < text.length) {
                var lt = text.indexOf("<", at);
                if (lt < 0) {
                    addText(text.slice(at));
                    break;
                }
                addText(text.slice(at, lt));
                if (text.startsWith("<!--", lt)) {
                    var end = text.indexOf("-->", lt);
                    at = end < 0 ? text.length : end + 3;
                } else if (text.startsWith("<![CDATA[", lt)) {
                    var close = text.indexOf("]]>", lt);
                    var data = text.slice(lt + 9, close < 0 ? text.length : close);
                    if (!(ignoreWhite && /^\s*$/.test(data)))
                        current.appendChild(new XMLNode(3, data));
                    at = close < 0 ? text.length : close + 3;
                } else if (text.startsWith("<?", lt)) {
                    var q = text.indexOf("?>", lt);
                    this.xmlDecl = text.slice(lt, q < 0 ? text.length : q + 2);
                    at = q < 0 ? text.length : q + 2;
                } else if (text.startsWith("<!", lt)) {
                    var gt = text.indexOf(">", lt);
                    this.docTypeDecl = text.slice(lt, gt < 0 ? text.length : gt + 1);
                    at = gt < 0 ? text.length : gt + 1;
                } else if (text[lt + 1] === "/") {
                    var shut = text.indexOf(">", lt);
                    if (current._up)
                        current = current._up;
                    else
                        this.status = -9;
                    at = shut < 0 ? text.length : shut + 1;
                } else {
                    var tag = /^<([^\s/>]+)((?:\s+[^\s=>]+\s*=\s*(?:"[^"]*"|'[^']*'))*)\s*(\/?)>/.exec(text.slice(lt));
                    if (!tag) {
                        this.status = -3;
                        break;
                    }
                    var node = new XMLNode(1, tag[1]);
                    var re = /([^\s=]+)\s*=\s*("([^"]*)"|'([^']*)')/g, m;
                    while ((m = re.exec(tag[2])))
                        node.attributes[m[1]] = unescapeXml(m[3] !== undefined ? m[3] : m[4]);
                    current.appendChild(node);
                    if (!tag[3])
                        current = node;
                    at = lt + tag[0].length;
                }
            }
            if (current !== this && this.status === 0)
                this.status = -9;
        },
        load: function (url) {
            var self = this;
            this.loaded = false;
            fetchText($.s(url), "GET").then(function (text) {
                $.guard(function () { $.callMethod(self, "onData", [text]); });
            }, function () {
                $.guard(function () { $.callMethod(self, "onData", [undefined]); });
            });
            return true;
        },
        onData: function (text) {
            if (text === undefined) {
                $.callMethod(this, "onLoad", [false]);
                return;
            }
            this.parseXML(text);
            this.loaded = true;
            $.callMethod(this, "onLoad", [true]);
        },
        send: function (url, target) { $.getURL(url, target, "POST"); return true; },
        sendAndLoad: function (url, into) {
            fetchText($.s(url), "POST", this.toString()).then(function (text) {
                $.guard(function () { $.callMethod(into, "onData", [text]); });
            }, function () {
                $.guard(function () { $.callMethod(into, "onData", [undefined]); });
            });
            return true;
        },
        getBytesLoaded: function () { return this.loaded ? 1 : 0; },
        getBytesTotal: function () { return 1; },
        addRequestHeader: function () {}
    });
    XML.prototype.contentType = "application/x-www-form-urlencoded";
    hide(XML.prototype);
}

// ---- Events: handlers on clips and buttons, clip events, listeners

/// The instance's on() and onClipEvent() handlers, by the id the content
/// gives its entry ("h"), from the translated movie.
var instanceHandlers = {};

/// Where a timeline's entries have handlers: key -> id, per timeline.
var handlerKeys = new WeakMap();
function handlerIdOf(child) {
    var parent = child._parent;
    var timeline = parent && parent._timeline;
    if (!timeline || child._timelineKey === null || child._timelineKey === undefined)
        return undefined;
    var keys = handlerKeys.get(timeline);
    if (!keys) {
        keys = new Map();
        (timeline.states || []).forEach(function (state) {
            state.forEach(function (entry) {
                if (entry.h !== undefined)
                    keys.set(entry.k, entry.h);
            });
        });
        handlerKeys.set(timeline, keys);
    }
    return keys.get(child._timelineKey);
}

/// Runs a handler's code: a button's on its timeline, a clip's on itself.
function runHandler(d, handler) {
    var button = d instanceof display.SimpleButton;
    var owner = button ? d._parent : d;
    var self = face(owner);
    if (!self)
        return;
    $.guard(function () { handler.run.call(self, $.scope(self)); });
}
function handlersFor(d, event) {
    var list = d._as2Handlers;
    if (!list)
        return [];
    return list.filter(function (h) { return h.events.indexOf(event) >= 0; });
}
function clipEvent(d, event) {
    handlersFor(d, event).forEach(function (h) {
        if (h.clip)
            runHandler(d, h);
    });
}
function onEvent(d, event) {
    handlersFor(d, event).forEach(function (h) {
        if (!h.clip)
            runHandler(d, h);
    });
}
function callHandler(d, name, args) {
    var self = faces.get(d);
    if (!self)
        return;
    var fn = self[name];
    if (typeof fn === "function")
        $.guard(function () { fn.apply(self, args || []); });
}

/// A display object came onto a timeline: its handlers, its class, onLoad.
function added(child) {
    if (!hasFace(child))
        return;
    if (!child._as2Handlers) {
        var id = handlerIdOf(child);
        if (id !== undefined && instanceHandlers[id])
            child._as2Handlers = instanceHandlers[id];
    }
    face(child);
    if (child instanceof flash.text.TextField)
        bindField(child);
    if (child instanceof display.Sprite && !child._as2Loaded) {
        child._as2Loaded = true;
        if (child._as2Handlers && child._as2Handlers.some(function (h) {
            return h.events.some(function (e) { return buttonEvents.indexOf("on" + e[0].toUpperCase() + e.slice(1)) >= 0; });
        }))
            child.buttonMode = true;
        clipEvent(child, "load");
        callHandler(child, "onLoad");
    }
}
function removed(child) {
    if (!(child instanceof display.Sprite) || !child._as2Loaded)
        return;
    child._as2Loaded = false;
    clipEvent(child, "unload");
    callHandler(child, "onUnload");
}

/// Every clip about, children before their parents.
function eachClip(fn) {
    var visit = function (d) {
        var children = d._children;
        if (children)
            children.slice().forEach(visit);
        if (d !== stage && faces.has(d))
            fn(d);
    };
    if (stage)
        visit(stage);
}

function enterFrame() {
    eachClip(function (d) {
        if (!(d instanceof display.Sprite))
            return;
        clipEvent(d, "enterFrame");
        callHandler(d, "onEnterFrame");
    });
    syncFields();
}

// The pointer: what is pressed and what the pointer is over, as AS2's
// buttons and clips with button handlers have them.
var pressed = null, over = null, dragged = null;
function isButtonClip(d) {
    if (d instanceof display.SimpleButton)
        return d.enabled !== false && (!!(d._as2Handlers && d._as2Handlers.length) || hasButtonHandler(d));
    if (!(d instanceof display.Sprite) || d._as2Enabled === false)
        return false;
    // on() handlers on a movie clip make it a button; onClipEvent()s do not.
    if (d._as2Handlers && d._as2Handlers.some(function (h) { return !h.clip; }))
        return true;
    return hasButtonHandler(d);
}
function hasButtonHandler(d) {
    var self = faces.get(d);
    if (!self)
        return false;
    for (var i = 0; i < buttonEvents.length; ++i)
        if (typeof self[buttonEvents[i]] === "function")
            return true;
    return false;
}
/// The outermost clip or button with button handlers over \a target.
function buttonOf(target) {
    var found = null;
    for (var at = target; at && at !== stage; at = at._parent)
        if (isButtonClip(at))
            found = at;
    return found;
}
function buttonEvent(d, name) {
    onEvent(d, name);
    callHandler(d, "on" + name[0].toUpperCase() + name.slice(1));
}
function pointerMoved(target) {
    var now = buttonOf(target);
    if (now === over)
        return;
    var was = over;
    over = now;
    if (was)
        buttonEvent(was, pressed === was ? "dragOut" : "rollOut");
    if (now)
        buttonEvent(now, pressed === now ? "dragOver" : (pressed ? "dragOver" : "rollOver"));
}

function mouseDown(event) {
    pointerMoved(event.target);
    pressed = buttonOf(event.target);
    if (pressed)
        buttonEvent(pressed, "press");
    eachClip(function (d) {
        clipEvent(d, "mouseDown");
        callHandler(d, "onMouseDown");
    });
    broadcast(G.Mouse, "onMouseDown");
}
function mouseUp(event) {
    var target = buttonOf(event.target);
    var was = pressed;
    pressed = null;
    if (was) {
        if (target === was)
            buttonEvent(was, "release");
        else
            buttonEvent(was, "releaseOutside");
    }
    eachClip(function (d) {
        clipEvent(d, "mouseUp");
        callHandler(d, "onMouseUp");
    });
    broadcast(G.Mouse, "onMouseUp");
    pointerMoved(event.target);
}
function mouseMove(event) {
    pointerMoved(event.target);
    eachClip(function (d) {
        clipEvent(d, "mouseMove");
        callHandler(d, "onMouseMove");
    });
    broadcast(G.Mouse, "onMouseMove");
}

var keyNames = {"<Left>": 37, "<Right>": 39, "<Up>": 38, "<Down>": 40, "<Home>": 36, "<End>": 35, "<PgUp>": 33,
    "<PgDn>": 34, "<Insert>": 45, "<Delete>": 46, "<Backspace>": 8, "<Enter>": 13, "<Escape>": 27, "<Tab>": 9,
    "<Space>": 32};
/// Whether an on(keyPress ...) event names the key pressed.
function keyPressMatches(event, code, ascii) {
    var m = /^keyPress\s*(.*)$/.exec(event);
    if (!m)
        return false;
    var key = m[1].trim().replace(/^"|"$/g, "");
    if (keyNames[key] !== undefined)
        return keyNames[key] === code;
    return key.length === 1 && key.charCodeAt(0) === ascii;
}
function keyDown(event) {
    lastKey = event.keyCode;
    lastAscii = event.charCode;
    keysDown.add(event.keyCode);
    eachClip(function (d) {
        clipEvent(d, "keyDown");
        (d._as2Handlers || []).forEach(function (h) {
            if (h.events.some(function (e) { return keyPressMatches(e, lastKey, lastAscii); }))
                runHandler(d, h);
        });
    });
    // Buttons' keyPress handlers.
    walkAll(function (d) {
        if (d instanceof display.SimpleButton)
            (d._as2Handlers || []).forEach(function (h) {
                if (h.events.some(function (e) { return keyPressMatches(e, lastKey, lastAscii); }))
                    runHandler(d, h);
            });
    });
    broadcast(G.Key, "onKeyDown");
    var focused = stage && stage.focus;
    if (focused && faces.has(focused))
        callHandler(focused, "onKeyDown");
}
function keyUp(event) {
    lastKey = event.keyCode;
    keysDown.delete(event.keyCode);
    eachClip(function (d) { clipEvent(d, "keyUp"); });
    broadcast(G.Key, "onKeyUp");
    var focused = stage && stage.focus;
    if (focused && faces.has(focused))
        callHandler(focused, "onKeyUp");
}
function walkAll(fn) {
    var visit = function (d) {
        fn(d);
        (d._children || []).slice().forEach(visit);
    };
    if (stage)
        visit(stage);
}
function stopDrag() {
    if (dragged) {
        dragged.stopDrag();
        dragged = null;
    }
}

// Text fields with a variable: they show it, and input ones set it.
var boundFields = new Set();
function bindField(field) {
    var desc = null;
    var character = field._character;
    if (!field._as2Variable && typeof character === "string" && character[0] === "x") {
        var texts = phoenix._.library ? phoenix._.library.texts : [];
        desc = texts[+character.slice(1)];
        if (desc && desc.var)
            field._as2Variable = desc.var;
        if (desc && desc.html)
            field._as2Html = true;
    }
    if (field._as2Variable && !boundFields.has(field)) {
        boundFields.add(field);
        field.addEventListener(Event.CHANGE, function () {
            var owner = field._parent && face(field._parent);
            if (owner)
                $.scope(owner).setPath(field._as2Variable, field.text);
        });
        syncField(field);
    }
    if (!field._as2Listening) {
        field._as2Listening = true;
        field.addEventListener(Event.CHANGE, function () {
            var self = faces.get(field);
            if (self) {
                callHandler(field, "onChanged", [self]);
                broadcast(self, "onChanged", [self]);
            }
        });
        field.addEventListener("focusIn", function (e) { callHandler(field, "onSetFocus", [e.relatedObject ? face(e.relatedObject) : null]); });
        field.addEventListener("focusOut", function (e) { callHandler(field, "onKillFocus", [e.relatedObject ? face(e.relatedObject) : null]); });
    }
}
function syncField(field) {
    if (!field._parent) {
        boundFields.delete(field);
        return;
    }
    if (field._editing)
        return;
    var owner = face(field._parent);
    if (!owner)
        return;
    var value = $.scope(owner).eval(field._as2Variable);
    if (value === undefined)
        return;
    var text = $.s(value);
    if (field._as2Html) {
        if (field.htmlText !== text)
            field.htmlText = text;
    } else if (field.text !== text)
        field.text = text;
}
function syncFields() {
    boundFields.forEach(syncField);
}

// ---- The movie: its translated scripts registered with the runtime

/// What avm1.runMovies gives translated modules.
function movieHost(as3) {
    var frames = {};
    return {
        classes: function (defines) {
            $.defineClasses(defines, $.scope({}));
        },
        register: function (linkage, name) {
            var cls = $.qualified(name);
            if (typeof cls === "function")
                $.registered[linkage] = cls;
        },
        initclip: function (timeline, order, run) {
            initclips.push({order: order, run: run});
        },
        frame: function (timeline, frame, run) {
            var list = (frames[timeline] = frames[timeline] || {});
            if (!list[frame]) {
                var runs = list[frame] = [];
                var scripts = {};
                scripts[frame] = function () {
                    var self = face(this);
                    if (!self)
                        return;
                    runs.forEach(function (r) {
                        $.guard(function () { r.call(self, $.scope(self)); });
                    });
                    syncFields();
                };
                if (!as3.frameScripts(timeline, scripts))
                    console.warn("ActionScript: there is no symbol " + timeline + " for its frame scripts.");
            }
            list[frame].push(run);
        },
        handlers: function (id, list) {
            instanceHandlers[id] = list;
        }
    };
}
var initclips = [];

/// The host avm1.js asks: the root, paths, scenes, the URL and fscommand.
function plugHost() {
    var host = $.host;
    host.root = function (clip) {
        var d = displayOf(clip);
        var r = d ? rootOf(d) : root();
        return r ? face(r) : undefined;
    };
    host.level = function (n) {
        return n === 0 && root() ? face(root()) : undefined;
    };
    host.targetPath = function (clip) {
        var d = displayOf(clip);
        return d ? dotPath(d) : undefined;
    };
    host.gotoScene = function (clip, scene, frame, play) {
        var d = displayOf(clip);
        if (!(d instanceof display.MovieClip))
            return;
        try {
            if (play)
                d.gotoAndPlay(frame, scene);
            else
                d.gotoAndStop(frame, scene);
        } catch (error) {
            goTo(d, frame, play);
        }
    };
    host.scene = function (clip, step) {
        var d = displayOf(clip);
        if (d instanceof display.MovieClip)
            step > 0 ? d.nextScene() : d.prevScene();
    };
    host.callFrame = function (clip, frame) {
        var d = displayOf(clip);
        if (!(d instanceof display.MovieClip) || !d._frameScripts)
            return;
        var at = typeof frame === "string" && Number.isNaN(Number(frame)) ? d._frameOf(frame) : absoluteFrame(d, frame);
        var script = d._frameScripts[at];
        if (script)
            script.call(d);
    };
    host.stopDrag = stopDrag;
    host.getURL = function (url, window, method) {
        if (/^javascript:/i.test(url) || typeof globalThis.open !== "function")
            return;
        var target = window || "_self";
        if (/^FSCommand:/i.test(url))
            return;
        globalThis.open(url, target);
    };
    host.fscommand = function (command, argument) {
        console.info("fscommand(" + JSON.stringify(command) + ", " + JSON.stringify(argument) + "): there is no player to send it to.");
    };
    host.stopAllSounds = function () {
        if (flash.media.SoundMixer.stopAll)
            flash.media.SoundMixer.stopAll();
    };
    host.updateAfterEvent = function () {};
    host.loadMovieNum = function (url, level) {
        if (level === 0 || !root())
            console.warn("loadMovieNum: replacing the movie is not supported in an HTML5 page.");
        else
            loadInto(root(), url, null);
    };
    $.output = function (text) { phoenix.trace(text); };
    $.error = function (error) { phoenix._.reportError(error, "ActionScript"); };
    $.clock = function () { return phoenix.now ? phoenix.now() - started : Date.now() - started; };
}
var started = typeof performance !== "undefined" ? performance.now() : Date.now();

/// Hooks into the runtime: children coming and going, the frame's
/// enterFrame, the pointer and the keys.
function hookRuntime() {
    var Container = display.DisplayObjectContainer.prototype;
    var insert = Container._insert;
    Container._insert = function (child, index) {
        insert.call(this, child, index);
        $.guard(function () { added(child); });
    };
    var removeAt = Container._removeAt;
    Container._removeAt = function (index) {
        var child = this._children[index];
        removeAt.call(this, index);
        if (child && child._parent !== this)
            $.guard(function () { removed(child); });
    };
}

defineMovieClip();
defineButton();
defineTextField();
defineKey();
defineMedia();
defineLoading();
plugHost();
hookRuntime();

phoenix.actionscript(function (as3) {
    started = phoenix.now ? phoenix.now() : Date.now();
    $.runMovies(movieHost(as3));
    // #initclip before the first frame, in its order.
    initclips.sort(function (a, b) { return a.order - b.order; }).forEach(function (i) {
        $.guard(function () { i.run.call(undefined, $.scope(undefined)); });
    });
    initclips = [];
});
phoenix._.onRoot = phoenix._.onRoot || [];
phoenix._.onRoot.push(function (theStage, rootClip) {
    stage = theStage;
    stage.addEventListener(Event.ENTER_FRAME, enterFrame);
    stage.addEventListener(Event.EXIT_FRAME || "exitFrame", syncFields);
    stage.addEventListener("mouseDown", mouseDown);
    stage.addEventListener("mouseUp", mouseUp);
    stage.addEventListener("mouseMove", mouseMove);
    stage.addEventListener("mouseOver", function (event) { pointerMoved(event.target); });
    stage.addEventListener("keyDown", keyDown);
    stage.addEventListener("keyUp", keyUp);
    stage.addEventListener(Event.RESIZE, function () { broadcast(G.Stage, "onResize"); });
    // The root's own: made before the stage was known.
    walkAll(function (d) {
        if (d !== stage)
            added(d);
    });
});
})(phoenix, avm1);
