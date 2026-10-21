// flash.geom's ColorTransform and Transform for ActionScript 2. A color
// transform keeps its eight numbers out of sight, read and set through its
// prototype's properties; a transform reads and sets a movie clip's matrix
// and colors through the player ($phoenix, taken here and then let go).

(function() {
    var player = _global.$phoenix;
    var hide = function(object, names) {
        ASSetPropFlags(object, names, 1, 0);
    };

    // ---- ColorTransform

    var fields = ["$ct0", "$ct1", "$ct2", "$ct3", "$ct4", "$ct5", "$ct6", "$ct7"];
    var isColorTransform = function(object) {
        return object instanceof Object && object.$ct0 !== undefined;
    };
    var setAll = function(target, values) {
        for (var i = 0; i < 8; i++) {
            target[fields[i]] = values[i];
        }
        hide(target, fields);
    };

    flash.geom.ColorTransform = function(redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier,
        redOffset, greenOffset, blueOffset, alphaOffset) {
        if (arguments.length >= 8) {
            var values = [];
            for (var i = 0; i < 8; i++) {
                values.push(Number(arguments[i]));
            }
            setAll(this, values);
        } else if (arguments.length == 1 && isColorTransform(redMultiplier)) {
            var copy = [];
            for (var j = 0; j < 8; j++) {
                copy.push(redMultiplier[fields[j]]);
            }
            setAll(this, copy);
        } else {
            setAll(this, [1, 1, 1, 1, 0, 0, 0, 0]);
        }
    };
    var c = flash.geom.ColorTransform.prototype;

    // Each number, read and set as one; on anything else, nothing.
    // Made in the player's order (alpha first), which for-in shows.
    var order = [3, 0, 1, 2, 7, 4, 5, 6];
    for (var k = 0; k < 8; k++) {
        (function(index) {
            var name = fields[index];
            c.addProperty(["redMultiplier", "greenMultiplier", "blueMultiplier", "alphaMultiplier", "redOffset",
                "greenOffset", "blueOffset", "alphaOffset"][index], function() {
                return isColorTransform(this) ? this[name] : undefined;
            }, function(value) {
                if (isColorTransform(this)) {
                    this[name] = Number(value);
                }
            });
        })(order[k]);
    }

    // The offsets as one color; set, the color itself (no multipliers).
    c.addProperty("rgb", function() {
        if (!isColorTransform(this)) {
            return undefined;
        }
        return (((this.$ct4 >>> 0) << 16) + ((this.$ct5 >>> 0) << 8) + (this.$ct6 >>> 0)) >>> 0;
    }, function(value) {
        if (!isColorTransform(this)) {
            return;
        }
        var rgb = value >>> 0;
        this.$ct0 = 0;
        this.$ct1 = 0;
        this.$ct2 = 0;
        this.$ct4 = (rgb >>> 16) & 255;
        this.$ct5 = (rgb >>> 8) & 255;
        this.$ct6 = rgb & 255;
    });

    // This one, then \a second: the offsets first, from the multipliers as
    // they were.
    c.concat = function(second) {
        if (!isColorTransform(this) || !isColorTransform(second)) {
            return;
        }
        this.$ct4 = second.$ct4 * this.$ct0 + this.$ct4;
        this.$ct5 = second.$ct5 * this.$ct1 + this.$ct5;
        this.$ct6 = second.$ct6 * this.$ct2 + this.$ct6;
        this.$ct7 = second.$ct7 * this.$ct3 + this.$ct7;
        this.$ct0 = second.$ct0 * this.$ct0;
        this.$ct1 = second.$ct1 * this.$ct1;
        this.$ct2 = second.$ct2 * this.$ct2;
        this.$ct3 = second.$ct3 * this.$ct3;
    };

    c.toString = function() {
        return "(redMultiplier=" + this.redMultiplier + ", greenMultiplier=" + this.greenMultiplier +
            ", blueMultiplier=" + this.blueMultiplier + ", alphaMultiplier=" + this.alphaMultiplier +
            ", redOffset=" + this.redOffset + ", greenOffset=" + this.greenOffset + ", blueOffset=" +
            this.blueOffset + ", alphaOffset=" + this.alphaOffset + ")";
    };

    var colorFrom = function(values) {
        return new flash.geom.ColorTransform(values[0], values[1], values[2], values[3], values[4], values[5],
            values[6], values[7]);
    };
    var matrixFrom = function(values) {
        return new flash.geom.Matrix(values[0], values[1], values[2], values[3], values[4], values[5]);
    };

    // ---- Transform

    // Given anything but one object, no transform at all.
    flash.geom.Transform = function(clip) {
        if (arguments.length != 1 || !(typeof clip == "object" || typeof clip == "movieclip") || clip == null) {
            return player.noObject;
        }
        this.$clip = player.isClip(clip) ? clip : undefined;
        hide(this, "$clip");
    };
    var t = flash.geom.Transform.prototype;

    t.addProperty("matrix", function() {
        var values = player.matrix(this.$clip);
        return values == undefined ? undefined : matrixFrom(values);
    }, function(value) {
        // Only an object with all six of a matrix's numbers.
        if (value instanceof Object && value.hasOwnProperty("a") && value.hasOwnProperty("b") &&
            value.hasOwnProperty("c") && value.hasOwnProperty("d") && value.hasOwnProperty("tx") &&
            value.hasOwnProperty("ty")) {
            player.setMatrix(this.$clip, value.a, value.b, value.c, value.d, value.tx, value.ty);
        }
    });
    t.addProperty("concatenatedMatrix", function() {
        var values = player.concatenatedMatrix(this.$clip);
        return values == undefined ? undefined : matrixFrom(values);
    }, null);
    t.addProperty("colorTransform", function() {
        var values = player.colorTransform(this.$clip);
        return values == undefined ? undefined : colorFrom(values);
    }, function(value) {
        if (isColorTransform(value)) {
            player.setColorTransform(this.$clip, value.$ct0, value.$ct1, value.$ct2, value.$ct3, value.$ct4,
                value.$ct5, value.$ct6, value.$ct7);
        }
    });
    t.addProperty("concatenatedColorTransform", function() {
        var values = player.concatenatedColorTransform(this.$clip);
        return values == undefined ? undefined : colorFrom(values);
    }, null);
    t.addProperty("pixelBounds", function() {
        var values = player.pixelBounds(this.$clip);
        return values == undefined ? undefined :
            new flash.geom.Rectangle(values[0], values[1], values[2], values[3]);
    }, null);

    // A movie clip's transform: a new one each time; set, the other's matrix
    // and colors taken.
    MovieClip.prototype.addProperty("transform", function() {
        return new flash.geom.Transform(this);
    }, function(value) {
        if (value instanceof flash.geom.Transform && player.isClip(value.$clip)) {
            var m = player.matrix(value.$clip);
            player.setMatrix(this, m[0], m[1], m[2], m[3], m[4], m[5]);
            var k = player.colorTransform(value.$clip);
            player.setColorTransform(this, k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7]);
        }
    });
    hide(MovieClip.prototype, "transform");
})();
