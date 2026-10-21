// The renderer: draws the display list onto a canvas. Objects with
// filters, a mask, or a blend mode other than normal are drawn on a
// picture of their own (a layer) that is then filtered and composited;
// the rest go straight onto the canvas, their color transforms worked into
// their colors. Timeline masks (a mask layer over the layers it masks) are
// drawn the same way, the masked objects' layer cut to the mask's shape.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;

var composites = {
    normal: "source-over", layer: "source-over", multiply: "multiply", screen: "screen", lighten: "lighten",
    darken: "darken", difference: "difference", add: "lighter", subtract: "difference", invert: "difference",
    alpha: "destination-in", erase: "destination-out", overlay: "overlay", hardlight: "hard-light", shader: "source-over"
};
// The canvas's own operations, which CreateJS's compositeOperation names.
var canvasOperations = ["source-over", "source-in", "source-out", "source-atop", "destination-over", "destination-in",
    "destination-out", "destination-atop", "lighter", "copy", "xor", "multiply", "screen", "overlay", "darken", "lighten",
    "color-dodge", "color-burn", "hard-light", "soft-light", "difference", "exclusion", "hue", "saturation", "color",
    "luminosity"];
function compositeOf(blend) {
    return composites[blend] || (canvasOperations.indexOf(blend) >= 0 ? blend : "source-over");
}
phoenix._.compositeOf = compositeOf;

/// Pictures through a color transform, kept per picture and transform
/// until the picture changes.
var tintCache = typeof WeakMap !== "undefined" ? new WeakMap() : null;
function tinted(source, c) {
    var revision = source._phoenixRevision || 0;
    var key = c.map(function (v) { return Math.round(v * 1000) / 1000; }).join(",");
    var entry = tintCache && tintCache.get(source);
    if (!entry || entry.revision !== revision) {
        entry = {revision: revision, pictures: new Map()};
        if (tintCache)
            tintCache.set(source, entry);
    }
    var made = entry.pictures.get(key);
    if (made)
        return made;
    var w = source.width, h = source.height;
    made = phoenix.makeCanvas(w, h);
    var ctx = made.getContext("2d");
    ctx.drawImage(source, 0, 0);
    try {
        var image = ctx.getImageData(0, 0, w, h), d = image.data;
        for (var i = 0; i < d.length; i += 4) {
            d[i] = d[i] * c[0] + c[4];
            d[i + 1] = d[i + 1] * c[1] + c[5];
            d[i + 2] = d[i + 2] * c[2] + c[6];
            d[i + 3] = d[i + 3] * c[3] + c[7];
        }
        ctx.putImageData(image, 0, 0);
    } catch (error) {
        // A picture from elsewhere the page may not read: alpha alone.
        ctx.clearRect(0, 0, w, h);
        ctx.globalAlpha = phoenix.clamp(c[3], 0, 1);
        ctx.drawImage(source, 0, 0);
    }
    if (entry.pictures.size > 32)
        entry.pictures.clear();
    entry.pictures.set(key, made);
    return made;
}
phoenix._.tinted = tinted;

/// A layer's picture through the color transform \a c.
function colorLayer(canvas, c) {
    if (C.isIdentity(c) || C.alphaOnly(c))
        return canvas;
    var ctx = canvas.getContext("2d");
    var image = ctx.getImageData(0, 0, canvas.width, canvas.height), d = image.data;
    for (var i = 0; i < d.length; i += 4) {
        if (!d[i + 3] && c[7] <= 0)
            continue;
        d[i] = d[i] * c[0] + c[4];
        d[i + 1] = d[i + 1] * c[1] + c[5];
        d[i + 2] = d[i + 2] * c[2] + c[6];
        d[i + 3] = d[i + 3] * c[3] + c[7];
    }
    ctx.putImageData(image, 0, 0);
    return canvas;
}

/// The renderer's state for one drawing: device pixels a stage pixel,
/// the device's box, and whether a mask's coverage is being drawn.
function state(stageToDevice, scale, viewport) {
    return {stageToDevice: stageToDevice, stageScale: scale, viewport: viewport, maskPass: false, inLayer: false, depth: 0};
}

