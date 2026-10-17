// flash.display.Graphics: the drawing API, kept as a list of fills,
// strokes, images and text runs that the renderer draws with the canvas.
// Shapes a published movie's library holds come in already made, as the
// same list (see Graphics._fromArt and README.md).

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;

/// A fill style: solid, gradient or bitmap.
function solid(rgb, alpha) {
    return {type: "solid", rgb: rgb >>> 0 & 0xffffff, alpha: alpha === undefined ? 1 : phoenix.clamp(+alpha, 0, 1)};
}
function gradient(type, colors, alphas, ratios, matrix, spread, interpolation, focal) {
    var stops = [];
    var count = Math.min(colors ? colors.length : 0, alphas ? alphas.length : 0, ratios ? ratios.length : 0);
    for (var i = 0; i < count; ++i)
        stops.push([phoenix.clamp(+ratios[i], 0, 255) / 255, colors[i] >>> 0 & 0xffffff, phoenix.clamp(+alphas[i], 0, 1)]);
    return {
        type: type === "radial" ? "radial" : "linear",
        stops: stops,
        // Without a matrix, the gradient square as it is.
        m: matrix ? M.fromMatrix(matrix) : [1, 0, 0, 1, 0, 0],
        spread: spread || "pad",
        interp: interpolation || "rgb",
        focal: phoenix.clamp(+focal || 0, -1, 1)
    };
}
/// A style from a published shape's JSON form (README.md, "Shapes").
function styleOf(json) {
    if (!json)
        return null;
    if (json.g)
        return {type: json.g === "r" ? "radial" : "linear", stops: json.s || [], m: json.m || [1, 0, 0, 1, 0, 0],
            spread: json.sp || "pad", interp: json.i || "rgb", focal: json.fp || 0};
    if (json.b !== undefined)
        return {type: "bitmap", bitmap: json.b, m: json.m || [1, 0, 0, 1, 0, 0], repeat: !!json.r, smooth: json.sm !== 0};
    return {type: "solid", rgb: json.c >>> 0, alpha: json.a === undefined ? 1 : json.a};
}
function lineOf(json) {
    return {width: json.w === undefined ? 1 : +json.w, fill: styleOf(json.f || {c: 0}), caps: json.cap || "round",
        joints: json.join || "round", miter: json.ml || 3, scaleMode: json.sm || "normal"};
}

/// The canvas source of a bitmap a style or an image op names: a
/// published image (by index) or a BitmapData.
function sourceOf(bitmap) {
    if (typeof bitmap === "number")
        return phoenix._.images ? phoenix._.images[bitmap] : null;
    return bitmap && bitmap._canvasForDraw ? bitmap._canvasForDraw() : null;
}

// Paths: commands kept as arrays (["M", x, y], ["L", x, y], ["Q", cx, cy,
// x, y], ["C", c1x, c1y, c2x, c2y, x, y], ["Z"]), or a published shape's
// SVG path data; made into a Path2D when first drawn.

function pathOf(op) {
    if (op._path)
        return op._path;
    if (typeof Path2D === "undefined")
        return null;
    if (op.d !== undefined) {
        op._path = new Path2D(op.d);
        return op._path;
    }
    var path = new Path2D();
    var cmds = op.cmds;
    for (var i = 0; i < cmds.length; ++i) {
        var c = cmds[i];
        switch (c[0]) {
        case "M": path.moveTo(c[1], c[2]); break;
        case "L": path.lineTo(c[1], c[2]); break;
        case "Q": path.quadraticCurveTo(c[1], c[2], c[3], c[4]); break;
        case "C": path.bezierCurveTo(c[1], c[2], c[3], c[4], c[5], c[6]); break;
        case "Z": path.closePath(); break;
        }
    }
    op._path = path;
    return path;
}
function boxOfCommands(cmds) {
    var box = null;
    for (var i = 0; i < cmds.length; ++i) {
        var c = cmds[i];
        for (var k = 1; k + 1 < c.length; k += 2) {
            var x = c[k], y = c[k + 1];
            if (!box)
                box = [x, y, x, y];
            else {
                if (x < box[0]) box[0] = x;
                if (y < box[1]) box[1] = y;
                if (x > box[2]) box[2] = x;
                if (y > box[3]) box[3] = y;
            }
        }
    }
    return box;
}

/// Something to test points against paths with.
var hitContext = null;
function hitCtx() {
    if (hitContext === null) {
        var canvas = phoenix.makeCanvas(1, 1);
        hitContext = canvas ? canvas.getContext("2d") : false;
    }
    return hitContext || null;
}

