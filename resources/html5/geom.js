// flash.geom: Point, Rectangle, Matrix, ColorTransform and Transform.

(function (phoenix) {
"use strict";

class Point {
    constructor(x, y) {
        this.x = x === undefined ? 0 : +x;
        this.y = y === undefined ? 0 : +y;
    }
    get length() { return Math.sqrt(this.x * this.x + this.y * this.y); }
    add(v) { return new Point(this.x + v.x, this.y + v.y); }
    subtract(v) { return new Point(this.x - v.x, this.y - v.y); }
    clone() { return new Point(this.x, this.y); }
    copyFrom(p) { this.x = p.x; this.y = p.y; }
    equals(p) { return !!p && p.x === this.x && p.y === this.y; }
    normalize(thickness) {
        var length = this.length;
        if (length > 0) {
            var k = thickness / length;
            this.x *= k;
            this.y *= k;
        }
    }
    offset(dx, dy) { this.x += dx; this.y += dy; }
    setTo(x, y) { this.x = x; this.y = y; }
    toString() { return "(x=" + this.x + ", y=" + this.y + ")"; }
    static distance(a, b) { return Math.sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
    static interpolate(a, b, f) { return new Point(b.x + (a.x - b.x) * f, b.y + (a.y - b.y) * f); }
    static polar(length, angle) { return new Point(length * Math.cos(angle), length * Math.sin(angle)); }
}

class Rectangle {
    constructor(x, y, width, height) {
        this.x = x === undefined ? 0 : +x;
        this.y = y === undefined ? 0 : +y;
        this.width = width === undefined ? 0 : +width;
        this.height = height === undefined ? 0 : +height;
    }
    get left() { return this.x; }
    set left(v) { this.width += this.x - v; this.x = v; }
    get right() { return this.x + this.width; }
    set right(v) { this.width = v - this.x; }
    get top() { return this.y; }
    set top(v) { this.height += this.y - v; this.y = v; }
    get bottom() { return this.y + this.height; }
    set bottom(v) { this.height = v - this.y; }
    get topLeft() { return new Point(this.x, this.y); }
    set topLeft(p) { this.left = p.x; this.top = p.y; }
    get bottomRight() { return new Point(this.right, this.bottom); }
    set bottomRight(p) { this.right = p.x; this.bottom = p.y; }
    get size() { return new Point(this.width, this.height); }
    set size(p) { this.width = p.x; this.height = p.y; }
    clone() { return new Rectangle(this.x, this.y, this.width, this.height); }
    contains(x, y) { return x >= this.x && y >= this.y && x < this.right && y < this.bottom; }
    containsPoint(p) { return this.contains(p.x, p.y); }
    containsRect(r) {
        if (r.isEmpty())
            return false;
        return r.x >= this.x && r.y >= this.y && r.right <= this.right && r.bottom <= this.bottom;
    }
    copyFrom(r) { this.x = r.x; this.y = r.y; this.width = r.width; this.height = r.height; }
    equals(r) { return !!r && r.x === this.x && r.y === this.y && r.width === this.width && r.height === this.height; }
    inflate(dx, dy) { this.x -= dx; this.width += 2 * dx; this.y -= dy; this.height += 2 * dy; }
    inflatePoint(p) { this.inflate(p.x, p.y); }
    intersection(r) {
        var x0 = Math.max(this.x, r.x), y0 = Math.max(this.y, r.y);
        var x1 = Math.min(this.right, r.right), y1 = Math.min(this.bottom, r.bottom);
        if (x1 <= x0 || y1 <= y0)
            return new Rectangle();
        return new Rectangle(x0, y0, x1 - x0, y1 - y0);
    }
    intersects(r) {
        return Math.max(this.x, r.x) < Math.min(this.right, r.right) &&
            Math.max(this.y, r.y) < Math.min(this.bottom, r.bottom);
    }
    isEmpty() { return this.width <= 0 || this.height <= 0; }
    offset(dx, dy) { this.x += dx; this.y += dy; }
    offsetPoint(p) { this.offset(p.x, p.y); }
    setEmpty() { this.x = this.y = this.width = this.height = 0; }
    setTo(x, y, width, height) { this.x = x; this.y = y; this.width = width; this.height = height; }
    union(r) {
        if (this.isEmpty())
            return r.clone();
        if (r.isEmpty())
            return this.clone();
        var x0 = Math.min(this.x, r.x), y0 = Math.min(this.y, r.y);
        return new Rectangle(x0, y0, Math.max(this.right, r.right) - x0, Math.max(this.bottom, r.bottom) - y0);
    }
    toString() { return "(x=" + this.x + ", y=" + this.y + ", w=" + this.width + ", h=" + this.height + ")"; }
}

class Matrix {
    constructor(a, b, c, d, tx, ty) {
        this.a = a === undefined ? 1 : +a;
        this.b = b === undefined ? 0 : +b;
        this.c = c === undefined ? 0 : +c;
        this.d = d === undefined ? 1 : +d;
        this.tx = tx === undefined ? 0 : +tx;
        this.ty = ty === undefined ? 0 : +ty;
    }
    clone() { return new Matrix(this.a, this.b, this.c, this.d, this.tx, this.ty); }
    copyFrom(m) { this.a = m.a; this.b = m.b; this.c = m.c; this.d = m.d; this.tx = m.tx; this.ty = m.ty; }
    setTo(a, b, c, d, tx, ty) { this.a = a; this.b = b; this.c = c; this.d = d; this.tx = tx; this.ty = ty; }
    identity() { this.setTo(1, 0, 0, 1, 0, 0); }
    /// This matrix followed by \a m (this, then m).
    concat(m) {
        var a = this.a * m.a + this.b * m.c, b = this.a * m.b + this.b * m.d;
        var c = this.c * m.a + this.d * m.c, d = this.c * m.b + this.d * m.d;
        var tx = this.tx * m.a + this.ty * m.c + m.tx, ty = this.tx * m.b + this.ty * m.d + m.ty;
        this.setTo(a, b, c, d, tx, ty);
    }
    invert() {
        var det = this.a * this.d - this.b * this.c;
        if (det === 0) {
            this.setTo(0, 0, 0, 0, -this.tx, -this.ty);
            return;
        }
        var a = this.d / det, b = -this.b / det, c = -this.c / det, d = this.a / det;
        var tx = -(a * this.tx + c * this.ty), ty = -(b * this.tx + d * this.ty);
        this.setTo(a, b, c, d, tx, ty);
    }
    rotate(angle) {
        var cos = Math.cos(angle), sin = Math.sin(angle);
        this.concat(new Matrix(cos, sin, -sin, cos, 0, 0));
    }
    scale(sx, sy) { this.concat(new Matrix(sx, 0, 0, sy, 0, 0)); }
    translate(dx, dy) { this.tx += dx; this.ty += dy; }
    createBox(scaleX, scaleY, rotation, tx, ty) {
        rotation = rotation || 0;
        var cos = Math.cos(rotation), sin = Math.sin(rotation);
        // As the player builds it: turned, then scaled along the stage's
        // axes (so b goes with the y scale and c with the x).
        this.setTo(scaleX * cos, scaleY * sin, -scaleX * sin, scaleY * cos, tx || 0, ty || 0);
    }
    /// The matrix beginGradientFill takes: the gradient square (-819.2 to
    /// 819.2 on each side) onto a box.
    createGradientBox(width, height, rotation, tx, ty) {
        this.createBox(width / 1638.4, height / 1638.4, rotation || 0, (tx || 0) + width / 2, (ty || 0) + height / 2);
    }
    deltaTransformPoint(p) { return new Point(this.a * p.x + this.c * p.y, this.b * p.x + this.d * p.y); }
    transformPoint(p) { return new Point(this.a * p.x + this.c * p.y + this.tx, this.b * p.x + this.d * p.y + this.ty); }
    copyColumnFrom(column, v) {
        if (column === 0) { this.a = v.x; this.b = v.y; }
        else if (column === 1) { this.c = v.x; this.d = v.y; }
        else { this.tx = v.x; this.ty = v.y; }
    }
    copyColumnTo(column, v) {
        if (column === 0) { v.x = this.a; v.y = this.b; v.z = 0; }
        else if (column === 1) { v.x = this.c; v.y = this.d; v.z = 0; }
        else { v.x = this.tx; v.y = this.ty; v.z = 1; }
    }
    copyRowFrom(row, v) {
        if (row === 0) { this.a = v.x; this.c = v.y; this.tx = v.z; }
        else if (row === 1) { this.b = v.x; this.d = v.y; this.ty = v.z; }
    }
    copyRowTo(row, v) {
        if (row === 0) { v.x = this.a; v.y = this.c; v.z = this.tx; }
        else if (row === 1) { v.x = this.b; v.y = this.d; v.z = this.ty; }
        else { v.x = 0; v.y = 0; v.z = 1; }
    }
    toString() {
        return "(a=" + this.a + ", b=" + this.b + ", c=" + this.c + ", d=" + this.d + ", tx=" + this.tx + ", ty=" + this.ty + ")";
    }
}

/// Plain affine math on [a, b, c, d, tx, ty] arrays, for the renderer.
var M = {
    identity: [1, 0, 0, 1, 0, 0],
    /// \a m then \a n.
    multiply: function (m, n) {
        return [m[0] * n[0] + m[1] * n[2], m[0] * n[1] + m[1] * n[3],
            m[2] * n[0] + m[3] * n[2], m[2] * n[1] + m[3] * n[3],
            m[4] * n[0] + m[5] * n[2] + n[4], m[4] * n[1] + m[5] * n[3] + n[5]];
    },
    invert: function (m) {
        var det = m[0] * m[3] - m[1] * m[2];
        if (!det)
            return null;
        var a = m[3] / det, b = -m[1] / det, c = -m[2] / det, d = m[0] / det;
        return [a, b, c, d, -(a * m[4] + c * m[5]), -(b * m[4] + d * m[5])];
    },
    apply: function (m, x, y) {
        return [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]];
    },
    /// The box \a box ([x0, y0, x1, y1]) covers once mapped by \a m.
    mapBox: function (m, box) {
        if (!box)
            return null;
        var xs = [], ys = [];
        [[box[0], box[1]], [box[2], box[1]], [box[0], box[3]], [box[2], box[3]]].forEach(function (p) {
            var q = M.apply(m, p[0], p[1]);
            xs.push(q[0]);
            ys.push(q[1]);
        });
        return [Math.min.apply(null, xs), Math.min.apply(null, ys), Math.max.apply(null, xs), Math.max.apply(null, ys)];
    },
    union: function (a, b) {
        if (!a)
            return b ? b.slice() : null;
        if (!b)
            return a.slice();
        return [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[2], b[2]), Math.max(a[3], b[3])];
    },
    fromMatrix: function (m) { return [m.a, m.b, m.c, m.d, m.tx, m.ty]; },
    toMatrix: function (m) { return new Matrix(m[0], m[1], m[2], m[3], m[4], m[5]); },
    /// Whether \a m only turns and scales evenly (no skew or squash).
    similar: function (m) {
        return Math.abs(m[0] - m[3]) < 1e-9 * (1 + Math.abs(m[0])) && Math.abs(m[1] + m[2]) < 1e-9 * (1 + Math.abs(m[1]));
    },
    /// The scale \a m gives lengths, on average.
    scaleOf: function (m) { return Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2])); }
};

