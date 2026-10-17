// Node.js for HTML extension panels whose manifest asks for it
// (--enable-nodejs or --mixed-context in its CEFCommandLine). Phoenix runs
// this after adobe_cep.js, before the panel's own scripts, and only for
// those panels.
//
// The modules are written here in JavaScript, over a few calls into
// Phoenix through the same bridge CSInterface uses: files ("node.fs.*"),
// the system ("node.os.info"), zlib ("node.zlib"), processes ("proc.*")
// and the web ("net.*"). Calls answer at once, so the Sync functions are
// synchronous; the callback and promise forms answer on a later turn of
// the event loop. Processes and web requests answer through
// window.__phoenixCep.processEvents.
//
// The page gets require, process, Buffer, global, __dirname, __filename,
// setImmediate and window.cep_node, as a panel in mixed context does.
// module and exports are left off the page, so libraries that look for
// them (jQuery and other UMD scripts) still set their globals.

(function () {
    "use strict";
    var bridge = window.__phoenixCep;
    if (!bridge || window.cep_node)
        return;

    var NODE_VERSION = "15.9.0";

    // -----------------------------------------------------------------------
    // Calls into Phoenix

    var ERRNO_POSIX = {
        EPERM: -1, ENOENT: -2, ESRCH: -3, EIO: -5, EBADF: -9, EACCES: -13, EBUSY: -16, EEXIST: -17, EXDEV: -18,
        ENOTDIR: -20, EISDIR: -21, EINVAL: -22, EMFILE: -24, EPIPE: -32, ENOSYS: -38, ENOTEMPTY: -39
    };
    var ERRNO_WIN = {
        EPERM: -4048, ENOENT: -4058, ESRCH: -4040, EIO: -4070, EBADF: -4083, EACCES: -4092, EBUSY: -4082,
        EEXIST: -4075, EXDEV: -4037, ENOTDIR: -4052, EISDIR: -4068, EINVAL: -4071, EMFILE: -4066, EPIPE: -4047,
        ENOSYS: -4054, ENOTEMPTY: -4051
    };

    function nativeError(reply) {
        var error = new Error(reply.error);
        error.code = reply.code;
        var table = isWindows ? ERRNO_WIN : ERRNO_POSIX;
        if (table[reply.code] !== undefined)
            error.errno = table[reply.code];
        if (reply.syscall)
            error.syscall = reply.syscall;
        if (reply.path)
            error.path = reply.path;
        return error;
    }

    function native(name, args) {
        var reply = bridge.request(name, args || []);
        if (reply.error)
            throw nativeError(reply);
        return reply.value;
    }

    function codeError(code, message, Type) {
        var error = new (Type || Error)(message);
        error.code = code;
        return error;
    }

    function notAvailable(what) {
        return codeError("ERR_NOT_AVAILABLE", what + " is not available in Phoenix's HTML panels");
    }

    var info = native("node.os.info");
    var isWindows = info.platform === "win32";

    var later = function (fn) { setTimeout(fn, 0); };
    var microtask = typeof queueMicrotask === "function" ? function (fn) { queueMicrotask(fn); }
                                                         : function (fn) { Promise.resolve().then(fn); };

    // -----------------------------------------------------------------------
    // Bytes and text

    var hasTextCodec = typeof TextEncoder === "function" && typeof TextDecoder === "function";
    var textEncoder = hasTextCodec ? new TextEncoder() : null;
    var textDecoder = hasTextCodec ? new TextDecoder("utf-8") : null;

    function utf8Encode(text) {
        if (textEncoder)
            return textEncoder.encode(text);
        var out = [];
        for (var i = 0; i < text.length; i++) {
            var c = text.charCodeAt(i);
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.length) {
                var d = text.charCodeAt(i + 1);
                if (d >= 0xDC00 && d < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                    i++;
                } else {
                    c = 0xFFFD;
                }
            } else if (c >= 0xD800 && c < 0xE000) {
                c = 0xFFFD;
            }
            if (c < 0x80)
                out.push(c);
            else if (c < 0x800)
                out.push(0xC0 | (c >> 6), 0x80 | (c & 63));
            else if (c < 0x10000)
                out.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
            else
                out.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        }
        return new Uint8Array(out);
    }

    function utf8Decode(bytes) {
        if (textDecoder)
            return textDecoder.decode(bytes);
        var out = "";
        var i = 0;
        while (i < bytes.length) {
            var b = bytes[i];
            var code = 0xFFFD, need = 0;
            if (b < 0x80) {
                code = b;
            } else if (b >= 0xC2 && b < 0xE0) {
                need = 1;
                code = b & 0x1F;
            } else if (b >= 0xE0 && b < 0xF0) {
                need = 2;
                code = b & 0x0F;
            } else if (b >= 0xF0 && b < 0xF5) {
                need = 3;
                code = b & 0x07;
            }
            var j = 1;
            for (; j <= need; j++) {
                var next = bytes[i + j];
                if (next === undefined || (next & 0xC0) !== 0x80)
                    break;
                code = (code << 6) | (next & 0x3F);
            }
            if (j <= need) {
                code = 0xFFFD;
                i += j;
            } else {
                i += need + 1;
            }
            if (code >= 0x10000) {
                code -= 0x10000;
                out += String.fromCharCode(0xD800 + (code >> 10), 0xDC00 + (code & 0x3FF));
            } else {
                out += String.fromCharCode(code);
            }
        }
        return out;
    }

    var B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var B64_VALUES = {};
    for (var b64i = 0; b64i < 64; b64i++)
        B64_VALUES[B64.charAt(b64i)] = b64i;
    B64_VALUES["-"] = 62;
    B64_VALUES["_"] = 63;

    function base64Encode(bytes, url) {
        var out = "";
        var i = 0;
        for (; i + 2 < bytes.length; i += 3) {
            var n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
            out += B64.charAt((n >> 18) & 63) + B64.charAt((n >> 12) & 63) + B64.charAt((n >> 6) & 63) + B64.charAt(n & 63);
        }
        if (i < bytes.length) {
            var m = bytes[i] << 16;
            if (i + 1 < bytes.length)
                m |= bytes[i + 1] << 8;
            out += B64.charAt((m >> 18) & 63) + B64.charAt((m >> 12) & 63);
            out += i + 1 < bytes.length ? B64.charAt((m >> 6) & 63) : (url ? "" : "=");
            out += url ? "" : "=";
        }
        if (url)
            out = out.replace(/\+/g, "-").replace(/\//g, "_");
        return out;
    }

    function base64Decode(text) {
        var out = new Uint8Array(Math.floor(text.length * 3 / 4) + 3);
        var length = 0, buffer = 0, bits = 0;
        for (var i = 0; i < text.length; i++) {
            var v = B64_VALUES[text.charAt(i)];
            if (v === undefined)
                continue;
            buffer = ((buffer << 6) | v) & 0xFFFFFF;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out[length++] = (buffer >> bits) & 0xFF;
            }
        }
        return out.subarray(0, length);
    }

    function normalizeEncoding(encoding) {
        if (encoding === undefined || encoding === null || encoding === "")
            return "utf8";
        switch (String(encoding).toLowerCase()) {
        case "utf8": case "utf-8": return "utf8";
        case "hex": return "hex";
        case "base64": return "base64";
        case "base64url": return "base64url";
        case "ascii": return "ascii";
        case "latin1": case "binary": return "latin1";
        case "ucs2": case "ucs-2": case "utf16le": case "utf-16le": return "utf16le";
        default: return null;
        }
    }

    function checkedEncoding(encoding) {
        var e = normalizeEncoding(encoding);
        if (e === null)
            throw codeError("ERR_UNKNOWN_ENCODING", "Unknown encoding: " + encoding, TypeError);
        return e;
    }

    function stringToBytes(text, encoding) {
        text = String(text);
        var i, out;
        switch (checkedEncoding(encoding)) {
        case "hex":
            out = new Uint8Array(text.length >>> 1);
            for (i = 0; i < out.length; i++) {
                var byte = parseInt(text.substr(i * 2, 2), 16);
                if (isNaN(byte))
                    return out.subarray(0, i);
                out[i] = byte;
            }
            return out;
        case "base64":
        case "base64url":
            return base64Decode(text);
        case "ascii":
        case "latin1":
            out = new Uint8Array(text.length);
            for (i = 0; i < text.length; i++)
                out[i] = text.charCodeAt(i) & 0xFF;
            return out;
        case "utf16le":
            out = new Uint8Array(text.length * 2);
            for (i = 0; i < text.length; i++) {
                var c = text.charCodeAt(i);
                out[i * 2] = c & 0xFF;
                out[i * 2 + 1] = c >> 8;
            }
            return out;
        default:
            return utf8Encode(text);
        }
    }

    function bytesToString(bytes, encoding) {
        var i, out = "";
        switch (checkedEncoding(encoding)) {
        case "hex":
            for (i = 0; i < bytes.length; i++)
                out += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
            return out;
        case "base64":
            return base64Encode(bytes, false);
        case "base64url":
            return base64Encode(bytes, true);
        case "ascii":
            for (i = 0; i < bytes.length; i += 8192)
                out += String.fromCharCode.apply(null, Array.prototype.map.call(bytes.subarray(i, i + 8192), function (b) { return b & 0x7F; }));
            return out;
        case "latin1":
            for (i = 0; i < bytes.length; i += 8192)
                out += String.fromCharCode.apply(null, bytes.subarray(i, i + 8192));
            return out;
        case "utf16le":
            for (i = 0; i + 1 < bytes.length; i += 2)
                out += String.fromCharCode(bytes[i] | (bytes[i + 1] << 8));
            return out;
        default:
            return utf8Decode(bytes);
        }
    }

    // -----------------------------------------------------------------------
    // Modules

    var factories = Object.create(null);
    var builtins = Object.create(null);

    function define(name, factory) {
        factories[name] = factory;
    }

    function builtin(name) {
        if (!(name in builtins))
            builtins[name] = factories[name]();
        return builtins[name];
    }

    // -----------------------------------------------------------------------
    // events

    define("events", function () {
        function EventEmitter() {
            EventEmitter.init.call(this);
        }
        EventEmitter.EventEmitter = EventEmitter;
        EventEmitter.defaultMaxListeners = 10;
        EventEmitter.errorMonitor = typeof Symbol === "function" ? Symbol("events.errorMonitor") : "errorMonitor";
        EventEmitter.init = function () {
            if (!this._events || this._events === Object.getPrototypeOf(this)._events)
                this._events = Object.create(null);
            this._maxListeners = this._maxListeners || undefined;
        };

        function events(emitter) {
            if (!emitter._events)
                emitter._events = Object.create(null);
            return emitter._events;
        }

        function add(emitter, type, listener, prepend) {
            if (typeof listener !== "function")
                throw codeError("ERR_INVALID_ARG_TYPE", 'The "listener" argument must be of type function', TypeError);
            var list = events(emitter);
            if (list.newListener)
                emitter.emit("newListener", type, listener.listener ? listener.listener : listener);
            list = events(emitter);
            if (!list[type])
                list[type] = [];
            if (prepend)
                list[type].unshift(listener);
            else
                list[type].push(listener);
            return emitter;
        }

        function onceWrapper(emitter, type, listener) {
            var fired = false;
            function wrapper() {
                if (fired)
                    return undefined;
                fired = true;
                emitter.removeListener(type, wrapper);
                return listener.apply(emitter, arguments);
            }
            wrapper.listener = listener;
            return wrapper;
        }

        var proto = EventEmitter.prototype;
        proto._events = undefined;
        proto._maxListeners = undefined;
        proto.setMaxListeners = function (n) {
            this._maxListeners = n;
            return this;
        };
        proto.getMaxListeners = function () {
            return this._maxListeners === undefined ? EventEmitter.defaultMaxListeners : this._maxListeners;
        };
        proto.on = proto.addListener = function (type, listener) {
            return add(this, type, listener, false);
        };
        proto.prependListener = function (type, listener) {
            return add(this, type, listener, true);
        };
        proto.once = function (type, listener) {
            return add(this, type, onceWrapper(this, type, listener), false);
        };
        proto.prependOnceListener = function (type, listener) {
            return add(this, type, onceWrapper(this, type, listener), true);
        };
        proto.off = proto.removeListener = function (type, listener) {
            var list = events(this)[type];
            if (!list)
                return this;
            for (var i = list.length - 1; i >= 0; i--) {
                if (list[i] === listener || list[i].listener === listener) {
                    list.splice(i, 1);
                    if (list.length === 0)
                        delete this._events[type];
                    if (this._events.removeListener)
                        this.emit("removeListener", type, listener);
                    break;
                }
            }
            return this;
        };
        proto.removeAllListeners = function (type) {
            if (type === undefined)
                this._events = Object.create(null);
            else if (this._events)
                delete this._events[type];
            return this;
        };
        proto.emit = function (type) {
            var list = events(this)[type];
            var args = Array.prototype.slice.call(arguments, 1);
            if (type === "error" && this._events[EventEmitter.errorMonitor]) {
                var monitors = this._events[EventEmitter.errorMonitor].slice();
                for (var m = 0; m < monitors.length; m++)
                    monitors[m].apply(this, args);
            }
            if (!list || list.length === 0) {
                if (type === "error") {
                    var error = args[0];
                    if (error instanceof Error)
                        throw error;
                    var wrapped = new Error("Unhandled error. (" + String(error) + ")");
                    wrapped.context = error;
                    throw wrapped;
                }
                return false;
            }
            list = list.slice();
            for (var i = 0; i < list.length; i++)
                list[i].apply(this, args);
            return true;
        };
        proto.listeners = function (type) {
            return (events(this)[type] || []).map(function (l) { return l.listener || l; });
        };
        proto.rawListeners = function (type) {
            return (events(this)[type] || []).slice();
        };
        proto.listenerCount = function (type) {
            return (events(this)[type] || []).length;
        };
        proto.eventNames = function () {
            return Object.keys(events(this));
        };

        EventEmitter.listenerCount = function (emitter, type) {
            return emitter.listenerCount(type);
        };
        EventEmitter.once = function (emitter, type) {
            return new Promise(function (resolve, reject) {
                function onError(error) {
                    emitter.removeListener(type, onEvent);
                    reject(error);
                }
                function onEvent() {
                    if (type !== "error")
                        emitter.removeListener("error", onError);
                    resolve(Array.prototype.slice.call(arguments));
                }
                emitter.once(type, onEvent);
                if (type !== "error")
                    emitter.once("error", onError);
            });
        };
        EventEmitter.on = function () {
            throw notAvailable("events.on");
        };
        return EventEmitter;
    });

    function EventEmitter() {
        return builtin("events");
    }

    function inherits(ctor, superCtor) {
        ctor.super_ = superCtor;
        Object.setPrototypeOf(ctor.prototype, superCtor.prototype);
    }

    // -----------------------------------------------------------------------
    // util

    function deepEqual(a, b, strict, seen) {
        if (strict ? Object.is(a, b) : (a == b || (a !== a && b !== b))) // eslint-disable-line eqeqeq
            return true;
        if (typeof a !== "object" || typeof b !== "object" || a === null || b === null)
            return false;
        if (strict && Object.getPrototypeOf(a) !== Object.getPrototypeOf(b))
            return false;
        if (a instanceof Date && b instanceof Date)
            return a.getTime() === b.getTime();
        if (a instanceof RegExp && b instanceof RegExp)
            return String(a) === String(b);
        if (ArrayBuffer.isView(a) && ArrayBuffer.isView(b)) {
            if (a.length !== b.length)
                return false;
            for (var t = 0; t < a.length; t++) {
                if (a[t] !== b[t])
                    return false;
            }
            return true;
        }
        seen = seen || [];
        for (var s = 0; s < seen.length; s++) {
            if (seen[s][0] === a && seen[s][1] === b)
                return true;
        }
        seen.push([a, b]);
        if (a instanceof Map && b instanceof Map) {
            if (a.size !== b.size)
                return false;
            var equal = true;
            a.forEach(function (value, key) {
                if (equal && (!b.has(key) || !deepEqual(value, b.get(key), strict, seen)))
                    equal = false;
            });
            return equal;
        }
        if (a instanceof Set && b instanceof Set) {
            if (a.size !== b.size)
                return false;
            var all = true;
            a.forEach(function (value) {
                if (all && !b.has(value))
                    all = false;
            });
            return all;
        }
        if (Array.isArray(a) !== Array.isArray(b))
            return false;
        var keysA = Object.keys(a), keysB = Object.keys(b);
        if (keysA.length !== keysB.length)
            return false;
        for (var k = 0; k < keysA.length; k++) {
            if (!Object.prototype.hasOwnProperty.call(b, keysA[k]) || !deepEqual(a[keysA[k]], b[keysA[k]], strict, seen))
                return false;
        }
        return true;
    }

    define("util", function () {
        var customInspect = typeof Symbol === "function" ? Symbol.for("nodejs.util.inspect.custom") : "inspect";
        var customPromisify = typeof Symbol === "function" ? Symbol.for("nodejs.util.promisify.custom") : "__promisify__";

        function inspect(value, options) {
            var depth = options && options.depth !== undefined ? options.depth : 2;
            if (depth === null)
                depth = Infinity;
            var seen = [];
            function quote(text) {
                return "'" + String(text).replace(/\\/g, "\\\\").replace(/'/g, "\\'").replace(/\n/g, "\\n") + "'";
            }
            function format(v, level, nested) {
                switch (typeof v) {
                case "string": return nested ? quote(v) : v;
                case "number": return Object.is(v, -0) ? "-0" : String(v);
                case "bigint": return String(v) + "n";
                case "boolean": return String(v);
                case "undefined": return "undefined";
                case "symbol": return v.toString();
                case "function":
                    return v.prototype && /^class\s/.test(Function.prototype.toString.call(v))
                        ? "[class " + (v.name || "(anonymous)") + "]"
                        : "[Function: " + (v.name || "(anonymous)") + "]";
                }
                if (v === null)
                    return "null";
                if (v && typeof v[customInspect] === "function" && v[customInspect] !== inspect)
                    return String(v[customInspect](level, options || {}));
                if (seen.indexOf(v) >= 0)
                    return "[Circular]";
                if (v instanceof Date)
                    return isNaN(v.getTime()) ? "Invalid Date" : v.toISOString();
                if (v instanceof RegExp)
                    return String(v);
                if (v instanceof Error)
                    return v.stack || (v.name + ": " + v.message);
                if (typeof Buffer === "function" && v instanceof Buffer) {
                    var hex = [];
                    for (var h = 0; h < Math.min(v.length, 50); h++)
                        hex.push((v[h] < 16 ? "0" : "") + v[h].toString(16));
                    return "<Buffer" + (hex.length ? " " + hex.join(" ") : "") + (v.length > 50 ? " ... " + (v.length - 50) + " more bytes" : "") + ">";
                }
                if (level > depth)
                    return Array.isArray(v) ? "[Array]" : "[Object]";
                seen.push(v);
                var parts = [];
                var out;
                if (Array.isArray(v) || ArrayBuffer.isView(v)) {
                    for (var i = 0; i < Math.min(v.length, 100); i++)
                        parts.push(format(v[i], level + 1, true));
                    if (v.length > 100)
                        parts.push("... " + (v.length - 100) + " more items");
                    out = (Array.isArray(v) ? "" : v.constructor.name + "(" + v.length + ") ") + "[ " + parts.join(", ") + " ]";
                    if (parts.length === 0)
                        out = Array.isArray(v) ? "[]" : v.constructor.name + "(0) []";
                } else if (v instanceof Map) {
                    v.forEach(function (value, key) {
                        parts.push(format(key, level + 1, true) + " => " + format(value, level + 1, true));
                    });
                    out = "Map(" + v.size + ") {" + (parts.length ? " " + parts.join(", ") + " " : "") + "}";
                } else if (v instanceof Set) {
                    v.forEach(function (value) {
                        parts.push(format(value, level + 1, true));
                    });
                    out = "Set(" + v.size + ") {" + (parts.length ? " " + parts.join(", ") + " " : "") + "}";
                } else {
                    var keys = Object.keys(v);
                    for (var k = 0; k < keys.length; k++) {
                        var key = /^[A-Za-z_$][\w$]*$/.test(keys[k]) ? keys[k] : quote(keys[k]);
                        parts.push(key + ": " + format(v[keys[k]], level + 1, true));
                    }
                    var name = v.constructor && v.constructor !== Object && v.constructor.name ? v.constructor.name + " " : "";
                    if (Object.getPrototypeOf(v) === null)
                        name = "[Object: null prototype] ";
                    out = parts.length ? name + "{ " + parts.join(", ") + " }" : name + "{}";
                }
                seen.pop();
                return out;
            }
            return format(value, 0, typeof value === "string" ? true : false);
        }
        inspect.custom = customInspect;
        inspect.defaultOptions = { depth: 2 };

        function format(first) {
            var args = arguments;
            var index = 1;
            var out;
            if (typeof first !== "string") {
                var all = [];
                for (var a = 0; a < args.length; a++)
                    all.push(typeof args[a] === "string" ? args[a] : inspect(args[a]));
                return all.join(" ");
            }
            out = first.replace(/%[sdifjoOc%]/g, function (token) {
                if (token === "%%")
                    return "%";
                if (index >= args.length)
                    return token;
                var arg = args[index++];
                switch (token) {
                case "%s": return typeof arg === "string" ? arg : (typeof arg === "object" && arg !== null ? inspect(arg, { depth: 0 }) : String(arg));
                case "%d": return typeof arg === "object" && arg !== null ? "NaN" : String(Number(arg));
                case "%i": return String(parseInt(arg, 10));
                case "%f": return String(parseFloat(arg));
                case "%j":
                    try {
                        return JSON.stringify(arg);
                    } catch (e) {
                        return "[Circular]";
                    }
                case "%o": case "%O": return inspect(arg);
                case "%c": return "";
                }
                return token;
            });
            for (; index < args.length; index++)
                out += " " + (typeof args[index] === "string" ? args[index] : inspect(args[index]));
            return out;
        }

        function promisify(fn) {
            if (typeof fn !== "function")
                throw codeError("ERR_INVALID_ARG_TYPE", 'The "original" argument must be of type function', TypeError);
            if (fn[customPromisify])
                return fn[customPromisify];
            function promisified() {
                var self = this;
                var args = Array.prototype.slice.call(arguments);
                return new Promise(function (resolve, reject) {
                    args.push(function (error, value) {
                        if (error)
                            reject(error);
                        else
                            resolve(value);
                    });
                    fn.apply(self, args);
                });
            }
            return promisified;
        }
        promisify.custom = customPromisify;

        function callbackify(fn) {
            return function () {
                var args = Array.prototype.slice.call(arguments);
                var callback = args.pop();
                fn.apply(this, args).then(function (value) {
                    later(function () { callback(null, value); });
                }, function (error) {
                    later(function () { callback(error); });
                });
            };
        }

        var warned = {};
        function deprecate(fn, message, code) {
            return function () {
                if (!warned[code || message]) {
                    warned[code || message] = true;
                    if (window.console)
                        console.warn("DeprecationWarning: " + message);
                }
                return fn.apply(this, arguments);
            };
        }

        function debuglog() {
            var log = function () {};
            log.enabled = false;
            return log;
        }

        var types = {
            isPromise: function (v) { return v instanceof Promise; },
            isDate: function (v) { return v instanceof Date; },
            isRegExp: function (v) { return v instanceof RegExp; },
            isMap: function (v) { return v instanceof Map; },
            isSet: function (v) { return v instanceof Set; },
            isNativeError: function (v) { return v instanceof Error; },
            isTypedArray: function (v) { return ArrayBuffer.isView(v) && !(v instanceof DataView); },
            isUint8Array: function (v) { return v instanceof Uint8Array; },
            isArrayBuffer: function (v) { return v instanceof ArrayBuffer; },
            isAnyArrayBuffer: function (v) { return v instanceof ArrayBuffer; },
            isArrayBufferView: function (v) { return ArrayBuffer.isView(v); },
            isAsyncFunction: function (v) { return typeof v === "function" && v.constructor && v.constructor.name === "AsyncFunction"; },
            isGeneratorFunction: function (v) { return typeof v === "function" && v.constructor && /GeneratorFunction$/.test(v.constructor.name); },
            isBoxedPrimitive: function (v) { return v instanceof Number || v instanceof String || v instanceof Boolean; }
        };

        return {
            format: format,
            formatWithOptions: function (options) {
                return format.apply(null, Array.prototype.slice.call(arguments, 1));
            },
            inspect: inspect,
            inherits: inherits,
            promisify: promisify,
            callbackify: callbackify,
            deprecate: deprecate,
            debuglog: debuglog,
            debug: debuglog,
            types: types,
            isDeepStrictEqual: function (a, b) { return deepEqual(a, b, true); },
            isArray: Array.isArray,
            isBoolean: function (v) { return typeof v === "boolean"; },
            isNull: function (v) { return v === null; },
            isNullOrUndefined: function (v) { return v === null || v === undefined; },
            isNumber: function (v) { return typeof v === "number"; },
            isString: function (v) { return typeof v === "string"; },
            isSymbol: function (v) { return typeof v === "symbol"; },
            isUndefined: function (v) { return v === undefined; },
            isRegExp: types.isRegExp,
            isObject: function (v) { return v !== null && typeof v === "object"; },
            isDate: types.isDate,
            isError: types.isNativeError,
            isFunction: function (v) { return typeof v === "function"; },
            isPrimitive: function (v) { return v === null || (typeof v !== "object" && typeof v !== "function"); },
            isBuffer: function (v) { return v instanceof Buffer; },
            TextEncoder: window.TextEncoder,
            TextDecoder: window.TextDecoder
        };
    });

    // -----------------------------------------------------------------------
    // buffer

    function toBytes(value, encoding) {
        if (typeof value === "string")
            return stringToBytes(value, encoding);
        if (value instanceof Uint8Array)
            return value;
        if (value instanceof ArrayBuffer)
            return new Uint8Array(value);
        if (ArrayBuffer.isView(value))
            return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        if (value && value.type === "Buffer" && Array.isArray(value.data))
            return new Uint8Array(value.data);
        if (value && typeof value.length === "number") {
            var out = new Uint8Array(value.length);
            for (var i = 0; i < value.length; i++)
                out[i] = Number(value[i]) & 0xFF;
            return out;
        }
        if (value && typeof value.valueOf === "function" && value.valueOf() !== value)
            return toBytes(value.valueOf(), encoding);
        throw codeError("ERR_INVALID_ARG_TYPE",
            "The first argument must be of type string or an instance of Buffer, ArrayBuffer, or Array or an Array-like Object.",
            TypeError);
    }

    class Buffer extends Uint8Array {
        constructor(value, encodingOrOffset, length) {
            if (typeof value === "number") {
                super(value);
            } else if (value instanceof ArrayBuffer) {
                var offset = encodingOrOffset === undefined ? 0 : Number(encodingOrOffset);
                super(value, offset, length === undefined ? value.byteLength - offset : Number(length));
            } else {
                var bytes = toBytes(value, encodingOrOffset);
                super(bytes.length);
                this.set(bytes);
            }
        }

        static from(value, encodingOrOffset, length) {
            if (value instanceof ArrayBuffer)
                return new Buffer(value, encodingOrOffset, length);
            return new Buffer(toBytes(value, encodingOrOffset));
        }
        static alloc(size, fill, encoding) {
            var buffer = new Buffer(size);
            if (fill !== undefined && fill !== 0)
                buffer.fill(fill, 0, size, encoding);
            return buffer;
        }
        static allocUnsafe(size) {
            return new Buffer(size);
        }
        static allocUnsafeSlow(size) {
            return new Buffer(size);
        }
        static isBuffer(value) {
            return value instanceof Buffer;
        }
        static isEncoding(encoding) {
            return typeof encoding === "string" && normalizeEncoding(encoding) !== null;
        }
        static byteLength(value, encoding) {
            if (typeof value === "string")
                return stringToBytes(value, encoding).length;
            return value.byteLength;
        }
        static concat(list, totalLength) {
            if (totalLength === undefined) {
                totalLength = 0;
                for (var i = 0; i < list.length; i++)
                    totalLength += list[i].length;
            }
            var out = Buffer.alloc(totalLength);
            var at = 0;
            for (var j = 0; j < list.length && at < totalLength; j++) {
                var part = toBytes(list[j]);
                var count = Math.min(part.length, totalLength - at);
                out.set(part.subarray(0, count), at);
                at += count;
            }
            return out;
        }
        static compare(a, b) {
            var n = Math.min(a.length, b.length);
            for (var i = 0; i < n; i++) {
                if (a[i] !== b[i])
                    return a[i] < b[i] ? -1 : 1;
            }
            return a.length === b.length ? 0 : (a.length < b.length ? -1 : 1);
        }

        toString(encoding, start, end) {
            start = start === undefined ? 0 : Math.max(0, start | 0);
            end = end === undefined ? this.length : Math.min(this.length, end | 0);
            if (end <= start)
                return "";
            return bytesToString(start === 0 && end === this.length ? this : this.subarray(start, end), encoding);
        }
        toLocaleString(encoding, start, end) {
            return this.toString(encoding, start, end);
        }
        toJSON() {
            return { type: "Buffer", data: Array.prototype.slice.call(this) };
        }
        equals(other) {
            return Buffer.compare(this, other) === 0;
        }
        compare(target, targetStart, targetEnd, sourceStart, sourceEnd) {
            var a = this.subarray(sourceStart || 0, sourceEnd === undefined ? this.length : sourceEnd);
            var b = target.subarray(targetStart || 0, targetEnd === undefined ? target.length : targetEnd);
            return Buffer.compare(a, b);
        }
        copy(target, targetStart, sourceStart, sourceEnd) {
            targetStart = targetStart || 0;
            sourceStart = sourceStart || 0;
            sourceEnd = sourceEnd === undefined ? this.length : Math.min(sourceEnd, this.length);
            var count = Math.max(0, Math.min(sourceEnd - sourceStart, target.length - targetStart));
            if (count > 0)
                target.set(this.subarray(sourceStart, sourceStart + count), targetStart);
            return count;
        }
        slice(start, end) {
            return this.subarray(start, end);
        }
        write(text, offset, length, encoding) {
            if (typeof offset === "string") {
                encoding = offset;
                offset = 0;
                length = undefined;
            } else if (typeof length === "string") {
                encoding = length;
                length = undefined;
            }
            offset = offset === undefined ? 0 : offset >>> 0;
            var bytes = stringToBytes(text, encoding);
            var count = Math.min(bytes.length, this.length - offset, length === undefined ? Infinity : length);
            if (count <= 0)
                return 0;
            this.set(bytes.subarray(0, count), offset);
            return count;
        }
        fill(value, offset, end, encoding) {
            if (typeof offset === "string") {
                encoding = offset;
                offset = 0;
                end = this.length;
            } else if (typeof end === "string") {
                encoding = end;
                end = this.length;
            }
            offset = offset === undefined ? 0 : offset;
            end = end === undefined ? this.length : end;
            if (typeof value === "number" || typeof value === "boolean") {
                Uint8Array.prototype.fill.call(this, Number(value) & 0xFF, offset, end);
                return this;
            }
            var pattern = toBytes(value, encoding);
            if (pattern.length === 0) {
                Uint8Array.prototype.fill.call(this, 0, offset, end);
                return this;
            }
            for (var i = offset, j = 0; i < end; i++, j = (j + 1) % pattern.length)
                this[i] = pattern[j];
            return this;
        }
        indexOf(value, byteOffset, encoding) {
            if (typeof byteOffset === "string") {
                encoding = byteOffset;
                byteOffset = 0;
            }
            byteOffset = byteOffset === undefined ? 0 : (byteOffset < 0 ? Math.max(0, this.length + byteOffset) : byteOffset);
            var needle = typeof value === "number" ? [value & 0xFF] : toBytes(value, encoding);
            if (needle.length === 0)
                return byteOffset <= this.length ? byteOffset : this.length;
            outer:
            for (var i = byteOffset; i <= this.length - needle.length; i++) {
                for (var j = 0; j < needle.length; j++) {
                    if (this[i + j] !== needle[j])
                        continue outer;
                }
                return i;
            }
            return -1;
        }
        lastIndexOf(value, byteOffset, encoding) {
            var needle = typeof value === "number" ? [value & 0xFF] : toBytes(value, encoding);
            var from = byteOffset === undefined ? this.length - needle.length : Math.min(byteOffset, this.length - needle.length);
            outer:
            for (var i = from; i >= 0; i--) {
                for (var j = 0; j < needle.length; j++) {
                    if (this[i + j] !== needle[j])
                        continue outer;
                }
                return i;
            }
            return -1;
        }
        includes(value, byteOffset, encoding) {
            return this.indexOf(value, byteOffset, encoding) !== -1;
        }
        _view() {
            return new DataView(this.buffer, this.byteOffset, this.byteLength);
        }
    }

    (function addNumberMethods() {
        var kinds = [
            ["UInt8", "Uint8", 1], ["Int8", "Int8", 1],
            ["UInt16", "Uint16", 2], ["Int16", "Int16", 2],
            ["UInt32", "Uint32", 4], ["Int32", "Int32", 4],
            ["Float", "Float32", 4], ["Double", "Float64", 8],
            ["BigUInt64", "BigUint64", 8], ["BigInt64", "BigInt64", 8]
        ];
        kinds.forEach(function (kind) {
            var name = kind[0], view = kind[1], size = kind[2];
            var ends = size === 1 ? [""] : ["LE", "BE"];
            ends.forEach(function (end) {
                var little = end === "LE";
                var get = "get" + view, set = "set" + view;
                if (typeof DataView.prototype[get] !== "function")
                    return;
                var read = function (offset) {
                    offset = offset === undefined ? 0 : offset;
                    if (offset < 0 || offset + size > this.length)
                        throw codeError("ERR_OUT_OF_RANGE", 'The value of "offset" is out of range.', RangeError);
                    return this._view()[get](offset, little);
                };
                var write = function (value, offset) {
                    offset = offset === undefined ? 0 : offset;
                    if (offset < 0 || offset + size > this.length)
                        throw codeError("ERR_OUT_OF_RANGE", 'The value of "offset" is out of range.', RangeError);
                    this._view()[set](offset, value, little);
                    return offset + size;
                };
                Buffer.prototype["read" + name + end] = read;
                Buffer.prototype["write" + name + end] = write;
                if (name.indexOf("UInt") >= 0) {
                    Buffer.prototype["read" + name.replace("UInt", "Uint") + end] = read;
                    Buffer.prototype["write" + name.replace("UInt", "Uint") + end] = write;
                }
            });
        });
    })();
    Buffer.poolSize = 8192;

    define("buffer", function () {
        return {
            Buffer: Buffer,
            SlowBuffer: Buffer,
            kMaxLength: 0x7fffffff,
            kStringMaxLength: 0x1fffffe8,
            constants: { MAX_LENGTH: 0x7fffffff, MAX_STRING_LENGTH: 0x1fffffe8 },
            INSPECT_MAX_BYTES: 50,
            atob: function (text) { return bytesToString(base64Decode(String(text)), "latin1"); },
            btoa: function (text) { return base64Encode(stringToBytes(String(text), "latin1"), false); },
            Blob: window.Blob
        };
    });

    // -----------------------------------------------------------------------
    // path

    var cwd = info.cwd;

    function validatePath(p, name) {
        if (typeof p !== "string")
            throw codeError("ERR_INVALID_ARG_TYPE", 'The "' + (name || "path") + '" argument must be of type string. Received ' + typeof p, TypeError);
    }

    function normalizeString(path, allowAboveRoot, separator, isSep) {
        var res = "";
        var lastSegmentLength = 0;
        var lastSlash = -1;
        var dots = 0;
        var code = 0;
        for (var i = 0; i <= path.length; ++i) {
            if (i < path.length)
                code = path.charCodeAt(i);
            else if (isSep(code))
                break;
            else
                code = 47;
            if (isSep(code)) {
                if (lastSlash === i - 1 || dots === 1) {
                    // nothing
                } else if (dots === 2) {
                    if (res.length < 2 || lastSegmentLength !== 2 || res.charCodeAt(res.length - 1) !== 46 ||
                        res.charCodeAt(res.length - 2) !== 46) {
                        if (res.length > 2) {
                            var lastSlashIndex = res.lastIndexOf(separator);
                            if (lastSlashIndex === -1) {
                                res = "";
                                lastSegmentLength = 0;
                            } else {
                                res = res.slice(0, lastSlashIndex);
                                lastSegmentLength = res.length - 1 - res.lastIndexOf(separator);
                            }
                            lastSlash = i;
                            dots = 0;
                            continue;
                        } else if (res.length !== 0) {
                            res = "";
                            lastSegmentLength = 0;
                            lastSlash = i;
                            dots = 0;
                            continue;
                        }
                    }
                    if (allowAboveRoot) {
                        res += res.length > 0 ? separator + ".." : "..";
                        lastSegmentLength = 2;
                    }
                } else {
                    if (res.length > 0)
                        res += separator + path.slice(lastSlash + 1, i);
                    else
                        res = path.slice(lastSlash + 1, i);
                    lastSegmentLength = i - lastSlash - 1;
                }
                lastSlash = i;
                dots = 0;
            } else if (code === 46 && dots !== -1) {
                ++dots;
            } else {
                dots = -1;
            }
        }
        return res;
    }

    function isPosixSep(code) {
        return code === 47;
    }
    function isWinSep(code) {
        return code === 47 || code === 92;
    }
    function isLetter(code) {
        return (code >= 65 && code <= 90) || (code >= 97 && code <= 122);
    }

    // The name after the last separator, without a trailing one, and
    // without the given ending.
    function baseName(p, ext, isSep, start) {
        var end = p.length;
        while (end > start && isSep(p.charCodeAt(end - 1)))
            end--;
        var begin = end;
        while (begin > start && !isSep(p.charCodeAt(begin - 1)))
            begin--;
        var base = p.slice(begin, end);
        if (ext !== undefined && ext.length > 0 && base !== ext && base.length >= ext.length &&
            base.slice(base.length - ext.length) === ext)
            base = base.slice(0, base.length - ext.length);
        return base;
    }

    function extName(base) {
        if (base === "..")
            return "";
        var dot = base.lastIndexOf(".");
        return dot <= 0 ? "" : base.slice(dot);
    }

    function parsePath(p, rootEnd, isSep) {
        var result = { root: p.slice(0, rootEnd), dir: "", base: "", ext: "", name: "" };
        if (p.length === 0)
            return result;
        var end = p.length;
        while (end > rootEnd && isSep(p.charCodeAt(end - 1)))
            end--;
        var trimmed = p.slice(0, end);
        result.base = baseName(trimmed, undefined, isSep, rootEnd);
        result.ext = extName(result.base);
        result.name = result.ext ? result.base.slice(0, result.base.length - result.ext.length) : result.base;
        var lastSep = -1;
        for (var i = end - 1; i >= rootEnd; i--) {
            if (isSep(trimmed.charCodeAt(i))) {
                lastSep = i;
                break;
            }
        }
        result.dir = lastSep === -1 ? trimmed.slice(0, rootEnd) : trimmed.slice(0, lastSep);
        if (result.dir === "" && rootEnd > 0)
            result.dir = result.root;
        return result;
    }

    function formatPath(sep, object) {
        var dir = object.dir || object.root;
        var base = object.base || (object.name || "") + (object.ext || "");
        if (!dir)
            return base;
        return dir === object.root ? dir + base : dir + sep + base;
    }

    var posix = {
        sep: "/",
        delimiter: ":",
        resolve: function () {
            var resolved = "";
            var absolute = false;
            for (var i = arguments.length - 1; i >= -1 && !absolute; i--) {
                var p = i >= 0 ? arguments[i] : (isWindows ? "/" : cwd);
                validatePath(p, "paths[" + i + "]");
                if (p.length === 0)
                    continue;
                resolved = p + "/" + resolved;
                absolute = p.charCodeAt(0) === 47;
            }
            resolved = normalizeString(resolved, !absolute, "/", isPosixSep);
            if (absolute)
                return "/" + resolved;
            return resolved.length > 0 ? resolved : ".";
        },
        normalize: function (p) {
            validatePath(p);
            if (p.length === 0)
                return ".";
            var absolute = p.charCodeAt(0) === 47;
            var trailing = p.charCodeAt(p.length - 1) === 47;
            p = normalizeString(p, !absolute, "/", isPosixSep);
            if (p.length === 0) {
                if (absolute)
                    return "/";
                return trailing ? "./" : ".";
            }
            if (trailing)
                p += "/";
            return absolute ? "/" + p : p;
        },
        isAbsolute: function (p) {
            validatePath(p);
            return p.length > 0 && p.charCodeAt(0) === 47;
        },
        join: function () {
            var joined;
            for (var i = 0; i < arguments.length; i++) {
                validatePath(arguments[i]);
                if (arguments[i].length > 0)
                    joined = joined === undefined ? arguments[i] : joined + "/" + arguments[i];
            }
            return joined === undefined ? "." : posix.normalize(joined);
        },
        relative: function (from, to) {
            validatePath(from, "from");
            validatePath(to, "to");
            if (from === to)
                return "";
            from = posix.resolve(from);
            to = posix.resolve(to);
            if (from === to)
                return "";
            var fromStart = 1, fromEnd = from.length, fromLen = fromEnd - fromStart;
            var toStart = 1, toLen = to.length - toStart;
            var length = fromLen < toLen ? fromLen : toLen;
            var lastCommonSep = -1;
            var i = 0;
            for (; i < length; i++) {
                var fromCode = from.charCodeAt(fromStart + i);
                if (fromCode !== to.charCodeAt(toStart + i))
                    break;
                else if (fromCode === 47)
                    lastCommonSep = i;
            }
            if (i === length) {
                if (toLen > length) {
                    if (to.charCodeAt(toStart + i) === 47)
                        return to.slice(toStart + i + 1);
                    if (i === 0)
                        return to.slice(toStart + i);
                } else if (fromLen > length) {
                    if (from.charCodeAt(fromStart + i) === 47)
                        lastCommonSep = i;
                    else if (i === 0)
                        lastCommonSep = 0;
                }
            }
            var out = "";
            for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
                if (i === fromEnd || from.charCodeAt(i) === 47)
                    out += out.length === 0 ? ".." : "/..";
            }
            return out + to.slice(toStart + lastCommonSep);
        },
        dirname: function (p) {
            validatePath(p);
            if (p.length === 0)
                return ".";
            var hasRoot = p.charCodeAt(0) === 47;
            var end = -1;
            var matchedSlash = true;
            for (var i = p.length - 1; i >= 1; --i) {
                if (p.charCodeAt(i) === 47) {
                    if (!matchedSlash) {
                        end = i;
                        break;
                    }
                } else {
                    matchedSlash = false;
                }
            }
            if (end === -1)
                return hasRoot ? "/" : ".";
            if (hasRoot && end === 1)
                return "//";
            return p.slice(0, end);
        },
        basename: function (p, ext) {
            validatePath(p);
            return baseName(p, ext, isPosixSep, 0);
        },
        extname: function (p) {
            validatePath(p);
            return extName(baseName(p, undefined, isPosixSep, 0));
        },
        parse: function (p) {
            validatePath(p);
            return parsePath(p, p.charCodeAt(0) === 47 ? 1 : 0, isPosixSep);
        },
        format: function (object) {
            return formatPath("/", object);
        },
        toNamespacedPath: function (p) {
            return p;
        }
    };

    // Where a Windows path's root ends: its drive or \\server\share and the
    // separator after it.
    function winRoot(p) {
        var length = p.length;
        var result = { device: "", rootEnd: 0, isAbsolute: false };
        if (length === 0)
            return result;
        var code = p.charCodeAt(0);
        if (length === 1) {
            if (isWinSep(code)) {
                result.rootEnd = 1;
                result.isAbsolute = true;
            }
            return result;
        }
        if (isWinSep(code)) {
            result.isAbsolute = true;
            result.rootEnd = 1;
            if (isWinSep(p.charCodeAt(1))) {
                var j = 2, last = j;
                while (j < length && !isWinSep(p.charCodeAt(j)))
                    j++;
                if (j < length && j !== last) {
                    var server = p.slice(last, j);
                    last = j;
                    while (j < length && isWinSep(p.charCodeAt(j)))
                        j++;
                    if (j < length && j !== last) {
                        last = j;
                        while (j < length && !isWinSep(p.charCodeAt(j)))
                            j++;
                        result.device = "\\\\" + server + "\\" + p.slice(last, j);
                        // The separator after the share is part of the root.
                        result.rootEnd = j < length ? j + 1 : j;
                    }
                }
            }
        } else if (isLetter(code) && p.charCodeAt(1) === 58) {
            result.device = p.slice(0, 2);
            result.rootEnd = 2;
            if (length > 2 && isWinSep(p.charCodeAt(2))) {
                result.isAbsolute = true;
                result.rootEnd = 3;
            }
        }
        return result;
    }

    var win32 = {
        sep: "\\",
        delimiter: ";",
        resolve: function () {
            var resolvedDevice = "";
            var resolvedTail = "";
            var resolvedAbsolute = false;
            for (var i = arguments.length - 1; i >= -1; i--) {
                var p;
                if (i >= 0) {
                    p = arguments[i];
                    validatePath(p, "paths[" + i + "]");
                } else if (resolvedDevice.length === 0) {
                    p = isWindows ? cwd : "C:\\";
                } else {
                    p = isWindows ? cwd : "";
                    if (p.slice(0, 2).toLowerCase() !== resolvedDevice.toLowerCase())
                        p = resolvedDevice + "\\";
                }
                if (p.length === 0)
                    continue;
                var root = winRoot(p);
                if (root.device.length > 0) {
                    if (resolvedDevice.length > 0) {
                        if (root.device.toLowerCase() !== resolvedDevice.toLowerCase())
                            continue;
                    } else {
                        resolvedDevice = root.device;
                    }
                }
                if (resolvedAbsolute) {
                    if (resolvedDevice.length > 0)
                        break;
                } else {
                    resolvedTail = p.slice(root.rootEnd) + "\\" + resolvedTail;
                    resolvedAbsolute = root.isAbsolute;
                    if (root.isAbsolute && resolvedDevice.length > 0)
                        break;
                }
            }
            resolvedTail = normalizeString(resolvedTail, !resolvedAbsolute, "\\", isWinSep);
            return resolvedAbsolute ? resolvedDevice + "\\" + resolvedTail : (resolvedDevice + resolvedTail) || ".";
        },
        normalize: function (p) {
            validatePath(p);
            var length = p.length;
            if (length === 0)
                return ".";
            if (length === 1)
                return isWinSep(p.charCodeAt(0)) ? "\\" : p;
            var root = winRoot(p);
            var tail = root.rootEnd < length ? normalizeString(p.slice(root.rootEnd), !root.isAbsolute, "\\", isWinSep) : "";
            if (tail.length === 0 && !root.isAbsolute)
                tail = ".";
            if (tail.length > 0 && isWinSep(p.charCodeAt(length - 1)))
                tail += "\\";
            if (root.device.length === 0)
                return root.isAbsolute ? "\\" + tail : tail;
            return root.isAbsolute ? root.device + "\\" + tail : root.device + tail;
        },
        isAbsolute: function (p) {
            validatePath(p);
            var length = p.length;
            if (length === 0)
                return false;
            var code = p.charCodeAt(0);
            return isWinSep(code) || (length > 2 && isLetter(code) && p.charCodeAt(1) === 58 && isWinSep(p.charCodeAt(2)));
        },
        join: function () {
            var parts = [];
            for (var i = 0; i < arguments.length; i++) {
                validatePath(arguments[i]);
                if (arguments[i].length > 0)
                    parts.push(arguments[i]);
            }
            if (parts.length === 0)
                return ".";
            var joined = parts.join("\\");
            // A join does not make a \\server\share root of its own.
            if (!(isWinSep(parts[0].charCodeAt(0)) && isWinSep(parts[0].charCodeAt(1)))) {
                var slashes = 0;
                while (slashes < joined.length && isWinSep(joined.charCodeAt(slashes)))
                    slashes++;
                if (slashes >= 2)
                    joined = "\\" + joined.slice(slashes);
            }
            var normalized = win32.normalize(joined);
            // A relative path with a colon in it would read as a drive:
            // ".\" in front keeps it relative.
            if (!win32.isAbsolute(parts[0]) && winRoot(parts[0]).device.length === 0 &&
                /^[A-Za-z]:|\\[A-Za-z]:(\\|$)/.test(normalized) && normalized.slice(0, 2) !== ".\\")
                normalized = ".\\" + normalized;
            return normalized;
        },
        relative: function (from, to) {
            validatePath(from, "from");
            validatePath(to, "to");
            if (from === to)
                return "";
            var fromOrig = win32.resolve(from);
            var toOrig = win32.resolve(to);
            if (fromOrig === toOrig)
                return "";
            from = fromOrig.toLowerCase();
            to = toOrig.toLowerCase();
            if (from === to)
                return "";
            var fromStart = 0;
            while (fromStart < from.length && from.charCodeAt(fromStart) === 92)
                fromStart++;
            var fromEnd = from.length;
            while (fromEnd - 1 > fromStart && from.charCodeAt(fromEnd - 1) === 92)
                fromEnd--;
            var fromLen = fromEnd - fromStart;
            var toStart = 0;
            while (toStart < to.length && to.charCodeAt(toStart) === 92)
                toStart++;
            var toEnd = to.length;
            while (toEnd - 1 > toStart && to.charCodeAt(toEnd - 1) === 92)
                toEnd--;
            var toLen = toEnd - toStart;
            var length = fromLen < toLen ? fromLen : toLen;
            var lastCommonSep = -1;
            var i = 0;
            for (; i < length; i++) {
                var fromCode = from.charCodeAt(fromStart + i);
                if (fromCode !== to.charCodeAt(toStart + i))
                    break;
                else if (fromCode === 92)
                    lastCommonSep = i;
            }
            if (i !== length) {
                if (lastCommonSep === -1)
                    return toOrig;
            } else {
                if (toLen > length) {
                    if (to.charCodeAt(toStart + i) === 92)
                        return toOrig.slice(toStart + i + 1);
                    if (i === 2)
                        return toOrig.slice(toStart + i);
                }
                if (fromLen > length) {
                    if (from.charCodeAt(fromStart + i) === 92)
                        lastCommonSep = i;
                    else if (i === 2)
                        lastCommonSep = 3;
                }
                if (lastCommonSep === -1)
                    lastCommonSep = 0;
            }
            var out = "";
            for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
                if (i === fromEnd || from.charCodeAt(i) === 92)
                    out += out.length === 0 ? ".." : "\\..";
            }
            toStart += lastCommonSep;
            if (out.length > 0)
                return out + toOrig.slice(toStart, toEnd);
            if (toOrig.charCodeAt(toStart) === 92)
                ++toStart;
            return toOrig.slice(toStart, toEnd);
        },
        dirname: function (p) {
            validatePath(p);
            var length = p.length;
            if (length === 0)
                return ".";
            var root = winRoot(p);
            if (root.rootEnd >= length)
                return p;
            var end = -1;
            var matchedSlash = true;
            for (var i = length - 1; i >= root.rootEnd; --i) {
                if (isWinSep(p.charCodeAt(i))) {
                    if (!matchedSlash) {
                        end = i;
                        break;
                    }
                } else {
                    matchedSlash = false;
                }
            }
            if (end === -1) {
                if (root.rootEnd === 0)
                    return ".";
                end = root.rootEnd;
            }
            return p.slice(0, end);
        },
        basename: function (p, ext) {
            validatePath(p);
            var start = p.length >= 2 && isLetter(p.charCodeAt(0)) && p.charCodeAt(1) === 58 ? 2 : 0;
            return baseName(p, ext, isWinSep, start);
        },
        extname: function (p) {
            return extName(win32.basename(p));
        },
        parse: function (p) {
            validatePath(p);
            return parsePath(p, winRoot(p).rootEnd, isWinSep);
        },
        format: function (object) {
            return formatPath("\\", object);
        },
        toNamespacedPath: function (p) {
            if (typeof p !== "string" || p.length === 0)
                return p;
            var resolved = win32.resolve(p);
            if (resolved.length <= 2)
                return p;
            if (resolved.charCodeAt(0) === 92 && resolved.charCodeAt(1) === 92) {
                if (resolved.charCodeAt(2) !== 63 && resolved.charCodeAt(2) !== 46)
                    return "\\\\?\\UNC\\" + resolved.slice(2);
            } else if (isLetter(resolved.charCodeAt(0)) && resolved.charCodeAt(1) === 58 && resolved.charCodeAt(2) === 92) {
                return "\\\\?\\" + resolved;
            }
            return p;
        }
    };
    posix.posix = win32.posix = posix;
    posix.win32 = win32.win32 = win32;

    var path = isWindows ? win32 : posix;
    define("path", function () { return path; });
    define("path/posix", function () { return posix; });
    define("path/win32", function () { return win32; });

    // -----------------------------------------------------------------------
    // process

    var Emitter = builtin("events");
    var process = new Emitter();
    var started = Date.now();

    function makeEnv(source) {
        var env = {};
        for (var key in source)
            env[key] = source[key];
        if (!isWindows || typeof Proxy !== "function")
            return env;
        // Windows' environment names match in any case.
        function find(target, name) {
            if (typeof name !== "string")
                return name;
            if (Object.prototype.hasOwnProperty.call(target, name))
                return name;
            var lower = name.toLowerCase();
            for (var k in target) {
                if (k.toLowerCase() === lower)
                    return k;
            }
            return name;
        }
        return new Proxy(env, {
            get: function (target, name) { return target[find(target, name)]; },
            set: function (target, name, value) { target[find(target, name)] = String(value); return true; },
            has: function (target, name) { return find(target, name) in target; },
            deleteProperty: function (target, name) { delete target[find(target, name)]; return true; }
        });
    }

    process.title = "Phoenix";
    process.version = "v" + NODE_VERSION;
    process.versions = { node: NODE_VERSION, phoenix: "1", modules: "88", uv: "1.40.0", zlib: "1.2.11", v8: "8.6" };
    process.release = { name: "node" };
    process.platform = info.platform;
    process.arch = info.arch;
    process.pid = info.pid;
    process.ppid = 0;
    process.execPath = info.execPath;
    process.argv0 = "node";
    process.argv = [info.execPath, info.page];
    process.execArgv = [];
    process.env = makeEnv(info.env || {});
    process.exitCode = undefined;
    process.config = { variables: {} };
    process.features = {};
    process.browser = false;
    process.cwd = function () {
        return cwd;
    };
    process.chdir = function (directory) {
        var target = path.resolve(directory);
        var stat = native("node.fs.stat", [target, false]);
        if (stat.type !== "directory") {
            var error = new Error("ENOTDIR: not a directory, chdir '" + directory + "'");
            error.code = "ENOTDIR";
            throw error;
        }
        cwd = target;
    };
    process.umask = function () {
        return 18;
    };
    process.nextTick = function (callback) {
        var args = Array.prototype.slice.call(arguments, 1);
        microtask(function () { callback.apply(null, args); });
    };
    process.hrtime = function (previous) {
        var now = typeof performance !== "undefined" ? performance.now() : Date.now() - started;
        var seconds = Math.floor(now / 1000);
        var nanos = Math.floor((now % 1000) * 1e6);
        if (previous) {
            seconds -= previous[0];
            nanos -= previous[1];
            if (nanos < 0) {
                seconds--;
                nanos += 1e9;
            }
        }
        return [seconds, nanos];
    };
    if (typeof BigInt === "function") {
        process.hrtime.bigint = function () {
            var t = process.hrtime();
            return BigInt(t[0]) * BigInt(1e9) + BigInt(t[1]);
        };
    }
    process.uptime = function () {
        return (Date.now() - started) / 1000;
    };
    process.memoryUsage = function () {
        var heap = typeof performance !== "undefined" && performance.memory ? performance.memory : null;
        return {
            rss: heap ? heap.totalJSHeapSize : 0,
            heapTotal: heap ? heap.totalJSHeapSize : 0,
            heapUsed: heap ? heap.usedJSHeapSize : 0,
            external: 0,
            arrayBuffers: 0
        };
    };
    process.cpuUsage = function () {
        return { user: 0, system: 0 };
    };
    process.emitWarning = function (warning) {
        if (window.console)
            console.warn(typeof warning === "string" ? "Warning: " + warning : warning);
    };
    process.exit = function (code) {
        process.exitCode = code === undefined ? process.exitCode : code;
        process.emit("exit", process.exitCode || 0);
    };
    process.abort = function () {
        throw notAvailable("process.abort");
    };
    process.kill = function () {
        throw notAvailable("process.kill");
    };
    process.binding = function () {
        throw notAvailable("process.binding");
    };
    if (!isWindows) {
        process.getuid = function () { return -1; };
        process.getgid = function () { return -1; };
    }

    // -----------------------------------------------------------------------
    // stream

    define("stream", function () {
        function Stream(options) {
            Emitter.call(this, options);
        }
        inherits(Stream, Emitter);
        Stream.prototype.pipe = function (destination, options) {
            var source = this;
            source.on("data", function (chunk) {
                if (destination.write(chunk) === false && source.pause) {
                    source.pause();
                    destination.once("drain", function () { source.resume(); });
                }
            });
            if (!options || options.end !== false) {
                source.on("end", function () {
                    if (destination !== process.stdout && destination !== process.stderr && destination.end)
                        destination.end();
                });
            }
            if (destination.emit)
                destination.emit("pipe", source);
            return destination;
        };

        function Readable(options) {
            Stream.call(this, options);
            options = options || {};
            this.readable = true;
            this.readableEnded = false;
            this.destroyed = false;
            this._objectMode = !!(options.objectMode || options.readableObjectMode);
            this._chunks = [];
            this._flowing = null;
            this._ended = false;
            this._endEmitted = false;
            this._scheduled = false;
            this._reading = false;
            this._decoder = null;
            this._autoClose = options.autoDestroy !== false && options.emitClose !== false;
            if (typeof options.read === "function")
                this._read = options.read;
            if (typeof options.destroy === "function")
                this._destroy = options.destroy;
            if (options.encoding)
                this.setEncoding(options.encoding);
            var self = this;
            this.on("newListener", function (type) {
                if (type === "data" && self._flowing !== false)
                    self.resume();
                else if (type === "readable")
                    self._schedule();
            });
        }
        inherits(Readable, Stream);
        Readable.prototype._read = function () {};
        Readable.prototype.push = function (chunk, encoding) {
            if (chunk === null) {
                this._ended = true;
                this._schedule();
                return false;
            }
            if (!this._objectMode && typeof chunk === "string")
                chunk = Buffer.from(chunk, encoding);
            else if (!this._objectMode && chunk instanceof Uint8Array && !(chunk instanceof Buffer))
                chunk = Buffer.from(chunk);
            this._chunks.push(chunk);
            this._schedule();
            return true;
        };
        Readable.prototype.unshift = function (chunk) {
            this._chunks.unshift(chunk);
            this._schedule();
        };
        Readable.prototype._schedule = function () {
            if (this._scheduled)
                return;
            this._scheduled = true;
            var self = this;
            later(function () {
                self._scheduled = false;
                self._pump();
            });
        };
        Readable.prototype._decode = function (chunk) {
            return this._decoder && !this._objectMode ? this._decoder.write(chunk) : chunk;
        };
        Readable.prototype._pump = function () {
            if (this.destroyed)
                return;
            while (this._flowing && this._chunks.length > 0)
                this.emit("data", this._decode(this._chunks.shift()));
            if (!this._flowing && this._chunks.length > 0 && this.listenerCount("readable") > 0)
                this.emit("readable");
            if (this._chunks.length === 0 && this._ended && !this._endEmitted &&
                (this._flowing || this.listenerCount("readable") > 0 || this.listenerCount("end") > 0 && this._flowing !== false)) {
                this._endEmitted = true;
                this.readableEnded = true;
                if (this._decoder) {
                    var rest = this._decoder.end();
                    if (rest)
                        this.emit("data", rest);
                }
                if (this.listenerCount("readable") > 0)
                    this.emit("readable");
                this.emit("end");
                if (this._autoClose)
                    this.emit("close");
                return;
            }
            if (this._flowing && !this._ended && this._chunks.length === 0 && !this._reading) {
                this._reading = true;
                try {
                    this._read(16384);
                } catch (e) {
                    this.destroy(e);
                }
                this._reading = false;
            }
        };
        Readable.prototype.read = function () {
            if (this._chunks.length === 0) {
                if (this._ended)
                    this._schedule();
                return null;
            }
            var chunks = this._chunks;
            this._chunks = [];
            if (this._objectMode)
                return chunks[0] !== undefined ? (this._chunks = chunks.slice(1), chunks[0]) : null;
            var all = Buffer.concat(chunks);
            if (this._ended)
                this._schedule();
            return this._decoder ? this._decoder.write(all) : all;
        };
        Readable.prototype.setEncoding = function (encoding) {
            var StringDecoder = builtin("string_decoder").StringDecoder;
            this._decoder = new StringDecoder(encoding);
            return this;
        };
        Readable.prototype.pause = function () {
            this._flowing = false;
            return this;
        };
        Readable.prototype.resume = function () {
            this._flowing = true;
            this._schedule();
            return this;
        };
        Readable.prototype.isPaused = function () {
            return this._flowing === false;
        };
        Readable.prototype.unpipe = function () {
            return this;
        };
        Readable.prototype.destroy = function (error) {
            if (this.destroyed)
                return this;
            this.destroyed = true;
            var self = this;
            var finish = function (e) {
                later(function () {
                    if (e)
                        self.emit("error", e);
                    self.emit("close");
                });
            };
            if (this._destroy)
                this._destroy(error || null, finish);
            else
                finish(error);
            return this;
        };
        if (typeof Symbol === "function" && Symbol.asyncIterator) {
            Readable.prototype[Symbol.asyncIterator] = function () {
                var stream = this;
                var queue = [], waiting = [], done = false, failure = null;
                function settle() {
                    while (waiting.length > 0 && (queue.length > 0 || done || failure)) {
                        var w = waiting.shift();
                        if (failure)
                            w.reject(failure);
                        else if (queue.length > 0)
                            w.resolve({ value: queue.shift(), done: false });
                        else
                            w.resolve({ value: undefined, done: true });
                    }
                }
                stream.on("data", function (chunk) { queue.push(chunk); settle(); });
                stream.on("end", function () { done = true; settle(); });
                stream.on("error", function (e) { failure = e; settle(); });
                return {
                    next: function () {
                        return new Promise(function (resolve, reject) {
                            waiting.push({ resolve: resolve, reject: reject });
                            settle();
                        });
                    },
                    return: function () {
                        done = true;
                        stream.destroy();
                        return Promise.resolve({ value: undefined, done: true });
                    }
                };
            };
        }
        Readable.from = function (iterable, options) {
            var stream = new Readable(Object.assign({ objectMode: true }, options || {}));
            var items = typeof iterable === "string" || iterable instanceof Uint8Array ? [iterable] : Array.from(iterable);
            items.forEach(function (item) { stream.push(item); });
            stream.push(null);
            return stream;
        };

        function Writable(options) {
            Stream.call(this, options);
            options = options || {};
            this.writable = true;
            this.writableEnded = false;
            this.writableFinished = false;
            this.destroyed = false;
            this._objectMode = !!(options.objectMode || options.writableObjectMode);
            this._decodeStrings = options.decodeStrings !== false;
            this._queue = [];
            this._writing = false;
            this._ending = false;
            this._finishing = false;
            this._autoCloseWritable = options.autoDestroy !== false && options.emitClose !== false;
            if (typeof options.write === "function")
                this._write = options.write;
            if (typeof options.final === "function")
                this._final = options.final;
            if (typeof options.destroy === "function")
                this._destroy = options.destroy;
        }
        inherits(Writable, Stream);
        Writable.prototype._write = function (chunk, encoding, callback) {
            callback();
        };
        Writable.prototype.write = function (chunk, encoding, callback) {
            if (typeof encoding === "function") {
                callback = encoding;
                encoding = null;
            }
            if (this._ending) {
                var self = this;
                var error = codeError("ERR_STREAM_WRITE_AFTER_END", "write after end");
                later(function () {
                    if (callback)
                        callback(error);
                    self.emit("error", error);
                });
                return false;
            }
            if (!this._objectMode && typeof chunk === "string" && this._decodeStrings) {
                chunk = Buffer.from(chunk, encoding || "utf8");
                encoding = "buffer";
            } else if (!this._objectMode && chunk instanceof Uint8Array && !(chunk instanceof Buffer)) {
                chunk = Buffer.from(chunk);
            }
            this._queue.push([chunk, encoding || (chunk instanceof Buffer ? "buffer" : "utf8"), callback]);
            this._next();
            return true;
        };
        Writable.prototype._next = function () {
            if (this._writing || this.destroyed)
                return;
            var item = this._queue.shift();
            if (!item) {
                if (this._ending && !this._finishing)
                    this._finish();
                return;
            }
            var self = this;
            var sync = true;
            this._writing = true;
            this._write(item[0], item[1], function (error) {
                self._writing = false;
                if (item[2])
                    item[2](error);
                if (error) {
                    self.emit("error", error);
                    return;
                }
                if (sync)
                    later(function () { self._next(); });
                else
                    self._next();
            });
            sync = false;
        };
        Writable.prototype._finish = function () {
            this._finishing = true;
            var self = this;
            function done(error) {
                if (error) {
                    self.emit("error", error);
                    return;
                }
                self.writableFinished = true;
                self.emit("finish");
                if (self._autoCloseWritable)
                    self.emit("close");
            }
            if (this._final)
                this._final(function (error) { later(function () { done(error); }); });
            else
                later(function () { done(); });
        };
        Writable.prototype.end = function (chunk, encoding, callback) {
            if (typeof chunk === "function") {
                callback = chunk;
                chunk = null;
            } else if (typeof encoding === "function") {
                callback = encoding;
                encoding = null;
            }
            if (chunk !== null && chunk !== undefined)
                this.write(chunk, encoding);
            if (callback)
                this.once("finish", callback);
            if (this._ending)
                return this;
            this._ending = true;
            this.writableEnded = true;
            this._next();
            return this;
        };
        Writable.prototype.cork = function () {};
        Writable.prototype.uncork = function () {};
        Writable.prototype.setDefaultEncoding = function () {
            return this;
        };
        Writable.prototype.destroy = Readable.prototype.destroy;

        function Duplex(options) {
            Readable.call(this, options);
            Writable.call(this, options);
            if (options && options.allowHalfOpen === false) {
                var self = this;
                this.once("end", function () { self.end(); });
            }
        }
        inherits(Duplex, Readable);
        Object.keys(Writable.prototype).forEach(function (key) {
            if (!Duplex.prototype.hasOwnProperty(key) && key !== "destroy")
                Duplex.prototype[key] = Writable.prototype[key];
        });

        function Transform(options) {
            Duplex.call(this, options);
            if (options && typeof options.transform === "function")
                this._transform = options.transform;
            if (options && typeof options.flush === "function")
                this._flush = options.flush;
        }
        inherits(Transform, Duplex);
        Transform.prototype._transform = function (chunk, encoding, callback) {
            callback(null, chunk);
        };
        Transform.prototype._write = function (chunk, encoding, callback) {
            var self = this;
            this._transform(chunk, encoding, function (error, data) {
                if (data !== undefined && data !== null)
                    self.push(data);
                callback(error);
            });
        };
        Transform.prototype._final = function (callback) {
            var self = this;
            var finish = function (error, data) {
                if (data !== undefined && data !== null)
                    self.push(data);
                self.push(null);
                callback(error);
            };
            if (this._flush)
                this._flush(finish);
            else
                finish();
        };

        function PassThrough(options) {
            Transform.call(this, options);
        }
        inherits(PassThrough, Transform);

        function finished(stream, options, callback) {
            if (typeof options === "function")
                callback = options;
            var called = false;
            function done(error) {
                if (called)
                    return;
                called = true;
                callback(error);
            }
            stream.on("end", function () { done(); });
            stream.on("finish", function () { done(); });
            stream.on("error", done);
            return function () {};
        }

        function pipeline() {
            var streams = Array.prototype.slice.call(arguments);
            var callback = typeof streams[streams.length - 1] === "function" ? streams.pop() : function () {};
            if (Array.isArray(streams[0]))
                streams = streams[0];
            var called = false;
            function done(error) {
                if (called)
                    return;
                called = true;
                callback(error);
            }
            streams.forEach(function (stream) { stream.on("error", done); });
            for (var i = 0; i < streams.length - 1; i++)
                streams[i].pipe(streams[i + 1]);
            var last = streams[streams.length - 1];
            last.on(last instanceof Writable || last.end ? "finish" : "end", function () { done(); });
            return last;
        }

        Stream.Stream = Stream;
        Stream.Readable = Readable;
        Stream.Writable = Writable;
        Stream.Duplex = Duplex;
        Stream.Transform = Transform;
        Stream.PassThrough = PassThrough;
        Stream.finished = finished;
        Stream.pipeline = pipeline;
        Stream.promises = {
            pipeline: function () {
                var streams = Array.prototype.slice.call(arguments);
                return new Promise(function (resolve, reject) {
                    streams.push(function (error) {
                        if (error)
                            reject(error);
                        else
                            resolve();
                    });
                    pipeline.apply(null, streams);
                });
            },
            finished: function (stream) {
                return new Promise(function (resolve, reject) {
                    finished(stream, function (error) {
                        if (error)
                            reject(error);
                        else
                            resolve();
                    });
                });
            }
        };
        return Stream;
    });
    define("stream/promises", function () { return builtin("stream").promises; });

    define("string_decoder", function () {
        function StringDecoder(encoding) {
            this.encoding = checkedEncoding(encoding);
            this._rest = null;
        }
        // How many bytes at the end of \a bytes start a character not yet
        // complete.
        function incomplete(bytes, encoding) {
            var n = bytes.length;
            if (encoding === "utf16le")
                return n % 2;
            if (encoding === "base64" || encoding === "base64url")
                return n % 3;
            if (encoding !== "utf8")
                return 0;
            for (var back = 1; back <= Math.min(3, n); back++) {
                var b = bytes[n - back];
                if ((b & 0xC0) === 0x80)
                    continue;
                var need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
                return need > back ? back : 0;
            }
            return 0;
        }
        StringDecoder.prototype.write = function (chunk) {
            if (typeof chunk === "string")
                return chunk;
            var bytes = toBytes(chunk);
            if (this._rest) {
                var joined = new Uint8Array(this._rest.length + bytes.length);
                joined.set(this._rest, 0);
                joined.set(bytes, this._rest.length);
                bytes = joined;
                this._rest = null;
            }
            var cut = incomplete(bytes, this.encoding);
            if (cut > 0) {
                this._rest = bytes.slice(bytes.length - cut);
                bytes = bytes.subarray(0, bytes.length - cut);
            }
            return bytesToString(bytes, this.encoding);
        };
        StringDecoder.prototype.end = function (chunk) {
            var out = chunk ? this.write(chunk) : "";
            if (this._rest) {
                out += this.encoding === "utf8" ? "\ufffd" : bytesToString(this._rest, this.encoding);
                this._rest = null;
            }
            return out;
        };
        return { StringDecoder: StringDecoder };
    });

    // -----------------------------------------------------------------------
    // os

    define("os", function () {
        var cpus = [];
        for (var i = 0; i < (info.cpus || 1); i++)
            cpus.push({ model: "", speed: 0, times: { user: 0, nice: 0, sys: 0, idle: 0, irq: 0 } });
        return {
            EOL: info.eol,
            devNull: isWindows ? "\\\\.\\nul" : "/dev/null",
            platform: function () { return info.platform; },
            type: function () { return info.type; },
            release: function () { return info.release || ""; },
            version: function () { return info.type + " " + (info.release || ""); },
            arch: function () { return info.arch; },
            machine: function () { return info.arch === "x64" ? "x86_64" : info.arch; },
            hostname: function () { return info.hostname || "localhost"; },
            homedir: function () { return info.homedir; },
            tmpdir: function () { return info.tmpdir; },
            totalmem: function () { return info.totalmem || 0; },
            freemem: function () {
                try {
                    return native("node.os.info").freemem || 0;
                } catch (e) {
                    return info.freemem || 0;
                }
            },
            uptime: function () { return info.uptime || 0; },
            cpus: function () { return cpus; },
            availableParallelism: function () { return cpus.length; },
            loadavg: function () { return [0, 0, 0]; },
            endianness: function () { return "LE"; },
            networkInterfaces: function () { return {}; },
            userInfo: function () {
                return { username: info.username || "", homedir: info.homedir, shell: isWindows ? null : "/bin/sh", uid: -1, gid: -1 };
            },
            constants: { signals: { SIGHUP: 1, SIGINT: 2, SIGKILL: 9, SIGTERM: 15 }, errno: {} }
        };
    });

    // -----------------------------------------------------------------------
    // url and querystring

    define("querystring", function () {
        function escape(text) {
            return encodeURIComponent(text);
        }
        function unescape(text) {
            try {
                return decodeURIComponent(text);
            } catch (e) {
                return text;
            }
        }
        function stringify(object, sep, eq) {
            sep = sep || "&";
            eq = eq || "=";
            if (!object || typeof object !== "object")
                return "";
            var parts = [];
            Object.keys(object).forEach(function (key) {
                var value = object[key];
                var values = Array.isArray(value) ? value : [value];
                values.forEach(function (v) {
                    if (v === undefined || v === null || typeof v === "object")
                        v = "";
                    parts.push(escape(key) + eq + escape(String(v)));
                });
            });
            return parts.join(sep);
        }
        function parse(text, sep, eq) {
            sep = sep || "&";
            eq = eq || "=";
            var out = Object.create(null);
            if (typeof text !== "string" || text.length === 0)
                return out;
            text.split(sep).forEach(function (part) {
                if (!part)
                    return;
                var at = part.indexOf(eq);
                var key = unescape((at >= 0 ? part.slice(0, at) : part).replace(/\+/g, " "));
                var value = at >= 0 ? unescape(part.slice(at + eq.length).replace(/\+/g, " ")) : "";
                if (key in out)
                    out[key] = [].concat(out[key], value);
                else
                    out[key] = value;
            });
            return out;
        }
        return { stringify: stringify, encode: stringify, parse: parse, decode: parse, escape: escape, unescape: unescape };
    });

    function fileURLToPath(url) {
        var href = typeof url === "string" ? url : url && url.href;
        if (typeof href !== "string" || !/^file:/i.test(href))
            throw codeError("ERR_INVALID_URL_SCHEME", "The URL must be of scheme file", TypeError);
        var rest = href.replace(/^file:/i, "");
        var host = "";
        if (rest.slice(0, 2) === "//") {
            rest = rest.slice(2);
            var slash = rest.indexOf("/");
            host = slash < 0 ? rest : rest.slice(0, slash);
            rest = slash < 0 ? "/" : rest.slice(slash);
        }
        rest = rest.replace(/[?#].*$/, "");
        var decoded = decodeURIComponent(rest);
        if (isWindows) {
            decoded = decoded.replace(/\//g, "\\");
            if (host && host !== "localhost")
                return "\\\\" + host + decoded;
            if (/^\\[A-Za-z][:|]/.test(decoded))
                return decoded.charAt(1) + ":" + decoded.slice(3);
            return decoded;
        }
        return decoded;
    }

    function pathToFileURL(p) {
        var resolved = path.resolve(p);
        if (isWindows)
            resolved = "/" + resolved.replace(/\\/g, "/");
        var href = "file://" + encodeURI(resolved).replace(/\?/g, "%3F").replace(/#/g, "%23");
        if (typeof URL === "function")
            return new URL(href);
        return { href: href, protocol: "file:", pathname: resolved, toString: function () { return href; } };
    }

    define("url", function () {
        var querystring = builtin("querystring");
        function Url() {
            this.protocol = null;
            this.slashes = null;
            this.auth = null;
            this.host = null;
            this.port = null;
            this.hostname = null;
            this.hash = null;
            this.search = null;
            this.query = null;
            this.pathname = null;
            this.path = null;
            this.href = null;
        }
        function parse(text, parseQuery) {
            var url = new Url();
            var rest = String(text).trim();
            url.href = rest;
            var hashAt = rest.indexOf("#");
            if (hashAt >= 0) {
                url.hash = rest.slice(hashAt);
                rest = rest.slice(0, hashAt);
            }
            var searchAt = rest.indexOf("?");
            if (searchAt >= 0) {
                url.search = rest.slice(searchAt);
                url.query = rest.slice(searchAt + 1);
                rest = rest.slice(0, searchAt);
            } else if (parseQuery) {
                url.search = "";
                url.query = {};
            }
            var protocol = /^([a-z][a-z0-9.+-]*:)/i.exec(rest);
            if (protocol) {
                url.protocol = protocol[1].toLowerCase();
                rest = rest.slice(protocol[1].length);
            }
            if (rest.slice(0, 2) === "//") {
                url.slashes = true;
                rest = rest.slice(2);
                var end = rest.search(/[\/\\]/);
                var authority = end < 0 ? rest : rest.slice(0, end);
                rest = end < 0 ? "" : rest.slice(end);
                var at = authority.lastIndexOf("@");
                if (at >= 0) {
                    url.auth = decodeURIComponent(authority.slice(0, at));
                    authority = authority.slice(at + 1);
                }
                url.host = authority.toLowerCase();
                var port = /:(\d*)$/.exec(authority);
                if (port) {
                    if (port[1])
                        url.port = port[1];
                    authority = authority.slice(0, authority.length - port[0].length);
                }
                url.hostname = authority.toLowerCase();
                if (!rest && url.slashes)
                    rest = "/";
            }
            url.pathname = rest || null;
            if (parseQuery && typeof url.query === "string")
                url.query = querystring.parse(url.query);
            url.path = (url.pathname || "") + (url.search || "") || null;
            return url;
        }
        function format(object) {
            if (typeof object === "string")
                object = parse(object);
            if (typeof URL === "function" && object instanceof URL)
                return object.href;
            var protocol = object.protocol || "";
            if (protocol && protocol.slice(-1) !== ":")
                protocol += ":";
            var host = object.host || ((object.hostname || "") + (object.port ? ":" + object.port : ""));
            var auth = object.auth ? encodeURIComponent(object.auth).replace(/%3A/i, ":") + "@" : "";
            var search = object.search || (object.query && typeof object.query === "object" && Object.keys(object.query).length
                ? "?" + querystring.stringify(object.query) : "");
            if (search && search.charAt(0) !== "?")
                search = "?" + search;
            var hash = object.hash || "";
            if (hash && hash.charAt(0) !== "#")
                hash = "#" + hash;
            var slashes = object.slashes || /^(https?|ftp|file|wss?):$/.test(protocol) ? "//" : "";
            return protocol + (host ? slashes + auth + host : "") + (object.pathname || "") + search + hash;
        }
        function resolve(from, to) {
            if (typeof URL === "function")
                return new URL(to, new URL(from, "resolve://")).href.replace(/^resolve:\/\//, "");
            return to;
        }
        Url.prototype.format = function () { return format(this); };
        return {
            URL: window.URL,
            URLSearchParams: window.URLSearchParams,
            Url: Url,
            parse: parse,
            format: format,
            resolve: resolve,
            fileURLToPath: fileURLToPath,
            pathToFileURL: pathToFileURL,
            domainToASCII: function (domain) { return String(domain).toLowerCase(); },
            domainToUnicode: function (domain) { return String(domain); }
        };
    });

    // -----------------------------------------------------------------------
    // fs

    // Where a path argument points, made absolute.
    function pathArg(p, name) {
        if (p instanceof Uint8Array)
            p = bytesToString(p, "utf8");
        else if (p && typeof p === "object" && typeof p.href === "string" && p.protocol === "file:")
            p = fileURLToPath(p);
        if (typeof p !== "string")
            throw codeError("ERR_INVALID_ARG_TYPE", 'The "' + (name || "path") + '" argument must be of type string or an instance of Buffer or URL. Received ' + typeof p, TypeError);
        if (p.indexOf("\u0000") >= 0)
            throw codeError("ERR_INVALID_ARG_VALUE", 'The argument "' + (name || "path") + '" must be a string without null bytes.', TypeError);
        return path.resolve(p);
    }

    function encodingOf(options, fallback) {
        if (typeof options === "string")
            return options;
        if (options && options.encoding !== undefined)
            return options.encoding;
        return fallback;
    }

    define("fs", function () {
        var constants = {
            F_OK: 0, R_OK: 4, W_OK: 2, X_OK: 1,
            COPYFILE_EXCL: 1, COPYFILE_FICLONE: 2, COPYFILE_FICLONE_FORCE: 4,
            O_RDONLY: 0, O_WRONLY: 1, O_RDWR: 2, O_CREAT: 256, O_EXCL: 1024, O_TRUNC: 512, O_APPEND: 8,
            S_IFMT: 61440, S_IFREG: 32768, S_IFDIR: 16384, S_IFLNK: 40960
        };

        function Stats(data) {
            this.dev = data.dev || 0;
            this.mode = data.mode || 0;
            this.nlink = data.nlink || 1;
            this.uid = data.uid || 0;
            this.gid = data.gid || 0;
            this.rdev = 0;
            this.blksize = 4096;
            this.ino = data.ino || 0;
            this.size = data.size || 0;
            this.blocks = Math.ceil(this.size / 512);
            this.atimeMs = data.atimeMs || 0;
            this.mtimeMs = data.mtimeMs || 0;
            this.ctimeMs = data.ctimeMs || 0;
            this.birthtimeMs = data.birthtimeMs || 0;
            this.atime = new Date(this.atimeMs);
            this.mtime = new Date(this.mtimeMs);
            this.ctime = new Date(this.ctimeMs);
            this.birthtime = new Date(this.birthtimeMs);
            Object.defineProperty(this, "_type", { value: data.type, enumerable: false });
        }
        Stats.prototype.isFile = function () { return this._type === "file"; };
        Stats.prototype.isDirectory = function () { return this._type === "directory"; };
        Stats.prototype.isSymbolicLink = function () { return this._type === "symlink"; };
        Stats.prototype.isFIFO = function () { return false; };
        Stats.prototype.isSocket = function () { return false; };
        Stats.prototype.isBlockDevice = function () { return false; };
        Stats.prototype.isCharacterDevice = function () { return false; };

        function Dirent(name, type, parent) {
            this.name = name;
            this.path = parent;
            this.parentPath = parent;
            Object.defineProperty(this, "_type", { value: type, enumerable: false });
        }
        Dirent.prototype = Object.create(Stats.prototype);
        Dirent.prototype.constructor = Dirent;

        function existsSync(p) {
            try {
                return native("node.fs.exists", [pathArg(p)]);
            } catch (e) {
                return false;
            }
        }

        function readFileSync(p, options) {
            var encoding = encodingOf(options, null);
            if (typeof p === "number")
                p = fdEntry(p).path;
            var file = pathArg(p);
            var e = encoding ? checkedEncoding(encoding) : null;
            if (e === "utf8")
                return stripNothing(native("node.fs.read", [file, "utf8"]));
            var bytes = Buffer.from(native("node.fs.read", [file, "base64"]), "base64");
            return e ? bytes.toString(e) : bytes;
        }
        function stripNothing(text) {
            return text;
        }

        function writeData(file, data, options, flag) {
            var encoding = encodingOf(options, "utf8");
            if (options && typeof options === "object" && options.flag)
                flag = options.flag;
            if (typeof data === "string" && checkedEncoding(encoding) === "utf8")
                return native("node.fs.write", [file, data, "utf8", flag]);
            if (typeof data !== "string" && !ArrayBuffer.isView(data))
                data = String(data);
            var bytes = typeof data === "string" ? stringToBytes(data, encoding) : toBytes(data);
            return native("node.fs.write", [file, base64Encode(bytes, false), "base64", flag]);
        }

        function writeFileSync(p, data, options) {
            if (typeof p === "number") {
                writeSync(p, data);
                return;
            }
            writeData(pathArg(p), data, options, "w");
        }

        function appendFileSync(p, data, options) {
            if (typeof p === "number") {
                writeSync(p, data);
                return;
            }
            writeData(pathArg(p), data, options, "a");
        }

        function statSync(p, options) {
            if (typeof p === "number")
                p = fdEntry(p).path;
            try {
                return new Stats(native("node.fs.stat", [pathArg(p), false]));
            } catch (e) {
                if (options && options.throwIfNoEntry === false && e.code === "ENOENT")
                    return undefined;
                throw e;
            }
        }

        function lstatSync(p, options) {
            try {
                return new Stats(native("node.fs.stat", [pathArg(p), true]));
            } catch (e) {
                if (options && options.throwIfNoEntry === false && e.code === "ENOENT")
                    return undefined;
                throw e;
            }
        }

        function readdirSync(p, options) {
            var folder = pathArg(p);
            var entries = native("node.fs.readdir", [folder]);
            var withTypes = options && typeof options === "object" && options.withFileTypes;
            var encoding = encodingOf(options, "utf8");
            var out = [];
            var recursive = options && typeof options === "object" && options.recursive;
            (function walk(dir, prefix, list) {
                list.sort(function (a, b) { return a.name < b.name ? -1 : a.name > b.name ? 1 : 0; });
                list.forEach(function (entry) {
                    var name = prefix ? prefix + path.sep + entry.name : entry.name;
                    if (withTypes)
                        out.push(new Dirent(entry.name, entry.type, dir));
                    else
                        out.push(encoding === "buffer" ? Buffer.from(name) : name);
                    if (recursive && entry.type === "directory") {
                        var inner = path.join(dir, entry.name);
                        walk(inner, name, native("node.fs.readdir", [inner]));
                    }
                });
            })(folder, "", entries);
            return out;
        }

        function mkdirSync(p, options) {
            var recursive = options && typeof options === "object" && options.recursive;
            var made = native("node.fs.mkdir", [pathArg(p), !!recursive]);
            return recursive ? (made || undefined) : undefined;
        }

        function rmdirSync(p, options) {
            native("node.fs.rmdir", [pathArg(p), !!(options && options.recursive), false]);
        }

        function rmSync(p, options) {
            native("node.fs.rm", [pathArg(p), !!(options && options.recursive), !!(options && options.force)]);
        }

        function unlinkSync(p) {
            native("node.fs.unlink", [pathArg(p), false, false]);
        }

        function renameSync(from, to) {
            native("node.fs.rename", [pathArg(from, "oldPath"), pathArg(to, "newPath")]);
        }

        function copyFileSync(from, to, mode) {
            native("node.fs.copy", [pathArg(from, "src"), pathArg(to, "dest"), mode || 0]);
        }

        function cpSync(from, to, options) {
            var source = pathArg(from, "src");
            var stat = native("node.fs.stat", [source, false]);
            if (stat.type === "directory" && !(options && options.recursive))
                throw codeError("ERR_FS_EISDIR", "Recursive option is required to copy a directory: " + source);
            if (stat.type === "directory")
                native("node.fs.cp", [source, pathArg(to, "dest")]);
            else
                native("node.fs.copy", [source, pathArg(to, "dest"), options && options.errorOnExist && options.force === false ? 1 : 0]);
        }

        function accessSync(p, mode) {
            native("node.fs.access", [pathArg(p), mode || 0]);
        }

        function realpathSync(p) {
            return native("node.fs.realpath", [pathArg(p)]);
        }
        realpathSync.native = realpathSync;

        function truncateSync(p, length) {
            if (typeof p === "number")
                p = fdEntry(p).path;
            native("node.fs.truncate", [pathArg(p), length || 0]);
        }

        function chmodSync(p, mode) {
            native("node.fs.chmod", [pathArg(p), typeof mode === "string" ? parseInt(mode, 8) : mode]);
        }

        function mkdtempSync(prefix) {
            return native("node.fs.mkdtemp", [pathArg(prefix, "prefix")]);
        }

        function utimesSync(p) {
            pathArg(p);
        }

        function notSupported(name) {
            return function () {
                throw codeError("ENOSYS", "ENOSYS: function not implemented, " + name);
            };
        }

        // Open files: a number standing for a path and a place in it.
        var fds = Object.create(null);
        var nextFd = 3;

        function fdEntry(fd) {
            var entry = fds[fd];
            if (!entry) {
                var error = new Error("EBADF: bad file descriptor");
                error.code = "EBADF";
                throw error;
            }
            return entry;
        }

        function flagString(flags) {
            if (flags === undefined || flags === null)
                return "r";
            if (typeof flags === "string")
                return flags.replace("s", "");
            var access = flags & 3;
            var out = flags & constants.O_APPEND ? "a" : (flags & constants.O_TRUNC || (access && flags & constants.O_CREAT)) ? "w" : "r";
            if (out === "r" && access)
                out = "r+";
            else if (access === 2 && out !== "r+")
                out += "+";
            if (flags & constants.O_EXCL)
                out += "x";
            return out;
        }

        function openSync(p, flags) {
            var file = pathArg(p);
            var f = flagString(flags);
            var exclusive = f.indexOf("x") >= 0;
            if (f.charAt(0) === "r") {
                var stat = native("node.fs.stat", [file, false]);
                if (stat.type === "directory" && f !== "r")
                    throw nativeError({ error: "EISDIR: illegal operation on a directory, open '" + file + "'", code: "EISDIR", syscall: "open", path: file });
            } else if (f.charAt(0) === "w") {
                native("node.fs.write", [file, "", "utf8", exclusive ? "wx" : "w"]);
            } else {
                native("node.fs.write", [file, "", "utf8", exclusive ? "ax" : "a"]);
            }
            var fd = nextFd++;
            fds[fd] = { path: file, flags: f, position: 0, append: f.charAt(0) === "a" };
            return fd;
        }

        function closeSync(fd) {
            fdEntry(fd);
            delete fds[fd];
        }

        function readSync(fd, buffer, offset, length, position) {
            var entry = fdEntry(fd);
            if (offset && typeof offset === "object") {
                position = offset.position;
                length = offset.length;
                offset = offset.offset;
            }
            offset = offset || 0;
            length = length === undefined ? buffer.length - offset : length;
            var data = Buffer.from(native("node.fs.read", [entry.path, "base64"]), "base64");
            var from = typeof position === "number" && position >= 0 ? position : entry.position;
            var count = Math.max(0, Math.min(length, data.length - from, buffer.length - offset));
            if (count > 0)
                buffer.set(data.subarray(from, from + count), offset);
            if (!(typeof position === "number" && position >= 0))
                entry.position += count;
            return count;
        }

        function writeSync(fd, data, offset, length, position) {
            var entry = fdEntry(fd);
            var bytes;
            if (typeof data === "string") {
                // writeSync(fd, string, position, encoding)
                bytes = stringToBytes(data, typeof length === "string" ? length : "utf8");
                position = offset;
            } else {
                bytes = toBytes(data);
                offset = offset || 0;
                length = length === undefined ? bytes.length - offset : length;
                bytes = bytes.subarray(offset, offset + length);
            }
            var at = typeof position === "number" && position >= 0 ? position : entry.position;
            var size = native("node.fs.stat", [entry.path, false]).size;
            if (entry.append || at >= size) {
                if (!entry.append && at > size)
                    native("node.fs.write", [entry.path, base64Encode(new Uint8Array(at - size), false), "base64", "a"]);
                native("node.fs.write", [entry.path, base64Encode(bytes, false), "base64", "a"]);
            } else {
                var current = Buffer.from(native("node.fs.read", [entry.path, "base64"]), "base64");
                var merged = Buffer.alloc(Math.max(current.length, at + bytes.length));
                merged.set(current, 0);
                merged.set(bytes, at);
                native("node.fs.write", [entry.path, base64Encode(merged, false), "base64", "w"]);
            }
            if (!(typeof position === "number" && position >= 0))
                entry.position = (entry.append ? size : at) + bytes.length;
            return bytes.length;
        }

        function fstatSync(fd) {
            return statSync(fdEntry(fd).path);
        }

        function ftruncateSync(fd, length) {
            truncateSync(fdEntry(fd).path, length);
        }

        // The callback form of a Sync function: the result on a later turn.
        function callbackForm(sync) {
            return function () {
                var args = Array.prototype.slice.call(arguments);
                var callback = typeof args[args.length - 1] === "function" ? args.pop() : null;
                if (!callback)
                    throw codeError("ERR_INVALID_ARG_TYPE", 'The "cb" argument must be of type function', TypeError);
                var result, error = null;
                try {
                    result = sync.apply(null, args);
                } catch (e) {
                    error = e;
                }
                later(function () {
                    if (error)
                        callback(error);
                    else
                        callback(null, result);
                });
            };
        }

        function promiseForm(sync) {
            return function () {
                var args = arguments;
                return new Promise(function (resolve, reject) {
                    later(function () {
                        try {
                            resolve(sync.apply(null, args));
                        } catch (e) {
                            reject(e);
                        }
                    });
                });
            };
        }

        var syncs = {
            readFile: readFileSync, writeFile: writeFileSync, appendFile: appendFileSync,
            stat: statSync, lstat: lstatSync, readdir: readdirSync, mkdir: mkdirSync, rmdir: rmdirSync,
            rm: rmSync, unlink: unlinkSync, rename: renameSync, copyFile: copyFileSync, cp: cpSync,
            access: accessSync, realpath: realpathSync, truncate: truncateSync, chmod: chmodSync,
            mkdtemp: mkdtempSync, utimes: utimesSync, open: openSync, close: closeSync, fstat: fstatSync,
            ftruncate: ftruncateSync, lchmod: chmodSync, lutimes: utimesSync, fsync: function (fd) { fdEntry(fd); },
            fdatasync: function (fd) { fdEntry(fd); }
        };

        var fs = {
            constants: constants,
            F_OK: 0, R_OK: 4, W_OK: 2, X_OK: 1,
            Stats: Stats,
            Dirent: Dirent,
            existsSync: existsSync,
            readSync: readSync,
            writeSync: writeSync,
            symlinkSync: notSupported("symlink"),
            linkSync: notSupported("link"),
            readlinkSync: notSupported("readlink"),
            chownSync: function () {},
            lchownSync: function () {},
            fchmodSync: function (fd, mode) { chmodSync(fdEntry(fd).path, mode); },
            fchownSync: function () {},
            futimesSync: function () {}
        };
        Object.keys(syncs).forEach(function (name) {
            fs[name + "Sync"] = syncs[name];
            fs[name] = callbackForm(syncs[name]);
        });
        fs.realpath.native = fs.realpath;
        fs.exists = function (p, callback) {
            var result = existsSync(p);
            later(function () { callback(result); });
        };
        var customPromisify = typeof Symbol === "function" ? Symbol.for("nodejs.util.promisify.custom") : "__promisify__";
        fs.exists[customPromisify] = function (p) {
            return Promise.resolve(existsSync(p));
        };
        fs.read = function (fd, buffer, offset, length, position, callback) {
            var args = Array.prototype.slice.call(arguments);
            callback = args.pop();
            var count, error = null;
            try {
                count = readSync.apply(null, args);
            } catch (e) {
                error = e;
            }
            later(function () { callback(error, count, args[1]); });
        };
        fs.write = function (fd, data) {
            var args = Array.prototype.slice.call(arguments);
            var callback = typeof args[args.length - 1] === "function" ? args.pop() : function () {};
            var count, error = null;
            try {
                count = writeSync.apply(null, args);
            } catch (e) {
                error = e;
            }
            later(function () { callback(error, count, data); });
        };

        // Polled watching: a file's time and size, or a folder's names.
        function snapshot(p) {
            try {
                var stat = native("node.fs.stat", [p, false]);
                var key = stat.mtimeMs + ":" + stat.size;
                if (stat.type === "directory")
                    key += ":" + native("node.fs.readdir", [p]).map(function (e) { return e.name; }).join("/");
                return { stat: stat, key: key };
            } catch (e) {
                return { stat: null, key: "" };
            }
        }
        var watchers = {};
        fs.watchFile = function (p, options, listener) {
            if (typeof options === "function") {
                listener = options;
                options = {};
            }
            var file = pathArg(p);
            var last = snapshot(file);
            var timer = setInterval(function () {
                var now = snapshot(file);
                if (now.key !== last.key) {
                    var previous = last;
                    last = now;
                    listener(new Stats(now.stat || { type: "other" }), new Stats(previous.stat || { type: "other" }));
                }
            }, (options && options.interval) || 5007);
            (watchers[file] = watchers[file] || []).push({ timer: timer, listener: listener });
            var watcher = new Emitter();
            watcher.unref = watcher.ref = function () { return watcher; };
            return watcher;
        };
        fs.unwatchFile = function (p, listener) {
            var file = pathArg(p);
            (watchers[file] || []).forEach(function (w) {
                if (!listener || w.listener === listener)
                    clearInterval(w.timer);
            });
            watchers[file] = (watchers[file] || []).filter(function (w) { return listener && w.listener !== listener; });
        };
        fs.watch = function (p, options, listener) {
            if (typeof options === "function") {
                listener = options;
                options = {};
            }
            var file = pathArg(p);
            var watcher = new Emitter();
            var last = snapshot(file);
            if (!last.stat)
                throw nativeError({ error: "ENOENT: no such file or directory, watch '" + file + "'", code: "ENOENT", syscall: "watch", path: file });
            if (listener)
                watcher.on("change", listener);
            var timer = setInterval(function () {
                var now = snapshot(file);
                if (now.key !== last.key) {
                    var renamed = !now.stat || (last.stat && now.stat.type === "directory" && now.key.split(":")[2] !== last.key.split(":")[2]);
                    last = now;
                    watcher.emit("change", renamed ? "rename" : "change", path.basename(file));
                }
            }, 1000);
            watcher.close = function () {
                clearInterval(timer);
                watcher.emit("close");
            };
            watcher.ref = watcher.unref = function () { return watcher; };
            return watcher;
        };

        fs.createReadStream = function (p, options) {
            var Readable = builtin("stream").Readable;
            if (typeof options === "string")
                options = { encoding: options };
            options = options || {};
            var stream = new Readable({ encoding: options.encoding, highWaterMark: options.highWaterMark });
            var file;
            try {
                file = typeof p === "number" ? fdEntry(p).path : pathArg(p);
            } catch (e) {
                later(function () { stream.destroy(e); });
                return stream;
            }
            stream.path = file;
            stream.bytesRead = 0;
            stream.pending = true;
            var sent = false;
            stream._read = function () {
                if (sent)
                    return;
                sent = true;
                var data;
                try {
                    data = Buffer.from(native("node.fs.read", [file, "base64"]), "base64");
                } catch (e) {
                    stream.destroy(e);
                    return;
                }
                var start = options.start || 0;
                var end = options.end === undefined ? data.length : Math.min(data.length, options.end + 1);
                var size = options.highWaterMark || 65536;
                for (var at = start; at < end; at += size)
                    stream.push(data.subarray(at, Math.min(end, at + size)));
                stream.bytesRead = Math.max(0, end - start);
                stream.push(null);
            };
            later(function () {
                if (stream.destroyed)
                    return;
                stream.pending = false;
                stream.emit("open", 0);
                stream.emit("ready");
            });
            stream.close = function (callback) {
                stream.destroy();
                if (callback)
                    stream.once("close", callback);
            };
            return stream;
        };

        fs.createWriteStream = function (p, options) {
            var Writable = builtin("stream").Writable;
            if (typeof options === "string")
                options = { encoding: options };
            options = options || {};
            var stream = new Writable({ decodeStrings: true });
            var flags = options.flags || "w";
            var file;
            try {
                file = typeof p === "number" ? fdEntry(p).path : pathArg(p);
                if (flags.charAt(0) === "w")
                    native("node.fs.write", [file, "", "utf8", flags.indexOf("x") >= 0 ? "wx" : "w"]);
            } catch (e) {
                later(function () { stream.destroy(e); });
                return stream;
            }
            stream.path = file;
            stream.bytesWritten = 0;
            stream.pending = false;
            stream._write = function (chunk, encoding, callback) {
                try {
                    var bytes = typeof chunk === "string" ? stringToBytes(chunk, encoding) : toBytes(chunk);
                    native("node.fs.write", [file, base64Encode(bytes, false), "base64", "a"]);
                    stream.bytesWritten += bytes.length;
                    callback();
                } catch (e) {
                    callback(e);
                }
            };
            later(function () {
                stream.emit("open", 0);
                stream.emit("ready");
            });
            stream.close = function (callback) {
                stream.end(callback);
            };
            return stream;
        };

        var promises = { constants: constants };
        Object.keys(syncs).forEach(function (name) {
            if (name !== "open" && name !== "close" && name !== "fstat" && name !== "ftruncate" && name !== "fsync" && name !== "fdatasync")
                promises[name] = promiseForm(syncs[name]);
        });
        promises.open = function (p, flags) {
            return promiseForm(openSync)(p, flags).then(function (fd) {
                return {
                    fd: fd,
                    readFile: function (o) { return promiseForm(readFileSync)(fdEntry(fd).path, o); },
                    writeFile: function (d, o) { return promiseForm(writeFileSync)(fd, d, o); },
                    appendFile: function (d, o) { return promiseForm(appendFileSync)(fd, d, o); },
                    write: function (d, a, b, c) {
                        return promiseForm(writeSync)(fd, d, a, b, c).then(function (n) { return { bytesWritten: n, buffer: d }; });
                    },
                    read: function (buffer, offset, length, position) {
                        return promiseForm(readSync)(fd, buffer, offset, length, position).then(function (n) { return { bytesRead: n, buffer: buffer }; });
                    },
                    stat: function () { return promiseForm(fstatSync)(fd); },
                    truncate: function (length) { return promiseForm(ftruncateSync)(fd, length); },
                    close: function () { return promiseForm(closeSync)(fd); }
                };
            });
        };
        promises.watch = function () {
            throw notAvailable("fs.promises.watch");
        };
        fs.promises = promises;
        return fs;
    });
    define("fs/promises", function () { return builtin("fs").promises; });

    // -----------------------------------------------------------------------
    // child_process

    define("child_process", function () {
        var stream = builtin("stream");

        function ChildProcess() {
            Emitter.call(this);
            this.pid = undefined;
            this.exitCode = null;
            this.signalCode = null;
            this.killed = false;
            this.connected = false;
            this.stdin = null;
            this.stdout = null;
            this.stderr = null;
            this.stdio = [null, null, null];
            this.spawnfile = null;
            this.spawnargs = [];
        }
        inherits(ChildProcess, Emitter);
        ChildProcess.prototype.kill = function (signal) {
            if (this._id === undefined || this.exitCode !== null || this.signalCode !== null)
                return false;
            try {
                var killed = native("proc.kill", [this._id]);
                this.killed = this.killed || killed;
                this._killSignal = typeof signal === "string" ? signal : "SIGTERM";
                return killed;
            } catch (e) {
                return false;
            }
        };
        ChildProcess.prototype.ref = function () {};
        ChildProcess.prototype.unref = function () {};
        ChildProcess.prototype.disconnect = function () {};
        ChildProcess.prototype.send = function () {
            throw notAvailable("ChildProcess.send");
        };

        function normalizeArgs(file, args, options) {
            if (!Array.isArray(args)) {
                options = args;
                args = [];
            }
            options = Object.assign({}, options || {});
            return { file: String(file), args: args.map(String), options: options };
        }

        function nativeArgs(spec) {
            var options = spec.options;
            var env = options.env ? Object.assign({}, options.env) : null;
            var cwdOption = options.cwd;
            if (cwdOption && typeof cwdOption === "object" && cwdOption.href)
                cwdOption = fileURLToPath(cwdOption);
            return [spec.file, spec.args, cwdOption ? path.resolve(String(cwdOption)) : cwd, env,
                options.shell === true ? true : (typeof options.shell === "string" ? options.shell : false)];
        }

        function stdioOf(options, index) {
            var stdio = options.stdio;
            if (typeof stdio === "string")
                return stdio;
            if (Array.isArray(stdio) && stdio[index] !== undefined && stdio[index] !== null)
                return typeof stdio[index] === "string" ? stdio[index] : "pipe";
            return "pipe";
        }

        function spawn(file, args, options) {
            var spec = normalizeArgs(file, args, options);
            var child = new ChildProcess();
            child.spawnfile = spec.options.shell ? (isWindows ? "cmd.exe" : "/bin/sh") : spec.file;
            child.spawnargs = [spec.file].concat(spec.args);
            var outMode = stdioOf(spec.options, 1), errMode = stdioOf(spec.options, 2);
            child.stdout = outMode === "pipe" ? new stream.Readable() : null;
            child.stderr = errMode === "pipe" ? new stream.Readable() : null;
            child.stdin = stdioOf(spec.options, 0) === "pipe" ? new stream.Writable({
                write: function (chunk, encoding, callback) {
                    try {
                        native("proc.write", [child._id, base64Encode(toBytes(chunk, encoding), false)]);
                        callback();
                    } catch (e) {
                        callback(e);
                    }
                },
                final: function (callback) {
                    try {
                        if (child._id !== undefined)
                            native("proc.end", [child._id]);
                    } catch (e) {
                        // already gone
                    }
                    callback();
                }
            }) : null;
            child.stdio = [child.stdin, child.stdout, child.stderr];

            var started;
            try {
                started = native("proc.spawn", nativeArgs(spec));
            } catch (error) {
                error.spawnargs = spec.args;
                later(function () {
                    child.emit("error", error);
                    if (child.stdout)
                        child.stdout.push(null);
                    if (child.stderr)
                        child.stderr.push(null);
                    child.emit("close", -2, null);
                });
                return child;
            }
            child._id = started.id;
            child.pid = started.pid;
            bridge.processHandlers[started.id] = function (event) {
                if (event.kind === "stdout" || event.kind === "stderr") {
                    var bytes = Buffer.from(event.data, "base64");
                    var target = child[event.kind];
                    if (target)
                        target.push(bytes);
                    else if ((event.kind === "stdout" ? outMode : errMode) === "inherit" && window.console)
                        console[event.kind === "stdout" ? "log" : "error"](bytes.toString());
                    return;
                }
                if (event.kind !== "exit")
                    return;
                delete bridge.processHandlers[started.id];
                child.exitCode = event.signal ? null : event.code;
                child.signalCode = event.signal ? (child._killSignal || event.signal) : null;
                if (child.stdout)
                    child.stdout.push(null);
                if (child.stderr)
                    child.stderr.push(null);
                child.emit("exit", child.exitCode, child.signalCode);
                // "close" once the output streams have ended.
                var waiting = (child.stdout ? 1 : 0) + (child.stderr ? 1 : 0);
                var closed = false;
                function close() {
                    if (closed)
                        return;
                    closed = true;
                    child.emit("close", child.exitCode, child.signalCode);
                }
                if (waiting === 0) {
                    later(close);
                    return;
                }
                [child.stdout, child.stderr].forEach(function (s) {
                    if (!s)
                        return;
                    var done = function () {
                        if (--waiting === 0)
                            later(close);
                    };
                    if (s.readableEnded)
                        done();
                    else {
                        s.once("end", done);
                        // Nobody reads it: let it flow so it ends.
                        if (s.listenerCount("data") === 0 && s.listenerCount("readable") === 0)
                            s.resume();
                    }
                });
            };
            if (spec.options.timeout > 0) {
                setTimeout(function () { child.kill(spec.options.killSignal); }, spec.options.timeout);
            }
            return child;
        }

        function collectOutput(child, encoding, callback, describe) {
            var out = [], err = [];
            if (child.stdout)
                child.stdout.on("data", function (chunk) { out.push(chunk); });
            if (child.stderr)
                child.stderr.on("data", function (chunk) { err.push(chunk); });
            var finished = false;
            function finish(error, code, signal) {
                if (finished)
                    return;
                finished = true;
                var stdout = Buffer.concat(out), stderr = Buffer.concat(err);
                if (encoding !== "buffer") {
                    stdout = stdout.toString(encoding);
                    stderr = stderr.toString(encoding);
                }
                if (!error && (code !== 0 || signal)) {
                    error = new Error("Command failed: " + describe + (stderr.length ? "\n" + stderr : ""));
                    error.code = code;
                    error.killed = child.killed;
                    error.signal = signal;
                    error.cmd = describe;
                }
                if (error) {
                    error.stdout = stdout;
                    error.stderr = stderr;
                }
                if (callback)
                    callback(error || null, stdout, stderr);
            }
            child.on("error", function (error) { finish(error); });
            child.on("close", function (code, signal) { finish(null, code, signal); });
        }

        function exec(command, options, callback) {
            if (typeof options === "function") {
                callback = options;
                options = {};
            }
            options = Object.assign({}, options || {});
            options.shell = typeof options.shell === "string" ? options.shell : true;
            var child = spawn(command, [], options);
            collectOutput(child, options.encoding || "utf8", callback, command);
            return child;
        }

        function execFile(file, args, options, callback) {
            if (typeof args === "function") {
                callback = args;
                args = [];
                options = {};
            } else if (typeof options === "function") {
                callback = options;
                if (Array.isArray(args)) {
                    options = {};
                } else {
                    options = args;
                    args = [];
                }
            }
            if (!Array.isArray(args)) {
                options = args;
                args = [];
            }
            options = Object.assign({}, options || {});
            var child = spawn(file, args, options);
            collectOutput(child, options.encoding || "utf8", callback, [file].concat(args).join(" "));
            return child;
        }

        var customPromisify = typeof Symbol === "function" ? Symbol.for("nodejs.util.promisify.custom") : "__promisify__";
        function promised(fn) {
            return function () {
                var args = Array.prototype.slice.call(arguments);
                var child;
                var promise = new Promise(function (resolve, reject) {
                    args.push(function (error, stdout, stderr) {
                        if (error)
                            reject(error);
                        else
                            resolve({ stdout: stdout, stderr: stderr });
                    });
                    child = fn.apply(null, args);
                });
                promise.child = child;
                return promise;
            };
        }
        exec[customPromisify] = promised(exec);
        execFile[customPromisify] = promised(execFile);

        function spawnSync(file, args, options) {
            var spec = normalizeArgs(file, args, options);
            var input = spec.options.input;
            var inputBase64 = input === undefined || input === null ? "" : base64Encode(toBytes(input, "utf8"), false);
            var result;
            try {
                result = native("proc.run", nativeArgs(spec).concat([inputBase64, spec.options.timeout || 0]));
            } catch (error) {
                error.spawnargs = spec.args;
                return { pid: 0, output: [null, null, null], stdout: null, stderr: null, status: null, signal: null, error: error };
            }
            var encoding = spec.options.encoding && spec.options.encoding !== "buffer" ? spec.options.encoding : null;
            var stdout = Buffer.from(result.stdout, "base64"), stderr = Buffer.from(result.stderr, "base64");
            if (encoding) {
                stdout = stdout.toString(encoding);
                stderr = stderr.toString(encoding);
            }
            var out = { pid: result.pid, output: [null, stdout, stderr], stdout: stdout, stderr: stderr, status: result.status, signal: result.signal };
            if (result.timedOut) {
                out.error = codeError("ETIMEDOUT", "spawnSync " + spec.file + " ETIMEDOUT");
                out.error.errno = isWindows ? -4039 : -110;
                out.error.syscall = "spawnSync " + spec.file;
            }
            return out;
        }

        function checkSync(result, describe, options) {
            if (result.error)
                throw result.error;
            if (result.stderr && result.stderr.length && stdioOf(options || {}, 2) !== "pipe" && window.console)
                console.error(String(result.stderr));
            if (result.status !== 0 || result.signal) {
                var error = new Error("Command failed: " + describe + (result.stderr && result.stderr.length ? "\n" + result.stderr : ""));
                error.status = result.status;
                error.signal = result.signal;
                error.output = result.output;
                error.stdout = result.stdout;
                error.stderr = result.stderr;
                error.pid = result.pid;
                throw error;
            }
            return result.stdout;
        }

        function execSync(command, options) {
            options = Object.assign({}, options || {});
            options.shell = typeof options.shell === "string" ? options.shell : true;
            return checkSync(spawnSync(command, [], options), command, options);
        }

        function execFileSync(file, args, options) {
            if (!Array.isArray(args)) {
                options = args;
                args = [];
            }
            return checkSync(spawnSync(file, args, options), [file].concat(args).join(" "), options);
        }

        return {
            ChildProcess: ChildProcess,
            spawn: spawn,
            exec: exec,
            execFile: execFile,
            spawnSync: spawnSync,
            execSync: execSync,
            execFileSync: execFileSync,
            fork: function () {
                throw notAvailable("child_process.fork (it runs Node.js scripts)");
            }
        };
    });

    // -----------------------------------------------------------------------
    // crypto: hashes written out here, random numbers from the web view

    define("crypto", function () {
        function rotl(x, n) {
            return (x << n) | (x >>> (32 - n));
        }

        function pad(bytes, littleEndian) {
            var length = bytes.length;
            var total = ((length + 9 + 63) >> 6) << 6;
            var out = new Uint8Array(total);
            out.set(bytes, 0);
            out[length] = 0x80;
            var bitsLow = (length * 8) >>> 0;
            var bitsHigh = Math.floor(length / 0x20000000) >>> 0;
            for (var i = 0; i < 4; i++) {
                if (littleEndian) {
                    out[total - 8 + i] = (bitsLow >>> (8 * i)) & 0xFF;
                    out[total - 4 + i] = (bitsHigh >>> (8 * i)) & 0xFF;
                } else {
                    out[total - 1 - i] = (bitsLow >>> (8 * i)) & 0xFF;
                    out[total - 5 - i] = (bitsHigh >>> (8 * i)) & 0xFF;
                }
            }
            return out;
        }

        var MD5_S = [7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21];
        var MD5_K = [];
        for (var k = 0; k < 64; k++)
            MD5_K[k] = Math.floor(Math.abs(Math.sin(k + 1)) * 4294967296) | 0;

        function md5(bytes) {
            var m = pad(bytes, true);
            var a0 = 0x67452301, b0 = 0xefcdab89 | 0, c0 = 0x98badcfe | 0, d0 = 0x10325476;
            var w = new Int32Array(16);
            for (var chunk = 0; chunk < m.length; chunk += 64) {
                for (var j = 0; j < 16; j++)
                    w[j] = m[chunk + j * 4] | (m[chunk + j * 4 + 1] << 8) | (m[chunk + j * 4 + 2] << 16) | (m[chunk + j * 4 + 3] << 24);
                var a = a0, b = b0, c = c0, d = d0;
                for (var i = 0; i < 64; i++) {
                    var f, g;
                    if (i < 16) {
                        f = (b & c) | (~b & d);
                        g = i;
                    } else if (i < 32) {
                        f = (d & b) | (~d & c);
                        g = (5 * i + 1) % 16;
                    } else if (i < 48) {
                        f = b ^ c ^ d;
                        g = (3 * i + 5) % 16;
                    } else {
                        f = c ^ (b | ~d);
                        g = (7 * i) % 16;
                    }
                    f = (f + a + MD5_K[i] + w[g]) | 0;
                    a = d;
                    d = c;
                    c = b;
                    b = (b + rotl(f, MD5_S[i])) | 0;
                }
                a0 = (a0 + a) | 0;
                b0 = (b0 + b) | 0;
                c0 = (c0 + c) | 0;
                d0 = (d0 + d) | 0;
            }
            var out = new Uint8Array(16);
            [a0, b0, c0, d0].forEach(function (v, n) {
                for (var q = 0; q < 4; q++)
                    out[n * 4 + q] = (v >>> (8 * q)) & 0xFF;
            });
            return out;
        }

        function bigEndianWords(words) {
            var out = new Uint8Array(words.length * 4);
            for (var i = 0; i < words.length; i++) {
                out[i * 4] = (words[i] >>> 24) & 0xFF;
                out[i * 4 + 1] = (words[i] >>> 16) & 0xFF;
                out[i * 4 + 2] = (words[i] >>> 8) & 0xFF;
                out[i * 4 + 3] = words[i] & 0xFF;
            }
            return out;
        }

        function sha1(bytes) {
            var m = pad(bytes, false);
            var h = [0x67452301, 0xEFCDAB89 | 0, 0x98BADCFE | 0, 0x10325476, 0xC3D2E1F0 | 0];
            var w = new Int32Array(80);
            for (var chunk = 0; chunk < m.length; chunk += 64) {
                for (var j = 0; j < 16; j++)
                    w[j] = (m[chunk + j * 4] << 24) | (m[chunk + j * 4 + 1] << 16) | (m[chunk + j * 4 + 2] << 8) | m[chunk + j * 4 + 3];
                for (j = 16; j < 80; j++)
                    w[j] = rotl(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
                var a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (var i = 0; i < 80; i++) {
                    var f, kk;
                    if (i < 20) {
                        f = (b & c) | (~b & d);
                        kk = 0x5A827999;
                    } else if (i < 40) {
                        f = b ^ c ^ d;
                        kk = 0x6ED9EBA1;
                    } else if (i < 60) {
                        f = (b & c) | (b & d) | (c & d);
                        kk = 0x8F1BBCDC | 0;
                    } else {
                        f = b ^ c ^ d;
                        kk = 0xCA62C1D6 | 0;
                    }
                    var t = (rotl(a, 5) + f + e + kk + w[i]) | 0;
                    e = d;
                    d = c;
                    c = rotl(b, 30);
                    b = a;
                    a = t;
                }
                h[0] = (h[0] + a) | 0;
                h[1] = (h[1] + b) | 0;
                h[2] = (h[2] + c) | 0;
                h[3] = (h[3] + d) | 0;
                h[4] = (h[4] + e) | 0;
            }
            return bigEndianWords(h);
        }

        var SHA256_K = [
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        ].map(function (v) { return v | 0; });

        function sha256(bytes, initial) {
            var m = pad(bytes, false);
            var h = (initial || [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19])
                .map(function (v) { return v | 0; });
            var w = new Int32Array(64);
            function rotr(x, n) {
                return (x >>> n) | (x << (32 - n));
            }
            for (var chunk = 0; chunk < m.length; chunk += 64) {
                for (var j = 0; j < 16; j++)
                    w[j] = (m[chunk + j * 4] << 24) | (m[chunk + j * 4 + 1] << 16) | (m[chunk + j * 4 + 2] << 8) | m[chunk + j * 4 + 3];
                for (j = 16; j < 64; j++) {
                    var s0 = rotr(w[j - 15], 7) ^ rotr(w[j - 15], 18) ^ (w[j - 15] >>> 3);
                    var s1 = rotr(w[j - 2], 17) ^ rotr(w[j - 2], 19) ^ (w[j - 2] >>> 10);
                    w[j] = (w[j - 16] + s0 + w[j - 7] + s1) | 0;
                }
                var a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
                for (var i = 0; i < 64; i++) {
                    var S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                    var ch = (e & f) ^ (~e & g);
                    var t1 = (hh + S1 + ch + SHA256_K[i] + w[i]) | 0;
                    var S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                    var maj = (a & b) ^ (a & c) ^ (b & c);
                    var t2 = (S0 + maj) | 0;
                    hh = g;
                    g = f;
                    f = e;
                    e = (d + t1) | 0;
                    d = c;
                    c = b;
                    b = a;
                    a = (t1 + t2) | 0;
                }
                h[0] = (h[0] + a) | 0;
                h[1] = (h[1] + b) | 0;
                h[2] = (h[2] + c) | 0;
                h[3] = (h[3] + d) | 0;
                h[4] = (h[4] + e) | 0;
                h[5] = (h[5] + f) | 0;
                h[6] = (h[6] + g) | 0;
                h[7] = (h[7] + hh) | 0;
            }
            return bigEndianWords(h);
        }

        function sha224(bytes) {
            return sha256(bytes, [0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4]).subarray(0, 28);
        }

        var digests = { md5: md5, sha1: sha1, sha256: sha256, sha224: sha224 };
        function digestOf(algorithm) {
            var name = String(algorithm).toLowerCase().replace(/^sha-/, "sha");
            var fn = digests[name];
            if (!fn)
                throw new Error("Digest method not supported");
            return fn;
        }

        function Hash(algorithm) {
            this._fn = digestOf(algorithm);
            this._parts = [];
        }
        Hash.prototype.update = function (data, encoding) {
            this._parts.push(Buffer.from(toBytes(data, encoding)));
            return this;
        };
        Hash.prototype.digest = function (encoding) {
            var result = Buffer.from(this._fn(Buffer.concat(this._parts)));
            return encoding && encoding !== "buffer" ? result.toString(encoding) : result;
        };
        Hash.prototype.copy = function () {
            var copy = Object.create(Hash.prototype);
            copy._fn = this._fn;
            copy._parts = this._parts.slice();
            return copy;
        };

        function Hmac(algorithm, key) {
            this._fn = digestOf(algorithm);
            key = Buffer.from(toBytes(key, "utf8"));
            if (key.length > 64)
                key = Buffer.from(this._fn(key));
            var block = Buffer.alloc(64);
            block.set(key, 0);
            this._inner = Buffer.alloc(64);
            this._outer = Buffer.alloc(64);
            for (var i = 0; i < 64; i++) {
                this._inner[i] = block[i] ^ 0x36;
                this._outer[i] = block[i] ^ 0x5c;
            }
            this._parts = [this._inner];
        }
        Hmac.prototype.update = Hash.prototype.update;
        Hmac.prototype.digest = function (encoding) {
            var inner = this._fn(Buffer.concat(this._parts));
            var result = Buffer.from(this._fn(Buffer.concat([this._outer, Buffer.from(inner)])));
            return encoding && encoding !== "buffer" ? result.toString(encoding) : result;
        };

        function randomFill(bytes) {
            var webCrypto = window.crypto;
            if (webCrypto && webCrypto.getRandomValues) {
                for (var at = 0; at < bytes.length; at += 65536)
                    webCrypto.getRandomValues(bytes.subarray(at, Math.min(bytes.length, at + 65536)));
            } else {
                for (var i = 0; i < bytes.length; i++)
                    bytes[i] = Math.floor(Math.random() * 256);
            }
            return bytes;
        }

        function randomBytes(size, callback) {
            var bytes = randomFill(Buffer.alloc(size));
            if (callback) {
                later(function () { callback(null, bytes); });
                return undefined;
            }
            return bytes;
        }

        function randomUUID() {
            var b = randomFill(new Uint8Array(16));
            b[6] = (b[6] & 0x0f) | 0x40;
            b[8] = (b[8] & 0x3f) | 0x80;
            var hex = bytesToString(b, "hex");
            return hex.slice(0, 8) + "-" + hex.slice(8, 12) + "-" + hex.slice(12, 16) + "-" + hex.slice(16, 20) + "-" + hex.slice(20);
        }

        function randomInt(min, max, callback) {
            if (max === undefined || typeof max === "function") {
                callback = max;
                max = min;
                min = 0;
            }
            var b = randomFill(new Uint8Array(6));
            var n = 0;
            for (var i = 0; i < 6; i++)
                n = n * 256 + b[i];
            var value = min + Math.floor(n / 281474976710656 * (max - min));
            if (callback) {
                later(function () { callback(null, value); });
                return undefined;
            }
            return value;
        }

        return {
            createHash: function (algorithm) { return new Hash(algorithm); },
            createHmac: function (algorithm, key) { return new Hmac(algorithm, key); },
            getHashes: function () { return Object.keys(digests); },
            randomBytes: randomBytes,
            pseudoRandomBytes: randomBytes,
            randomFillSync: function (buffer, offset, size) {
                offset = offset || 0;
                size = size === undefined ? buffer.length - offset : size;
                randomFill(new Uint8Array(buffer.buffer, buffer.byteOffset + offset, size));
                return buffer;
            },
            randomUUID: randomUUID,
            randomInt: randomInt,
            getRandomValues: function (array) { return randomFill(new Uint8Array(array.buffer, array.byteOffset, array.byteLength)) && array; },
            timingSafeEqual: function (a, b) {
                if (a.length !== b.length)
                    throw codeError("ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH", "Input buffers must have the same byte length", RangeError);
                var diff = 0;
                for (var i = 0; i < a.length; i++)
                    diff |= a[i] ^ b[i];
                return diff === 0;
            },
            webcrypto: window.crypto,
            subtle: window.crypto && window.crypto.subtle,
            createCipheriv: function () { throw notAvailable("crypto.createCipheriv"); },
            createDecipheriv: function () { throw notAvailable("crypto.createDecipheriv"); },
            createSign: function () { throw notAvailable("crypto.createSign"); },
            createVerify: function () { throw notAvailable("crypto.createVerify"); },
            pbkdf2Sync: function () { throw notAvailable("crypto.pbkdf2Sync"); },
            pbkdf2: function () { throw notAvailable("crypto.pbkdf2"); }
        };
    });

    // -----------------------------------------------------------------------
    // zlib

    define("zlib", function () {
        var stream = builtin("stream");
        function run(operation, data, options) {
            var bytes = typeof data === "string" ? stringToBytes(data, "utf8") : toBytes(data);
            var level = options && typeof options.level === "number" ? options.level : -1;
            return Buffer.from(native("node.zlib", [operation, base64Encode(bytes, false), level]), "base64");
        }
        var zlib = {
            constants: {
                Z_NO_FLUSH: 0, Z_PARTIAL_FLUSH: 1, Z_SYNC_FLUSH: 2, Z_FULL_FLUSH: 3, Z_FINISH: 4,
                Z_OK: 0, Z_STREAM_END: 1, Z_NEED_DICT: 2, Z_ERRNO: -1, Z_STREAM_ERROR: -2, Z_DATA_ERROR: -3,
                Z_NO_COMPRESSION: 0, Z_BEST_SPEED: 1, Z_BEST_COMPRESSION: 9, Z_DEFAULT_COMPRESSION: -1,
                Z_FILTERED: 1, Z_HUFFMAN_ONLY: 2, Z_RLE: 3, Z_FIXED: 4, Z_DEFAULT_STRATEGY: 0
            }
        };
        ["deflate", "inflate", "gzip", "gunzip", "deflateRaw", "inflateRaw", "unzip"].forEach(function (operation) {
            zlib[operation + "Sync"] = function (data, options) {
                return run(operation, data, options);
            };
            zlib[operation] = function (data, options, callback) {
                if (typeof options === "function") {
                    callback = options;
                    options = {};
                }
                var result, error = null;
                try {
                    result = run(operation, data, options);
                } catch (e) {
                    error = e;
                }
                later(function () { callback(error, result); });
            };
            var streamName = "create" + operation.charAt(0).toUpperCase() + operation.slice(1);
            zlib[streamName] = function (options) {
                var parts = [];
                return new stream.Transform({
                    transform: function (chunk, encoding, callback) {
                        parts.push(Buffer.from(toBytes(chunk, encoding)));
                        callback();
                    },
                    flush: function (callback) {
                        try {
                            callback(null, run(operation, Buffer.concat(parts), options));
                        } catch (e) {
                            callback(e);
                        }
                    }
                });
            };
        });
        Object.keys(zlib.constants).forEach(function (name) { zlib[name] = zlib.constants[name]; });
        zlib.crc32 = function (data, value) {
            var bytes = typeof data === "string" ? stringToBytes(data, "utf8") : toBytes(data);
            var crc = (value === undefined ? 0 : value) ^ -1;
            for (var i = 0; i < bytes.length; i++) {
                crc ^= bytes[i];
                for (var b = 0; b < 8; b++)
                    crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
            }
            return (crc ^ -1) >>> 0;
        };
        return zlib;
    });

    // -----------------------------------------------------------------------
    // http and https: requests made by Phoenix ("net.request"), so they are
    // not held to the page's own origin.

    var STATUS_CODES = {
        200: "OK", 201: "Created", 202: "Accepted", 204: "No Content", 206: "Partial Content",
        301: "Moved Permanently", 302: "Found", 303: "See Other", 304: "Not Modified", 307: "Temporary Redirect",
        308: "Permanent Redirect", 400: "Bad Request", 401: "Unauthorized", 403: "Forbidden", 404: "Not Found",
        405: "Method Not Allowed", 408: "Request Timeout", 409: "Conflict", 410: "Gone", 413: "Payload Too Large",
        415: "Unsupported Media Type", 429: "Too Many Requests", 500: "Internal Server Error", 501: "Not Implemented",
        502: "Bad Gateway", 503: "Service Unavailable", 504: "Gateway Timeout"
    };

    function makeHttp(defaultProtocol) {
        var stream = builtin("stream");

        function IncomingMessage() {
            stream.Readable.call(this);
            this.statusCode = 0;
            this.statusMessage = "";
            this.headers = {};
            this.rawHeaders = [];
            this.httpVersion = "1.1";
            this.complete = false;
        }
        inherits(IncomingMessage, stream.Readable);
        IncomingMessage.prototype.setTimeout = function () { return this; };

        function ClientRequest(options, callback) {
            stream.Writable.call(this);
            var self = this;
            this._options = options;
            this._headers = {};
            this._body = [];
            this.method = (options.method || "GET").toUpperCase();
            this.path = options.path || "/";
            this.host = options.hostname || options.host || "localhost";
            this.protocol = options.protocol || defaultProtocol;
            this.aborted = false;
            this.finished = false;
            Object.keys(options.headers || {}).forEach(function (name) {
                self._headers[name.toLowerCase()] = [name, options.headers[name]];
            });
            if (options.auth && !this._headers.authorization)
                this._headers.authorization = ["Authorization", "Basic " + base64Encode(stringToBytes(options.auth, "utf8"))];
            if (callback)
                this.once("response", callback);
            if (options.timeout)
                this.setTimeout(options.timeout);
        }
        inherits(ClientRequest, stream.Writable);
        ClientRequest.prototype.setHeader = function (name, value) {
            this._headers[String(name).toLowerCase()] = [name, value];
            return this;
        };
        ClientRequest.prototype.getHeader = function (name) {
            var h = this._headers[String(name).toLowerCase()];
            return h ? h[1] : undefined;
        };
        ClientRequest.prototype.removeHeader = function (name) {
            delete this._headers[String(name).toLowerCase()];
        };
        ClientRequest.prototype.getHeaders = function () {
            var out = {};
            for (var key in this._headers)
                out[key] = this._headers[key][1];
            return out;
        };
        ClientRequest.prototype.setTimeout = function (ms, callback) {
            this._timeout = ms;
            if (callback)
                this.once("timeout", callback);
            return this;
        };
        ClientRequest.prototype.setNoDelay = function () {};
        ClientRequest.prototype.setSocketKeepAlive = function () {};
        ClientRequest.prototype.flushHeaders = function () {};
        ClientRequest.prototype._write = function (chunk, encoding, callback) {
            this._body.push(Buffer.from(toBytes(chunk, encoding)));
            callback();
        };
        ClientRequest.prototype._final = function (callback) {
            this.finished = true;
            this._send();
            callback();
        };
        ClientRequest.prototype.abort = ClientRequest.prototype.destroy = function (error) {
            if (this.aborted)
                return this;
            this.aborted = true;
            this.destroyed = true;
            if (this._id !== undefined) {
                try {
                    native("net.abort", [this._id]);
                } catch (e) {
                    // already done
                }
                delete bridge.processHandlers[this._id];
            }
            var self = this;
            later(function () {
                self.emit("abort");
                if (error)
                    self.emit("error", error);
                self.emit("close");
            });
            return this;
        };
        ClientRequest.prototype._send = function () {
            if (this.aborted)
                return;
            var self = this;
            var o = this._options;
            var port = o.port ? ":" + o.port : "";
            var host = this.host;
            if (host.indexOf(":") >= 0 && host.charAt(0) !== "[")
                host = "[" + host + "]";
            var url = this.protocol + "//" + host + port + this.path;
            var headers = {};
            for (var key in this._headers)
                headers[this._headers[key][0]] = String(this._headers[key][1]);
            var started;
            try {
                started = native("net.request", [{
                    url: url,
                    method: this.method,
                    headers: headers,
                    body: this._body.length ? base64Encode(Buffer.concat(this._body), false) : "",
                    timeout: this._timeout || 0
                }]);
            } catch (error) {
                later(function () { self.emit("error", error); });
                return;
            }
            this._id = started.id;
            var response = null;
            bridge.processHandlers[started.id] = function (event) {
                if (self.aborted)
                    return;
                if (event.kind === "response") {
                    response = new IncomingMessage();
                    response.statusCode = event.status;
                    response.statusMessage = event.statusText || STATUS_CODES[event.status] || "";
                    (event.headers || []).forEach(function (pair) {
                        response.rawHeaders.push(pair[0], pair[1]);
                        var name = pair[0].toLowerCase();
                        if (name === "set-cookie")
                            response.headers[name] = (response.headers[name] || []).concat(pair[1]);
                        else if (response.headers[name] !== undefined)
                            response.headers[name] += ", " + pair[1];
                        else
                            response.headers[name] = pair[1];
                    });
                    response.req = self;
                    self.res = response;
                    self.emit("response", response);
                } else if (event.kind === "data") {
                    if (response)
                        response.push(Buffer.from(event.data, "base64"));
                } else if (event.kind === "end") {
                    delete bridge.processHandlers[started.id];
                    if (response) {
                        response.complete = true;
                        response.push(null);
                    }
                    self.emit("close");
                } else if (event.kind === "error") {
                    delete bridge.processHandlers[started.id];
                    var error = new Error(event.message || "request failed");
                    error.code = event.code || "ECONNRESET";
                    if (event.code === "ETIMEDOUT")
                        self.emit("timeout");
                    if (response)
                        response.destroy(error);
                    self.emit("error", error);
                }
            };
        };

        function toOptions(input, options) {
            var result = {};
            if (typeof input === "string" || (input && typeof input === "object" && typeof input.href === "string" && input.searchParams)) {
                var parsed = builtin("url").parse(String(input.href || input));
                result.protocol = parsed.protocol;
                result.hostname = parsed.hostname;
                result.port = parsed.port;
                result.path = parsed.path;
                if (parsed.auth)
                    result.auth = parsed.auth;
                if (options && typeof options === "object")
                    Object.assign(result, options);
                return result;
            }
            return Object.assign(result, input || {});
        }

        function request(input, options, callback) {
            if (typeof options === "function") {
                callback = options;
                options = null;
            }
            return new ClientRequest(toOptions(input, options), callback);
        }

        function get(input, options, callback) {
            var req = request(input, options, callback);
            req.end();
            return req;
        }

        function Agent(options) {
            this.options = options || {};
            this.maxSockets = Infinity;
        }
        Agent.prototype.destroy = function () {};

        return {
            request: request,
            get: get,
            Agent: Agent,
            globalAgent: new Agent(),
            ClientRequest: ClientRequest,
            IncomingMessage: IncomingMessage,
            STATUS_CODES: STATUS_CODES,
            METHODS: ["DELETE", "GET", "HEAD", "OPTIONS", "PATCH", "POST", "PUT"],
            createServer: function () {
                throw notAvailable("A server (http.createServer)");
            }
        };
    }
    define("http", function () { return makeHttp("http:"); });
    define("https", function () { return makeHttp("https:"); });

    // -----------------------------------------------------------------------
    // The smaller ones

    define("assert", function () {
        var util = builtin("util");
        function AssertionError(options) {
            var error = new Error(options.message || (util.inspect(options.actual) + " " + options.operator + " " + util.inspect(options.expected)));
            Object.setPrototypeOf(error, AssertionError.prototype);
            error.name = "AssertionError";
            error.code = "ERR_ASSERTION";
            error.actual = options.actual;
            error.expected = options.expected;
            error.operator = options.operator;
            error.generatedMessage = !options.message;
            return error;
        }
        AssertionError.prototype = Object.create(Error.prototype);
        AssertionError.prototype.constructor = AssertionError;

        function fail(actual, expected, message, operator) {
            if (message instanceof Error)
                throw message;
            throw new AssertionError({ actual: actual, expected: expected, message: message, operator: operator });
        }
        function assert(value, message) {
            if (!value)
                fail(value, true, message || "The expression evaluated to a falsy value", "==");
        }
        assert.ok = assert;
        assert.AssertionError = AssertionError;
        assert.fail = function (message) {
            fail(undefined, undefined, message || "Failed", "fail");
        };
        assert.equal = function (a, b, m) { if (a != b) fail(a, b, m, "=="); }; // eslint-disable-line eqeqeq
        assert.notEqual = function (a, b, m) { if (a == b) fail(a, b, m, "!="); }; // eslint-disable-line eqeqeq
        assert.strictEqual = function (a, b, m) { if (!Object.is(a, b)) fail(a, b, m, "strictEqual"); };
        assert.notStrictEqual = function (a, b, m) { if (Object.is(a, b)) fail(a, b, m, "notStrictEqual"); };
        assert.deepEqual = function (a, b, m) { if (!deepEqual(a, b, false)) fail(a, b, m, "deepEqual"); };
        assert.notDeepEqual = function (a, b, m) { if (deepEqual(a, b, false)) fail(a, b, m, "notDeepEqual"); };
        assert.deepStrictEqual = function (a, b, m) { if (!deepEqual(a, b, true)) fail(a, b, m, "deepStrictEqual"); };
        assert.notDeepStrictEqual = function (a, b, m) { if (deepEqual(a, b, true)) fail(a, b, m, "notDeepStrictEqual"); };
        assert.match = function (s, re, m) { if (!re.test(s)) fail(s, re, m, "match"); };
        assert.doesNotMatch = function (s, re, m) { if (re.test(s)) fail(s, re, m, "doesNotMatch"); };
        assert.ifError = function (value) { if (value !== null && value !== undefined) throw value; };
        assert.throws = function (fn, expected, message) {
            try {
                fn();
            } catch (e) {
                if (typeof expected === "function" && expected.prototype !== undefined && !(e instanceof expected))
                    throw e;
                if (expected instanceof RegExp && !expected.test(String(e)))
                    fail(e, expected, message, "throws");
                return;
            }
            fail(undefined, expected, typeof expected === "string" ? expected : message || "Missing expected exception.", "throws");
        };
        assert.doesNotThrow = function (fn, message) {
            try {
                fn();
            } catch (e) {
                fail(e, undefined, message || "Got unwanted exception.", "doesNotThrow");
            }
        };
        assert.rejects = function (promise) {
            return Promise.resolve(typeof promise === "function" ? promise() : promise).then(function () {
                fail(undefined, undefined, "Missing expected rejection.", "rejects");
            }, function () {});
        };
        assert.strict = assert;
        return assert;
    });
    define("assert/strict", function () { return builtin("assert"); });

    define("timers", function () {
        return {
            setTimeout: window.setTimeout.bind(window),
            clearTimeout: window.clearTimeout.bind(window),
            setInterval: window.setInterval.bind(window),
            clearInterval: window.clearInterval.bind(window),
            setImmediate: setImmediate,
            clearImmediate: clearImmediate
        };
    });
    define("timers/promises", function () {
        return {
            setTimeout: function (ms, value) {
                return new Promise(function (resolve) { setTimeout(function () { resolve(value); }, ms); });
            },
            setImmediate: function (value) {
                return new Promise(function (resolve) { setImmediate(function () { resolve(value); }); });
            }
        };
    });
    define("process", function () { return process; });
    define("console", function () { return window.console; });
    define("perf_hooks", function () { return { performance: window.performance }; });
    define("tty", function () {
        return { isatty: function () { return false; } };
    });
    define("vm", function () {
        function runInThisContext(code) {
            return (0, eval)(String(code));
        }
        function Script(code) {
            this.code = String(code);
        }
        Script.prototype.runInThisContext = function () { return runInThisContext(this.code); };
        return {
            Script: Script,
            runInThisContext: runInThisContext,
            runInNewContext: function () { throw notAvailable("vm.runInNewContext"); },
            createContext: function (o) { return o || {}; }
        };
    });
    define("readline", function () {
        return {
            createInterface: function () {
                var rl = new Emitter();
                rl.close = function () { rl.emit("close"); };
                rl.question = function (q, cb) { later(function () { cb(""); }); };
                rl.setPrompt = rl.prompt = rl.write = function () {};
                return rl;
            }
        };
    });
    define("module", function () {
        Module.builtinModules = Object.keys(factories);
        return Module;
    });
    ["net", "tls", "dgram", "dns", "cluster", "worker_threads", "http2", "inspector", "repl", "v8", "async_hooks", "wasi",
        "trace_events", "domain", "diagnostics_channel"].forEach(function (name) {
        define(name, function () {
            var message = "Node.js's " + name + " module";
            var stub = {};
            if (typeof Proxy !== "function")
                return stub;
            return new Proxy(stub, {
                get: function (target, key) {
                    if (key === "__esModule" || typeof key === "symbol" || key === "then")
                        return undefined;
                    return function () { throw notAvailable(message + " (" + String(key) + ")"); };
                }
            });
        });
    });

    function setImmediate(callback) {
        var args = Array.prototype.slice.call(arguments, 1);
        return setTimeout(function () { callback.apply(null, args); }, 0);
    }
    function clearImmediate(id) {
        clearTimeout(id);
    }

    // process.stdout and process.stderr go to the web view's console.
    (function () {
        var stream = builtin("stream");
        function consoleStream(method) {
            var s = new stream.Writable({
                write: function (chunk, encoding, callback) {
                    var text = typeof chunk === "string" ? chunk : Buffer.from(chunk).toString("utf8");
                    if (window.console)
                        console[method](text.replace(/\n$/, ""));
                    callback();
                }
            });
            s.isTTY = false;
            s.columns = 80;
            s.fd = method === "log" ? 1 : 2;
            return s;
        }
        process.stdout = consoleStream("log");
        process.stderr = consoleStream("error");
        process.stdin = new stream.Readable();
        process.stdin.fd = 0;
        process.stdin.isTTY = false;
    })();

    // -----------------------------------------------------------------------
    // require

    var moduleCache = Object.create(null);
    var extensions = Object.create(null);

    function Module(filename, parent) {
        this.id = filename;
        this.filename = filename;
        this.path = filename ? path.dirname(filename) : cwd;
        this.exports = {};
        this.parent = parent || null;
        this.children = [];
        this.loaded = false;
        this.paths = moduleFolders(this.path);
    }
    // Module's own functions, which packages sometimes wrap: require goes
    // through them.
    Module.Module = Module;
    Module._cache = moduleCache;
    Module._extensions = extensions;
    Module.builtinModules = Object.keys(factories);
    Module._load = function (request, parent) {
        return load(request, parent || mainModule);
    };
    Module._resolveFilename = function (request, parent) {
        return resolveFilename(request, parent || mainModule);
    };
    Module._resolveLookupPaths = function (request, parent) {
        if (isRelative(request))
            return [parent.path];
        return parent.paths.slice();
    };
    Module._nodeModulePaths = function (from) {
        return moduleFolders(path.resolve(from));
    };
    Module.createRequire = function (filename) {
        var file = typeof filename === "string" ? filename : fileURLToPath(filename);
        return makeRequire(new Module(path.resolve(file), null));
    };
    Module.wrap = function (source) {
        return "(function (exports, require, module, __filename, __dirname) { " + source + "\n});";
    };
    Module.prototype.require = function (request) {
        return Module._load(request, this);
    };

    function moduleFolders(from) {
        var folders = [];
        var dir = from;
        while (true) {
            if (path.basename(dir) !== "node_modules")
                folders.push(path.join(dir, "node_modules"));
            var up = path.dirname(dir);
            if (up === dir)
                break;
            dir = up;
        }
        return folders;
    }

    function kind(file) {
        try {
            return native("node.fs.stat", [file, false]).type;
        } catch (e) {
            return null;
        }
    }

    var EXTENSIONS = [".js", ".json", ".node"];

    function tryFile(file) {
        if (kind(file) === "file")
            return file;
        for (var i = 0; i < EXTENSIONS.length; i++) {
            if (kind(file + EXTENSIONS[i]) === "file")
                return file + EXTENSIONS[i];
        }
        return null;
    }

    function tryIndex(folder) {
        for (var i = 0; i < EXTENSIONS.length; i++) {
            var file = path.join(folder, "index" + EXTENSIONS[i]);
            if (kind(file) === "file")
                return file;
        }
        return null;
    }

    function tryFolder(folder) {
        if (kind(folder) !== "directory")
            return null;
        var manifest = path.join(folder, "package.json");
        if (kind(manifest) === "file") {
            try {
                var main = JSON.parse(native("node.fs.read", [manifest, "utf8"])).main;
                if (typeof main === "string" && main) {
                    var target = path.resolve(folder, main);
                    var found = tryFile(target) || tryIndex(target);
                    if (found)
                        return found;
                }
            } catch (e) {
                // a package.json that does not read is passed over
            }
        }
        return tryIndex(folder);
    }

    function isRelative(request) {
        return request === "." || request === ".." || request.slice(0, 2) === "./" || request.slice(0, 3) === "../" ||
            (isWindows && (request.slice(0, 2) === ".\\" || request.slice(0, 3) === "..\\")) || path.isAbsolute(request);
    }

    function builtinName(request) {
        var name = request.slice(0, 5) === "node:" ? request.slice(5) : request;
        return name in factories ? name : null;
    }

    function resolveFilename(request, parent) {
        if (typeof request !== "string" || request.length === 0)
            throw codeError("ERR_INVALID_ARG_VALUE", "The argument 'id' must be a non-empty string.", TypeError);
        if (!isRelative(request) && builtinName(request))
            return builtinName(request);
        var found = null;
        if (isRelative(request)) {
            var target = path.resolve(parent.path, request);
            found = tryFile(target) || tryFolder(target);
        } else {
            var folders = Module._resolveLookupPaths(request, parent) || [];
            for (var i = 0; i < folders.length && !found; i++) {
                var candidate = path.join(folders[i], request);
                found = tryFile(candidate) || tryFolder(candidate);
            }
        }
        if (!found) {
            var error = new Error("Cannot find module '" + request + "'");
            error.code = "MODULE_NOT_FOUND";
            throw error;
        }
        return found;
    }

    function fileUrl(file) {
        try {
            return pathToFileURL(file).href;
        } catch (e) {
            return file;
        }
    }

    extensions[".js"] = function (module, filename) {
        var source = native("node.fs.read", [filename, "utf8"]);
        if (source.charCodeAt(0) === 0xFEFF)
            source = source.slice(1);
        if (source.slice(0, 2) === "#!")
            source = "//" + source;
        var wrapper = (0, eval)("(function (exports, require, module, __filename, __dirname) {" + source +
            "\n})\n//# sourceURL=" + fileUrl(filename));
        wrapper.call(module.exports, module.exports, makeRequire(module), module, filename, path.dirname(filename));
    };
    extensions[".json"] = function (module, filename) {
        var source = native("node.fs.read", [filename, "utf8"]);
        if (source.charCodeAt(0) === 0xFEFF)
            source = source.slice(1);
        try {
            module.exports = JSON.parse(source);
        } catch (e) {
            e.message = filename + ": " + e.message;
            throw e;
        }
    };
    extensions[".node"] = function (module, filename) {
        throw notAvailable("A native Node.js addon (" + path.basename(filename) + ")");
    };

    function load(request, parent) {
        if (!isRelative(request) && builtinName(request))
            return builtin(builtinName(request));
        var filename = Module._resolveFilename(request, parent);
        if (builtinName(filename) && !path.isAbsolute(filename))
            return builtin(builtinName(filename));
        var cached = moduleCache[filename];
        if (cached)
            return cached.exports;
        var module = new Module(filename, parent);
        moduleCache[filename] = module;
        parent.children.push(module);
        var loaded = false;
        try {
            var handler = extensions[path.extname(filename).toLowerCase()] || extensions[".js"];
            handler(module, filename);
            loaded = true;
        } finally {
            if (!loaded)
                delete moduleCache[filename];
        }
        module.loaded = true;
        return module.exports;
    }

    function makeRequire(module) {
        function require(request) {
            return Module._load(request, module);
        }
        require.resolve = function (request) {
            return resolveFilename(request, module);
        };
        require.resolve.paths = function (request) {
            return isRelative(request) || builtinName(request) ? null : module.paths.slice();
        };
        require.cache = moduleCache;
        require.main = mainModule;
        require.extensions = extensions;
        return require;
    }

    var pageFile = info.page || path.join(info.root || cwd, "index.html");
    var mainModule = new Module(pageFile, null);
    mainModule.loaded = true;
    process.mainModule = mainModule;
    var require = makeRequire(mainModule);

    // -----------------------------------------------------------------------
    // The page's globals

    window.global = window;
    window.require = require;
    window.process = process;
    window.Buffer = Buffer;
    window.__dirname = path.dirname(pageFile);
    window.__filename = pageFile;
    if (typeof window.setImmediate !== "function") {
        window.setImmediate = setImmediate;
        window.clearImmediate = clearImmediate;
    }
    window.cep_node = {
        require: require,
        process: process,
        Buffer: Buffer,
        global: window,
        __dirname: window.__dirname,
        __filename: pageFile
    };
})();
