// flash.filters for ActionScript 2. Each filter keeps its numbers as the
// player stores them, out of sight: $filter names the kind and $v holds the
// values in a fixed order the player reads them in (alphas in 255ths,
// strengths in 256ths, angles in radians, matrices as 32-bit floats). Each
// setter coerces as the player's does; a filter's constructor runs them on
// the arguments given, in order.

(function() {
    // flash.* is there from SWF 8.
    if (_global.$phoenix.version < 8) {
        return;
    }
    var player = _global.$phoenix;
    if (_global.flash == undefined) {
        _global.flash = {};
    }
    if (_global.flash.filters == undefined) {
        _global.flash.filters = {};
    }
    var hide = function(object, names) {
        ASSetPropFlags(object, names, 1, 0);
    };
    var f32 = player.f32;
    var toRadians = Math.PI / 180;
    var toDegrees = 180 / Math.PI;

    // Numbers as the player cuts them into bytes and shorts: NaN as 0, held
    // to the range.
    var byte = function(x) {
        return x >= 255 ? 255 : (x > 0 ? Math.floor(x) : 0);
    };
    var short = function(x) {
        return x >= 65535 ? 65535 : (x > 0 ? Math.floor(x) : 0);
    };
    var clamp = function(x, low, high) {
        return x < low ? low : (x > high ? high : x);
    };
    // Held to the range, NaN as the low end.
    var clampAlsoNaN = function(x, low, high) {
        return x > low ? (x < high ? x : high) : low;
    };
    var whole = function(x) {
        return x | 0;
    };

    // ---- BitmapFilter

    var BitmapFilter = function() {
    };
    flash.filters.BitmapFilter = BitmapFilter;
    // A copy of the values, arrays and all.
    var copied = function(values) {
        var out = [];
        for (var i = 0; i < values.length; i++) {
            out.push(values[i] instanceof Array ? values[i].concat() : values[i]);
        }
        return out;
    };
    var make = function(proto, kind, values) {
        var made = new Object();
        made.__proto__ = proto;
        made.$filter = kind;
        made.$v = values;
        hide(made, ["__proto__", "$filter", "$v"]);
        return made;
    };
    BitmapFilter.prototype.clone = function() {
        if (this.$filter == undefined) {
            return undefined;
        }
        return make(this.__proto__, this.$filter, copied(this.$v));
    };

    // A filter class: its kind, its defaults, and its properties in order,
    // each [name, getter of the values, setter of them].
    var define = function(name, kind, defaults, properties, construct) {
        var cls = function() {
            this.$filter = kind;
            this.$v = copied(defaults);
            hide(this, ["$filter", "$v"]);
            if (construct != undefined) {
                construct.call(this, arguments, properties);
            } else {
                for (var i = 0; i < properties.length && i < arguments.length; i++) {
                    properties[i][2].call(this, arguments[i]);
                }
            }
        };
        cls.prototype = new BitmapFilter();
        flash.filters[name] = cls;
        var p = cls.prototype;
        p.constructor = cls;
        for (var k = 0; k < properties.length; k++) {
            (function(property) {
                p.addProperty(property[0], function() {
                    return this.$filter == kind ? property[1].call(this, this.$v) : undefined;
                }, function(value) {
                    if (this.$filter == kind) {
                        property[2].call(this, value);
                    }
                });
            })(properties[k]);
        }
        hide(p, "constructor");
        return cls;
    };
    // A property of one value at \a index, read as it is kept, set through
    // \a coerce.
    var plain = function(name, index, coerce) {
        return [name, function(v) {
            return v[index];
        }, function(value) {
            this.$v[index] = coerce(value);
        }];
    };
    var number = function(value) {
        return Number(value);
    };
    var blur = function(value) {
        return clamp(Number(value), 0, 255);
    };
    var quality = function(value) {
        return clamp(whole(value), 0, 15);
    };
    var flag = function(value) {
        return Boolean(value);
    };
    var angle = function(name, index) {
        return [name, function(v) {
            return v[index] * toDegrees;
        }, function(value) {
            this.$v[index] = (Number(value) % 360) * toRadians;
        }];
    };
    var color = function(name, index) {
        return [name, function(v) {
            return v[index];
        }, function(value) {
            this.$v[index] = (value >>> 0) & 0xFFFFFF;
        }];
    };
    // An alpha kept in 255ths: cut, or with \a nanLow, held to 0 to 1 first.
    var alpha = function(name, index, held) {
        return [name, function(v) {
            return v[index] / 255;
        }, function(value) {
            var a = Number(value);
            if (held) {
                a = clampAlsoNaN(a, 0, 1);
            }
            this.$v[index] = byte(a * 255);
        }];
    };
    var strength = function(name, index) {
        return [name, function(v) {
            return v[index] / 256;
        }, function(value) {
            this.$v[index] = clamp(short(Number(value) * 256), 0, 0xFF00);
        }];
    };
    // inner, outer, or full for anything else.
    var bevelType = function(name, index) {
        return [name, function(v) {
            return v[index];
        }, function(value) {
            var type = String(value);
            this.$v[index] = type == "inner" || type == "outer" ? type : "full";
        }];
    };

    // ---- The filters

    define("BevelFilter", "bevel", [4, 0.785398163, 0xFFFFFF, 255, 0, 255, 1, 256, false, 4, 4, "inner"], [
        plain("distance", 0, number), angle("angle", 1), color("highlightColor", 2), alpha("highlightAlpha", 3),
        color("shadowColor", 4), alpha("shadowAlpha", 5), plain("quality", 6, quality),
        strength("strength", 7), plain("knockout", 8, flag), plain("blurX", 9, blur), plain("blurY", 10, blur),
        bevelType("type", 11)], function(args, properties) {
        // distance, angle, highlightColor, highlightAlpha, shadowColor,
        // shadowAlpha, blurX, blurY, strength, quality, type, knockout.
        var order = [0, 1, 2, 3, 4, 5, 9, 10, 7, 6, 11, 8];
        for (var i = 0; i < order.length && i < args.length; i++) {
            properties[order[i]][2].call(this, args[i]);
        }
    });

    define("BlurFilter", "blur", [4, 4, 1], [
        plain("blurX", 0, blur), plain("blurY", 1, blur), plain("quality", 2, quality)]);

    // Twenty numbers: from an object its elements (NaN past its end), from
    // anything else but null and undefined NaN; null and undefined change
    // nothing.
    var identity = [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
    define("ColorMatrixFilter", "colorMatrix", [identity], [
        ["matrix", function(v) {
            return v[0].concat();
        }, function(value) {
            if (value === null || value === undefined) {
                return;
            }
            var matrix = [];
            var isObject = typeof value == "object" || typeof value == "function" || typeof value == "movieclip";
            for (var i = 0; i < 20; i++) {
                matrix.push(isObject && i < value.length ? f32(Number(value[i])) : NaN);
            }
            this.$v[0] = matrix;
        }]]);

    // The matrix grown with zeros to matrixX by matrixY, never shrunk.
    var fitMatrix = function(v) {
        while (v[2].length < v[0] * v[1]) {
            v[2].push(0);
        }
    };
    var size = function(index) {
        return function(value) {
            this.$v[index] = clamp(whole(value), 0, 15);
            fitMatrix(this.$v);
        };
    };
    var convolution = [
        ["matrixX", function(v) {
            return v[0];
        }, size(0)],
        ["matrixY", function(v) {
            return v[1];
        }, size(1)],
        ["matrix", function(v) {
            return v[2].concat();
        }, function(value) {
            // As an object: a string's length makes that many NaNs.
            var length = value == null ? 0 : whole(value.length);
            var matrix = [];
            for (var i = 0; i < length; i++) {
                matrix.push(f32(Number(value[i])));
            }
            this.$v[2] = matrix;
            fitMatrix(this.$v);
        }],
        ["divisor", function(v) {
            return v[3];
        }, function(value) {
            this.$v[3] = f32(Number(value));
        }],
        ["bias", function(v) {
            return v[4];
        }, function(value) {
            this.$v[4] = f32(Number(value));
        }],
        plain("preserveAlpha", 5, flag), plain("clamp", 6, flag), color("color", 7), alpha("alpha", 8, true)];
    define("ConvolutionFilter", "convolution", [0, 0, [], 1, 0, true, true, 0, 0], convolution,
        function(args, properties) {
        for (var i = 0; i < 3 && i < args.length; i++) {
            properties[i][2].call(this, args[i]);
        }
        // Without a divisor, the matrix's sum.
        if (args.length > 3) {
            properties[3][2].call(this, args[3]);
        } else if (args.length > 0) {
            var sum = 0;
            for (var k = 0; k < this.$v[2].length; k++) {
                sum += this.$v[2][k];
            }
            this.$v[3] = f32(sum);
        }
        for (var j = 4; j < 7 && j < args.length; j++) {
            properties[j][2].call(this, args[j]);
        }
        // A color given makes the alpha whole, whatever alpha follows.
        if (args.length > 7) {
            properties[7][2].call(this, args[7]);
            this.$v[8] = 255;
        }
        if (args.length > 8) {
            properties[8][2].call(this, args[8]);
        }
    });

    var map = [
        ["mapBitmap", function(v) {
            if (v[0] == undefined) {
                return undefined;
            }
            // A new BitmapData of the same pixels.
            var bitmap = new Object();
            bitmap.__proto__ = flash.display.BitmapData.prototype;
            bitmap.$bitmap = v[0];
            hide(bitmap, ["__proto__", "$bitmap"]);
            return bitmap;
        }, function(value) {
            if (typeof value == "object" && value != null && value.$bitmap != undefined) {
                this.$v[0] = value.$bitmap;
            }
        }],
        ["mapPoint", function(v) {
            return new flash.geom.Point(v[1], v[2]);
        }, function(value) {
            // Both of its own x and y, as whole numbers (out of range the
            // least), or the corner; an error reading them is set and then
            // thrown.
            var x = 0, y = 0, errorX, errorY, failedX = false, failedY = false;
            var hasX = typeof value == "object" && value != null && value.hasOwnProperty("x");
            var hasY = typeof value == "object" && value != null && value.hasOwnProperty("y");
            var cut = function(n) {
                return n >= -2147483648 && n <= 2147483647 ? (n < 0 ? Math.ceil(n) : Math.floor(n)) : -2147483648;
            };
            if (hasX) {
                try {
                    x = cut(Number(value.x));
                } catch (e) {
                    x = -2147483648;
                    errorX = e;
                    failedX = true;
                }
            }
            if (hasY) {
                try {
                    y = cut(Number(value.y));
                } catch (e2) {
                    y = -2147483648;
                    errorY = e2;
                    failedY = true;
                }
            }
            this.$v[1] = hasX && hasY ? x : 0;
            this.$v[2] = hasX && hasY ? y : 0;
            if (failedX) {
                throw errorX;
            }
            if (failedY) {
                throw errorY;
            }
        }],
        plain("componentX", 3, whole), plain("componentY", 4, whole),
        ["scaleX", function(v) {
            return v[5];
        }, function(value) {
            this.$v[5] = f32(clampAlsoNaN(Number(value), -65535, 65535));
        }],
        ["scaleY", function(v) {
            return v[6];
        }, function(value) {
            this.$v[6] = f32(clampAlsoNaN(Number(value), -65535, 65535));
        }],
        ["mode", function(v) {
            return v[7];
        }, function(value) {
            var mode = String(value);
            this.$v[7] = mode == "clamp" || mode == "ignore" || mode == "color" ? mode : "wrap";
        }],
        color("color", 8), alpha("alpha", 9, true)];
    define("DisplacementMapFilter", "displacementMap", [undefined, 0, 0, 0, 0, 0, 0, "wrap", 0, 0], map);

    define("DropShadowFilter", "dropShadow", [4, 0.785398163, 0, 255, 1, false, false, 4, 4, 256, false], [
        plain("distance", 0, number), angle("angle", 1), color("color", 2), alpha("alpha", 3),
        plain("quality", 4, quality), plain("inner", 5, flag), plain("knockout", 6, flag), plain("blurX", 7, blur),
        plain("blurY", 8, blur), strength("strength", 9), plain("hideObject", 10, flag)],
        function(args, properties) {
        // distance, angle, color, alpha, blurX, blurY, strength, quality,
        // inner, knockout, hideObject.
        var order = [0, 1, 2, 3, 7, 8, 9, 4, 5, 6, 10];
        for (var i = 0; i < order.length && i < args.length; i++) {
            properties[order[i]][2].call(this, args[i]);
        }
    });

    define("GlowFilter", "glow", [0xFF0000, 255, 1, false, false, 6, 6, 512], [
        color("color", 0), alpha("alpha", 1), plain("quality", 2, quality), plain("inner", 3, flag),
        plain("knockout", 4, flag), plain("blurX", 5, blur), plain("blurY", 6, blur), strength("strength", 7)],
        function(args, properties) {
        // color, alpha, blurX, blurY, strength, quality, inner, knockout.
        var order = [0, 1, 5, 6, 7, 2, 3, 4];
        for (var i = 0; i < order.length && i < args.length; i++) {
            properties[order[i]][2].call(this, args[i]);
        }
    });

    // Gradient bevels and glows: sixteen colors, each with an alpha and a
    // ratio, of which the first so many (at 11) are used. Colors left out
    // keep their alphas and ratios for when they come back.
    var used = function(list, v) {
        return list.slice(0, v[11]);
    };
    var gradient = [
        plain("distance", 0, number), angle("angle", 1),
        ["colors", function(v) {
            return used(v[2], v);
        }, function(value) {
            var length = value == null ? 0 : whole(value.length);
            if (length > 16) {
                length = 16;
            }
            for (var i = 0; i < length; i++) {
                this.$v[2][i] = whole(value[i]) & 0xFFFFFF;
            }
            this.$v[11] = length;
        }],
        ["alphas", function(v) {
            var out = [];
            for (var i = 0; i < v[11]; i++) {
                out.push(v[3][i] / 255);
            }
            return out;
        }, function(value) {
            if (typeof value != "object" || value == null) {
                return;
            }
            var v = this.$v;
            for (var i = 0; i < v[11]; i++) {
                var a = i < value.length ? Number(value[i]) : NaN;
                v[3][i] = isFinite(a) ? byte(255 * a + 0.5) : 255;
            }
        }],
        ["ratios", function(v) {
            return used(v[4], v);
        }, function(value) {
            if (typeof value != "object" || value == null) {
                return;
            }
            // Fewer ratios make fewer colors, never more.
            var v = this.$v;
            var count = whole(value.length);
            if (count > v[11]) {
                count = v[11];
            }
            v[11] = count;
            for (var i = 0; i < count; i++) {
                v[4][i] = clamp(whole(value[i]), 0, 255);
            }
        }],
        ["blurX", function(v) {
            return v[5];
        }, function(value) {
            this.$v[5] = clampAlsoNaN(Number(value), 0, 255);
        }],
        ["blurY", function(v) {
            return v[6];
        }, function(value) {
            this.$v[6] = clampAlsoNaN(Number(value), 0, 255);
        }],
        plain("quality", 8, quality), strength("strength", 7), plain("knockout", 10, flag), bevelType("type", 9)];
    var gradientConstruct = function(args, properties) {
        // distance, angle, colors, alphas, ratios, blurX, blurY, strength,
        // quality, type, knockout.
        var order = [0, 1, 2, 3, 4, 5, 6, 8, 7, 10, 9];
        for (var i = 0; i < order.length && i < args.length; i++) {
            properties[order[i]][2].call(this, args[i]);
        }
    };
    var sixteen = function() {
        return [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    };
    var gradientDefaults = [4, 0.785398163, sixteen(), sixteen(), sixteen(), 4, 4, 256, 1, "inner", false, 0];
    define("GradientBevelFilter", "gradientBevel", gradientDefaults, gradient, gradientConstruct);
    define("GradientGlowFilter", "gradientGlow", gradientDefaults, gradient, gradientConstruct);

    // ---- A movie clip's filters: copies each way.

    var classes = {bevel: "BevelFilter", blur: "BlurFilter", colorMatrix: "ColorMatrixFilter",
        convolution: "ConvolutionFilter", displacementMap: "DisplacementMapFilter", dropShadow: "DropShadowFilter",
        glow: "GlowFilter", gradientBevel: "GradientBevelFilter", gradientGlow: "GradientGlowFilter"};

    MovieClip.prototype.addProperty("filters", function() {
        var list = player.filters(this);
        var out = [];
        for (var i = 0; list != undefined && i < list.length; i++) {
            var kind = list[i][0];
            var cls = flash.filters[classes[kind]];
            out.push(make(cls.prototype, kind, list[i].slice(1)));
        }
        return out;
    }, function(value) {
        player.setFilters(this, value);
    });
    hide(MovieClip.prototype, "filters");
})();