class ColorTransform {
    constructor(redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier, redOffset, greenOffset, blueOffset, alphaOffset) {
        this.redMultiplier = redMultiplier === undefined ? 1 : +redMultiplier;
        this.greenMultiplier = greenMultiplier === undefined ? 1 : +greenMultiplier;
        this.blueMultiplier = blueMultiplier === undefined ? 1 : +blueMultiplier;
        this.alphaMultiplier = alphaMultiplier === undefined ? 1 : +alphaMultiplier;
        this.redOffset = redOffset === undefined ? 0 : +redOffset;
        this.greenOffset = greenOffset === undefined ? 0 : +greenOffset;
        this.blueOffset = blueOffset === undefined ? 0 : +blueOffset;
        this.alphaOffset = alphaOffset === undefined ? 0 : +alphaOffset;
    }
    /// Setting color makes the transform a tint to that color.
    get color() {
        return (this.redOffset << 16) | (this.greenOffset << 8) | this.blueOffset;
    }
    set color(value) {
        this.redMultiplier = this.greenMultiplier = this.blueMultiplier = 0;
        this.redOffset = (value >> 16) & 255;
        this.greenOffset = (value >> 8) & 255;
        this.blueOffset = value & 255;
    }
    /// This, then \a second, as the display list combines a child's
    /// transform with its parent's.
    concat(second) {
        this.redOffset += second.redOffset * this.redMultiplier;
        this.greenOffset += second.greenOffset * this.greenMultiplier;
        this.blueOffset += second.blueOffset * this.blueMultiplier;
        this.alphaOffset += second.alphaOffset * this.alphaMultiplier;
        this.redMultiplier *= second.redMultiplier;
        this.greenMultiplier *= second.greenMultiplier;
        this.blueMultiplier *= second.blueMultiplier;
        this.alphaMultiplier *= second.alphaMultiplier;
    }
    clone() {
        return new ColorTransform(this.redMultiplier, this.greenMultiplier, this.blueMultiplier, this.alphaMultiplier,
            this.redOffset, this.greenOffset, this.blueOffset, this.alphaOffset);
    }
    toString() {
        return "(redMultiplier=" + this.redMultiplier + ", greenMultiplier=" + this.greenMultiplier +
            ", blueMultiplier=" + this.blueMultiplier + ", alphaMultiplier=" + this.alphaMultiplier +
            ", redOffset=" + this.redOffset + ", greenOffset=" + this.greenOffset + ", blueOffset=" + this.blueOffset +
            ", alphaOffset=" + this.alphaOffset + ")";
    }
}

