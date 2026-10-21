// AsBroadcaster: what Key, Mouse, Stage, text fields and MovieClipLoader
// tell their listeners by. Its methods go through what an object has
// (removeListener, push, splice), so a script that changes them changes how
// listening works, as in the player.

(function() {
    // broadcastMessage(name, args...): each listener there was as it
    // started, null and undefined left out; true when there was one.
    var broadcaster = function() {
        return function(name) {
            if (arguments.length == 0) {
                return undefined;
            }
            var listeners = this._listeners;
            if (typeof listeners != "object" && typeof listeners != "function" || listeners === null) {
                return undefined;
            }
            var each = [];
            var count = listeners.length;
            for (var i = 0; i < count; i++) {
                each.push(listeners[i]);
            }
            var given = arguments.slice(1);
            var any = false;
            for (var i = 0; i < each.length; i++) {
                var listener = each[i];
                if (listener === undefined || listener === null) {
                    continue;
                }
                any = true;
                var method = listener[name];
                if (typeof method == "function") {
                    method.apply(listener, given);
                }
            }
            return any ? true : undefined;
        };
    };

    var AsBroadcaster = function() {
    };
    _global.AsBroadcaster = AsBroadcaster;
    AsBroadcaster.broadcastMessage = broadcaster();
    // One listed already goes to the end.
    AsBroadcaster.addListener = function(listener) {
        this.removeListener(listener);
        this._listeners.push(listener);
        return true;
    };
    // The first that equals it (==), taken out.
    AsBroadcaster.removeListener = function(listener) {
        var listeners = this._listeners;
        for (var i = 0; i < listeners.length; i++) {
            if (listeners[i] == listener) {
                listeners.splice(i, 1);
                return true;
            }
        }
        return false;
    };
    AsBroadcaster.initialize = function(object) {
        object.broadcastMessage = broadcaster();
        object.addListener = AsBroadcaster.addListener;
        object.removeListener = AsBroadcaster.removeListener;
        object._listeners = [];
        ASSetPropFlags(object, "broadcastMessage,addListener,removeListener,_listeners", 3);
    };
    ASSetPropFlags(AsBroadcaster, null, 3);

    // The player's own broadcasters.
    var own = [Selection, Mouse, Key];
    for (var i = 0; i < own.length; i++) {
        if (typeof own[i] == "object") {
            AsBroadcaster.initialize(own[i]);
        }
    }
})();