class Graphics {
    constructor(owner) {
        Object.defineProperty(this, "_owner", {value: owner || null});
        this._ops = [];
        this._fill = null;
        this._fillStrokes = [];
        this._line = null;
        this._stroke = null;
        this._x = 0;
        this._y = 0;
        this._revision = 0;
        this._box = null;
        this._boxStrokes = null;
    }
    _touched() {
        ++this._revision;
        this._box = this._boxStrokes = undefined;
        if (this._owner)
            this._owner._changed();
    }
    // Fills.
    beginFill(color, alpha) {
        this._endFill();
        this._fill = {k: "f", style: solid(color, alpha === undefined ? 1 : alpha), cmds: [], rule: "evenodd"};
    }
    beginGradientFill(type, colors, alphas, ratios, matrix, spreadMethod, interpolationMethod, focalPointRatio) {
        this._endFill();
        this._fill = {k: "f", style: gradient(type, colors, alphas, ratios, matrix, spreadMethod, interpolationMethod,
            focalPointRatio), cmds: [], rule: "evenodd"};
    }
    beginBitmapFill(bitmap, matrix, repeat, smooth) {
        this._endFill();
        this._fill = {k: "f", style: {type: "bitmap", bitmap: bitmap, m: matrix ? M.fromMatrix(matrix) : [1, 0, 0, 1, 0, 0],
            repeat: repeat === undefined ? true : !!repeat, smooth: !!smooth}, cmds: [], rule: "evenodd"};
    }
    beginShaderFill(shader, matrix) {
        this._endFill();
        this._fill = {k: "f", style: solid(0x808080, 1), cmds: [], rule: "evenodd"};
    }
    endFill() {
        this._endFill();
        this._touched();
    }
    _endFill() {
        this._finishStroke();
        if (this._fill) {
            if (this._fill.cmds.length) {
                this._closeFill();
                this._ops.push(this._fill);
            }
            for (var i = 0; i < this._fillStrokes.length; ++i)
                this._ops.push(this._fillStrokes[i]);
        }
        this._fill = null;
        this._fillStrokes = [];
        // The stroke goes on from where the pen is.
        if (this._line)
            this._stroke = {k: "s", line: this._line, cmds: []};
    }
    /// Each of the fill's outlines closed back to where it began.
    _closeFill() {
        var cmds = this._fill.cmds, start = null, last = null;
        var closed = [];
        for (var i = 0; i < cmds.length; ++i) {
            var c = cmds[i];
            if (c[0] === "M") {
                if (start && last && (last[0] !== start[0] || last[1] !== start[1]))
                    closed.push(["L", start[0], start[1]]);
                start = [c[1], c[2]];
            }
            closed.push(c);
            last = [c[c.length - 2], c[c.length - 1]];
        }
        if (start && last && (last[0] !== start[0] || last[1] !== start[1]))
            closed.push(["L", start[0], start[1]]);
        this._fill.cmds = closed;
        this._fill._path = null;
    }
    // Lines.
    lineStyle(thickness, color, alpha, pixelHinting, scaleMode, caps, joints, miterLimit) {
        this._finishStroke();
        if (thickness === undefined || thickness === null || isNaN(thickness)) {
            this._line = null;
            this._stroke = null;
            return;
        }
        this._line = {width: Math.max(0, Math.min(255, +thickness)), fill: solid(color || 0, alpha === undefined ? 1 : alpha),
            caps: caps === "none" ? "butt" : caps || "round", joints: joints || "round",
            miter: miterLimit === undefined ? 3 : Math.max(1, +miterLimit), scaleMode: scaleMode || "normal",
            pixelHinting: !!pixelHinting};
        this._stroke = {k: "s", line: this._line, cmds: []};
    }
    lineGradientStyle(type, colors, alphas, ratios, matrix, spreadMethod, interpolationMethod, focalPointRatio) {
        if (!this._line)
            return;
        this._finishStroke();
        this._line = Object.assign({}, this._line, {fill: gradient(type, colors, alphas, ratios, matrix, spreadMethod,
            interpolationMethod, focalPointRatio)});
        this._stroke = {k: "s", line: this._line, cmds: []};
    }
    lineBitmapStyle(bitmap, matrix, repeat, smooth) {
        if (!this._line)
            return;
        this._finishStroke();
        this._line = Object.assign({}, this._line, {fill: {type: "bitmap", bitmap: bitmap,
            m: matrix ? M.fromMatrix(matrix) : [1, 0, 0, 1, 0, 0], repeat: repeat === undefined ? true : !!repeat,
            smooth: !!smooth}});
        this._stroke = {k: "s", line: this._line, cmds: []};
    }
    lineShaderStyle(shader, matrix) { phoenix.unsupported("Graphics.lineShaderStyle"); }
    _finishStroke() {
        if (this._stroke && this._stroke.cmds.some(function (c) { return c[0] !== "M"; })) {
            if (this._fill)
                this._fillStrokes.push(this._stroke);
            else
                this._ops.push(this._stroke);
        }
        this._stroke = this._line ? {k: "s", line: this._line, cmds: []} : null;
    }
    // The pen.
    _segment(command) {
        var x = command[command.length - 2], y = command[command.length - 1];
        if (this._fill) {
            if (!this._fill.cmds.length)
                this._fill.cmds.push(["M", this._x, this._y]);
            this._fill.cmds.push(command);
            this._fill._path = null;
        }
        if (this._stroke) {
            if (!this._stroke.cmds.length)
                this._stroke.cmds.push(["M", this._x, this._y]);
            this._stroke.cmds.push(command);
            this._stroke._path = null;
        }
        this._x = x;
        this._y = y;
        this._touched();
    }
    moveTo(x, y) {
        x = +x; y = +y;
        if (this._fill) {
            this._fill.cmds.push(["M", x, y]);
            this._fill._path = null;
        }
        if (this._stroke) {
            this._stroke.cmds.push(["M", x, y]);
            this._stroke._path = null;
        }
        this._x = x;
        this._y = y;
    }
    lineTo(x, y) { this._segment(["L", +x, +y]); }
    curveTo(controlX, controlY, anchorX, anchorY) { this._segment(["Q", +controlX, +controlY, +anchorX, +anchorY]); }
    cubicCurveTo(c1x, c1y, c2x, c2y, x, y) { this._segment(["C", +c1x, +c1y, +c2x, +c2y, +x, +y]); }
    drawRect(x, y, width, height) {
        x = +x; y = +y; width = +width; height = +height;
        this.moveTo(x, y);
        this.lineTo(x + width, y);
        this.lineTo(x + width, y + height);
        this.lineTo(x, y + height);
        this.lineTo(x, y);
    }
    /// An elliptical arc of a quarter turn as a cubic, from the pen.
    _quarter(cx, cy, rx, ry, from) {
        var k = 0.5522847498;
        var a0 = from, a1 = from + Math.PI / 2;
        var x0 = cx + rx * Math.cos(a0), y0 = cy + ry * Math.sin(a0);
        var x1 = cx + rx * Math.cos(a1), y1 = cy + ry * Math.sin(a1);
        this.cubicCurveTo(x0 - k * rx * Math.sin(a0), y0 + k * ry * Math.cos(a0),
            x1 + k * rx * Math.sin(a1), y1 - k * ry * Math.cos(a1), x1, y1);
    }
    drawEllipse(x, y, width, height) {
        var rx = width / 2, ry = height / 2, cx = +x + rx, cy = +y + ry;
        this.moveTo(cx + rx, cy);
        for (var i = 0; i < 4; ++i)
            this._quarter(cx, cy, rx, ry, i * Math.PI / 2);
    }
    drawCircle(x, y, radius) { this.drawEllipse(x - radius, y - radius, radius * 2, radius * 2); }
    drawRoundRect(x, y, width, height, ellipseWidth, ellipseHeight) {
        if (ellipseHeight === undefined || isNaN(ellipseHeight))
            ellipseHeight = ellipseWidth;
        var rx = Math.min(Math.abs(ellipseWidth) / 2, Math.abs(width) / 2);
        var ry = Math.min(Math.abs(ellipseHeight) / 2, Math.abs(height) / 2);
        this.drawRoundRectComplex(x, y, width, height, rx, rx, rx, rx, ry);
    }
    drawRoundRectComplex(x, y, width, height, topLeft, topRight, bottomLeft, bottomRight, ellipseY) {
        x = +x; y = +y; width = +width; height = +height;
        var corners = [topLeft, topRight, bottomRight, bottomLeft].map(function (r) { return Math.max(0, +r || 0); });
        var ratio = ellipseY === undefined ? 1 : (corners[0] ? ellipseY / corners[0] : 1);
        var right = x + width, bottom = y + height;
        this.moveTo(x + corners[0], y);
        this.lineTo(right - corners[1], y);
        if (corners[1])
            this._quarter(right - corners[1], y + corners[1] * ratio, corners[1], corners[1] * ratio, -Math.PI / 2);
        this.lineTo(right, bottom - corners[2] * ratio);
        if (corners[2])
            this._quarter(right - corners[2], bottom - corners[2] * ratio, corners[2], corners[2] * ratio, 0);
        this.lineTo(x + corners[3], bottom);
        if (corners[3])
            this._quarter(x + corners[3], bottom - corners[3] * ratio, corners[3], corners[3] * ratio, Math.PI / 2);
        this.lineTo(x, y + corners[0] * ratio);
        if (corners[0])
            this._quarter(x + corners[0], y + corners[0] * ratio, corners[0], corners[0] * ratio, Math.PI);
    }
    drawPath(commands, data, winding) {
        var at = 0;
        for (var i = 0; i < commands.length; ++i) {
            switch (commands[i]) {
            case 1: this.moveTo(data[at], data[at + 1]); at += 2; break;
            case 2: this.lineTo(data[at], data[at + 1]); at += 2; break;
            case 3: this.curveTo(data[at], data[at + 1], data[at + 2], data[at + 3]); at += 4; break;
            case 4: this.moveTo(data[at + 2], data[at + 3]); at += 4; break;
            case 5: this.lineTo(data[at + 2], data[at + 3]); at += 4; break;
            case 6: this.cubicCurveTo(data[at], data[at + 1], data[at + 2], data[at + 3], data[at + 4], data[at + 5]); at += 6; break;
            default: break;
            }
        }
        if (this._fill && winding === "nonZero")
            this._fill.rule = "nonzero";
    }
    drawTriangles(vertices, indices, uvtData, culling) {
        var count = indices ? indices.length : vertices.length / 2;
        for (var i = 0; i + 2 < count; i += 3) {
            var a = indices ? indices[i] : i, b = indices ? indices[i + 1] : i + 1, c = indices ? indices[i + 2] : i + 2;
            this.moveTo(vertices[a * 2], vertices[a * 2 + 1]);
            this.lineTo(vertices[b * 2], vertices[b * 2 + 1]);
            this.lineTo(vertices[c * 2], vertices[c * 2 + 1]);
            this.lineTo(vertices[a * 2], vertices[a * 2 + 1]);
        }
    }
    drawGraphicsData(graphicsData) {
        for (var i = 0; i < graphicsData.length; ++i) {
            var d = graphicsData[i];
            if (d && typeof d._applyTo === "function")
                d._applyTo(this);
        }
    }
    readGraphicsData(recurse) { return []; }
    clear() {
        this._ops = [];
        this._fill = null;
        this._fillStrokes = [];
        this._line = null;
        this._stroke = null;
        this._x = this._y = 0;
        this._touched();
    }
    copyFrom(source) {
        var copy = function (op) {
            var made = Object.assign({}, op);
            if (op.cmds)
                made.cmds = op.cmds.slice();
            made._path = op._path;
            return made;
        };
        this._ops = source._ops.map(copy);
        this._fill = source._fill ? copy(source._fill) : null;
        this._fillStrokes = source._fillStrokes.map(copy);
        this._line = source._line;
        this._stroke = source._stroke ? copy(source._stroke) : null;
        this._x = source._x;
        this._y = source._y;
        this._art = source._art;
        this._touched();
    }