/// The box \a object covers on the device, given its matrix there.
function deviceBox(object, m) {
    var box = object._localBounds(true);
    if (!box)
        return null;
    return M.mapBox(m, box);
}
function inflate(box, by) {
    return [box[0] - by, box[1] - by, box[2] + by, box[3] + by];
}
function intersect(a, b) {
    var box = [Math.max(a[0], b[0]), Math.max(a[1], b[1]), Math.min(a[2], b[2]), Math.min(a[3], b[3])];
    return box[2] > box[0] && box[3] > box[1] ? box : null;
}
function whole(box) {
    return [Math.floor(box[0]), Math.floor(box[1]), Math.ceil(box[2]), Math.ceil(box[3])];
}
var maxLayer = 8192;

/// The matrix \a object draws its content with: its own, on its parent's
/// space on the device, scrolled by its scroll rect.
function matrixOf(object, parentM) {
    var m = M.multiply(object._m, parentM);
    if (object._scrollRect)
        m = M.multiply([1, 0, 0, 1, -object._scrollRect.x, -object._scrollRect.y], m);
    return m;
}

function renderObject(ctx, object, parentM, parentC, r) {
    if (!object._visible && !r.drawingMask)
        return;
    if (object._maskOwner && !r.drawingMask)
        return;
    if (r.depth > 64)
        return;
    var m = matrixOf(object, parentM);
    var c = r.maskPass ? parentC : C.multiply(parentC, C.fromTransform(object._colorTransform));
    if (!r.maskPass && c[3] <= 0 && c[7] <= 0)
        return;
    var blend = object._blendMode;
    // Alpha and erase work on a layer their parent makes; outside one there
    // is nothing for them to work on, and they draw nothing.
    if ((blend === "alpha" || blend === "erase") && !r.inLayer)
        return;
    var layered = !r.maskPass && (object._filters.length > 0 || !!object._mask ||
        (blend !== "normal" && blend !== "shader") || object._opaqueBackground !== null);
    ++r.depth;
    try {
        if (!layered && object._scale9Grid && !object._m[1] && !object._m[2] && drawNineSlice(ctx, object, parentM, c, r))
            return;
        if (!layered) {
            if (object._scrollRect) {
                ctx.save();
                clipToScroll(ctx, object, parentM);
                object._draw(ctx, m, c, r);
                ctx.restore();
            } else
                object._draw(ctx, m, c, r);
            return;
        }
        drawLayered(ctx, object, parentM, m, c, blend, r);
    } finally {
        --r.depth;
    }
}
/// 9-slice scaling: the object's content cut by its grid into nine cells,
/// the corners kept at their size, the edges stretched one way and the
/// middle both, to fill the box its scale gives it. Each cell is its
/// content drawn through that cell's own stretch, clipped to the cell.
/// With more guides, the strips between them take turns, the first kept at
/// its size, the next stretched, and so on.
function drawNineSlice(ctx, object, parentM, c, r) {
    var box = object._localBounds(false), grid = object._scale9Grid;
    if (!box)
        return false;
    var m = object._m;
    var cut = function (low, high, guides, scale) {
        var from = [low];
        for (var g = 0; g < guides.length; ++g)
            from.push(Math.max(low, Math.min(high, guides[g])));
        from.push(high);
        var reach = (high - low) * Math.abs(scale);
        var fixed = 0, stretching = 0;
        for (var s = 0; s + 1 < from.length; ++s) {
            if (s % 2 === 0)
                fixed += from[s + 1] - from[s];
            else
                stretching += from[s + 1] - from[s];
        }
        var k = fixed > reach && fixed > 0 ? reach / fixed : 1;
        var room = Math.max(0, reach - fixed * k);
        var sign = scale < 0 ? -1 : 1;
        var to = [low * scale];
        for (var t = 0; t + 1 < from.length; ++t) {
            var width = from[t + 1] - from[t];
            var placed = t % 2 === 0 ? width * k : (stretching > 0 ? width / stretching * room : 0);
            to.push(to[t] + sign * placed);
        }
        return {from: from, to: to};
    };
    var slices = object._slices;
    var xs = cut(box[0], box[2], slices ? slices[0] : [grid.x, grid.x + grid.width], m[0]);
    var ys = cut(box[1], box[3], slices ? slices[1] : [grid.y, grid.y + grid.height], m[3]);
    for (var j = 0; j + 1 < ys.from.length; ++j) {
        for (var i = 0; i + 1 < xs.from.length; ++i) {
            var fx = xs.from[i + 1] - xs.from[i], fy = ys.from[j + 1] - ys.from[j];
            var tx = xs.to[i + 1] - xs.to[i], ty = ys.to[j + 1] - ys.to[j];
            if (fx <= 0 || fy <= 0 || !tx || !ty)
                continue;
            var sx = tx / fx, sy = ty / fy;
            var cell = [sx, 0, 0, sy, xs.to[i] - xs.from[i] * sx + m[4], ys.to[j] - ys.from[j] * sy + m[5]];
            ctx.save();
            ctx.setTransform(parentM[0], parentM[1], parentM[2], parentM[3], parentM[4], parentM[5]);
            ctx.beginPath();
            var x0 = Math.min(xs.to[i], xs.to[i + 1]) + m[4], y0 = Math.min(ys.to[j], ys.to[j + 1]) + m[5];
            // A hair over, so the cells meet without a seam.
            ctx.rect(x0 - 0.25, y0 - 0.25, Math.abs(tx) + 0.5, Math.abs(ty) + 0.5);
            ctx.clip();
            object._draw(ctx, M.multiply(cell, parentM), c, r);
            ctx.restore();
        }
    }
    return true;
}

