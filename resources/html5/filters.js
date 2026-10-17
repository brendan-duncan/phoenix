// flash.filters: the filter classes, and how the renderer works each on a
// picture of the object (a canvas in device pixels). Blur uses the
// canvas's own blur; shadows and glows are made of blurred, colored
// copies of the object's silhouette; bevels are a pair of inner shadows;
// the color matrix and convolution work on the pixels.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;

var BitmapFilterQuality = {LOW: 1, MEDIUM: 2, HIGH: 3};
var BitmapFilterType = {FULL: "full", INNER: "inner", OUTER: "outer"};

class BitmapFilter {
    clone() { return Object.assign(Object.create(Object.getPrototypeOf(this)), this); }
    /// How far the filter reaches past the object, in device pixels at
    /// \a scale device pixels a stage pixel.
    _margin(scale) { return 0; }
    /// The filtered picture: \a canvas itself, changed, or a new one.
    _apply(canvas, scale) { return canvas; }
}

/// The canvas blur's radius for a box blur of a size and passes.
function sigmaOf(blur, quality, scale) {
    return Math.max(0, blur) * scale * Math.sqrt(Math.max(1, quality | 0) / 12);
}
function newLike(canvas) {
    return phoenix.makeCanvas(canvas.width, canvas.height);
}
/// \a canvas blurred, as a new canvas.
function blurred(canvas, blurX, blurY, quality, scale) {
    var sx = sigmaOf(blurX, quality, scale), sy = sigmaOf(blurY, quality, scale);
    var out = newLike(canvas);
    var ctx = out.getContext("2d");
    if (sx < 0.05 && sy < 0.05) {
        ctx.drawImage(canvas, 0, 0);
        return out;
    }
    if ("filter" in ctx) {
        // The canvas blurs evenly; uneven blurs are drawn squashed, blurred,
        // and stretched back.
        if (Math.abs(sx - sy) < 0.5) {
            ctx.filter = "blur(" + ((sx + sy) / 2).toFixed(2) + "px)";
            ctx.drawImage(canvas, 0, 0);
            ctx.filter = "none";
            return out;
        }
        var wide = sx > sy, ratio = wide ? Math.max(sy, 0.05) / sx : Math.max(sx, 0.05) / sy;
        var w = wide ? Math.max(1, Math.round(canvas.width * ratio)) : canvas.width;
        var h = wide ? canvas.height : Math.max(1, Math.round(canvas.height * ratio));
        var squashed = phoenix.makeCanvas(w, h);
        var sctx = squashed.getContext("2d");
        sctx.filter = "blur(" + Math.min(sx, sy).toFixed(2) + "px)";
        sctx.drawImage(canvas, 0, 0, w, h);
        ctx.drawImage(squashed, 0, 0, canvas.width, canvas.height);
        return out;
    }
    ctx.drawImage(canvas, 0, 0);
    boxBlur(ctx, out.width, out.height, Math.round(blurX * scale), Math.round(blurY * scale), quality);
    return out;
}
/// A box blur on the pixels, for canvases without filters.
function boxBlur(ctx, width, height, bx, by, passes) {
    var image = ctx.getImageData(0, 0, width, height), d = image.data;
    var tmp = new Float32Array(Math.max(width, height) * 4);
    for (var pass = 0; pass < Math.max(1, passes); ++pass) {
        [[bx, true], [by, false]].forEach(function (axis) {
            var radius = Math.floor(axis[0] / 2);
            if (radius < 1)
                return;
            var horizontal = axis[1];
            var lines = horizontal ? height : width, length = horizontal ? width : height;
            for (var line = 0; line < lines; ++line) {
                for (var i = 0; i < length; ++i) {
                    var at = horizontal ? (line * width + i) * 4 : (i * width + line) * 4;
                    tmp[i * 4] = d[at]; tmp[i * 4 + 1] = d[at + 1]; tmp[i * 4 + 2] = d[at + 2]; tmp[i * 4 + 3] = d[at + 3];
                }
                var sum = [0, 0, 0, 0], window = radius * 2 + 1;
                for (var k = -radius; k <= radius; ++k) {
                    var j = Math.min(length - 1, Math.max(0, k));
                    for (var c = 0; c < 4; ++c)
                        sum[c] += tmp[j * 4 + c];
                }
                for (var p = 0; p < length; ++p) {
                    var out = horizontal ? (line * width + p) * 4 : (p * width + line) * 4;
                    for (var cc = 0; cc < 4; ++cc)
                        d[out + cc] = sum[cc] / window;
                    var add = Math.min(length - 1, p + radius + 1), drop = Math.max(0, p - radius);
                    for (var c2 = 0; c2 < 4; ++c2)
                        sum[c2] += tmp[add * 4 + c2] - tmp[drop * 4 + c2];
                }
            }
        });
    }
    ctx.putImageData(image, 0, 0);
}
/// \a canvas's silhouette in a color, its coverage times \a alpha and
/// \a strength.
function silhouette(canvas, rgb, alpha, strength) {
    var out = newLike(canvas);
    var ctx = out.getContext("2d");
    ctx.drawImage(canvas, 0, 0);
    ctx.globalCompositeOperation = "source-in";
    ctx.fillStyle = phoenix._.C.css(rgb, 1, null);
    ctx.fillRect(0, 0, out.width, out.height);
    ctx.globalCompositeOperation = "source-over";
    scaleAlpha(out, alpha * strength);
    return out;
}
function scaleAlpha(canvas, k) {
    if (Math.abs(k - 1) < 1e-6)
        return;
    var ctx = canvas.getContext("2d");
    if (k < 1) {
        var copy = newLike(canvas);
        copy.getContext("2d").drawImage(canvas, 0, 0);
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.globalAlpha = Math.max(0, k);
        ctx.drawImage(copy, 0, 0);
        ctx.globalAlpha = 1;
        return;
    }
    var image = ctx.getImageData(0, 0, canvas.width, canvas.height), d = image.data;
    for (var i = 3; i < d.length; i += 4)
        d[i] = Math.min(255, d[i] * k);
    ctx.putImageData(image, 0, 0);
}
/// What lies outside \a canvas's silhouette, in a color (for inner
/// shadows and glows).
function outside(canvas, rgb, dx, dy) {
    var out = newLike(canvas);
    var ctx = out.getContext("2d");
    ctx.fillStyle = phoenix._.C.css(rgb, 1, null);
    ctx.fillRect(0, 0, out.width, out.height);
    ctx.globalCompositeOperation = "destination-out";
    ctx.drawImage(canvas, dx, dy);
    return out;
}

