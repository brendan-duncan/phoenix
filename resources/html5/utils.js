// The rest of the flash.* API movies lean on: flash.utils (Timer,
// getTimer, the timeouts, class lookup), flash.ui (Keyboard, Mouse),
// flash.net (URLRequest, URLLoader, navigateToURL, SharedObject),
// flash.system, flash.external and a Loader for images.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var EventDispatcher = flash.events.EventDispatcher;
var Event = flash.events.Event;
var TimerEvent = flash.events.TimerEvent;

// flash.utils.

class Timer extends EventDispatcher {
    constructor(delay, repeatCount) {
        super();
        if (!(delay >= 0) || !isFinite(delay))
            throw phoenix.as3Error("RangeError", 2066, "The Timer delay specified is out of range.");
        this._delay = +delay;
        this._repeatCount = repeatCount | 0;
        this._currentCount = 0;
        this._handle = null;
    }
    get delay() { return this._delay; }
    set delay(value) {
        if (!(value >= 0) || !isFinite(value))
            throw phoenix.as3Error("RangeError", 2066, "The Timer delay specified is out of range.");
        this._delay = +value;
        if (this.running) {
            this.stop();
            this.start();
        }
    }
    get repeatCount() { return this._repeatCount; }
    set repeatCount(value) {
        this._repeatCount = value | 0;
        if (this.running && this._repeatCount && this._currentCount >= this._repeatCount)
            this.stop();
    }
    get currentCount() { return this._currentCount; }
    get running() { return this._handle !== null; }
    start() {
        if (this._handle !== null)
            return;
        var self = this;
        this._handle = setInterval(function () { self._tick(); }, Math.max(1, this._delay));
    }
    stop() {
        if (this._handle !== null)
            clearInterval(this._handle);
        this._handle = null;
    }
    reset() {
        this.stop();
        this._currentCount = 0;
    }
    _tick() {
        ++this._currentCount;
        var done = this._repeatCount > 0 && this._currentCount >= this._repeatCount;
        if (done)
            this.stop();
        this.dispatchEvent(new TimerEvent(TimerEvent.TIMER));
        if (done)
            this.dispatchEvent(new TimerEvent(TimerEvent.TIMER_COMPLETE));
        phoenix._.afterEvents && phoenix._.afterEvents();
    }
}

/// The ActionScript timeouts: the closure called with its arguments, errors
/// reported rather than thrown at the browser.
function wrapped(closure, args) {
    return function () {
        try {
            closure.apply(null, args);
        } catch (error) {
            phoenix._.reportError(error, "timer");
        }
        phoenix._.afterEvents && phoenix._.afterEvents();
    };
}
var utils = phoenix.ns("flash.utils");
utils.getTimer = phoenix.getTimer;
utils.setTimeout = function (closure, delay) {
    return setTimeout(wrapped(closure, Array.prototype.slice.call(arguments, 2)), +delay || 0);
};
utils.clearTimeout = function (id) { clearTimeout(id); };
utils.setInterval = function (closure, delay) {
    return setInterval(wrapped(closure, Array.prototype.slice.call(arguments, 2)), Math.max(1, +delay || 0));
};
utils.clearInterval = function (id) { clearInterval(id); };
utils.getDefinitionByName = function (name) {
    var found = phoenix.classByName(name);
    if (!found)
        throw phoenix.as3Error("ReferenceError", 1065, "Variable " + name + " is not defined.");
    return found;
};
utils.getQualifiedClassName = phoenix.qualifiedNameOf;
utils.getQualifiedSuperclassName = function (value) {
    var cls = typeof value === "function" ? value : value && value.constructor;
    var parent = cls ? Object.getPrototypeOf(cls) : null;
    return parent && parent !== Function.prototype ? phoenix.qualifiedNameOf(parent) : null;
};
utils.describeType = function (value) {
    return "<type name=\"" + phoenix.qualifiedNameOf(value) + "\"/>";
};
utils.escapeMultiByte = function (value) { return encodeURIComponent(String(value)); };
utils.unescapeMultiByte = function (value) { return decodeURIComponent(String(value)); };
phoenix.defineClass("flash.utils.Timer", Timer);

// flash.ui.

