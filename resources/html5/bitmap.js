// flash.display.Bitmap and BitmapData. A BitmapData is a canvas; its
// pixels are read into an ImageData for the per-pixel methods and written
// back before it is next drawn or drawn into.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;
var Rectangle = flash.geom.Rectangle;

/// The library bitmap a BitmapData class is linked to (README.md).
phoenix._.bitmapForClass = phoenix._.bitmapForClass || new Map();

class BitmapData {
    constructor(width, height, transparent, fillColor) {
        var linked = null;
        for (var at = new.target; at && at !== BitmapData && !linked; at = Object.getPrototypeOf(at))
            linked = phoenix._.bitmapForClass.get(at) || null;
        this._transparent = transparent === undefined ? true : !!transparent;
        this._disposed = false;
        this._data = null;
        this._dataDirty = false;
        this._locked = false;
        Object.defineProperty(this, "_phoenixRevision", {value: 0, writable: true});
        if (linked !== null) {
            // A library bitmap: its picture, whatever size was asked for.
            var image = phoenix._.images[linked];
            width = image ? image.width : 1;
            height = image ? image.height : 1;
            this._canvas = phoenix.makeCanvas(width, height);
            if (image)
                this._canvas.getContext("2d").drawImage(image, 0, 0);
            return;
        }
        width = width | 0;
        height = height | 0;
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
            throw phoenix.as3Error("ArgumentError", 2015, "Invalid BitmapData.");
        this._canvas = phoenix.makeCanvas(width, height);
        var fill = fillColor === undefined ? 0xffffffff : fillColor >>> 0;
        if (!this._transparent)
            fill = (fill | 0xff000000) >>> 0;
        if (fill >>> 24)
            this._fillCanvas(0, 0, width, height, fill);
    }
    get width() { return this._canvas ? this._canvas.width : 0; }
    get height() { return this._canvas ? this._canvas.height : 0; }
    get transparent() { return this._transparent; }
    get rect() { return new Rectangle(0, 0, this.width, this.height); }
    _ctx() { return this._canvas.getContext("2d"); }
    _check() {
        if (this._disposed)
            throw phoenix.as3Error("ArgumentError", 2015, "Invalid BitmapData.");
    }
    /// The pixels, read once and kept while they are worked on.
    _pixels() {
        this._check();
        if (!this._data)
            this._data = this._ctx().getImageData(0, 0, this.width, this.height);
        return this._data;
    }
    _touched() {
        this._phoenixRevision++;
        phoenix._.requestDraw && phoenix._.requestDraw();
    }
    /// The pixels written back to the canvas, for drawing.
    _flush() {
        if (this._data && this._dataDirty) {
            this._ctx().putImageData(this._data, 0, 0);
            this._dataDirty = false;
        }
    }
    /// Before the canvas is drawn into: the pixels written back and let go.
    _canvasOp() {
        this._check();
        this._flush();
        this._data = null;
    }
    _canvasForDraw() {
        this._flush();
        return this._canvas;
    }
    _fillCanvas(x, y, width, height, argb) {
        var ctx = this._ctx();
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(x, y, width, height);
        var alpha = this._transparent ? (argb >>> 24) / 255 : 1;
        if (alpha > 0) {
            ctx.fillStyle = C.css(argb & 0xffffff, alpha, null);
            ctx.fillRect(x, y, width, height);
        }
        ctx.restore();
    }
    getPixel(x, y) { return this.getPixel32(x, y) & 0xffffff; }
    getPixel32(x, y) {
        x = x | 0;
        y = y | 0;
        if (x < 0 || y < 0 || x >= this.width || y >= this.height)
            return 0;
        var d = this._pixels().data, at = (y * this.width + x) * 4;
        return ((d[at + 3] << 24) | (d[at] << 16) | (d[at + 1] << 8) | d[at + 2]) >>> 0;
    }
    setPixel(x, y, color) {
        var alpha = this._transparent ? this.getPixel32(x, y) >>> 24 : 255;
        this.setPixel32(x, y, ((alpha << 24) | (color & 0xffffff)) >>> 0);
    }
    setPixel32(x, y, color) {
        x = x | 0;
        y = y | 0;
        if (x < 0 || y < 0 || x >= this.width || y >= this.height)
            return;
        var d = this._pixels().data, at = (y * this.width + x) * 4;
        color = color >>> 0;
        d[at] = (color >> 16) & 255;
        d[at + 1] = (color >> 8) & 255;
        d[at + 2] = color & 255;
        d[at + 3] = this._transparent ? color >>> 24 : 255;
        this._dataDirty = true;
        if (!this._locked)
            this._touched();
    }
    lock() { this._locked = true; }
    unlock(changeRect) {
        this._locked = false;
        this._touched();
    }
    fillRect(rect, color) {
        this._canvasOp();
        color = color >>> 0;
        if (!this._transparent)
            color = (color | 0xff000000) >>> 0;
        this._fillCanvas(rect.x, rect.y, rect.width, rect.height, color);
        this._touched();
    }
    copyPixels(sourceBitmapData, sourceRect, destPoint, alphaBitmapData, alphaPoint, mergeAlpha) {
        this._canvasOp();
        var source = sourceBitmapData._canvasForDraw();
        var ctx = this._ctx();
        var sx = Math.round(sourceRect.x), sy = Math.round(sourceRect.y);
        var w = Math.round(sourceRect.width), h = Math.round(sourceRect.height);
        var dx = Math.round(destPoint.x), dy = Math.round(destPoint.y);
        // Clipped to the source.
        if (sx < 0) { dx -= sx; w += sx; sx = 0; }
        if (sy < 0) { dy -= sy; h += sy; sy = 0; }
        w = Math.min(w, source.width - sx);
        h = Math.min(h, source.height - sy);
        if (w <= 0 || h <= 0)
            return;
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        if (!mergeAlpha || !this._transparent)
            ctx.clearRect(dx, dy, w, h);
        if (alphaBitmapData) {
            var masked = phoenix.makeCanvas(w, h), mctx = masked.getContext("2d");
            mctx.drawImage(source, sx, sy, w, h, 0, 0, w, h);
            mctx.globalCompositeOperation = "destination-in";
            var ax = alphaPoint ? alphaPoint.x : 0, ay = alphaPoint ? alphaPoint.y : 0;
            mctx.drawImage(alphaBitmapData._canvasForDraw(), ax, ay, w, h, 0, 0, w, h);
            ctx.drawImage(masked, dx, dy);
        } else
            ctx.drawImage(source, sx, sy, w, h, dx, dy, w, h);
        ctx.restore();
        if (!this._transparent)
            this._opaque(dx, dy, w, h);
        this._touched();
    }
    /// An opaque bitmap keeps no alpha: what was drawn goes over black.
    _opaque(x, y, w, h) {
        var ctx = this._ctx();
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.globalCompositeOperation = "destination-over";
        ctx.fillStyle = "#000";
        ctx.fillRect(x, y, w, h);
        ctx.restore();
    }
    draw(source, matrix, colorTransform, blendMode, clipRect, smoothing) {
        this._canvasOp();
        phoenix._.drawInto(this._canvas, source, matrix ? M.fromMatrix(matrix) : [1, 0, 0, 1, 0, 0], colorTransform || null,
            blendMode || null, clipRect || null, !!smoothing);
        if (!this._transparent)
            this._opaque(0, 0, this.width, this.height);
        this._touched();
    }
    drawWithQuality(source, matrix, colorTransform, blendMode, clipRect, smoothing, quality) {
        this.draw(source, matrix, colorTransform, blendMode, clipRect, smoothing);
    }
    clone() {
        this._flush();
        var copy = new BitmapData(this.width, this.height, this._transparent, 0);
        var ctx = copy._ctx();
        ctx.clearRect(0, 0, copy.width, copy.height);
        ctx.drawImage(this._canvas, 0, 0);
        return copy;
    }
    dispose() {
        this._disposed = true;
        this._data = null;
        if (this._canvas) {
            this._canvas.width = this._canvas.height = 1;
        }
    }
    colorTransform(rect, colorTransform) {
        var c = C.fromTransform(colorTransform);
        var image = this._pixels(), d = image.data, w = this.width;
        var x0 = Math.max(0, rect.x | 0), y0 = Math.max(0, rect.y | 0);
        var x1 = Math.min(w, Math.ceil(rect.x + rect.width)), y1 = Math.min(this.height, Math.ceil(rect.y + rect.height));
        for (var y = y0; y < y1; ++y)
            for (var x = x0; x < x1; ++x) {
                var at = (y * w + x) * 4;
                d[at] = d[at] * c[0] + c[4];
                d[at + 1] = d[at + 1] * c[1] + c[5];
                d[at + 2] = d[at + 2] * c[2] + c[6];
                if (this._transparent)
                    d[at + 3] = d[at + 3] * c[3] + c[7];
            }
        this._dataDirty = true;
        this._touched();
    }
    floodFill(x, y, color) {
        x = x | 0;
        y = y | 0;
        var w = this.width, h = this.height;
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        var d = this._pixels().data;
        var at = (y * w + x) * 4;
        var target = [d[at], d[at + 1], d[at + 2], d[at + 3]];
        color = color >>> 0;
        var fill = [(color >> 16) & 255, (color >> 8) & 255, color & 255, this._transparent ? color >>> 24 : 255];
        if (target[0] === fill[0] && target[1] === fill[1] && target[2] === fill[2] && target[3] === fill[3])
            return;
        var same = function (p) { return d[p] === target[0] && d[p + 1] === target[1] && d[p + 2] === target[2] && d[p + 3] === target[3]; };
        var stack = [x, y];
        while (stack.length) {
            var py = stack.pop(), px = stack.pop();
            var left = px;
            while (left > 0 && same((py * w + left - 1) * 4))
                --left;
            var above = false, below = false;
            for (var cx = left; cx < w && same((py * w + cx) * 4); ++cx) {
                var p = (py * w + cx) * 4;
                d[p] = fill[0]; d[p + 1] = fill[1]; d[p + 2] = fill[2]; d[p + 3] = fill[3];
                if (py > 0) {
                    var up = same(((py - 1) * w + cx) * 4);
                    if (up && !above) stack.push(cx, py - 1);
                    above = up;
                }
                if (py < h - 1) {
                    var down = same(((py + 1) * w + cx) * 4);
                    if (down && !below) stack.push(cx, py + 1);
                    below = down;
                }
            }
        }
        this._dataDirty = true;
        this._touched();
    }
    getColorBoundsRect(mask, color, findColor) {
        if (findColor === undefined)
            findColor = true;
        var d = this._pixels().data, w = this.width, h = this.height;
        var x0 = w, y0 = h, x1 = -1, y1 = -1;
        mask = mask >>> 0;
        color = color >>> 0;
        for (var y = 0; y < h; ++y)
            for (var x = 0; x < w; ++x) {
                var at = (y * w + x) * 4;
                var argb = ((d[at + 3] << 24) | (d[at] << 16) | (d[at + 1] << 8) | d[at + 2]) >>> 0;
                var match = ((argb & mask) >>> 0) === ((color & mask) >>> 0);
                if (match === !!findColor) {
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
            }
        return x1 < 0 ? new Rectangle() : new Rectangle(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
    scroll(x, y) {
        this._canvasOp();
        var copy = phoenix.makeCanvas(this.width, this.height);
        copy.getContext("2d").drawImage(this._canvas, 0, 0);
        var ctx = this._ctx();
        ctx.drawImage(copy, x | 0, y | 0);
        this._touched();
    }
    noise(randomSeed, low, high, channelOptions, grayScale) {
        low = low === undefined ? 0 : low | 0;
        high = high === undefined ? 255 : high | 0;
        channelOptions = channelOptions === undefined ? 7 : channelOptions | 0;
        var seed = (randomSeed | 0) || 1;
        var random = function () {
            seed = (seed * 16807) % 2147483647;
            return seed / 2147483647;
        };
        var d = this._pixels().data;
        for (var i = 0; i < d.length; i += 4) {
            var gray = low + Math.floor(random() * (high - low + 1));
            d[i] = grayScale ? gray : (channelOptions & 1 ? low + Math.floor(random() * (high - low + 1)) : 0);
            d[i + 1] = grayScale ? gray : (channelOptions & 2 ? low + Math.floor(random() * (high - low + 1)) : 0);
            d[i + 2] = grayScale ? gray : (channelOptions & 4 ? low + Math.floor(random() * (high - low + 1)) : 0);
            d[i + 3] = channelOptions & 8 && this._transparent ? low + Math.floor(random() * (high - low + 1)) : 255;
        }
        this._dataDirty = true;
        this._touched();
    }
    copyChannel(sourceBitmapData, sourceRect, destPoint, sourceChannel, destChannel) {
        var index = {1: 0, 2: 1, 4: 2, 8: 3};
        var from = index[sourceChannel], to = index[destChannel];
        if (from === undefined || to === undefined)
            return;
        var s = sourceBitmapData._pixels().data, d = this._pixels().data;
        for (var y = 0; y < sourceRect.height; ++y)
            for (var x = 0; x < sourceRect.width; ++x) {
                var sx = sourceRect.x + x, sy = sourceRect.y + y, dx = destPoint.x + x, dy = destPoint.y + y;
                if (sx < 0 || sy < 0 || sx >= sourceBitmapData.width || sy >= sourceBitmapData.height ||
                    dx < 0 || dy < 0 || dx >= this.width || dy >= this.height)
                    continue;
                d[(dy * this.width + dx) * 4 + to] = s[(sy * sourceBitmapData.width + sx) * 4 + from];
            }
        this._dataDirty = true;
        this._touched();
    }
    applyFilter(sourceBitmapData, sourceRect, destPoint, filter) {
        this._canvasOp();
        var w = Math.ceil(sourceRect.width), h = Math.ceil(sourceRect.height);
        var piece = phoenix.makeCanvas(w, h);
        piece.getContext("2d").drawImage(sourceBitmapData._canvasForDraw(), sourceRect.x, sourceRect.y, w, h, 0, 0, w, h);
        var out = filter._apply(piece, 1) || piece;
        var ctx = this._ctx();
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(destPoint.x, destPoint.y, w, h);
        ctx.drawImage(out, destPoint.x, destPoint.y);
        ctx.restore();
        this._touched();
    }
    generateFilterRect(sourceRect, filter) {
        var margin = filter._margin(1);
        return new Rectangle(sourceRect.x - margin, sourceRect.y - margin, sourceRect.width + 2 * margin, sourceRect.height + 2 * margin);
    }
    hitTest(firstPoint, firstAlphaThreshold, secondObject, secondBitmapDataPoint, secondAlphaThreshold) {
        if (secondObject && secondObject.x !== undefined && secondObject.width === undefined) {
            var px = (secondObject.x - firstPoint.x) | 0, py = (secondObject.y - firstPoint.y) | 0;
            return (this.getPixel32(px, py) >>> 24) >= (firstAlphaThreshold | 0);
        }
        if (secondObject instanceof Rectangle) {
            var r = secondObject;
            for (var y = Math.floor(r.y - firstPoint.y); y < Math.ceil(r.bottom - firstPoint.y); ++y)
                for (var x = Math.floor(r.x - firstPoint.x); x < Math.ceil(r.right - firstPoint.x); ++x)
                    if ((this.getPixel32(x, y) >>> 24) >= (firstAlphaThreshold | 0))
                        return true;
        }
        return false;
    }
    threshold(sourceBitmapData, sourceRect, destPoint, operation, threshold, color, mask, copySource) {
        mask = mask === undefined ? 0xffffffff : mask >>> 0;
        color = color >>> 0;
        threshold = threshold >>> 0;
        var tests = {"<": function (a, b) { return a < b; }, "<=": function (a, b) { return a <= b; },
            ">": function (a, b) { return a > b; }, ">=": function (a, b) { return a >= b; },
            "==": function (a, b) { return a === b; }, "!=": function (a, b) { return a !== b; }};
        var test = tests[operation];
        if (!test)
            throw phoenix.as3Error("ArgumentError", 2005, "Parameter 3 is of the incorrect type.");
        var count = 0;
        for (var y = 0; y < sourceRect.height; ++y)
            for (var x = 0; x < sourceRect.width; ++x) {
                var value = sourceBitmapData.getPixel32(sourceRect.x + x, sourceRect.y + y);
                if (test((value & mask) >>> 0, (threshold & mask) >>> 0)) {
                    this.setPixel32(destPoint.x + x, destPoint.y + y, color);
                    ++count;
                } else if (copySource)
                    this.setPixel32(destPoint.x + x, destPoint.y + y, value);
            }
        return count;
    }
    getVector(rect) {
        var out = [];
        for (var y = 0; y < rect.height; ++y)
            for (var x = 0; x < rect.width; ++x)
                out.push(this.getPixel32(rect.x + x, rect.y + y));
        return out;
    }
    setVector(rect, inputVector) {
        var i = 0;
        for (var y = 0; y < rect.height; ++y)
            for (var x = 0; x < rect.width; ++x)
                this.setPixel32(rect.x + x, rect.y + y, inputVector[i++]);
    }
    histogram(hRect) {
        var rect = hRect || this.rect, channels = [[], [], [], []];
        for (var k = 0; k < 4; ++k)
            for (var v = 0; v < 256; ++v)
                channels[k][v] = 0;
        for (var y = 0; y < rect.height; ++y)
            for (var x = 0; x < rect.width; ++x) {
                var p = this.getPixel32(rect.x + x, rect.y + y);
                channels[0][(p >> 16) & 255]++;
                channels[1][(p >> 8) & 255]++;
                channels[2][p & 255]++;
                channels[3][p >>> 24]++;
            }
        return channels;
    }
    merge(sourceBitmapData, sourceRect, destPoint, redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier) {
        var k = [redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier];
        for (var y = 0; y < sourceRect.height; ++y)
            for (var x = 0; x < sourceRect.width; ++x) {
                var s = sourceBitmapData.getPixel32(sourceRect.x + x, sourceRect.y + y);
                var d = this.getPixel32(destPoint.x + x, destPoint.y + y);
                var out = 0;
                [16, 8, 0, 24].forEach(function (shift, i) {
                    var sv = (s >>> shift) & 255, dv = (d >>> shift) & 255;
                    out |= ((sv * k[i] + dv * (256 - k[i])) >> 8) << shift;
                });
                this.setPixel32(destPoint.x + x, destPoint.y + y, out >>> 0);
            }
    }
    perlinNoise() {
        // Not worked out: a mid gray.
        this.fillRect(this.rect, 0xff808080);
    }
    paletteMap(sourceBitmapData, sourceRect, destPoint, redArray, greenArray, blueArray, alphaArray) {
        for (var y = 0; y < sourceRect.height; ++y)
            for (var x = 0; x < sourceRect.width; ++x) {
                var p = sourceBitmapData.getPixel32(sourceRect.x + x, sourceRect.y + y);
                var r = redArray ? redArray[(p >> 16) & 255] : p & 0xff0000;
                var g = greenArray ? greenArray[(p >> 8) & 255] : p & 0xff00;
                var b = blueArray ? blueArray[p & 255] : p & 0xff;
                var a = alphaArray ? alphaArray[p >>> 24] : p & 0xff000000;
                this.setPixel32(destPoint.x + x, destPoint.y + y, (r + g + b + a) >>> 0);
            }
    }
    compare(other) {
        if (other.width !== this.width)
            return -3;
        if (other.height !== this.height)
            return -4;
        var a = this._pixels().data, b = other._pixels().data, same = true;
        for (var i = 0; i < a.length && same; ++i)
            same = a[i] === b[i];
        return same ? 0 : new BitmapData(1, 1);
    }
}

class Bitmap extends flash.display.DisplayObject {
    constructor(bitmapData, pixelSnapping, smoothing) {
        super();
        var pending = this._placement;
        this._bitmapData = bitmapData || null;
        // Placed by a timeline: a library bitmap's picture.
        if (!this._bitmapData && pending && pending.image !== undefined)
            this._bitmapData = phoenix._.libraryBitmapData(pending.image);
        this.pixelSnapping = pixelSnapping || "auto";
        this._smoothing = !!smoothing;
    }
    $construct(bitmapData, pixelSnapping, smoothing) {
        super.$construct();
        if (bitmapData)
            this._bitmapData = bitmapData;
        if (pixelSnapping)
            this.pixelSnapping = pixelSnapping;
        if (smoothing !== undefined)
            this._smoothing = !!smoothing;
    }
    get bitmapData() { return this._bitmapData; }
    set bitmapData(value) { this._bitmapData = value || null; this._changed(); }
    get smoothing() { return this._smoothing; }
    set smoothing(value) { this._smoothing = !!value; this._changed(); }
    _contentBounds(strokes) {
        var data = this._bitmapData;
        return data && !data._disposed ? [0, 0, data.width, data.height] : null;
    }
    _hitContent(x, y, shapeFlag) {
        var data = this._bitmapData;
        return !!data && x >= 0 && y >= 0 && x <= data.width && y <= data.height;
    }
    _draw(ctx, m, c, r) {
        var data = this._bitmapData;
        if (!data || data._disposed)
            return;
        var source = data._canvasForDraw();
        ctx.save();
        ctx.setTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
        if (r.maskPass) {
            ctx.fillStyle = "#000";
            ctx.fillRect(0, 0, source.width, source.height);
        } else {
            if (!C.alphaOnly(c))
                source = phoenix._.tinted(source, c);
            else
                ctx.globalAlpha = ctx.globalAlpha * phoenix.clamp(c[3], 0, 1);
            ctx.imageSmoothingEnabled = this._smoothing;
            ctx.drawImage(source, 0, 0);
        }
        ctx.restore();
    }
}

/// A BitmapData holding a published image's pixels.
phoenix._.libraryBitmapData = function (index) {
    var image = phoenix._.drawable(phoenix._.images[index]);
    var data = Object.create(BitmapData.prototype);
    data._transparent = true;
    data._disposed = false;
    data._data = null;
    data._dataDirty = false;
    data._locked = false;
    Object.defineProperty(data, "_phoenixRevision", {value: 0, writable: true});
    data._canvas = phoenix.makeCanvas(image ? image.width : 1, image ? image.height : 1);
    if (image)
        data._canvas.getContext("2d").drawImage(image, 0, 0);
    return data;
};

phoenix.defineClass("flash.display.BitmapData", BitmapData);
phoenix.defineClass("flash.display.Bitmap", Bitmap);
phoenix.defineClass("flash.display.BitmapDataChannel", function BitmapDataChannel() {});
Object.assign(flash.display.BitmapDataChannel, {ALPHA: 8, BLUE: 4, GREEN: 2, RED: 1});
})(phoenix);