/// The shadow and glow work: an outer or inner, offset, blurred and
/// colored copy of the silhouette, with the object kept, knocked out or
/// hidden.
function shadow(canvas, scale, o) {
    var dx = Math.cos(o.angle * Math.PI / 180) * o.distance * scale;
    var dy = Math.sin(o.angle * Math.PI / 180) * o.distance * scale;
    var out = newLike(canvas);
    var ctx = out.getContext("2d");
    if (!o.inner) {
        var shade = blurred(silhouette(canvas, o.color, o.alpha, 1), o.blurX, o.blurY, o.quality, scale);
        scaleAlpha(shade, o.strength);
        ctx.drawImage(shade, dx, dy);
        if (o.knockout) {
            ctx.globalCompositeOperation = "destination-out";
            ctx.drawImage(canvas, 0, 0);
        } else if (!o.hideObject) {
            ctx.globalCompositeOperation = "source-over";
            ctx.drawImage(canvas, 0, 0);
        }
        return out;
    }
    var inside = blurred(outside(canvas, o.color, dx, dy), o.blurX, o.blurY, o.quality, scale);
    scaleAlpha(inside, o.alpha * o.strength);
    if (!o.knockout && !o.hideObject)
        ctx.drawImage(canvas, 0, 0);
    else {
        ctx.drawImage(canvas, 0, 0);
        ctx.globalCompositeOperation = "source-in";
        ctx.drawImage(inside, 0, 0);
        return out;
    }
    ctx.globalCompositeOperation = "source-atop";
    ctx.drawImage(inside, 0, 0);
    return out;
}

class BlurFilter extends BitmapFilter {
    constructor(blurX, blurY, quality) {
        super();
        this.blurX = blurX === undefined ? 4 : +blurX;
        this.blurY = blurY === undefined ? 4 : +blurY;
        this.quality = quality === undefined ? 1 : quality | 0;
    }
    _margin(scale) { return Math.ceil(Math.max(this.blurX, this.blurY) * scale * Math.max(1, this.quality) / 2 + 2); }
    _apply(canvas, scale) { return blurred(canvas, this.blurX, this.blurY, this.quality, scale); }
}