var Keyboard = {
    BACKSPACE: 8, TAB: 9, ENTER: 13, COMMAND: 15, SHIFT: 16, CONTROL: 17, ALTERNATE: 18, CAPS_LOCK: 20, ESCAPE: 27,
    SPACE: 32, PAGE_UP: 33, PAGE_DOWN: 34, END: 35, HOME: 36, LEFT: 37, UP: 38, RIGHT: 39, DOWN: 40, INSERT: 45,
    DELETE: 46, NUMPAD: 21, NUMPAD_0: 96, NUMPAD_1: 97, NUMPAD_2: 98, NUMPAD_3: 99, NUMPAD_4: 100, NUMPAD_5: 101,
    NUMPAD_6: 102, NUMPAD_7: 103, NUMPAD_8: 104, NUMPAD_9: 105, NUMPAD_MULTIPLY: 106, NUMPAD_ADD: 107,
    NUMPAD_ENTER: 108, NUMPAD_SUBTRACT: 109, NUMPAD_DECIMAL: 110, NUMPAD_DIVIDE: 111, F1: 112, F2: 113, F3: 114,
    F4: 115, F5: 116, F6: 117, F7: 118, F8: 119, F9: 120, F10: 121, F11: 122, F12: 123, F13: 124, F14: 125, F15: 126,
    SEMICOLON: 186, EQUAL: 187, COMMA: 188, MINUS: 189, PERIOD: 190, SLASH: 191, BACKQUOTE: 192, LEFTBRACKET: 219,
    BACKSLASH: 220, RIGHTBRACKET: 221, QUOTE: 222,
    capsLock: false, numLock: false, hasVirtualKeyboard: false, physicalKeyboardType: "alphanumeric",
    isAccessible: function () { return true; }
};
for (var letter = 65; letter <= 90; ++letter)
    Keyboard[String.fromCharCode(letter)] = letter;
["ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"].forEach(function (name, i) {
    Keyboard["NUMBER_" + i] = 48 + i;
    Keyboard[name] = 48 + i;
});
phoenix.defineClass("flash.ui.Keyboard", function KeyboardClass() {});
Object.assign(flash.ui.Keyboard, Keyboard);
phoenix.defineClass("flash.ui.KeyLocation", function KeyLocationClass() {});
Object.assign(flash.ui.KeyLocation, {STANDARD: 0, LEFT: 1, RIGHT: 2, NUM_PAD: 3});
var Mouse = {
    _hidden: false,
    _cursor: "auto",
    hide: function () { Mouse._hidden = true; phoenix._.updateCursor && phoenix._.updateCursor(); },
    show: function () { Mouse._hidden = false; phoenix._.updateCursor && phoenix._.updateCursor(); },
    supportsCursor: true,
    supportsNativeCursor: true,
    registerCursor: function () {},
    unregisterCursor: function () {}
};
Object.defineProperty(Mouse, "cursor", {
    get: function () { return Mouse._cursor; },
    set: function (value) { Mouse._cursor = String(value); phoenix._.updateCursor && phoenix._.updateCursor(); }
});
phoenix.defineClass("flash.ui.Mouse", function MouseClass() {});
Object.getOwnPropertyNames(Mouse).forEach(function (key) {
    Object.defineProperty(flash.ui.Mouse, key, Object.getOwnPropertyDescriptor(Mouse, key));
});
phoenix._.mouse = flash.ui.Mouse;
phoenix.defineClass("flash.ui.MouseCursor", function MouseCursorClass() {});
Object.assign(flash.ui.MouseCursor, {ARROW: "arrow", AUTO: "auto", BUTTON: "button", HAND: "hand", IBEAM: "ibeam"});
class ContextMenu extends EventDispatcher {
    constructor() {
        super();
        this.customItems = [];
        this.builtInItems = {};
    }
    hideBuiltInItems() {}
}
phoenix.defineClass("flash.ui.ContextMenu", ContextMenu);
phoenix.defineClass("flash.ui.ContextMenuItem", class ContextMenuItem extends EventDispatcher {
    constructor(caption, separatorBefore, enabled, visible) {
        super();
        this.caption = caption;
        this.separatorBefore = !!separatorBefore;
        this.enabled = enabled === undefined ? true : !!enabled;
        this.visible = visible === undefined ? true : !!visible;
    }
});

// flash.net.

