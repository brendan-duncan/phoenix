// Phoenix HTML5 runtime: the flash.* API in plain JavaScript, over one
// canvas. This part sets up the package tree and what the rest share.
// See README.md for how published pages load the runtime and how
// translated ActionScript plugs into it.

(function (global) {
"use strict";

/// as3.js (libas3's runtime for translated ActionScript) comes first in
/// phoenix.js: its registry of definitions is the one the runtime's classes
/// and the movie's share, and it has the language's own classes (int and
/// uint, Vector, Dictionary, ByteArray, XML, the errors).
var as3 = global.as3 || null;

/// The package tree. ns("flash.display") is the object holding that
/// package's classes, made the first time it is asked for; user packages
/// ("com.studio") live in the same tree, so a qualified class name always
/// resolves by walking it. With as3.js the packages are its (the global
/// flash, com...), and the top level its as3.toplevel.
var root = as3 ? as3.toplevel : {};
function ns(name) {
    if (as3)
        return as3.package(name ? String(name) : "");
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

/// A runtime class's ActionScript constructor for translated subclasses
/// (libas3's construction protocol): a subclass's JavaScript constructor
/// calls super() with no arguments, then $construct(...args) runs, and
/// super(...) in ActionScript is super.$construct(...). Classes whose
/// state is their own properties get one that makes an object with the
/// arguments and takes its properties; those that do more (an
/// EventDispatcher's) define their own.
function adoptClass(cls) {
    if (typeof cls !== "function" || !cls.prototype || Object.prototype.hasOwnProperty.call(cls.prototype, "$construct"))
        return;
    if (!/^class\b/.test(Function.prototype.toString.call(cls)))
        return;
    Object.defineProperty(cls.prototype, "$construct", {
        value: function $construct() {
            if (!arguments.length)
                return;
            var made = Reflect.construct(cls, Array.prototype.slice.call(arguments));
            var self = this;
            Object.keys(made).forEach(function (key) { self[key] = made[key]; });
        },
        writable: true,
        configurable: true
    });
}

/// Classes by qualified name ("flash.display.MovieClip", "com.studio.Hat",
/// "Main" for the top-level package), and their names by class.
var classes = new Map();
var classNames = new Map();
function defineClass(qualifiedName, cls) {
    var dot = qualifiedName.lastIndexOf(".");
    var pkg = dot < 0 ? "" : qualifiedName.slice(0, dot);
    var name = dot < 0 ? qualifiedName : qualifiedName.slice(dot + 1);
    adoptClass(cls);
    if (as3)
        as3.register(qualifiedName, cls);
    else
        ns(pkg)[name] = cls;
    classes.set(qualifiedName, cls);
    classNames.set(cls, qualifiedName);
    return cls;
}
/// A function or value of a package (flash.net.navigateToURL), as
/// defineClass for a class.
function define(qualifiedName, value) {
    if (as3)
        return as3.register(qualifiedName, value);
    var dot = qualifiedName.lastIndexOf(".");
    ns(dot < 0 ? "" : qualifiedName.slice(0, dot))[dot < 0 ? qualifiedName : qualifiedName.slice(dot + 1)] = value;
    return value;
}
/// Whether as3 has  name, asked without the resolver below telling of
/// one it does not have (the runtime asks before translated code loads).
var probing = false;
function isDefined(name) {
    probing = true;
    try {
        return as3.isDefined(name);
    } finally {
        probing = false;
    }
}
function classByName(qualifiedName) {
    var name = String(qualifiedName).replace("::", ".");
    if (as3 && isDefined(name)) {
        var defined = as3.use(name);
        return typeof defined === "function" ? defined : null;
    }
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
    if (as3 && cls && Object.prototype.hasOwnProperty.call(cls, as3.INFO))
        return as3.qualifiedName(cls);
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
    if (global.console && global.console.log) {
        phoenix._.tracing = true;
        try {
            global.console.log(line);
        } finally {
            phoenix._.tracing = false;
        }
    }
    for (var i = 0; i < traceListeners.length; ++i)
        traceListeners[i](line);
}

/// An ActionScript error object of a class from the top-level package (or
/// flash.errors), as3.js's when it is there so that translated code's
/// catch clauses know it.
function as3Error(type, id, message) {
    var Ctor = as3 ? as3[type] || (as3.isDefined("flash.errors." + type) ? as3.use("flash.errors." + type) : as3.Error)
        : global[type] || Error;
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
/// Where the packages start: phoenix.root.flash is flash.
phoenix.root = as3 ? global : root;
phoenix.defineClass = defineClass;
phoenix.define = define;
phoenix.adoptClass = adoptClass;
phoenix.isDefined = function (name) { return as3 ? isDefined(name) : !!classByName(name); };
/// Whether as3.js is there, with the language's classes.
phoenix.as3Runtime = !!as3;
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
if (as3) {
    // Translated code's trace goes where the runtime's does, and its clock
    // is the movie's.
    as3.output = function (text) { trace(text); };
    as3.clock = function () { return now() - started; };
    // A flash.* class or function the runtime does not have: said once in
    // the console; as3.js then stands in for it with what throws
    // ReferenceError #1065 when it is used, as a player without it would.
    as3.resolvers.push(function (name) {
        if (!probing && /^(flash|fl)\./.test(name))
            unsupported(name, "is not in Phoenix's HTML5 runtime: using it throws");
        return undefined;
    });
}

/// What the runtime does not do, said in the console once each, so that a
/// movie that leans on it shows why: unsupported("Video") or
/// unsupported("BitmapData.applyFilter with a ShaderFilter", "draws nothing").
var told = new Set();
function unsupported(what, consequence) {
    if (told.has(what))
        return;
    told.add(what);
    if (global.console && global.console.warn)
        global.console.warn("Phoenix HTML5: " + what + " " + (consequence || "is not supported: it does nothing") + ".");
}
phoenix.unsupported = unsupported;
})(typeof globalThis !== "undefined" ? globalThis : this);