class DropShadowFilter extends BitmapFilter {
    constructor(distance, angle, color, alpha, blurX, blurY, strength, quality, inner, knockout, hideObject) {
        super();
        this.distance = distance === undefined ? 4 : +distance;
        this.angle = angle === undefined ? 45 : +angle;
        this.color = color === undefined ? 0 : color >>> 0;
        this.alpha = alpha === undefined ? 1 : +alpha;
        this.blurX = blurX === undefined ? 4 : +blurX;
        this.blurY = blurY === undefined ? 4 : +blurY;
        this.strength = strength === undefined ? 1 : +strength;
        this.quality = quality === undefined ? 1 : quality | 0;
        this.inner = !!inner;
        this.knockout = !!knockout;
        this.hideObject = !!hideObject;
    }
    _margin(scale) {
        return Math.ceil((Math.abs(this.distance) + Math.max(this.blurX, this.blurY) * Math.max(1, this.quality) / 2) * scale + 2);
    }
    _apply(canvas, scale) { return shadow(canvas, scale, this); }
}

class GlowFilter extends BitmapFilter {
    constructor(color, alpha, blurX, blurY, strength, quality, inner, knockout) {
        super();
        this.color = color === undefined ? 0xff0000 : color >>> 0;
        this.alpha = alpha === undefined ? 1 : +alpha;
        this.blurX = blurX === undefined ? 6 : +blurX;
        this.blurY = blurY === undefined ? 6 : +blurY;
        this.strength = strength === undefined ? 2 : +strength;
        this.quality = quality === undefined ? 1 : quality | 0;
        this.inner = !!inner;
        this.knockout = !!knockout;
    }
    _margin(scale) { return Math.ceil(Math.max(this.blurX, this.blurY) * Math.max(1, this.quality) / 2 * scale + 2); }
    _apply(canvas, scale) {
        return shadow(canvas, scale, {distance: 0, angle: 0, color: this.color, alpha: this.alpha, blurX: this.blurX,
            blurY: this.blurY, strength: this.strength, quality: this.quality, inner: this.inner, knockout: this.knockout,
            hideObject: false});
    }
}

/// A bevel: the shadow color on the side away from the light and the
/// highlight on the side toward it, inside the object (outside for an
/// outer bevel, both for a full one).
class BevelFilter extends BitmapFilter {
    constructor(distance, angle, highlightColor, highlightAlpha, shadowColor, shadowAlpha, blurX, blurY, strength, quality, type, knockout) {
        super();
        this.distance = distance === undefined ? 4 : +distance;
        this.angle = angle === undefined ? 45 : +angle;
        this.highlightColor = highlightColor === undefined ? 0xffffff : highlightColor >>> 0;
        this.highlightAlpha = highlightAlpha === undefined ? 1 : +highlightAlpha;
        this.shadowColor = shadowColor === undefined ? 0 : shadowColor >>> 0;
        this.shadowAlpha = shadowAlpha === undefined ? 1 : +shadowAlpha;
        this.blurX = blurX === undefined ? 4 : +blurX;
        this.blurY = blurY === undefined ? 4 : +blurY;
        this.strength = strength === undefined ? 1 : +strength;
        this.quality = quality === undefined ? 1 : quality | 0;
        this.type = type || "inner";
        this.knockout = !!knockout;
    }
    _margin(scale) {
        return Math.ceil((Math.abs(this.distance) + Math.max(this.blurX, this.blurY) * Math.max(1, this.quality) / 2) * scale + 2);
    }
    _apply(canvas, scale) {
        var inner = this.type !== "outer", outer = this.type !== "inner";
        var common = {distance: this.distance, blurX: this.blurX, blurY: this.blurY, strength: this.strength,
            quality: this.quality, knockout: false, hideObject: false};
        var out = canvas;
        if (inner) {
            out = shadow(out, scale, Object.assign({}, common, {angle: this.angle, color: this.shadowColor,
                alpha: this.shadowAlpha, inner: true}));
            out = shadow(out, scale, Object.assign({}, common, {angle: this.angle + 180, color: this.highlightColor,
                alpha: this.highlightAlpha, inner: true}));
        }
        if (outer) {
            out = shadow(out, scale, Object.assign({}, common, {angle: this.angle, color: this.shadowColor,
                alpha: this.shadowAlpha, inner: false}));
            out = shadow(out, scale, Object.assign({}, common, {angle: this.angle + 180, color: this.highlightColor,
                alpha: this.highlightAlpha, inner: false}));
        }
        if (this.knockout) {
            var ctx = out.getContext("2d");
            ctx.globalCompositeOperation = "destination-out";
            ctx.drawImage(canvas, 0, 0);
            ctx.globalCompositeOperation = "source-over";
        }
        return out;
    }
}

