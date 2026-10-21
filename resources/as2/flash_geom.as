// flash.geom for ActionScript 2: Point, Rectangle and Matrix, written as the
// player has them, in ActionScript. The order each one reads and writes
// properties in, and the methods it calls on itself, are part of how they
// behave: a script can see both, so they are kept as they are.

// Run once, in a function of its own, so its names stay out of the movie.
(function() {
    if (_global.flash == undefined) {
        _global.flash = {};
    }
    if (_global.flash.geom == undefined) {
        _global.flash.geom = {};
    }

    // ---- Point

    flash.geom.Point = function(x, y) {
        if (arguments.length == 0) {
            this.x = 0;
            this.y = 0;
        } else {
            this.x = x;
            this.y = y;
        }
    };
    var p = flash.geom.Point.prototype;

    p.addProperty("length", function() {
        return Math.sqrt(this.x * this.x + this.y * this.y);
    }, null);

    p.add = function(v) {
        return new flash.geom.Point(this.x + v.x, this.y + v.y);
    };

    p.subtract = function(v) {
        return new flash.geom.Point(this.x - v.x, this.y - v.y);
    };

    // A plain object, even one with an x and y, never equals a point.
    p.equals = function(toCompare) {
        return toCompare instanceof flash.geom.Point && toCompare.x == this.x && toCompare.y == this.y;
    };

    p.clone = function() {
        return new flash.geom.Point(this.x, this.y);
    };

    // Its length made so, keeping its direction; a point of no length stays.
    p.normalize = function(length) {
        var current = this.length;
        if (current > 0) {
            var scale = length / current;
            this.x *= scale;
            this.y *= scale;
        }
    };

    p.offset = function(dx, dy) {
        this.x += dx;
        this.y += dy;
    };

    p.toString = function() {
        return "(x=" + this.x + ", y=" + this.y + ")";
    };

    flash.geom.Point.distance = function(pt1, pt2) {
        return pt1.subtract(pt2).length;
    };

    // From pt2 (f is 0) to pt1 (f is 1).
    flash.geom.Point.interpolate = function(pt1, pt2, f) {
        return new flash.geom.Point(pt2.x + (pt1.x - pt2.x) * f, pt2.y + (pt1.y - pt2.y) * f);
    };

    flash.geom.Point.polar = function(len, angle) {
        return new flash.geom.Point(len * Math.cos(angle), len * Math.sin(angle));
    };

    // ---- Rectangle

    flash.geom.Rectangle = function(x, y, width, height) {
        if (x == undefined) {
            this.setEmpty();
        } else {
            this.x = x;
            this.y = y;
            this.width = width;
            this.height = height;
        }
    };
    var r = flash.geom.Rectangle.prototype;

    r.clone = function() {
        return new flash.geom.Rectangle(this.x, this.y, this.width, this.height);
    };

    r.setEmpty = function() {
        this.x = this.y = this.width = this.height = 0;
    };

    r.isEmpty = function() {
        return this.width <= 0 || this.height <= 0;
    };

    // The edges: moving one keeps the opposite edge where it is.
    r.addProperty("left", function() {
        return this.x;
    }, function(value) {
        this.width += this.x - value;
        this.x = value;
    });
    r.addProperty("right", function() {
        return this.x + this.width;
    }, function(value) {
        this.width = value - this.x;
    });
    r.addProperty("top", function() {
        return this.y;
    }, function(value) {
        this.height += this.y - value;
        this.y = value;
    });
    r.addProperty("bottom", function() {
        return this.y + this.height;
    }, function(value) {
        this.height = value - this.y;
    });
    r.addProperty("topLeft", function() {
        return new flash.geom.Point(this.x, this.y);
    }, function(value) {
        this.width += this.x - value.x;
        this.height += this.y - value.y;
        this.x = value.x;
        this.y = value.y;
    });
    r.addProperty("bottomRight", function() {
        return new flash.geom.Point(this.x + this.width, this.y + this.height);
    }, function(value) {
        this.width = value.x - this.x;
        this.height = value.y - this.y;
    });
    r.addProperty("size", function() {
        return new flash.geom.Point(this.width, this.height);
    }, function(value) {
        this.width = value.x;
        this.height = value.y;
    });

    r.inflate = function(dx, dy) {
        this.x -= dx;
        this.width += 2 * dx;
        this.y -= dy;
        this.height += 2 * dy;
    };

    r.inflatePoint = function(point) {
        this.x -= point.x;
        this.width += 2 * point.x;
        this.y -= point.y;
        this.height += 2 * point.y;
    };

    r.offset = function(dx, dy) {
        this.x += dx;
        this.y += dy;
    };

    r.offsetPoint = function(point) {
        this.x += point.x;
        this.y += point.y;
    };

    r.contains = function(x, y) {
        return x >= this.x && x < this.x + this.width && y >= this.y && y < this.y + this.height;
    };

    r.containsPoint = function(point) {
        return point.x >= this.x && point.x < this.x + this.width && point.y >= this.y &&
            point.y < this.y + this.height;
    };

    r.containsRectangle = function(rect) {
        var right = rect.x + rect.width;
        var bottom = rect.y + rect.height;
        var thisRight = this.x + this.width;
        var thisBottom = this.y + this.height;
        return rect.x >= this.x && rect.x < thisRight && rect.y >= this.y && rect.y < thisBottom &&
            right > this.x && right <= thisRight && bottom > this.y && bottom <= thisBottom;
    };

    r.intersection = function(toIntersect) {
        var result = new flash.geom.Rectangle();
        if (this.isEmpty() || toIntersect.isEmpty()) {
            result.setEmpty();
            return result;
        }
        result.x = Math.max(this.x, toIntersect.x);
        result.y = Math.max(this.y, toIntersect.y);
        result.width = Math.min(this.x + this.width, toIntersect.x + toIntersect.width) - result.x;
        result.height = Math.min(this.y + this.height, toIntersect.y + toIntersect.height) - result.y;
        if (result.width <= 0 || result.height <= 0) {
            result.setEmpty();
        }
        return result;
    };

    r.intersects = function(toIntersect) {
        return !this.intersection(toIntersect).isEmpty();
    };

    r.union = function(toUnion) {
        if (this.isEmpty()) {
            return toUnion.clone();
        }
        if (toUnion.isEmpty()) {
            return this.clone();
        }
        var result = new flash.geom.Rectangle();
        result.x = Math.min(this.x, toUnion.x);
        result.y = Math.min(this.y, toUnion.y);
        result.width = Math.max(this.x + this.width, toUnion.x + toUnion.width) - result.x;
        result.height = Math.max(this.y + this.height, toUnion.y + toUnion.height) - result.y;
        return result;
    };

    r.equals = function(toCompare) {
        return toCompare instanceof flash.geom.Rectangle && toCompare.x == this.x && toCompare.y == this.y &&
            toCompare.width == this.width && toCompare.height == this.height;
    };

    r.toString = function() {
        return "(x=" + this.x + ", y=" + this.y + ", w=" + this.width + ", h=" + this.height + ")";
    };

    // ---- Matrix

    flash.geom.Matrix = function(a, b, c, d, tx, ty) {
        if (arguments.length == 0) {
            this.identity();
        } else {
            this.a = a;
            this.b = b;
            this.c = c;
            this.d = d;
            this.tx = tx;
            this.ty = ty;
        }
    };
    var m = flash.geom.Matrix.prototype;

    // This matrix, then \a other: the skew terms only when either has skew.
    m.concat = function(other) {
        var a = this.a * other.a;
        var d = this.d * other.d;
        var b = 0;
        var c = 0;
        var tx = this.tx * other.a + other.tx;
        var ty = this.ty * other.d + other.ty;
        if (this.b != 0 || this.c != 0 || other.b != 0 || other.c != 0) {
            a += this.b * other.c;
            d += this.c * other.b;
            b += this.a * other.b + this.b * other.d;
            c += this.c * other.a + this.d * other.c;
            tx += this.ty * other.c;
            ty += this.tx * other.b;
        }
        this.a = a;
        this.b = b;
        this.c = c;
        this.d = d;
        this.tx = tx;
        this.ty = ty;
    };

    // Without skew, each scale its reciprocal; with skew, by the determinant;
    // a matrix that cannot be undone becomes the identity.
    m.invert = function() {
        if (this.b == 0 && this.c == 0) {
            this.a = 1 / this.a;
            this.d = 1 / this.d;
            this.c = 0;
            this.b = 0;
            this.tx = -this.a * this.tx;
            this.ty = -this.d * this.ty;
            return;
        }
        var a = this.a;
        var b = this.b;
        var c = this.c;
        var d = this.d;
        var determinant = a * d - b * c;
        if (determinant === 0) {
            this.identity();
            return;
        }
        determinant = 1 / determinant;
        this.a = d * determinant;
        this.b = -b * determinant;
        this.c = -c * determinant;
        this.d = a * determinant;
        var ty = this.ty;
        var tx = this.tx;
        var moved = this.deltaTransformPoint(new flash.geom.Point(tx, ty));
        this.tx = -moved.x;
        this.ty = -moved.y;
    };

    m.createBox = function(scaleX, scaleY, rotation, tx, ty) {
        var count = arguments.length;
        if (count < 4) {
            tx = 0;
        }
        if (count < 5) {
            ty = 0;
        }
        this.identity();
        this.rotate(rotation);
        this.scale(scaleX, scaleY);
        this.tx = tx;
        this.ty = ty;
    };

    // A gradient's box: its 1638.4 square fitted to width by height.
    m.createGradientBox = function(width, height, rotation, tx, ty) {
        var count = arguments.length;
        if (count < 3) {
            rotation = 0;
        }
        if (count < 4) {
            tx = 0;
        }
        if (count < 5) {
            ty = 0;
        }
        var centerY = ty + height / 2;
        var centerX = tx + width / 2;
        var scaleY = height / 1638.4;
        var scaleX = width / 1638.4;
        this.createBox(scaleX, scaleY, rotation, centerX, centerY);
    };

    m.clone = function() {
        var ty = this.ty;
        var tx = this.tx;
        var d = this.d;
        var c = this.c;
        var b = this.b;
        var a = this.a;
        return new flash.geom.Matrix(a, b, c, d, tx, ty);
    };

    m.identity = function() {
        this.d = 1;
        this.a = 1;
        this.c = 0;
        this.b = 0;
        this.ty = 0;
        this.tx = 0;
    };

    m.rotate = function(angle) {
        var cos = Math.cos(angle);
        var sin = Math.sin(angle);
        this.concat(new flash.geom.Matrix(cos, sin, -sin, cos, 0, 0));
    };

    m.translate = function(tx, ty) {
        this.tx += tx;
        this.ty += ty;
    };

    m.scale = function(sx, sy) {
        this.concat(new flash.geom.Matrix(sx, 0, 0, sy, 0, 0));
    };

    m.deltaTransformPoint = function(point) {
        var y = this.d * point.y + this.b * point.x;
        var x = this.a * point.x + this.c * point.y;
        return new flash.geom.Point(x, y);
    };

    m.transformPoint = function(point) {
        var y = this.d * point.y + this.b * point.x + this.ty;
        var x = this.a * point.x + this.c * point.y + this.tx;
        return new flash.geom.Point(x, y);
    };

    m.toString = function() {
        return "(a=" + this.a + ", b=" + this.b + ", c=" + this.c + ", d=" + this.d + ", tx=" + this.tx + ", ty=" +
            this.ty + ")";
    };
})();