    /// Everything to draw, the open fill and stroke as they stand.
    _all() {
        if (!this._fill && !this._stroke)
            return this._ops;
        var all = this._ops.slice();
        if (this._fill && this._fill.cmds.length) {
            var open = Object.assign({}, this._fill);
            all.push(open);
        }
        if (this._fill)
            all = all.concat(this._fillStrokes);
        if (this._stroke && this._stroke.cmds.length > 1)
            all.push(this._stroke);
        return all;
    }
    _bounds(strokes) {
        var key = strokes ? "_boxStrokes" : "_box";
        if (this[key] !== undefined && this[key] !== null)
            return this[key];
        var box = null;
        var ops = this._all();
        for (var i = 0; i < ops.length; ++i) {
            var op = ops[i];
            if (op.k === "s" && !strokes && !op.box)
                continue;
            var b = op.box || (op.cmds ? boxOfCommands(op.cmds) : null);
            if (b && op.k === "s" && strokes && !op.box) {
                var half = Math.max(op.line.width, 1) / 2;
                b = [b[0] - half, b[1] - half, b[2] + half, b[3] + half];
            }
            box = M.union(box, b);
        }
        if (this._art && this._art.b)
            box = this._art.b.slice();
        this[key] = box;
        return box;
    }
    _hit(x, y, shapeFlag) {
        var box = this._bounds(true);
        if (!box || x < box[0] || x > box[2] || y < box[1] || y > box[3])
            return false;
        var ctx = hitCtx();
        if (!ctx || typeof Path2D === "undefined")
            return true;
        var ops = this._all();
        for (var i = ops.length - 1; i >= 0; --i) {
            var op = ops[i];
            if (op.k === "f") {
                var path = pathOf(op);
                if (path && ctx.isPointInPath(path, x, y, op.rule || "nonzero"))
                    return true;
            } else if (op.k === "s") {
                var stroke = pathOf(op);
                if (stroke) {
                    ctx.lineWidth = Math.max(op.line.width, 1);
                    ctx.lineCap = op.line.caps;
                    ctx.lineJoin = op.line.joints;
                    if (ctx.isPointInStroke(stroke, x, y))
                        return true;
                }
            } else if (op.box && x >= op.box[0] && x <= op.box[2] && y >= op.box[1] && y <= op.box[3])
                return true;
        }
        return false;
    }
    /// A published shape (README.md, "Shapes") as this Graphics' list.
    _setArt(art) {
        this.clear();
        this._art = art;
        var ops = art.o || [];
        for (var i = 0; i < ops.length; ++i) {
            var j = ops[i], op;
            if (j.p !== undefined && j.s)
                op = {k: "s", d: j.p, line: lineOf(j.s)};
            else if (j.p !== undefined)
                op = {k: "f", d: j.p, style: styleOf(j.f), rule: j.e ? "evenodd" : "nonzero"};
            else if (j.i !== undefined)
                op = {k: "i", image: j.i, m: j.m || [1, 0, 0, 1, 0, 0], smooth: j.sm !== 0};
            else if (j.t !== undefined)
                op = {k: "t", text: j.t, font: j.font || "12px sans-serif", x: j.x || 0, y: j.y || 0,
                    style: styleOf(j.f || {c: 0}), m: j.m || [1, 0, 0, 1, 0, 0], ls: j.ls || 0};
            else
                continue;
            if (j.b)
                op.box = j.b;
            if (j.o !== undefined)
                op.opacity = j.o;
            if (j.bm)
                op.blend = j.bm;
            if (j.clip !== undefined)
                op.clip = j.clip;
            this._ops.push(op);
        }
        this._touched();
    }
}