function clipToScroll(ctx, object, parentM) {
    var sm = M.multiply(object._m, parentM), s = object._scrollRect;
    ctx.setTransform(sm[0], sm[1], sm[2], sm[3], sm[4], sm[5]);
    ctx.beginPath();
    ctx.rect(0, 0, s.width, s.height);
    ctx.clip();
}

function drawLayered(ctx, object, parentM, m, c, blend, r) {
    var margin = 0;
    for (var i = 0; i < object._filters.length; ++i)
        margin += object._filters[i]._margin(r.stageScale);
    var box = object._scrollRect ? M.mapBox(M.multiply(object._m, parentM), [0, 0, object._scrollRect.width, object._scrollRect.height])
        : deviceBox(object, m);
    if (!box)
        return;
    box = intersect(inflate(box, margin), inflate(r.viewport, margin));
    if (!box)
        return;
    box = whole(box);
    var w = box[2] - box[0], h = box[3] - box[1];
    if (w <= 0 || h <= 0 || w > maxLayer || h > maxLayer)
        return;
    var layer = phoenix.makeCanvas(w, h);
    var lctx = layer.getContext("2d");
    var shift = [1, 0, 0, 1, -box[0], -box[1]];
    var inner = Object.assign({}, r, {viewport: [0, 0, w, h], inLayer: true});
    if (object._opaqueBackground !== null) {
        var bbox = object._localBounds(false);
        if (bbox) {
            var bm = M.multiply(m, shift);
            lctx.setTransform(bm[0], bm[1], bm[2], bm[3], bm[4], bm[5]);
            lctx.fillStyle = C.css(object._opaqueBackground, 1, null);
            lctx.fillRect(bbox[0], bbox[1], bbox[2] - bbox[0], bbox[3] - bbox[1]);
        }
    }
    if (object._scrollRect) {
        lctx.save();
        clipToScroll(lctx, object, M.multiply(parentM, shift));
    }
    // Filtered, the object's own color transform goes on after the filters,
    // so a faded object's shadow fades with it.
    var filtered = object._filters.length > 0;
    object._draw(lctx, M.multiply(m, shift), filtered ? C.identity : c, inner);
    if (object._scrollRect)
        lctx.restore();
    lctx.setTransform(1, 0, 0, 1, 0, 0);
    if (object._mask) {
        var mask = object._mask;
        var maskLayer = phoenix.makeCanvas(w, h);
        var mctx = maskLayer.getContext("2d");
        // A mask off the display list is in the stage's space, or (CreateJS's
        // way) in the space of what it masks.
        var maskSpace = mask._parent || (mask._maskInOwnerSpace && object._parent);
        var maskParent = maskSpace ? M.multiply(maskSpace._concatenatedMatrix(), r.stageToDevice) : r.stageToDevice;
        renderObject(mctx, mask, M.multiply(maskParent, shift), C.identity,
            Object.assign({}, inner, {maskPass: true, drawingMask: true}));
        lctx.globalCompositeOperation = "destination-in";
        lctx.drawImage(maskLayer, 0, 0);
        lctx.globalCompositeOperation = "source-over";
    }
    for (var f = 0; f < object._filters.length; ++f) {
        try {
            layer = object._filters[f]._apply(layer, r.stageScale) || layer;
        } catch (error) {
            phoenix._.reportError(error, "filter");
        }
    }
    if (filtered)
        layer = colorLayer(layer, c);
    if ((blend === "subtract" || blend === "invert") && blendPixels(ctx, layer, box, blend, filtered && C.alphaOnly(c) ? c[3] : 1))
        return;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    if (filtered && C.alphaOnly(c))
        ctx.globalAlpha = ctx.globalAlpha * phoenix.clamp(c[3], 0, 1);
    ctx.globalCompositeOperation = compositeOf(blend);
    ctx.drawImage(layer, box[0], box[1]);
    ctx.restore();
}
/// The blend modes the canvas has no operation for, worked on the pixels:
/// subtract takes the object's colors from what is under it, invert turns
/// what is under it to its opposite where the object covers it. False when
/// the canvas's pixels cannot be read.
function blendPixels(ctx, layer, box, blend, alpha) {
    var w = layer.width, h = layer.height;
    var under, over;
    try {
        under = ctx.getImageData(box[0], box[1], w, h);
        over = layer.getContext("2d").getImageData(0, 0, w, h).data;
    } catch (error) {
        return false;
    }
    var d = under.data;
    for (var i = 0; i < d.length; i += 4) {
        var a = over[i + 3] / 255 * alpha;
        if (a <= 0)
            continue;
        for (var k = 0; k < 3; ++k) {
            var made = blend === "invert" ? 255 - d[i + k] : Math.max(0, d[i + k] - over[i + k]);
            d[i + k] = d[i + k] + (made - d[i + k]) * a;
        }
    }
    ctx.putImageData(under, box[0], box[1]);
    return true;
}

