// TextField and TextFormat. A text field's properties are accessors on
// TextField.prototype that ask the player about the field ($phoenix.text);
// a TextFormat keeps its values out of sight ($tf), each null until set,
// as the player's own do.

(function() {
    var natives = _global.$phoenix.text;
    var version = _global.$phoenix.version;

    // Halves to even, as the player rounds a format's numbers from SWF 8;
    // past an int, the least int.
    var roundEven = function(n) {
        if (isNaN(n) || n >= 2147483647.5 || n <= -2147483648.5) {
            return -2147483648;
        }
        var low = Math.floor(n);
        var part = n - low;
        if (part > 0.5) {
            return low + 1;
        }
        if (part < 0.5) {
            return low;
        }
        return low % 2 == 0 ? low : low + 1;
    };
    // A whole number of pixels: cut before SWF 8, rounded to even after;
    // at least 0 when \a positive.
    var pixels = function(value, positive) {
        if (value === undefined || value === null) {
            return null;
        }
        if (version < 8) {
            var whole = value | 0;
            return positive && whole < 0 ? 0 : whole;
        }
        var n = Number(value);
        if (positive && !(n > 0)) {
            n = 0;
        }
        return roundEven(n);
    };
    var flag = function(value) {
        if (value === undefined || value === null) {
            return null;
        }
        return value ? true : false;
    };
    var words = function(value) {
        if (value === undefined || value === null) {
            return null;
        }
        return String(value);
    };

    var TextFormat = function(font, size, color, bold, italic, underline, url, target, align, leftMargin,
            rightMargin, indent, leading) {
        this.$tf = {};
        ASSetPropFlags(this, "$tf", 1);
        this.font = font;
        this.$tf.size = size === undefined || size === null ? null : size | 0;
        this.color = color;
        this.bold = bold;
        this.italic = italic;
        this.underline = underline;
        this.url = url;
        this.target = target;
        this.align = align;
        var whole = function(value) {
            return value === undefined || value === null ? null : value | 0;
        };
        this.$tf.leftMargin = whole(leftMargin);
        this.$tf.rightMargin = whole(rightMargin);
        this.$tf.indent = whole(indent);
        this.$tf.leading = whole(leading);
        this.$tf.display = "block";
    };
    _global.TextFormat = TextFormat;
    var f = TextFormat.prototype;
    var field = function(name, convert) {
        f.addProperty(name, function() {
            var value = this.$tf[name];
            return value === undefined ? null : value;
        }, function(value) {
            this.$tf[name] = convert(value);
        });
    };
    field("font", function(value) {
        var text = words(value);
        return text == null ? null : text.substr(0, 64);
    });
    field("size", function(value) {
        return pixels(value, false);
    });
    field("color", function(value) {
        return value === undefined || value === null ? null : value >>> 0;
    });
    field("url", words);
    field("target", words);
    field("bold", flag);
    field("italic", flag);
    field("underline", flag);
    f.addProperty("align", function() {
        var value = this.$tf.align;
        return value === undefined ? null : value;
    }, function(value) {
        if (value === undefined || value === null) {
            return;
        }
        var text = String(value).toLowerCase();
        if (text == "left" || text == "center" || text == "right" || text == "justify") {
            this.$tf.align = text;
        }
    });
    field("leftMargin", function(value) {
        return pixels(value, true);
    });
    field("rightMargin", function(value) {
        return pixels(value, true);
    });
    field("indent", function(value) {
        return pixels(value, version < 8);
    });
    field("leading", function(value) {
        return pixels(value, version < 8);
    });
    field("blockIndent", function(value) {
        return pixels(value, version < 8);
    });
    field("tabStops", function(value) {
        if (typeof value != "object" || value === null) {
            return null;
        }
        var stops = [];
        for (var i = 0; i < value.length; ++i) {
            stops.push(roundEven(Number(value[i])));
        }
        return stops;
    });
    field("bullet", flag);
    f.addProperty("display", function() {
        var value = this.$tf.display;
        return value === undefined ? null : value;
    }, function(value) {
        var text = value === undefined || value === null ? "block" : String(value);
        this.$tf.display = text == "inline" || text == "none" ? text : "block";
    });
    field("kerning", flag);
    field("letterSpacing", function(value) {
        return value === undefined || value === null ? null : Number(value);
    });
    // A format from what the player gives: only what is set.
    var formatOf = function(fields) {
        var made = new TextFormat();
        for (var name in fields) {
            made.$tf[name] = fields[name];
        }
        return made;
    };

    var TextField = function() {
    };
    _global.TextField = TextField;
    TextField.getFontList = natives.getFontList;
    var p = TextField.prototype;
    // In the player's order (for-in visits them last first); a name
    // without a setter is read only.
    var names = ["gridFitType", "antiAliasType", "thickness", "sharpness", "filters", "scroll", "maxscroll",
        "borderColor", "backgroundColor", "textColor", "tabIndex", "autoSize", "text", "type", "htmlText",
        "variable", "hscroll", "maxhscroll", "maxChars", "embedFonts", "html", "border", "background", "wordWrap",
        "password", "multiline", "selectable", "length", "bottomScroll", "textWidth", "textHeight", "restrict",
        "condenseWhite", "mouseWheelEnabled", "styleSheet"];
    var filters = _global.$phoenix.filtersProperty;
    for (var i = 0; i < names.length; ++i) {
        var name = names[i];
        if (name == "filters") {
            // Before SWF 8, there and empty.
            if (filters != undefined) {
                p.addProperty(name, filters[0], filters[1]);
            } else {
                p.addProperty(name, function() {
                    return undefined;
                }, function() {
                });
            }
        } else if (natives[name] != undefined) {
            var setter = natives["set" + name];
            p.addProperty(name, natives[name], setter == undefined ? null : setter);
        }
    }
    var methods = [];
    if (version >= 6) {
        p.replaceSel = natives.replaceSel;
        p.getTextFormat = function() {
            return formatOf(natives.getTextFormat.apply(this, arguments));
        };
        p.setTextFormat = function() {
            var given = arguments;
            var last = given.length >= 3 ? 2 : given.length - 1;
            if (last < 0 || !(given[last] instanceof TextFormat)) {
                return;
            }
            var values = given.slice(0, last);
            values.push(given[last].$tf);
            natives.setTextFormat.apply(this, values);
        };
        p.removeTextField = natives.removeTextField;
        p.getNewTextFormat = function() {
            return formatOf(natives.getNewTextFormat.call(this));
        };
        p.setNewTextFormat = function(format) {
            if (format instanceof TextFormat) {
                natives.setNewTextFormat.call(this, format.$tf);
            }
        };
        p.getDepth = natives.getDepth;
        methods.push("replaceSel", "getTextFormat", "setTextFormat", "removeTextField", "getNewTextFormat",
            "setNewTextFormat", "getDepth");
    }
    if (version >= 7) {
        p.replaceText = natives.replaceText;
        methods.push("replaceText");
    }
    ASSetPropFlags(p, methods, 3);
    ASSetPropFlags(p, "getDepth", 4);
})();
