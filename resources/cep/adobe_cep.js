// What an HTML extension panel's CSInterface.js expects to find in its page:
// window.__adobe_cep__ (the host's side of CSInterface) and window.cep (the
// file system and browser helpers). Phoenix runs this before the panel's own
// scripts. Calls that answer at once go to Phoenix through a prompt the web
// view hands over; evalScript and menus answer later through
// window.__phoenixCep.

(function () {
    "use strict";
    if (window.__adobe_cep__)
        return;

    var MARK = "\u0001phoenix-call\u0001";

    function call(name, args) {
        var answer = window.prompt(MARK + JSON.stringify({ name: name, args: args || [] }), "");
        if (answer === null || answer === undefined || answer === "")
            return undefined;
        var reply = JSON.parse(answer);
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

    window.__phoenixCep = {
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

    function unsupported() {
        return { err: ERR.ERR_UNKNOWN };
    }

    window.cep = {
        encoding: { UTF8: "UTF-8", Base64: "Base64" },
        fs: fs,
        process: {
            createProcess: unsupported, stdout: unsupported, stderr: unsupported, writeStdin: unsupported,
            getWorkingDirectory: unsupported, waitfor: unsupported, onquit: unsupported, isRunning: unsupported,
            terminate: unsupported
        },
        util: {
            openURLInDefaultBrowser: function (url) { return call("openURLInDefaultBrowser", [String(url)]); },
            registerExtensionUnloadCallback: function () {},
            storeProxyCredentials: function () {}
        }
    };
    window.cep_node = undefined;
})();
