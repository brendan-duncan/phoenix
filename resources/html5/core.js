// Phoenix HTML5 runtime: the flash.* API in plain JavaScript, over one
// canvas. This part sets up the package tree and what the rest share.
// See README.md for how published pages load the runtime and how
// translated ActionScript plugs into it.

(function (global) {
"use strict";

/// The package tree. ns("flash.display") is the object holding that
/// package's classes, made the first time it is asked for; user packages
/// ("com.studio") live in the same tree, so a qualified class name always
/// resolves by walking it.
var root = {};
function ns(name) {
    var at = root;
    if (!name)
        return at;
    var parts = String(name).split(".");
    for (var i = 0; i < parts.length; ++i) {
        var part = parts[i];
        if (!Object.prototype.hasOwnProperty.call(at, part))
            at[part] = {};
        at = at[part];
    }
    return at;
}

/// Classes by qualified name ("flash.display.MovieClip", "com.studio.Hat",
/// "Main" for the top-level package), and their names by class.
var classes = new Map();
var classNames = new Map();
function defineClass(qualifiedName, cls) {
    var dot = qualifiedName.lastIndexOf(".");
    var pkg = dot < 0 ? "" : qualifiedName.slice(0, dot);
    var name = dot < 0 ? qualifiedName : qualifiedName.slice(dot + 1);
    ns(pkg)[name] = cls;
    classes.set(qualifiedName, cls);
    classNames.set(cls, qualifiedName);
    return cls;
}
function classByName(qualifiedName) {
    var name = String(qualifiedName).replace("::", ".");
    if (classes.has(name))
        return classes.get(name);
    var dot = name.lastIndexOf(".");
    var pkg = dot < 0 ? root : ns(name.slice(0, dot));
    var found = pkg[dot < 0 ? name : name.slice(dot + 1)];
    return typeof found === "function" ? found : null;
}
/// "flash.display::MovieClip", as getQualifiedClassName gives it.
function qualifiedNameOf(value) {
    if (value === null)
        return "null";
    if (value === undefined)
        return "void";
    var cls = typeof value === "function" ? value : value.constructor;
    for (var at = cls; at; at = Object.getPrototypeOf(at)) {
        var name = classNames.get(at);
        if (name) {
            if (at !== cls)
                break;
            var dot = name.lastIndexOf(".");
            return dot < 0 ? name : name.slice(0, dot) + "::" + name.slice(dot + 1);
        }
    }
    if (typeof value === "number")
        return Number.isInteger(value) ? "int" : "Number";
    if (typeof value === "string")
        return "String";
    if (typeof value === "boolean")
        return "Boolean";
    if (Array.isArray(value))
        return "Array";
    return cls && cls.name ? cls.name : "Object";
}

/// What trace prints goes to the console, and to listeners (a page's
/// own panel, the test harness).
var traceListeners = [];
function show(value) {
    if (value === undefined)
        return "undefined";
    if (value === null)
        return "null";
    if (typeof value === "string")
        return value;
    if (Array.isArray(value))
        return value.map(show).join(",");
    return String(value);
}
function trace() {
    var line = Array.prototype.map.call(arguments, show).join(" ");
    if (global.console && global.console.log)
        global.console.log(line);
    for (var i = 0; i < traceListeners.length; ++i)
        traceListeners[i](line);
}

/// An ActionScript error object of a class from the top-level package.
function as3Error(type, id, message) {
    var Ctor = global[type] || Error;
    var error = new Ctor("Error #" + id + ": " + message);
    error.errorID = id;
    return error;
}

/// Numbers clamped as the flash.* API keeps them.
function clamp(value, low, high) {
    return value < low ? low : value > high ? high : value;
}
function toInt(value) {
    return value | 0;
}
function toNumber(value, fallback) {
    var number = Number(value);
    return isNaN(number) ? (fallback === undefined ? 0 : fallback) : number;
}

/// Whether the page has a DOM to draw with (not in the test harness).
var hasDom = typeof document !== "undefined" && typeof document.createElement === "function";
/// A canvas of a size: an OffscreenCanvas where there is no DOM.
function makeCanvas(width, height) {
    width = Math.max(1, Math.ceil(width));
    height = Math.max(1, Math.ceil(height));
    if (hasDom) {
        var canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        return canvas;
    }
    if (typeof OffscreenCanvas !== "undefined")
        return new OffscreenCanvas(width, height);
    return null;
}

/// Milliseconds since the movie started (getTimer).
var started = typeof performance !== "undefined" && performance.now ? performance.now() : Date.now();
function now() {
    return typeof performance !== "undefined" && performance.now ? performance.now() : Date.now();
}
function getTimer() {
    return Math.floor(now() - started);
}

var phoenix = global.phoenix || {};
phoenix.ns = ns;
phoenix.root = root;
phoenix.defineClass = defineClass;
phoenix.classByName = classByName;
phoenix.qualifiedNameOf = qualifiedNameOf;
phoenix.trace = trace;
phoenix.traceListeners = traceListeners;
phoenix.show = show;
phoenix.as3Error = as3Error;
phoenix.clamp = clamp;
phoenix.toInt = toInt;
phoenix.toNumber = toNumber;
phoenix.hasDom = hasDom;
phoenix.makeCanvas = makeCanvas;
phoenix.now = now;
phoenix.getTimer = getTimer;
phoenix.resetTimer = function () { started = now(); };
/// Internal state the parts share, not for movies' code.
phoenix._ = phoenix._ || {};
global.phoenix = phoenix;
global.flash = ns("flash");
global.trace = trace;
})(typeof globalThis !== "undefined" ? globalThis : this);