/// The gradient filters as their plain kin, in the gradient's strongest
/// color.
function strongest(colors, alphas) {
    var best = 0;
    for (var i = 0; i < (alphas || []).length; ++i)
        if (alphas[i] >= alphas[best])
            best = i;
    return {color: colors && colors.length ? colors[best] : 0, alpha: alphas && alphas.length ? alphas[best] : 1};
}
class GradientGlowFilter extends BitmapFilter {
    constructor(distance, angle, colors, alphas, ratios, blurX, blurY, strength, quality, type, knockout) {
        super();
        Object.assign(this, {distance: distance === undefined ? 4 : +distance, angle: angle === undefined ? 45 : +angle,
            colors: colors || [], alphas: alphas || [], ratios: ratios || [], blurX: blurX === undefined ? 4 : +blurX,
            blurY: blurY === undefined ? 4 : +blurY, strength: strength === undefined ? 1 : +strength,
            quality: quality === undefined ? 1 : quality | 0, type: type || "inner", knockout: !!knockout});
    }
    _margin(scale) {
        return Math.ceil((Math.abs(this.distance) + Math.max(this.blurX, this.blurY) * Math.max(1, this.quality) / 2) * scale + 2);
    }
    _apply(canvas, scale) {
        var s = strongest(this.colors, this.alphas);
        return shadow(canvas, scale, {distance: this.distance, angle: this.angle, color: s.color, alpha: s.alpha,
            blurX: this.blurX, blurY: this.blurY, strength: this.strength, quality: this.quality,
            inner: this.type === "inner", knockout: this.knockout, hideObject: false});
    }
}
class GradientBevelFilter extends GradientGlowFilter {
    _apply(canvas, scale) {
        var first = this.colors.length ? this.colors[0] : 0xffffff, last = this.colors.length ? this.colors[this.colors.length - 1] : 0;
        var bevel = new BevelFilter(this.distance, this.angle, first, this.alphas.length ? this.alphas[0] : 1, last,
            this.alphas.length ? this.alphas[this.alphas.length - 1] : 1, this.blurX, this.blurY, this.strength, this.quality,
            this.type, this.knockout);
        return bevel._apply(canvas, scale);
    }
}

