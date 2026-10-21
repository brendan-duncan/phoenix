// SharedObject for ActionScript 1 and 2 (SWF 6 and later): getLocal's
// object, its data kept by the player ($phoenix.sharedObject) in a .sol
// file and read back the next time the movie runs.

(function() {
    var player = _global.$phoenix;
    if (player.version < 6) {
        return;
    }
    var natives = player.sharedObject;
    var hide = function(object, names) {
        ASSetPropFlags(object, names, 1, 0);
    };

    var SharedObject = function() {
    };
    _global.SharedObject = SharedObject;
    var p = SharedObject.prototype;
    var opened = {};

    // Names with any of these are not allowed: null.
    var invalid = "~%&\\;:\"',<>?# ";
    SharedObject.getLocal = function(name, localPath, secure) {
        var text = String(name);
        for (var i = 0; i < invalid.length; i++) {
            if (text.indexOf(invalid.charAt(i)) >= 0) {
                return null;
            }
        }
        var where = localPath == undefined ? "" : String(localPath);
        var key = where + "/" + text;
        if (opened[key] != undefined) {
            return opened[key];
        }
        var made = new SharedObject();
        made.$name = text;
        made.$path = where;
        hide(made, ["$name", "$path"]);
        made.data = natives.load(text, where);
        opened[key] = made;
        return made;
    };
    SharedObject.getRemote = function() {
        return null;
    };
    SharedObject.deleteAll = function() {
        return false;
    };
    SharedObject.getDiskUsage = function() {
        return 0;
    };
    hide(SharedObject, ["deleteAll", "getDiskUsage"]);

    p.flush = function(minDiskSpace) {
        if (this.$name == undefined) {
            return undefined;
        }
        return natives.save(this.$name, this.$path, this.data);
    };
    p.getSize = function() {
        if (this.$name == undefined) {
            return undefined;
        }
        return natives.size(this.$name, this.data);
    };
    p.clear = function() {
        var data = this.data;
        for (var name in data) {
            delete data[name];
        }
        if (this.$name != undefined) {
            natives.save(this.$name, this.$path, data);
        }
    };
    p.close = function() {
    };
    p.connect = function() {
        return false;
    };
    p.send = function() {
    };
    p.setFps = function() {
        return false;
    };
    p.onStatus = function() {
    };
    p.onSync = function() {
    };
    ASSetPropFlags(p, null, 3, 0);
})();