class URLRequest {
    constructor(url) {
        this.url = url === undefined ? null : String(url);
        this.method = "GET";
        this.data = null;
        this.contentType = "application/x-www-form-urlencoded";
        this.requestHeaders = [];
    }
}
class URLVariables {
    constructor(source) {
        if (source)
            this.decode(source);
    }
    decode(source) {
        var self = this;
        String(source).split("&").forEach(function (pair) {
            if (!pair)
                return;
            var at = pair.indexOf("=");
            var key = decodeURIComponent(at < 0 ? pair : pair.slice(0, at));
            self[key] = at < 0 ? "" : decodeURIComponent(pair.slice(at + 1).replace(/\+/g, " "));
        });
    }
    toString() {
        var self = this;
        return Object.keys(this).map(function (key) {
            return encodeURIComponent(key) + "=" + encodeURIComponent(self[key]);
        }).join("&");
    }
}
function requestUrl(request) {
    var url = request.url || "";
    if (request.method === "GET" && request.data)
        url += (url.indexOf("?") < 0 ? "?" : "&") + String(request.data);
    return url;
}
class URLLoader extends EventDispatcher {
    constructor(request) {
        super();
        this.data = null;
        this.dataFormat = "text";
        this.bytesLoaded = 0;
        this.bytesTotal = 0;
        if (request)
            this.load(request);
    }
    load(request) {
        var self = this;
        if (typeof fetch === "undefined")
            return;
        var options = {method: request.method || "GET"};
        if (options.method !== "GET" && request.data !== null && request.data !== undefined) {
            options.body = String(request.data);
            options.headers = {"Content-Type": request.contentType};
        }
        this.dispatchEvent(new Event(Event.OPEN));
        fetch(requestUrl(request), options).then(function (response) {
            if (!response.ok)
                throw new Error("HTTP " + response.status);
            return self.dataFormat === "binary" ? response.arrayBuffer() : response.text();
        }).then(function (body) {
            self.data = self.dataFormat === "variables" ? new URLVariables(body) : body;
            self.bytesLoaded = self.bytesTotal = typeof body === "string" ? body.length : body.byteLength;
            self.dispatchEvent(new Event(Event.COMPLETE));
            phoenix._.afterEvents && phoenix._.afterEvents();
        }).catch(function (error) {
            self.dispatchEvent(new flash.events.IOErrorEvent("ioError", false, false,
                "Error #2032: Stream Error. URL: " + request.url, 2032));
            phoenix._.afterEvents && phoenix._.afterEvents();
        });
    }
    close() {}
}
var net = phoenix.ns("flash.net");
net.navigateToURL = function (request, window) {
    if (typeof open !== "undefined")
        open(requestUrl(request), window || "_blank");
};
net.sendToURL = function (request) {
    if (typeof fetch !== "undefined")
        fetch(requestUrl(request), {method: request.method || "GET", mode: "no-cors"}).catch(function () {});
};
/// SharedObject.getLocal: what a movie saves, kept in the browser's local
/// storage by name.
class SharedObject extends EventDispatcher {
    constructor(name) {
        super();
        this._key = "phoenix.so." + name;
        this.data = {};
        this.objectEncoding = 3;
        try {
            var saved = typeof localStorage !== "undefined" ? localStorage.getItem(this._key) : null;
            if (saved)
                this.data = JSON.parse(saved);
        } catch (error) {}
    }
    get size() {
        try { return JSON.stringify(this.data).length; } catch (error) { return 0; }
    }
    flush(minDiskSpace) {
        try {
            if (typeof localStorage !== "undefined")
                localStorage.setItem(this._key, JSON.stringify(this.data));
            return "flushed";
        } catch (error) {
            return "pending";
        }
    }
    clear() {
        this.data = {};
        try {
            if (typeof localStorage !== "undefined")
                localStorage.removeItem(this._key);
        } catch (error) {}
    }
    close() {}
    setProperty(name, value) { this.data[name] = value; }
    static getLocal(name, localPath, secure) { return new SharedObject((localPath || "") + "/" + name); }
}
phoenix.defineClass("flash.net.URLRequest", URLRequest);
phoenix.defineClass("flash.net.URLVariables", URLVariables);
phoenix.defineClass("flash.net.URLLoader", URLLoader);
phoenix.defineClass("flash.net.SharedObject", SharedObject);
phoenix.defineClass("flash.net.URLRequestMethod", function URLRequestMethod() {});
Object.assign(flash.net.URLRequestMethod, {GET: "GET", POST: "POST", PUT: "PUT", DELETE: "DELETE", HEAD: "HEAD", OPTIONS: "OPTIONS"});
phoenix.defineClass("flash.net.URLLoaderDataFormat", function URLLoaderDataFormat() {});
Object.assign(flash.net.URLLoaderDataFormat, {BINARY: "binary", TEXT: "text", VARIABLES: "variables"});
phoenix.defineClass("flash.net.URLRequestHeader", class URLRequestHeader {
    constructor(name, value) { this.name = name || ""; this.value = value || ""; }
});

// flash.system and flash.external.

