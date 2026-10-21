// MovieClip.prototype's methods that ask the player ($phoenix.clip):
// depths, where things are, the drawing API and how a clip looks. Each is
// there from the SWF version that has it, hidden from for-in and kept from
// delete as the player's own are.

(function() {
    var natives = _global.$phoenix.clip;
    var version = _global.$phoenix.version;
    var p = MovieClip.prototype;
    var names = [];
    var method = function(name, since) {
        if (version >= since) {
            p[name] = natives[name];
            names.push(name);
        }
    };
    var property = function(name, since, setter) {
        if (version >= since) {
            p.addProperty(name, natives[name], setter == undefined ? null : natives[setter]);
            names.push(name);
        }
    };

    method("attachMovie", 5);
    method("swapDepths", 5);
    method("localToGlobal", 5);
    method("globalToLocal", 5);
    method("getBounds", 5);
    method("duplicateMovieClip", 5);
    method("removeMovieClip", 5);
    method("getSWFVersion", 5);
    method("createTextField", 5);
    method("setMask", 6);
    method("createEmptyMovieClip", 6);
    method("beginFill", 6);
    method("moveTo", 6);
    method("lineTo", 6);
    method("curveTo", 6);
    method("lineStyle", 6);
    method("endFill", 6);
    method("clear", 6);
    method("beginGradientFill", 6);
    method("getNextHighestDepth", 7);
    method("getInstanceAtDepth", 7);
    method("lineGradientStyle", 8);
    if (version >= 6) {
        p.getDepth = natives.getDepth;
        ASSetPropFlags(p, "getDepth", 7);
    }
    if (version >= 8) {
        p.getRect = natives.getBounds;
        p.beginBitmapFill = function(bitmap, matrix, repeat, smooth) {
            var holder = bitmap == undefined ? undefined : bitmap.$bitmap;
            natives.beginBitmapFill.apply(this, [holder].concat(arguments.slice(1)));
        };
        p.attachBitmap = function(bitmap, depth, pixelSnapping, smoothing) {
            var holder = bitmap == undefined ? undefined : bitmap.$bitmap;
            natives.attachBitmap.apply(this, [holder].concat(arguments.slice(1)));
        };
        names.push("getRect", "beginBitmapFill", "attachBitmap");
    }
    property("blendMode", 8, "setBlendMode");
    property("cacheAsBitmap", 8, "setCacheAsBitmap");
    property("opaqueBackground", 8, "setOpaqueBackground");
    if (version >= 8) {
        p.addProperty("scrollRect", function() {
            var box = natives.scrollRect.call(this);
            if (box == undefined) {
                return undefined;
            }
            return new flash.geom.Rectangle(box[0], box[1], box[2], box[3]);
        }, natives.setScrollRect);
        names.push("scrollRect");
    }
    p.useHandCursor = true;
    p.enabled = true;
    ASSetPropFlags(p, names, 3);
    ASSetPropFlags(p, ["useHandCursor", "enabled"], 1);
})();