/// Plain color transform math on [rm, gm, bm, am, ro, go, bo, ao] arrays.
var C = {
    identity: [1, 1, 1, 1, 0, 0, 0, 0],
    fromTransform: function (t) {
        return [t.redMultiplier, t.greenMultiplier, t.blueMultiplier, t.alphaMultiplier,
            t.redOffset, t.greenOffset, t.blueOffset, t.alphaOffset];
    },
    /// The child's \a inner seen through its parent's \a outer.
    multiply: function (outer, inner) {
        return [outer[0] * inner[0], outer[1] * inner[1], outer[2] * inner[2], outer[3] * inner[3],
            outer[4] + outer[0] * inner[4], outer[5] + outer[1] * inner[5],
            outer[6] + outer[2] * inner[6], outer[7] + outer[3] * inner[7]];
    },
    isIdentity: function (c) {
        return c[0] === 1 && c[1] === 1 && c[2] === 1 && c[3] === 1 && !c[4] && !c[5] && !c[6] && !c[7];
    },
    /// Only alpha changes, by a multiplier.
    alphaOnly: function (c) {
        return c[0] === 1 && c[1] === 1 && c[2] === 1 && !c[4] && !c[5] && !c[6] && !c[7];
    },
    /// \a rgb (0xRRGGBB) and \a alpha (0 to 1) through \a c, as a CSS color.
    css: function (rgb, alpha, c) {
        var r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255, a = alpha * 255;
        if (c) {
            r = r * c[0] + c[4];
            g = g * c[1] + c[5];
            b = b * c[2] + c[6];
            a = a * c[3] + c[7];
        }
        r = r < 0 ? 0 : r > 255 ? 255 : Math.round(r);
        g = g < 0 ? 0 : g > 255 ? 255 : Math.round(g);
        b = b < 0 ? 0 : b > 255 ? 255 : Math.round(b);
        a = a < 0 ? 0 : a > 255 ? 255 : a;
        return "rgba(" + r + "," + g + "," + b + "," + (a / 255).toFixed(4) + ")";
    }
};

