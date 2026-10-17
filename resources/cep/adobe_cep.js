// What an HTML extension panel's CSInterface.js expects to find in its page:
// window.__adobe_cep__ (the host's side of CSInterface) and window.cep (the
// file system, processes and browser helpers). Phoenix runs this before the
// panel's own scripts, and node.js after it for a panel that asks for
// Node.js. Calls that answer at once go to Phoenix through a prompt the web
// view hands over; evalScript, menus and what processes write answer later
// through window.__phoenixCep.

(function () {
    "use strict";
    if (window.__adobe_cep__)
        return;

    var MARK = "\u0001phoenix-call\u0001";

    // The reply to a call as it comes: {value} or {error, code, ...}.
    function request(name, args) {
        var answer = window.prompt(MARK + JSON.stringify({ name: name, args: args || [] }), "");
        if (answer === null || answer === undefined || answer === "")
            return {};
        return JSON.parse(answer);
    }

    function call(name, args) {
        var reply = request(name, args);
        if (reply.error)
            throw new Error(reply.error);
        return reply.value;
    }

    var callbacks = {};
    var nextCallback = 1;
    function later(callback) {
        if (typeof callback !== "function")
            return 0;
        var id = nextCallback++;
        callbacks[id] = callback;
        return id;
    }

    // type -> [{ listener, scope }]
    var listeners = {};
    var flyoutMenu = null;
    var contextMenu = null;
    var contextMenuCallback = null;

    function deliver(event) {
        var list = (listeners[event.type] || []).slice();
        for (var i = 0; i < list.length; i++) {
            try {
                if (list[i].scope)
                    list[i].listener.call(list[i].scope, event);
                else
                    list[i].listener(event);
            } catch (e) {
                if (window.console)
                    console.error(e);
            }
        }
    }

    // What running processes write, and their ends, by process: the
    // handlers cep.process and child_process set.
    var processHandlers = {};

    window.__phoenixCep = {
        request: request,
        processHandlers: processHandlers,
        processEvents: function (events) {
            for (var i = 0; i < events.length; i++) {
                var handler = processHandlers[events[i].id];
                if (!handler)
                    continue;
                try {
                    handler(events[i]);
                } catch (e) {
                    if (window.console)
                        console.error(e);
                }
            }
        },
        resolve: function (id, result) {
            var callback = callbacks[id];
            delete callbacks[id];
            if (callback)
                callback(result);
        },
        dispatch: function (event) {
            deliver(event);
        },
        contextMenuClicked: function (menuId) {
            if (contextMenuCallback)
                contextMenuCallback(menuId);
        }
    };

    window.__adobe_cep__ = {
        getHostEnvironment: function () { return call("getHostEnvironment"); },
        getHostCapabilities: function () { return call("getHostCapabilities"); },
        getCurrentApiVersion: function () { return call("getCurrentApiVersion"); },
        getExtensionId: function () { return call("getExtensionId"); },
        getSystemPath: function (pathType) { return call("getSystemPath", [String(pathType)]); },
        getExtensions: function (ids) { return call("getExtensions", [ids === undefined ? "" : String(ids)]); },
        getNetworkPreferences: function () { return call("getNetworkPreferences"); },
        initResourceBundle: function () { return call("initResourceBundle"); },
        dumpInstallationInfo: function () { return ""; },
        loadSnapshot: function () {},
        getCurrentImsUserId: function () { return ""; },
        getScaleFactor: function () { return window.devicePixelRatio || 1; },
        getMonitorScaleFactor: function () { return window.devicePixelRatio || 1; },
        setScaleFactorChangedHandler: function () {},
        registerKeyEventsInterest: function () { return true; },
        registerInvalidCertificateCallback: function () {},
        setWindowTitle: function (title) { call("setWindowTitle", [String(title)]); },
        getWindowTitle: function () { return call("getWindowTitle"); },
        closeExtension: function () { call("closeExtension"); },
        requestOpenExtension: function (id, params) { call("requestOpenExtension", [String(id), params === undefined ? "" : String(params)]); },
        resizeContent: function (width, height) { call("resizeContent", [Number(width), Number(height)]); },

        evalScript: function (script, callback) {
            call("evalScript", [later(callback), String(script)]);
        },

        addEventListener: function (type, listener, scope) {
            if (typeof listener !== "function")
                return;
            (listeners[type] = listeners[type] || []).push({ listener: listener, scope: scope });
            call("addEventListener", [String(type)]);
        },
        removeEventListener: function (type, listener, scope) {
            var list = listeners[type] || [];
            for (var i = list.length - 1; i >= 0; i--) {
                if (list[i].listener === listener && list[i].scope === scope)
                    list.splice(i, 1);
            }
        },
        dispatchEvent: function (event) {
            var data = event.data;
            if (typeof data === "object" && data !== null)
                data = JSON.stringify(data);
            call("dispatchEvent", [{
                type: String(event.type),
                scope: event.scope || "APPLICATION",
                appId: event.appId || "",
                extensionId: event.extensionId || "",
                data: data === undefined ? "" : String(data)
            }]);
        },

        invokeSync: function (name, argument) {
            switch (name) {
            case "setPanelFlyoutMenu":
                flyoutMenu = argument;
                call("setPanelFlyoutMenu", [String(argument)]);
                return "";
            case "updatePanelMenuItem":
                call("updatePanelMenuItem", [String(argument)]);
                return "true";
            case "updateContextMenuItem":
                return "true";
            case "isWindowVisible":
                return call("isWindowVisible") ? "true" : "false";
            case "setWindowTitle":
                call("setWindowTitle", [String(argument)]);
                return "";
            case "getWindowTitle":
                return call("getWindowTitle");
            default:
                // Messages between applications (Vulcan): no other
                // application answers here.
                if (/^vulcan/.test(name)) {
                    var none = /GetEndPoints|GetTargetSpecifiers/.test(name) ? [] : (/GetAppPath/.test(name) ? "" : false);
                    return JSON.stringify({ result: none });
                }
                return "";
            }
        },
        invokeAsync: function (name, argument, callback) {
            if (name === "setContextMenu" || name === "setContextMenuByJSON") {
                contextMenu = { kind: name === "setContextMenu" ? "xml" : "json", menu: String(argument) };
                contextMenuCallback = callback;
            }
        }
    };

    // A context menu the panel set comes up in place of the page's own.
    document.addEventListener("contextmenu", function (event) {
        if (!contextMenu)
            return;
        event.preventDefault();
        call("showContextMenu", [contextMenu.kind, contextMenu.menu]);
    }, true);

    var ERR = {
        NO_ERROR: 0, ERR_UNKNOWN: 1, ERR_INVALID_PARAMS: 2, ERR_NOT_FOUND: 3, ERR_CANT_READ: 4,
        ERR_UNSUPPORTED_ENCODING: 5, ERR_CANT_WRITE: 6, ERR_OUT_OF_SPACE: 7, ERR_NOT_FILE: 8,
        ERR_NOT_DIRECTORY: 9, ERR_FILE_EXISTS: 10
    };

    function fsCall(name, args) {
        try {
            return call("fs." + name, args);
        } catch (e) {
            return { err: ERR.ERR_UNKNOWN };
        }
    }

    var fs = {
        showOpenDialog: function (allowMultiple, chooseDirectory, title, initialPath, fileTypes) {
            return fsCall("showOpenDialog", [!!allowMultiple, !!chooseDirectory, title || "", initialPath || "", fileTypes || []]);
        },
        showOpenDialogEx: function (allowMultiple, chooseDirectory, title, initialPath, fileTypes) {
            return fsCall("showOpenDialog", [!!allowMultiple, !!chooseDirectory, title || "", initialPath || "", fileTypes || []]);
        },
        showSaveDialogEx: function (title, initialPath, fileTypes, defaultName) {
            return fsCall("showSaveDialog", [title || "", initialPath || "", fileTypes || [], defaultName || ""]);
        },
        readdir: function (path) { return fsCall("readdir", [String(path)]); },
        makedir: function (path) { return fsCall("makedir", [String(path)]); },
        rename: function (from, to) { return fsCall("rename", [String(from), String(to)]); },
        stat: function (path) {
            var result = fsCall("stat", [String(path)]);
            if (result && result.data) {
                var info = result.data;
                result.data = {
                    isFile: function () { return info.file; },
                    isDirectory: function () { return info.directory; },
                    mtime: new Date(info.mtime)
                };
            }
            return result;
        },
        readFile: function (path, encoding) { return fsCall("readFile", [String(path), encoding || "UTF-8"]); },
        writeFile: function (path, data, encoding) { return fsCall("writeFile", [String(path), String(data), encoding || "UTF-8"]); },
        chmod: function (path, mode) { return fsCall("chmod", [String(path), Number(mode)]); },
        deleteFile: function (path) { return fsCall("deleteFile", [String(path)]); }
    };
    for (var key in ERR)
        fs[key] = ERR[key];

    // cep.process: programs run with their output handed to callbacks.
    // A process is known by the number createProcess gives.
    var processes = {};

    function decodeOutput(base64) {
        var binary = window.atob(base64);
        try {
            return decodeURIComponent(escape(binary));
        } catch (e) {
            return binary;
        }
    }

    function processState(id) {
        var state = processes[id];
        if (!state) {
            state = processes[id] = { stdout: null, stderr: null, onquit: null, pending: { stdout: [], stderr: [] }, ended: false };
            processHandlers[id] = function (event) {
                if (event.kind === "exit") {
                    state.ended = true;
                    if (state.onquit)
                        state.onquit(event.code);
                    return;
                }
                var text = decodeOutput(event.data);
                if (state[event.kind])
                    state[event.kind](text);
                else
                    state.pending[event.kind].push(text);
            };
        }
        return state;
    }

    function processCall(name, args) {
        var reply = request("proc." + name, args);
        if (reply.error)
            return { err: reply.code === "ENOENT" ? ERR.ERR_NOT_FOUND : ERR.ERR_UNKNOWN };
        return { err: ERR.NO_ERROR, data: reply.value };
    }

    function outputCallback(kind) {
        return function (pid, callback) {
            if (!processes[pid] || typeof callback !== "function")
                return { err: ERR.ERR_INVALID_PARAMS };
            var state = processes[pid];
            state[kind] = callback;
            var pending = state.pending[kind];
            state.pending[kind] = [];
            for (var i = 0; i < pending.length; i++)
                callback(pending[i]);
            return { err: ERR.NO_ERROR };
        };
    }

    var process = {
        createProcess: function () {
            if (arguments.length < 1)
                return { err: ERR.ERR_INVALID_PARAMS };
            var args = [];
            for (var i = 1; i < arguments.length; i++)
                args.push(String(arguments[i]));
            var result = processCall("spawn", [String(arguments[0]), args, "", null, false]);
            if (result.err)
                return result;
            processState(result.data.id);
            return { err: ERR.NO_ERROR, data: result.data.id };
        },
        stdout: outputCallback("stdout"),
        stderr: outputCallback("stderr"),
        writeStdin: function (pid, data) {
            var bytes = unescape(encodeURIComponent(String(data)));
            var result = processCall("write", [Number(pid), window.btoa(bytes)]);
            return { err: result.err };
        },
        getWorkingDirectory: function (pid) { return processCall("cwd", [Number(pid)]); },
        waitfor: function (pid) { return processCall("wait", [Number(pid), -1]); },
        onquit: function (pid, callback) {
            if (!processes[pid] || typeof callback !== "function")
                return { err: ERR.ERR_INVALID_PARAMS };
            processes[pid].onquit = callback;
            return { err: ERR.NO_ERROR };
        },
        isRunning: function (pid) { return processCall("running", [Number(pid)]); },
        terminate: function (pid) { return processCall("kill", [Number(pid)]); }
    };

    window.cep = {
        encoding: { UTF8: "UTF-8", Base64: "Base64" },
        fs: fs,
        process: process,
        util: {
            openURLInDefaultBrowser: function (url) { return call("openURLInDefaultBrowser", [String(url)]); },
            registerExtensionUnloadCallback: function () {},
            storeProxyCredentials: function () {}
        }
    };
})();