var Capabilities = {
    version: "WIN 32,0,0,0", playerType: "PlugIn", os: typeof navigator !== "undefined" ? navigator.platform : "",
    language: typeof navigator !== "undefined" && navigator.language ? navigator.language.split("-")[0] : "en",
    manufacturer: "Phoenix", isDebugger: false, hasAudio: true, hasMP3: true, hasStreamingAudio: true,
    screenResolutionX: typeof screen !== "undefined" ? screen.width : 1920,
    screenResolutionY: typeof screen !== "undefined" ? screen.height : 1080,
    screenDPI: 96, pixelAspectRatio: 1, touchscreenType: "none", cpuArchitecture: "x86", supports64BitProcesses: true,
    avHardwareDisable: true, localFileReadDisable: true, hasIME: false, serverString: ""
};
phoenix.defineClass("flash.system.Capabilities", function CapabilitiesClass() {});
Object.assign(flash.system.Capabilities, Capabilities);
phoenix.defineClass("flash.system.System", function SystemClass() {});
Object.assign(flash.system.System, {
    totalMemory: 64 * 1024 * 1024, totalMemoryNumber: 64 * 1024 * 1024, freeMemory: 32 * 1024 * 1024,
    privateMemory: 64 * 1024 * 1024, useCodePage: false,
    gc: function () {}, pause: function () {}, resume: function () {}, exit: function () {},
    setClipboard: function (text) {
        if (typeof navigator !== "undefined" && navigator.clipboard)
            navigator.clipboard.writeText(String(text)).catch(function () {});
    }
});
class ApplicationDomain {
    getDefinition(name) { return flash.utils.getDefinitionByName(name); }
    hasDefinition(name) { return !!phoenix.classByName(name); }
    static get currentDomain() { return domain; }
}
var domain = new ApplicationDomain();
phoenix.defineClass("flash.system.ApplicationDomain", ApplicationDomain);
phoenix.defineClass("flash.system.LoaderContext", class LoaderContext {
    constructor(checkPolicyFile, applicationDomain, securityDomain) {
        this.checkPolicyFile = !!checkPolicyFile;
        this.applicationDomain = applicationDomain || null;
        this.securityDomain = securityDomain || null;
    }
});
phoenix.defineClass("flash.system.Security", function SecurityClass() {});
Object.assign(flash.system.Security, {sandboxType: "remote", allowDomain: function () {}, allowInsecureDomain: function () {},
    loadPolicyFile: function () {}});
phoenix.defineClass("flash.external.ExternalInterface", function ExternalInterfaceClass() {});
Object.assign(flash.external.ExternalInterface, {
    available: typeof window !== "undefined",
    objectID: "phoenix",
    marshallExceptions: false,
    call: function (name) {
        var fn = typeof window !== "undefined" ? window[name] : null;
        return typeof fn === "function" ? fn.apply(window, Array.prototype.slice.call(arguments, 1)) : null;
    },
    addCallback: function (name, closure) {
        if (typeof window !== "undefined")
            window[name] = closure;
    }
});

// flash.display.Loader, for pictures.

class Loader extends flash.display.DisplayObjectContainer {
    constructor() {
        super();
        this._contentLoaderInfo = new flash.display.LoaderInfo(null);
        this._content = null;
    }
    get content() { return this._content; }
    get contentLoaderInfo() { return this._contentLoaderInfo; }
    load(request, context) {
        var self = this;
        if (typeof Image === "undefined")
            return;
        var image = new Image();
        image.onload = function () {
            var data = new flash.display.BitmapData(image.width, image.height, true, 0);
            data._ctx().drawImage(image, 0, 0);
            self._content = new flash.display.Bitmap(data);
            self._contentLoaderInfo._content = self._content;
            self.addChild(self._content);
            self._contentLoaderInfo.dispatchEvent(new Event(Event.INIT));
            self._contentLoaderInfo.dispatchEvent(new Event(Event.COMPLETE));
            phoenix._.afterEvents && phoenix._.afterEvents();
        };
        image.onerror = function () {
            self._contentLoaderInfo.dispatchEvent(new flash.events.IOErrorEvent("ioError", false, false,
                "Error #2035: URL Not Found. URL: " + request.url, 2035));
        };
        image.src = request.url;
    }
    loadBytes(bytes, context) {}
    unload() {
        if (this._content && this._content._parent === this)
            this.removeChild(this._content);
        this._content = null;
    }
    unloadAndStop(gc) { this.unload(); }
    close() {}
}
phoenix.defineClass("flash.display.Loader", Loader);
})(phoenix);