/// A container's children in order, each timeline mask with the objects
/// it masks.
function drawChildren(ctx, container, m, c, r) {
    var children = container._children;
    for (var i = 0; i < children.length; ++i) {
        var child = children[i];
        var group = child._maskGroup !== null ? child._maskGroup : child._clipGroup;
        if (group !== null && group !== undefined) {
            var masks = [], masked = [];
            var j = i;
            while (j < children.length && (children[j]._maskGroup === group || children[j]._clipGroup === group)) {
                (children[j]._maskGroup === group ? masks : masked).push(children[j]);
                ++j;
            }
            i = j - 1;
            if (r.maskPass) {
                // A mask inside a mask: what it masks counts.
                masked.forEach(function (each) { renderObject(ctx, each, m, c, r); });
                continue;
            }
            drawMasked(ctx, masks, masked, m, c, r);
            continue;
        }
        renderObject(ctx, child, m, c, r);
    }
}
function drawMasked(ctx, masks, masked, m, c, r) {
    if (!masked.length)
        return;
    if (!masks.length) {
        masked.forEach(function (each) { renderObject(ctx, each, m, c, r); });
        return;
    }
    var box = null;
    masks.forEach(function (each) {
        box = M.union(box, deviceBox(each, matrixOf(each, m)));
    });
    if (!box)
        return;
    box = intersect(box, r.viewport);
    if (!box)
        return;
    box = whole(box);
    var w = box[2] - box[0], h = box[3] - box[1];
    if (w <= 0 || h <= 0 || w > maxLayer || h > maxLayer)
        return;
    var shift = [1, 0, 0, 1, -box[0], -box[1]];
    var sm = M.multiply(m, shift);
    var layer = phoenix.makeCanvas(w, h), lctx = layer.getContext("2d");
    var inner = Object.assign({}, r, {viewport: [0, 0, w, h], inLayer: true});
    masked.forEach(function (each) { renderObject(lctx, each, sm, c, inner); });
    var maskLayer = phoenix.makeCanvas(w, h), mctx = maskLayer.getContext("2d");
    var maskState = Object.assign({}, inner, {maskPass: true});
    masks.forEach(function (each) { renderObject(mctx, each, sm, C.identity, maskState); });
    lctx.setTransform(1, 0, 0, 1, 0, 0);
    lctx.globalCompositeOperation = "destination-in";
    lctx.drawImage(maskLayer, 0, 0);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.drawImage(layer, box[0], box[1]);
    ctx.restore();
}