class ColorMatrixFilter extends BitmapFilter {
    constructor(matrix) {
        super();
        this._matrix = matrix && matrix.length === 20 ? Array.prototype.slice.call(matrix) :
            [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
    }
    get matrix() { return this._matrix.slice(); }
    set matrix(value) { this._matrix = value && value.length === 20 ? Array.prototype.slice.call(value) : this._matrix; }
    clone() { return new ColorMatrixFilter(this._matrix); }
    _apply(canvas, scale) {
        var ctx = canvas.getContext("2d");
        var image = ctx.getImageData(0, 0, canvas.width, canvas.height), d = image.data, m = this._matrix;
        for (var i = 0; i < d.length; i += 4) {
            var r = d[i], g = d[i + 1], b = d[i + 2], a = d[i + 3];
            d[i] = m[0] * r + m[1] * g + m[2] * b + m[3] * a + m[4];
            d[i + 1] = m[5] * r + m[6] * g + m[7] * b + m[8] * a + m[9];
            d[i + 2] = m[10] * r + m[11] * g + m[12] * b + m[13] * a + m[14];
            d[i + 3] = m[15] * r + m[16] * g + m[17] * b + m[18] * a + m[19];
        }
        ctx.putImageData(image, 0, 0);
        return canvas;
    }
}

class ConvolutionFilter extends BitmapFilter {
    constructor(matrixX, matrixY, matrix, divisor, bias, preserveAlpha, clamp, color, alpha) {
        super();
        this.matrixX = matrixX | 0;
        this.matrixY = matrixY | 0;
        this.matrix = matrix ? Array.prototype.slice.call(matrix) : [];
        this.divisor = divisor === undefined ? 1 : +divisor;
        this.bias = +bias || 0;
        this.preserveAlpha = preserveAlpha === undefined ? true : !!preserveAlpha;
        this.clamp = clamp === undefined ? true : !!clamp;
        this.color = color >>> 0;
        this.alpha = +alpha || 0;
    }
    _margin(scale) { return Math.max(this.matrixX, this.matrixY); }
    _apply(canvas, scale) {
        var mx = this.matrixX, my = this.matrixY, k = this.matrix;
        if (!mx || !my || k.length < mx * my)
            return canvas;
        var ctx = canvas.getContext("2d"), w = canvas.width, h = canvas.height;
        var source = ctx.getImageData(0, 0, w, h), s = source.data;
        var target = ctx.createImageData(w, h), t = target.data;
        var divisor = this.divisor || 1, hx = mx >> 1, hy = my >> 1;
        for (var y = 0; y < h; ++y) {
            for (var x = 0; x < w; ++x) {
                var sum = [0, 0, 0, 0];
                for (var j = 0; j < my; ++j) {
                    for (var i = 0; i < mx; ++i) {
                        var sx = Math.min(w - 1, Math.max(0, x + i - hx)), sy = Math.min(h - 1, Math.max(0, y + j - hy));
                        var at = (sy * w + sx) * 4, weight = k[j * mx + i];
                        sum[0] += s[at] * weight; sum[1] += s[at + 1] * weight; sum[2] += s[at + 2] * weight; sum[3] += s[at + 3] * weight;
                    }
                }
                var out = (y * w + x) * 4;
                t[out] = sum[0] / divisor + this.bias;
                t[out + 1] = sum[1] / divisor + this.bias;
                t[out + 2] = sum[2] / divisor + this.bias;
                t[out + 3] = this.preserveAlpha ? s[out + 3] : sum[3] / divisor + this.bias;
            }
        }
        ctx.putImageData(target, 0, 0);
        return canvas;
    }
}

class DisplacementMapFilter extends BitmapFilter {
    constructor(mapBitmap, mapPoint, componentX, componentY, scaleX, scaleY, mode, color, alpha) {
        super();
        Object.assign(this, {mapBitmap: mapBitmap || null, mapPoint: mapPoint || null, componentX: componentX | 0,
            componentY: componentY | 0, scaleX: +scaleX || 0, scaleY: +scaleY || 0, mode: mode || "wrap",
            color: color >>> 0, alpha: +alpha || 0});
    }
}
class ShaderFilter extends BitmapFilter {
    constructor(shader) { super(); this.shader = shader || null; }
}

phoenix.defineClass("flash.filters.BitmapFilter", BitmapFilter);
phoenix.defineClass("flash.filters.BlurFilter", BlurFilter);
phoenix.defineClass("flash.filters.DropShadowFilter", DropShadowFilter);
phoenix.defineClass("flash.filters.GlowFilter", GlowFilter);
phoenix.defineClass("flash.filters.BevelFilter", BevelFilter);
phoenix.defineClass("flash.filters.GradientGlowFilter", GradientGlowFilter);
phoenix.defineClass("flash.filters.GradientBevelFilter", GradientBevelFilter);
phoenix.defineClass("flash.filters.ColorMatrixFilter", ColorMatrixFilter);
phoenix.defineClass("flash.filters.ConvolutionFilter", ConvolutionFilter);
phoenix.defineClass("flash.filters.DisplacementMapFilter", DisplacementMapFilter);
phoenix.defineClass("flash.filters.ShaderFilter", ShaderFilter);
phoenix.defineClass("flash.filters.BitmapFilterQuality", function BitmapFilterQualityClass() {});
Object.assign(flash.filters.BitmapFilterQuality, BitmapFilterQuality);
phoenix.defineClass("flash.filters.BitmapFilterType", function BitmapFilterTypeClass() {});
Object.assign(flash.filters.BitmapFilterType, BitmapFilterType);

/// A published movie's filter (README.md, "Displays") as a filter object.
phoenix._.filterOf = function (json) {
    switch (json.t) {
    case "blur": return new BlurFilter(json.bx, json.by, json.q);
    case "shadow": return new DropShadowFilter(json.d, json.an, json.c, json.a, json.bx, json.by, json.s, json.q, !!json.in,
        !!json.ko, !!json.ho);
    case "glow": return new GlowFilter(json.c, json.a, json.bx, json.by, json.s, json.q, !!json.in, !!json.ko);
    case "bevel": return new BevelFilter(json.d, json.an, json.hc, json.ha, json.sc, json.sa, json.bx, json.by, json.s, json.q,
        json.bt || "inner", !!json.ko);
    case "gglow": return new GradientGlowFilter(json.d, json.an, json.cs, json.as, json.rs, json.bx, json.by, json.s, json.q,
        json.in ? "inner" : "outer", !!json.ko);
    case "gbevel": return new GradientBevelFilter(json.d, json.an, json.cs, json.as, json.rs, json.bx, json.by, json.s, json.q,
        json.bt || "inner", !!json.ko);
    case "matrix": return new ColorMatrixFilter(json.m);
    }
    return null;
};
phoenix._.boxBlur = boxBlur;
})(phoenix);
