// LoadVars for ActionScript 1 and 2 (SWF 6 and later): variables sent and
// loaded as URL-encoded text. Loading goes through the player
// ($phoenix.xml.load, as XML's does): onData has the text, and by default
// decodes it and calls onLoad.

(function() {
    var player = _global.$phoenix;
    if (player.version < 6) {
        return;
    }
    var loader = player.xml;

    var LoadVars = function() {
    };
    _global.LoadVars = LoadVars;
    var p = LoadVars.prototype;

    // The names in for-in's order (the last made first), as the player
    // writes them.
    var names = function(object) {
        var out = [];
        for (var name in object) {
            out.push(name);
        }
        return out;
    };
    // name=value pairs, escaped, joined by &.
    var encoded = function(object) {
        var list = names(object);
        var out = "";
        for (var i = 0; i < list.length; i++) {
            if (i > 0) {
                out += "&";
            }
            out += escape(list[i]) + "=" + escape(String(object[list[i]]));
        }
        return out;
    };
    var startLoading = function(target) {
        target._bytesLoaded = 0;
        target._bytesTotal = undefined;
        target.loaded = false;
        ASSetPropFlags(target, ["_bytesLoaded", "_bytesTotal", "loaded"], 3, 0);
    };
    var isGet = function(method) {
        return String(method).toUpperCase() == "GET";
    };

    p.load = function(url) {
        if (arguments.length == 0) {
            return false;
        }
        startLoading(this);
        loader.load(this, String(url));
        return true;
    };
    p.send = function(url, window, method) {
        if (arguments.length == 0) {
            return false;
        }
        var query = encoded(this);
        var address = String(url);
        getURL(query == "" ? address : address + (address.indexOf("?") >= 0 ? "&" : "?") + query,
            window == undefined ? "" : String(window));
        return true;
    };
    p.sendAndLoad = function(url, target, method) {
        if (!(target instanceof Object)) {
            return false;
        }
        startLoading(target);
        var query = encoded(this);
        var address = String(url);
        if (isGet(method)) {
            loader.load(target, query == "" ? address : address + (address.indexOf("?") >= 0 ? "&" : "?") + query);
        } else {
            loader.load(target, address, query);
        }
        return true;
    };
    // Each name=value pair of URL-encoded text set on it.
    p.decode = function(text) {
        if (arguments.length == 0) {
            return false;
        }
        var pairs = String(text).split("&");
        for (var i = 0; i < pairs.length; i++) {
            var pair = pairs[i];
            if (pair == "") {
                continue;
            }
            var equals = pair.indexOf("=");
            var name = equals >= 0 ? pair.substr(0, equals) : pair;
            var value = equals >= 0 ? pair.substr(equals + 1) : "";
            this[unescape(name.split("+").join(" "))] = unescape(value.split("+").join(" "));
        }
    };
    p.getBytesLoaded = function() {
        return this._bytesLoaded;
    };
    p.getBytesTotal = function() {
        return this._bytesTotal;
    };
    p.toString = function() {
        return encoded(this);
    };
    p.contentType = "application/x-www-form-urlencoded";
    p.onLoad = function(success) {
    };
    p.onData = function(text) {
        var success = text !== undefined && text !== null;
        if (success) {
            this.decode(text);
            this.loaded = true;
        }
        this.onLoad(success);
    };
    p.addRequestHeader = function(name, value) {
    };
    ASSetPropFlags(p, null, 3, 0);
})();