// Each kind of object's own drawing.
var display = flash.display;
display.DisplayObjectContainer.prototype._draw = function (ctx, m, c, r) {
    drawChildren(ctx, this, m, c, r);
};
display.Sprite.prototype._draw = function (ctx, m, c, r) {
    if (this._graphics)
        phoenix._.drawGraphics(ctx, this._graphics, m, c, r);
    drawChildren(ctx, this, m, c, r);
};
display.Shape.prototype._draw = function (ctx, m, c, r) {
    if (this._graphics)
        phoenix._.drawGraphics(ctx, this._graphics, m, c, r);
};

/// Draws the stage onto \a canvas: its color, then the display list, the
/// stage's space taken onto the canvas by \a stageToDevice.
function drawStage(stage, canvas, stageToDevice, scale) {
    var ctx = canvas.getContext("2d");
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    ctx.fillStyle = C.css(stage._color, 1, null);
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    var r = state(stageToDevice, scale, [0, 0, canvas.width, canvas.height]);
    drawChildren(ctx, stage, stageToDevice, C.identity, r);
    ctx.setTransform(1, 0, 0, 1, 0, 0);
}

/// Draws \a object into a canvas by \a matrix (BitmapData.draw).
function drawInto(canvas, object, matrix, colorTransform, blendMode, clipRect, smoothing) {
    var ctx = canvas.getContext("2d");
    var r = state(M.identity, M.scaleOf(matrix), [0, 0, canvas.width, canvas.height]);
    r.inLayer = true;
    ctx.save();
    if (clipRect) {
        ctx.beginPath();
        ctx.rect(clipRect.x, clipRect.y, clipRect.width, clipRect.height);
        ctx.clip();
    }
    ctx.imageSmoothingEnabled = smoothing !== false;
    if (blendMode && blendMode !== "normal")
        ctx.globalCompositeOperation = compositeOf(blendMode);
    var c = colorTransform ? C.fromTransform(colorTransform) : C.identity;
    if (object instanceof display.DisplayObject) {
        // The object's own transform is not used: \a matrix places it.
        var saved = object._m, visible = object._visible;
        object._m = [1, 0, 0, 1, 0, 0];
        object._visible = true;
        try {
            renderObject(ctx, object, matrix, c, r);
        } finally {
            object._m = saved;
            object._visible = visible;
        }
    } else if (object && object._canvasForDraw) {
        var source = object._canvasForDraw();
        if (!C.alphaOnly(c))
            source = tinted(source, c);
        else
            ctx.globalAlpha = phoenix.clamp(c[3], 0, 1);
        ctx.setTransform(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
        ctx.drawImage(source, 0, 0);
    }
    ctx.restore();
}

phoenix._.renderObject = renderObject;
phoenix._.drawChildren = drawChildren;
phoenix._.drawStage = drawStage;
phoenix._.drawInto = drawInto;
})(phoenix);