class Transform {
    constructor(displayObject) {
        Object.defineProperty(this, "_object", {value: displayObject});
    }
    get matrix() { return M.toMatrix(this._object._matrix()); }
    set matrix(m) { this._object._setMatrix(M.fromMatrix(m)); }
    get colorTransform() { return this._object._colorTransform.clone(); }
    set colorTransform(t) { this._object._setColorTransform(t); }
    get concatenatedMatrix() { return M.toMatrix(this._object._concatenatedMatrix()); }
    get concatenatedColorTransform() {
        var c = C.identity;
        for (var at = this._object; at; at = at._parent)
            c = C.multiply(C.fromTransform(at._colorTransform), c);
        return new ColorTransform(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
    }
    get pixelBounds() {
        var box = this._object._boundsIn(null, true);
        return box ? new Rectangle(box[0], box[1], box[2] - box[0], box[3] - box[1]) : new Rectangle();
    }
    get matrix3D() { return null; }
    set matrix3D(value) {}
    get perspectiveProjection() { return null; }
    set perspectiveProjection(value) {}
}

phoenix.defineClass("flash.geom.Point", Point);
phoenix.defineClass("flash.geom.Rectangle", Rectangle);
phoenix.defineClass("flash.geom.Matrix", Matrix);
phoenix.defineClass("flash.geom.ColorTransform", ColorTransform);
phoenix.defineClass("flash.geom.Transform", Transform);
phoenix._.M = M;
phoenix._.C = C;
})(phoenix);
