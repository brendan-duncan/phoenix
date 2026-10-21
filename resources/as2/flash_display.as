// flash.display.BitmapData for ActionScript 2: the pixels are the
// player's ($phoenix.bitmap), held by an object kept out of sight on each
// bitmap ($bitmap).

(function() {
    // flash.* is there from SWF 8.
    if (_global.$phoenix.version < 8) {
        return;
    }
    var natives = _global.$phoenix.bitmap;
    var noObject = _global.$phoenix.noObject;
    var make = natives.make;
    if (_global.flash == undefined) {
        _global.flash = {};
    }
    if (_global.flash.display == undefined) {
        _global.flash.display = {};
    }

    var hidden = function(object) {
        ASSetPropFlags(object, ["$bitmap", "__proto__"], 1, 0);
        return object;
    };
    // A bitmap of pixels a method made, with the prototype given.
    var bitmapOf = function(held, proto) {
        var made = new Object();
        made.__proto__ = proto;
        made.$bitmap = held;
        return hidden(made);
    };

    // A rectangle of whatever flash.geom.Rectangle is now; -1 when it is
    // not a class.
    var rectangleOf = function(x, y, width, height) {
        var Rectangle = flash.geom.Rectangle;
        if (typeof Rectangle != "function") {
            return -1;
        }
        return new Rectangle(x, y, width, height);
    };

    var BitmapData = function(width, height, transparent, fillColor) {
        if (!make.apply(this, arguments)) {
            return noObject;
        }
        hidden(this);
    };
    flash.display.BitmapData = BitmapData;
    var p = BitmapData.prototype;

    p.addProperty("width", natives.width, null);
    p.addProperty("height", natives.height, null);
    p.addProperty("rectangle", function() {
        var width = natives.width.call(this);
        if (width == -1) {
            return -1;
        }
        return rectangleOf(0, 0, width, natives.height.call(this));
    }, null);
    p.addProperty("transparent", natives.transparent, null);
    p.getPixel = natives.getPixel;
    p.setPixel = natives.setPixel;
    p.fillRect = natives.fillRect;
    p.copyPixels = natives.copyPixels;
    p.applyFilter = natives.applyFilter;
    p.scroll = natives.scroll;
    p.threshold = natives.threshold;
    p.draw = natives.draw;
    p.pixelDissolve = natives.pixelDissolve;
    p.getPixel32 = natives.getPixel32;
    p.setPixel32 = natives.setPixel32;
    p.floodFill = natives.floodFill;
    p.getColorBoundsRect = function(mask, color, findColor) {
        var box = natives.getColorBoundsRect.apply(this, arguments);
        if (!(box instanceof Array)) {
            return box;
        }
        return rectangleOf(box[0], box[1], box[2], box[3]);
    };
    p.perlinNoise = natives.perlinNoise;
    p.colorTransform = natives.colorTransform;
    p.hitTest = natives.hitTest;
    p.paletteMap = natives.paletteMap;
    p.merge = natives.merge;
    p.noise = natives.noise;
    p.copyChannel = natives.copyChannel;
    p.clone = function() {
        var held = natives.clone.call(this);
        return typeof held == "object" ? bitmapOf(held, this.__proto__) : held;
    };
    p.dispose = natives.dispose;
    p.generateFilterRect = function(sourceRect, filter) {
        var box = natives.generateFilterRect.apply(this, arguments);
        if (!(box instanceof Array)) {
            return box;
        }
        return rectangleOf(box[0], box[1], box[2], box[3]);
    };
    p.compare = function(other) {
        var held = natives.compare.apply(this, arguments);
        return typeof held == "object" ? bitmapOf(held, this.__proto__) : held;
    };

    BitmapData.RED_CHANNEL = 1;
    BitmapData.GREEN_CHANNEL = 2;
    BitmapData.BLUE_CHANNEL = 4;
    BitmapData.ALPHA_CHANNEL = 8;
    BitmapData.loadBitmap = function(name) {
        var held = natives.loadBitmap(name);
        return typeof held == "object" ? bitmapOf(held, this.prototype) : undefined;
    };
})();