// Drawing.

var linearRgbSteps = 6;
function toLinear(v) { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
function toSrgb(v) { v = v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055; return Math.round(phoenix.clamp(v, 0, 1) * 255); }
/// A gradient's stops as canvas color stops: through the color transform,
/// with in-betweens for linear RGB.
function colorStops(style, c) {
    var stops = style.stops;
    if (!stops.length)
        return [[0, "rgba(0,0,0,0)"]];
    var out = [];
    for (var i = 0; i < stops.length; ++i) {
        if (style.interp === "linearRGB" && i > 0) {
            var a = stops[i - 1], b = stops[i];
            for (var k = 1; k < linearRgbSteps; ++k) {
                var t = k / linearRgbSteps;
                var rgb = 0;
                for (var shift = 16; shift >= 0; shift -= 8) {
                    var va = toLinear((a[1] >> shift) & 255), vb = toLinear((b[1] >> shift) & 255);
                    rgb |= toSrgb(va + (vb - va) * t) << shift;
                }
                out.push([a[0] + (b[0] - a[0]) * t, C.css(rgb, a[2] + (b[2] - a[2]) * t, c)]);
            }
        }
        out.push([stops[i][0], C.css(stops[i][1], stops[i][2], c)]);
    }
    return out;
}
/// Adds \a stops to \a g for the cycles \a first to \a last of a repeated
/// or reflected gradient, over [0, 1].
function addCycles(g, stops, spread, first, last) {
    var span = last - first;
    for (var n = first; n < last; ++n) {
        var flip = spread === "reflect" && (n & 1);
        for (var i = 0; i < stops.length; ++i) {
            var s = flip ? stops[stops.length - 1 - i] : stops[i];
            var ratio = flip ? 1 - s[0] : s[0];
            g.addColorStop(phoenix.clamp((n - first + ratio) / span, 0, 1), s[1]);
        }
    }
}
/// The gradient-space box (the -819.2 to 819.2 square's space) \a box
/// covers, through the inverse of \a m.
function gradientBox(m, box) {
    var inverse = M.invert(m);
    return inverse && box ? M.mapBox(inverse, box) : [-819.2, -819.2, 819.2, 819.2];
}
function canvasLinear(ctx, style, c, box) {
    var inverse = M.invert(style.m);
    if (!inverse)
        return null;
    // t = (x' + 819.2) / 1638.4 where x' is the gradient-space x: an affine
    // function of the point, so the canvas gradient runs along its slope.
    var ax = inverse[0] / 1638.4, ay = inverse[2] / 1638.4, b = (inverse[4] + 819.2) / 1638.4;
    var length2 = ax * ax + ay * ay;
    if (!length2)
        return null;
    var dx = ax / length2, dy = ay / length2;
    var x0 = -b * dx, y0 = -b * dy;
    var stops = colorStops(style, c);
    if (style.spread === "pad" || !box) {
        var g = ctx.createLinearGradient(x0, y0, x0 + dx, y0 + dy);
        stops.forEach(function (s) { g.addColorStop(phoenix.clamp(s[0], 0, 1), s[1]); });
        return g;
    }
    var ts = [[box[0], box[1]], [box[2], box[1]], [box[0], box[3]], [box[2], box[3]]].map(function (p) {
        return ax * p[0] + ay * p[1] + b;
    });
    var first = Math.max(-64, Math.floor(Math.min.apply(null, ts)));
    var last = Math.min(64, Math.ceil(Math.max.apply(null, ts)));
    if (last <= first)
        last = first + 1;
    var repeated = ctx.createLinearGradient(x0 + first * dx, y0 + first * dy, x0 + last * dx, y0 + last * dy);
    addCycles(repeated, stops, style.spread, first, last);
    return repeated;
}
/// A radial gradient in the gradient's own space (center 0, 0, radius
/// 819.2), reaching out to cover \a gbox there.
function canvasRadial(ctx, style, c, gbox) {
    var stops = colorStops(style, c);
    var r = 819.2, fx = style.focal * r;
    if (style.spread === "pad" || !gbox) {
        var g = ctx.createRadialGradient(fx, 0, 0, 0, 0, r);
        stops.forEach(function (s) { g.addColorStop(phoenix.clamp(s[0], 0, 1), s[1]); });
        return g;
    }
    var reach = 0;
    [[gbox[0], gbox[1]], [gbox[2], gbox[1]], [gbox[0], gbox[3]], [gbox[2], gbox[3]]].forEach(function (p) {
        reach = Math.max(reach, Math.sqrt(p[0] * p[0] + p[1] * p[1]));
    });
    var cycles = Math.min(64, Math.max(1, Math.ceil(reach / r)));
    var repeated = ctx.createRadialGradient(0, 0, 0, 0, 0, r * cycles);
    addCycles(repeated, stops, style.spread, 0, cycles);
    return repeated;
}
/// \a source as a pattern, its pixels through \a c.
function canvasPattern(ctx, style, c) {
    var source = sourceOf(style.bitmap);
    if (!source)
        return null;
    if (c && !C.alphaOnly(c))
        source = phoenix._.tinted ? phoenix._.tinted(source, c) : source;
    var pattern = ctx.createPattern(source, style.repeat ? "repeat" : "no-repeat");
    if (pattern && pattern.setTransform && typeof DOMMatrix !== "undefined")
        pattern.setTransform(new DOMMatrix(style.m));
    return pattern;
}
function maskStyle() { return "#000"; }

/// Fills \a path with \a style; the context's transform is the shape's
/// space on the device.
function fillPath(ctx, path, rule, style, c, r, box) {
    if (r.maskPass) {
        ctx.fillStyle = maskStyle();
        ctx.fill(path, rule);
        return;
    }
    switch (style.type) {
    case "solid":
        ctx.fillStyle = C.css(style.rgb, style.alpha, c);
        ctx.fill(path, rule);
        return;
    case "linear":
        var linear = canvasLinear(ctx, style, c, box);
        if (!linear)
            return;
        ctx.fillStyle = linear;
        ctx.fill(path, rule);
        return;
    case "radial":
        // In the gradient's own space, inside the shape.
        ctx.save();
        ctx.clip(path, rule);
        var m = style.m;
        ctx.transform(m[0], m[1], m[2], m[3], m[4], m[5]);
        var gbox = gradientBox(m, box);
        ctx.fillStyle = canvasRadial(ctx, style, c, gbox);
        if (gbox)
            ctx.fillRect(gbox[0] - 1, gbox[1] - 1, gbox[2] - gbox[0] + 2, gbox[3] - gbox[1] + 2);
        ctx.restore();
        return;
    case "bitmap":
        var pattern = canvasPattern(ctx, style, c);
        if (!pattern)
            return;
        var alpha = ctx.globalAlpha;
        if (c && C.alphaOnly(c))
            ctx.globalAlpha = alpha * phoenix.clamp(c[3], 0, 1);
        ctx.imageSmoothingEnabled = style.smooth !== false;
        ctx.fillStyle = pattern;
        ctx.fill(path, rule);
        ctx.globalAlpha = alpha;
        return;
    }
}
function strokePath(ctx, path, line, c, r, m, box) {
    var width = line.width;
    var device = line.scaleMode === "none" || width === 0;
    ctx.lineCap = line.caps === "none" ? "butt" : line.caps;
    ctx.lineJoin = line.joints;
    ctx.miterLimit = line.miter;
    var style = line.fill;
    var paint;
    if (r.maskPass)
        paint = maskStyle();
    else if (style.type === "solid")
        paint = C.css(style.rgb, style.alpha, c);
    else if (style.type === "linear")
        paint = canvasLinear(ctx, style, c, box);
    else if (style.type === "radial") {
        // A stroke cannot clip itself: the gradient as the shape's space
        // has it, round its center.
        var g = style.m, scale = Math.sqrt(Math.abs(g[0] * g[3] - g[1] * g[2]));
        var stops = colorStops(style, c);
        paint = ctx.createRadialGradient(g[4] + style.focal * 819.2 * scale, g[5], 0, g[4], g[5], 819.2 * scale);
        stops.forEach(function (s) { paint.addColorStop(phoenix.clamp(s[0], 0, 1), s[1]); });
    } else
        paint = canvasPattern(ctx, style, c);
    if (!paint)
        return;
    ctx.strokeStyle = paint;
    if (device) {
        // Its width on the stage, whatever the shape's scale: the path taken
        // to the device and stroked there.
        var onDevice = new Path2D();
        onDevice.addPath(path, new DOMMatrix(m));
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.lineWidth = width === 0 ? 1 : width * r.stageScale;
        ctx.stroke(onDevice);
        ctx.restore();
        return;
    }
    ctx.lineWidth = width;
    ctx.stroke(path);
}

/// Draws \a graphics in a space \a m takes to the device, its colors
/// through \a c. \a r is the renderer's state (render.js).
function drawGraphics(ctx, graphics, m, c, r) {
    var ops = graphics._all();
    if (!ops.length)
        return;
    ctx.setTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
    var baseAlpha = ctx.globalAlpha;
    var alphaScale = 1;
    // Alpha alone is drawn by the canvas; a color transform goes into the
    // colors themselves.
    for (var i = 0; i < ops.length; ++i) {
        var op = ops[i];
        var clipped = op.clip !== undefined;
        if (clipped || op.blend) {
            ctx.save();
            if (clipped)
                ctx.clip(new Path2D(op.clip));
            if (op.blend && !r.maskPass)
                ctx.globalCompositeOperation = phoenix._.compositeOf(op.blend);
        }
        if (op.opacity !== undefined && !r.maskPass)
            ctx.globalAlpha = baseAlpha * op.opacity;
        if (op.k === "f") {
            var path = pathOf(op);
            if (path)
                fillPath(ctx, path, op.rule || "nonzero", op.style, c, r, op.box || graphics._bounds(false));
        } else if (op.k === "s") {
            var stroke = pathOf(op);
            if (stroke)
                strokePath(ctx, stroke, op.line, c, r, m, op.box || graphics._bounds(true));
        } else if (op.k === "i") {
            var source = sourceOf(op.image);
            if (source) {
                var im = M.multiply(op.m, m);
                ctx.save();
                ctx.setTransform(im[0], im[1], im[2], im[3], im[4], im[5]);
                if (r.maskPass) {
                    ctx.fillStyle = maskStyle();
                    ctx.fillRect(0, 0, source.width, source.height);
                } else {
                    if (c && !C.alphaOnly(c) && phoenix._.tinted)
                        source = phoenix._.tinted(source, c);
                    else if (c)
                        ctx.globalAlpha = ctx.globalAlpha * phoenix.clamp(c[3], 0, 1);
                    ctx.imageSmoothingEnabled = op.smooth !== false;
                    ctx.drawImage(source, 0, 0);
                }
                ctx.restore();
            }
        } else if (op.k === "t") {
            var tm = M.multiply(op.m, m);
            ctx.save();
            ctx.setTransform(tm[0], tm[1], tm[2], tm[3], tm[4], tm[5]);
            ctx.font = op.font;
            if ("letterSpacing" in ctx)
                ctx.letterSpacing = (op.ls || 0) + "px";
            ctx.textBaseline = "alphabetic";
            ctx.fillStyle = r.maskPass ? maskStyle() : op.style.type === "solid" ? C.css(op.style.rgb, op.style.alpha, c) : "#000";
            ctx.fillText(op.text, op.x, op.y);
            ctx.restore();
        }
        if (op.opacity !== undefined)
            ctx.globalAlpha = baseAlpha;
        if (clipped || op.blend)
            ctx.restore();
    }
    ctx.globalAlpha = baseAlpha;
}

// The graphics data classes, enough for drawGraphicsData.
class GraphicsSolidFill {
    constructor(color, alpha) { this.color = color >>> 0; this.alpha = alpha === undefined ? 1 : +alpha; }
    _applyTo(g) { g.beginFill(this.color, this.alpha); }
}
class GraphicsEndFill {
    _applyTo(g) { g.endFill(); }
}
class GraphicsStroke {
    constructor(thickness, pixelHinting, scaleMode, caps, joints, miterLimit, fill) {
        this.thickness = thickness === undefined ? NaN : thickness;
        this.pixelHinting = !!pixelHinting;
        this.scaleMode = scaleMode || "normal";
        this.caps = caps || "none";
        this.joints = joints || "round";
        this.miterLimit = miterLimit === undefined ? 3 : miterLimit;
        this.fill = fill || null;
    }
    _applyTo(g) {
        var fill = this.fill;
        g.lineStyle(this.thickness, fill && fill.color !== undefined ? fill.color : 0, fill && fill.alpha !== undefined ? fill.alpha : 1,
            this.pixelHinting, this.scaleMode, this.caps, this.joints, this.miterLimit);
    }
}
class GraphicsPath {
    constructor(commands, data, winding) {
        this.commands = commands || [];
        this.data = data || [];
        this.winding = winding || "evenOdd";
    }
    moveTo(x, y) { this.commands.push(1); this.data.push(x, y); }
    lineTo(x, y) { this.commands.push(2); this.data.push(x, y); }
    curveTo(cx, cy, x, y) { this.commands.push(3); this.data.push(cx, cy, x, y); }
    cubicCurveTo(c1x, c1y, c2x, c2y, x, y) { this.commands.push(6); this.data.push(c1x, c1y, c2x, c2y, x, y); }
    wideMoveTo(x, y) { this.commands.push(4); this.data.push(0, 0, x, y); }
    wideLineTo(x, y) { this.commands.push(5); this.data.push(0, 0, x, y); }
    _applyTo(g) { g.drawPath(this.commands, this.data, this.winding); }
}
class GraphicsGradientFill {
    constructor(type, colors, alphas, ratios, matrix, spreadMethod, interpolationMethod, focalPointRatio) {
        Object.assign(this, {type: type || "linear", colors: colors || [], alphas: alphas || [], ratios: ratios || [],
            matrix: matrix || null, spreadMethod: spreadMethod || "pad", interpolationMethod: interpolationMethod || "rgb",
            focalPointRatio: focalPointRatio || 0});
    }
    _applyTo(g) {
        g.beginGradientFill(this.type, this.colors, this.alphas, this.ratios, this.matrix, this.spreadMethod,
            this.interpolationMethod, this.focalPointRatio);
    }
}
class GraphicsBitmapFill {
    constructor(bitmapData, matrix, repeat, smooth) {
        this.bitmapData = bitmapData || null;
        this.matrix = matrix || null;
        this.repeat = repeat === undefined ? true : !!repeat;
        this.smooth = !!smooth;
    }
    _applyTo(g) { g.beginBitmapFill(this.bitmapData, this.matrix, this.repeat, this.smooth); }
}

phoenix.defineClass("flash.display.Graphics", Graphics);
phoenix.defineClass("flash.display.GraphicsSolidFill", GraphicsSolidFill);
phoenix.defineClass("flash.display.GraphicsEndFill", GraphicsEndFill);
phoenix.defineClass("flash.display.GraphicsStroke", GraphicsStroke);
phoenix.defineClass("flash.display.GraphicsPath", GraphicsPath);
phoenix.defineClass("flash.display.GraphicsGradientFill", GraphicsGradientFill);
phoenix.defineClass("flash.display.GraphicsBitmapFill", GraphicsBitmapFill);
phoenix._.drawGraphics = drawGraphics;
phoenix._.fillPath = fillPath;
phoenix._.pathOf = pathOf;
phoenix._.colorStops = colorStops;
})(phoenix);
