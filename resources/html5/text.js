// flash.text: TextField and TextFormat. A field's text is kept with a
// format for each character; it is laid out into lines (wrapped when the
// field wraps) with the canvas's own text measuring, and drawn with
// fillText. An input field being typed into has an HTML input or textarea
// laid over it (see the player), whose text comes back into the field.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;
var Event = flash.events.Event;

var TextFieldType = {DYNAMIC: "dynamic", INPUT: "input"};
var TextFieldAutoSize = {CENTER: "center", LEFT: "left", NONE: "none", RIGHT: "right"};
var TextFormatAlign = {CENTER: "center", END: "end", JUSTIFY: "justify", LEFT: "left", RIGHT: "right", START: "start"};
var AntiAliasType = {ADVANCED: "advanced", NORMAL: "normal"};
var GridFitType = {NONE: "none", PIXEL: "pixel", SUBPIXEL: "subpixel"};

var formatKeys = ["align", "blockIndent", "bold", "bullet", "color", "font", "indent", "italic", "kerning", "leading",
    "leftMargin", "letterSpacing", "rightMargin", "size", "tabStops", "target", "underline", "url"];

class TextFormat {
    constructor(font, size, color, bold, italic, underline, url, target, align, leftMargin, rightMargin, indent, leading) {
        this.font = font === undefined ? null : font;
        this.size = size === undefined ? null : size;
        this.color = color === undefined ? null : color;
        this.bold = bold === undefined ? null : bold;
        this.italic = italic === undefined ? null : italic;
        this.underline = underline === undefined ? null : underline;
        this.url = url === undefined ? null : url;
        this.target = target === undefined ? null : target;
        this.align = align === undefined ? null : align;
        this.leftMargin = leftMargin === undefined ? null : leftMargin;
        this.rightMargin = rightMargin === undefined ? null : rightMargin;
        this.indent = indent === undefined ? null : indent;
        this.leading = leading === undefined ? null : leading;
        this.blockIndent = null;
        this.bullet = null;
        this.kerning = null;
        this.letterSpacing = null;
        this.tabStops = null;
        this.display = "block";
    }
}

/// A complete format: every key set.
function defaults() {
    return {align: "left", blockIndent: 0, bold: false, bullet: false, color: 0, font: "Times New Roman", indent: 0,
        italic: false, kerning: false, leading: 0, leftMargin: 0, letterSpacing: 0, rightMargin: 0, size: 12,
        tabStops: [], target: "", underline: false, url: ""};
}
function merged(base, over) {
    var out = Object.assign({}, base);
    formatKeys.forEach(function (key) {
        if (over[key] !== null && over[key] !== undefined)
            out[key] = over[key];
    });
    return out;
}
function sameFormat(a, b) {
    for (var i = 0; i < formatKeys.length; ++i) {
        var key = formatKeys[i];
        if (key === "tabStops")
            continue;
        if (a[key] !== b[key])
            return false;
    }
    return true;
}
var families = {"_sans": "Arial, Helvetica, sans-serif", "_serif": "\"Times New Roman\", Times, serif",
    "_typewriter": "\"Courier New\", Courier, monospace"};
/// The CSS font for a format.
function cssFont(f) {
    var family = families[f.font] || ("\"" + String(f.font).replace(/"/g, "") + "\", sans-serif");
    return (f.italic ? "italic " : "") + (f.bold ? "bold " : "") + Math.max(1, f.size) + "px " + family;
}

var measureContext = null;
function measurer() {
    if (measureContext === null) {
        var canvas = phoenix.makeCanvas(1, 1);
        measureContext = canvas ? canvas.getContext("2d") : false;
    }
    return measureContext || null;
}
/// How wide \a text is in format \a f, and the font's ascent and descent.
function measure(text, f) {
    var ctx = measurer();
    if (!ctx)
        return {width: text.length * f.size * 0.5 + text.length * f.letterSpacing, ascent: f.size * 0.9, descent: f.size * 0.22};
    ctx.font = cssFont(f);
    var metrics = ctx.measureText(text);
    var ascent = metrics.fontBoundingBoxAscent !== undefined ? metrics.fontBoundingBoxAscent : f.size * 0.9;
    var descent = metrics.fontBoundingBoxDescent !== undefined ? metrics.fontBoundingBoxDescent : f.size * 0.22;
    return {width: metrics.width + text.length * (f.letterSpacing || 0), ascent: ascent, descent: descent};
}

var gutter = 2;

class TextField extends flash.display.InteractiveObject {
    constructor() {
        super();
        this._text = "";
        this._formats = [];
        this._default = defaults();
        this._box = [0, 0, 100, 100];
        this._type = "dynamic";
        this._multiline = false;
        this._wordWrap = false;
        this._autoSize = "none";
        this._selectable = true;
        this._border = false;
        this._borderColor = 0;
        this._background = false;
        this._backgroundColor = 0xffffff;
        this._password = false;
        this._maxChars = 0;
        this._restrict = null;
        this._scrollV = 1;
        this._scrollH = 0;
        this._layout = null;
        this._condenseWhite = false;
        this._htmlSource = null;
        this.antiAliasType = "normal";
        this.embedFonts = false;
        this.gridFitType = "pixel";
        this.sharpness = 0;
        this.thickness = 0;
        this.alwaysShowSelection = false;
        this.mouseWheelEnabled = true;
        this.useRichTextClipboard = false;
        this.styleSheet = null;
        this.displayAsPassword = false;
        this._caret = 0;
        this._selection = [0, 0];
    }
    /// A published dynamic or input field (README.md, "Text fields").
    _fromTimeline(desc) {
        this._box = [desc.x || 0, desc.y || 0, Math.max(1, desc.w || 1), Math.max(1, desc.h || 1)];
        this._type = desc.type === "input" ? "input" : "dynamic";
        this._multiline = !!desc.ml;
        this._wordWrap = !!desc.wrap;
        this._password = !!desc.pw;
        this._border = !!desc.border;
        this._selectable = desc.sel !== 0;
        this._maxChars = desc.max | 0;
        if (desc.auto)
            this._autoSize = desc.auto;
        var text = "", formats = [];
        var self = this;
        (desc.runs || []).forEach(function (run, i) {
            var f = defaults();
            f.font = run.f || "Times New Roman";
            f.size = run.s || 12;
            f.color = run.c || 0;
            f.bold = !!run.b;
            f.italic = !!run.i;
            f.align = run.a || "left";
            f.letterSpacing = run.ls || 0;
            f.leading = run.ld || 0;
            f.url = run.u || "";
            if (i === 0)
                self._default = Object.assign({}, f);
            for (var k = 0; k < run.t.length; ++k) {
                var ch = run.t[k];
                text += ch === "\n" ? "\r" : ch;
                formats.push(f);
            }
        });
        this._text = text;
        this._formats = formats;
        this._textChanged();
    }
    _textChanged() {
        this._layout = null;
        this._htmlSource = null;
        this._changed();
    }

    // Text.
    get text() { return this._text; }
    set text(value) {
        value = value === null || value === undefined ? "" : String(value);
        value = value.replace(/\r\n|\n/g, "\r");
        this._text = value;
        var f = this._default;
        this._formats = [];
        for (var i = 0; i < value.length; ++i)
            this._formats.push(f);
        this._textChanged();
    }
    appendText(newText) {
        newText = String(newText).replace(/\r\n|\n/g, "\r");
        var f = this._formats.length ? this._formats[this._formats.length - 1] : this._default;
        this._text += newText;
        for (var i = 0; i < newText.length; ++i)
            this._formats.push(f);
        this._textChanged();
    }
    replaceText(beginIndex, endIndex, newText) {
        newText = String(newText).replace(/\r\n|\n/g, "\r");
        var f = this._formats[beginIndex] || this._default;
        this._text = this._text.slice(0, beginIndex) + newText + this._text.slice(endIndex);
        var added = [];
        for (var i = 0; i < newText.length; ++i)
            added.push(f);
        this._formats = this._formats.slice(0, beginIndex).concat(added, this._formats.slice(endIndex));
        this._textChanged();
    }
    replaceSelectedText(value) { this.replaceText(this._selection[0], this._selection[1], value); }
    get length() { return this._text.length; }
    get htmlText() {
        if (this._htmlSource !== null)
            return this._htmlSource;
        var out = "", text = this._text, formats = this._formats;
        var paragraphs = text.split("\r"), at = 0;
        var escape = function (s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); };
        for (var p = 0; p < paragraphs.length; ++p) {
            var first = formats[at] || this._default;
            out += "<P ALIGN=\"" + first.align.toUpperCase() + "\">";
            var line = paragraphs[p];
            var i = 0;
            while (i < line.length) {
                var f = formats[at + i] || this._default, j = i;
                while (j < line.length && sameFormat(formats[at + j] || this._default, f))
                    ++j;
                var run = escape(line.slice(i, j));
                if (f.bold) run = "<B>" + run + "</B>";
                if (f.italic) run = "<I>" + run + "</I>";
                if (f.underline) run = "<U>" + run + "</U>";
                if (f.url) run = "<A HREF=\"" + f.url + "\" TARGET=\"" + (f.target || "") + "\">" + run + "</A>";
                out += "<FONT FACE=\"" + f.font + "\" SIZE=\"" + f.size + "\" COLOR=\"#" +
                    ("000000" + f.color.toString(16).toUpperCase()).slice(-6) + "\" LETTERSPACING=\"" + f.letterSpacing +
                    "\" KERNING=\"" + (f.kerning ? 1 : 0) + "\">" + run + "</FONT>";
                i = j;
            }
            out += "</P>";
            at += line.length + 1;
        }
        return out;
    }
    set htmlText(value) {
        var parsed = parseHtml(String(value === null || value === undefined ? "" : value), this._default, this._multiline,
            this._condenseWhite);
        this._text = parsed.text;
        this._formats = parsed.formats;
        this._textChanged();
        this._htmlSource = null;
    }
    get defaultTextFormat() { return toTextFormat(this._default); }
    set defaultTextFormat(format) { this._default = merged(this._default, format); }
    getTextFormat(beginIndex, endIndex) {
        beginIndex = beginIndex === undefined || beginIndex < 0 ? 0 : beginIndex;
        endIndex = endIndex === undefined || endIndex < 0 ? this._text.length : endIndex;
        if (!this._formats.length)
            return toTextFormat(this._default);
        var first = this._formats[beginIndex] || this._formats[this._formats.length - 1];
        var out = toTextFormat(first);
        for (var i = beginIndex + 1; i < endIndex && i < this._formats.length; ++i) {
            var f = this._formats[i];
            formatKeys.forEach(function (key) {
                if (out[key] !== null && f[key] !== out[key] && key !== "tabStops")
                    out[key] = null;
            });
        }
        return out;
    }
    setTextFormat(format, beginIndex, endIndex) {
        beginIndex = beginIndex === undefined || beginIndex < 0 ? 0 : beginIndex;
        endIndex = endIndex === undefined || endIndex < 0 ? this._text.length : endIndex;
        if (beginIndex >= 0 && endIndex === beginIndex && arguments.length === 2)
            endIndex = beginIndex + 1;
        var cache = new Map();
        for (var i = beginIndex; i < endIndex && i < this._formats.length; ++i) {
            var f = this._formats[i];
            var made = cache.get(f);
            if (!made) {
                made = merged(f, format);
                cache.set(f, made);
            }
            this._formats[i] = made;
        }
        if (!this._text.length)
            this._default = merged(this._default, format);
        this._textChanged();
    }

    // The box.
    get width() { this._lines(); return this._box[2] * Math.abs(this.scaleX); }
    set width(value) {
        this._box[2] = Math.max(0, +value);
        this._layout = null;
        this._changed();
    }
    get height() { this._lines(); return this._box[3] * Math.abs(this.scaleY); }
    set height(value) {
        this._box[3] = Math.max(0, +value);
        this._layout = null;
        this._changed();
    }
    get type() { return this._type; }
    set type(value) { this._type = value === "input" ? "input" : "dynamic"; this._changed(); }
    get multiline() { return this._multiline; }
    set multiline(value) { this._multiline = !!value; this._textChanged(); }
    get wordWrap() { return this._wordWrap; }
    set wordWrap(value) { this._wordWrap = !!value; this._textChanged(); }
    get autoSize() { return this._autoSize; }
    set autoSize(value) {
        if (value === true) value = "left";
        if (value === false) value = "none";
        this._autoSize = ["left", "center", "right"].indexOf(value) >= 0 ? value : "none";
        this._textChanged();
    }
    get selectable() { return this._selectable; }
    set selectable(value) { this._selectable = !!value; }
    get border() { return this._border; }
    set border(value) { this._border = !!value; this._changed(); }
    get borderColor() { return this._borderColor; }
    set borderColor(value) { this._borderColor = value >>> 0 & 0xffffff; this._changed(); }
    get background() { return this._background; }
    set background(value) { this._background = !!value; this._changed(); }
    get backgroundColor() { return this._backgroundColor; }
    set backgroundColor(value) { this._backgroundColor = value >>> 0 & 0xffffff; this._changed(); }
    get textColor() { return (this._formats[0] || this._default).color; }
    set textColor(value) {
        var format = new TextFormat();
        format.color = value >>> 0 & 0xffffff;
        this.setTextFormat(format);
        this._default = merged(this._default, format);
    }
    get displayAsPassword() { return this._password; }
    set displayAsPassword(value) { this._password = !!value; this._textChanged(); }
    get maxChars() { return this._maxChars; }
    set maxChars(value) { this._maxChars = Math.max(0, value | 0); }
    get restrict() { return this._restrict; }
    set restrict(value) { this._restrict = value === null || value === undefined ? null : String(value); }
    get condenseWhite() { return this._condenseWhite; }
    set condenseWhite(value) { this._condenseWhite = !!value; }
    get textWidth() { return this._lines().width; }
    get textHeight() { return this._lines().height; }
    get numLines() { return this._lines().lines.length; }
    get scrollV() { return this._scrollV; }
    set scrollV(value) { this._scrollV = Math.max(1, Math.min(this.maxScrollV, value | 0)); this._changed(); }
    get maxScrollV() {
        var layout = this._lines(), lines = layout.lines, visible = this._box[3] - 2 * gutter;
        var total = 0;
        for (var i = lines.length - 1; i >= 0; --i) {
            total += lines[i].height;
            if (total > visible)
                return Math.max(1, i + 2);
        }
        return 1;
    }
    get bottomScrollV() {
        var lines = this._lines().lines, visible = this._box[3] - 2 * gutter, total = 0;
        for (var i = this._scrollV - 1; i < lines.length; ++i) {
            total += lines[i].height;
            if (total > visible)
                return Math.max(this._scrollV, i);
        }
        return lines.length;
    }
    get scrollH() { return this._scrollH; }
    set scrollH(value) { this._scrollH = Math.max(0, value | 0); this._changed(); }
    get maxScrollH() { return Math.max(0, Math.ceil(this._lines().width - this._box[2] + 2 * gutter)); }
    get caretIndex() { return this._caret; }
    get selectionBeginIndex() { return this._selection[0]; }
    get selectionEndIndex() { return this._selection[1]; }
    setSelection(beginIndex, endIndex) {
        this._selection = [Math.max(0, beginIndex | 0), Math.max(0, endIndex | 0)];
        this._caret = this._selection[1];
    }
    getLineText(lineIndex) {
        var line = this._lines().lines[lineIndex];
        if (!line)
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        return this._text.slice(line.start, line.end);
    }
    getLineLength(lineIndex) { return this.getLineText(lineIndex).length; }
    getLineOffset(lineIndex) {
        var line = this._lines().lines[lineIndex];
        return line ? line.start : -1;
    }
    getLineIndexOfChar(charIndex) {
        var lines = this._lines().lines;
        for (var i = 0; i < lines.length; ++i)
            if (charIndex >= lines[i].start && charIndex <= lines[i].end)
                return i;
        return -1;
    }
    getCharIndexAtPoint(x, y) {
        var layout = this._lines(), top = gutter;
        for (var i = 0; i < layout.lines.length; ++i) {
            var line = layout.lines[i];
            if (y >= top && y < top + line.height) {
                for (var k = 0; k < line.runs.length; ++k) {
                    var run = line.runs[k];
                    if (x >= run.x && x < run.x + run.width)
                        return run.start + Math.floor((x - run.x) / Math.max(1, run.width) * (run.end - run.start));
                }
                return -1;
            }
            top += line.height;
        }
        return -1;
    }
    getLineIndexAtPoint(x, y) {
        var layout = this._lines(), top = gutter;
        for (var i = 0; i < layout.lines.length; ++i) {
            if (y >= top && y < top + layout.lines[i].height)
                return i;
            top += layout.lines[i].height;
        }
        return -1;
    }
    getCharBoundaries(charIndex) {
        var layout = this._lines(), top = gutter;
        for (var i = 0; i < layout.lines.length; ++i) {
            var line = layout.lines[i];
            for (var k = 0; k < line.runs.length; ++k) {
                var run = line.runs[k];
                if (charIndex >= run.start && charIndex < run.end) {
                    var before = measure(this._shown(run.start, charIndex), run.format).width;
                    var wide = measure(this._shown(charIndex, charIndex + 1), run.format).width;
                    return new flash.geom.Rectangle(run.x + before, top, wide, line.height);
                }
            }
            top += line.height;
        }
        return null;
    }
    getLineMetrics(lineIndex) {
        var line = this._lines().lines[lineIndex];
        if (!line)
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        return {x: line.x, width: line.width, height: line.height, ascent: line.ascent, descent: line.descent, leading: line.leading};
    }
    getParagraphLength(charIndex) {
        var start = this._text.lastIndexOf("\r", charIndex - 1) + 1;
        var end = this._text.indexOf("\r", charIndex);
        return (end < 0 ? this._text.length : end + 1) - start;
    }
    /// The text as shown: dots for a password.
    _shown(from, to) {
        var part = this._text.slice(from, to);
        return this._password ? part.replace(/./g, "*") : part;
    }

    /// The lines, laid out once until the text or the box changes.
    _lines() {
        if (this._layout)
            return this._layout;
        var text = this._text, formats = this._formats, self = this;
        var wrapWidth = this._wordWrap ? Math.max(1, this._box[2] - 2 * gutter) : Infinity;
        var lines = [];
        var paragraphStart = 0;
        // Line breaks show whether or not the field is multiline (which is
        // what Enter does in an input field).
        var paragraphs = text.split("\r");
        paragraphs.forEach(function (paragraph) {
            var start = paragraphStart;
            var end = start + paragraph.length;
            // Words: break points after spaces.
            var lineStart = start;
            while (true) {
                var lineEnd = end;
                if (wrapWidth !== Infinity) {
                    var width = 0, lastBreak = -1, i = lineStart;
                    for (; i < end; ++i) {
                        var f = formats[i] || self._default;
                        var w = measure(self._shown(i, i + 1), f).width;
                        if (width + w > wrapWidth && i > lineStart) {
                            lineEnd = lastBreak > lineStart ? lastBreak : i;
                            break;
                        }
                        width += w;
                        if (text[i] === " " || text[i] === "\t" || text[i] === "-")
                            lastBreak = i + 1;
                    }
                    if (i >= end)
                        lineEnd = end;
                }
                lines.push(self._line(lineStart, lineEnd));
                if (lineEnd >= end)
                    break;
                lineStart = lineEnd;
            }
            paragraphStart = end + 1;
        });
        var widest = 0, high = 0;
        lines.forEach(function (line) {
            widest = Math.max(widest, line.width);
            high += line.height;
        });
        // The last line's leading does not count.
        if (lines.length)
            high -= lines[lines.length - 1].leading;
        this._layout = {lines: lines, width: widest, height: high};
        this._fit();
        return this._layout;
    }
    /// One line: its runs of one format, measured.
    _line(start, end) {
        var formats = this._formats, runs = [], x = 0, ascent = 0, descent = 0, leading = 0;
        var first = formats[start] || this._formats[start - 1] || this._default;
        var i = start;
        while (i < end) {
            var f = formats[i] || this._default, j = i;
            while (j < end && sameFormat(formats[j] || this._default, f))
                ++j;
            var shown = this._shown(i, j);
            var m = measure(shown, f);
            runs.push({start: i, end: j, x: x, width: m.width, format: f, text: shown});
            x += m.width;
            ascent = Math.max(ascent, m.ascent);
            descent = Math.max(descent, m.descent);
            leading = Math.max(leading, f.leading || 0);
            i = j;
        }
        if (!runs.length) {
            var empty = measure("", first);
            ascent = empty.ascent;
            descent = empty.descent;
            leading = first.leading || 0;
        }
        // Trailing spaces do not count toward the line's width.
        var width = x;
        var trimmed = this._text.slice(start, end).replace(/\s+$/, "");
        if (trimmed.length < end - start && runs.length) {
            var last = runs[runs.length - 1];
            width = last.x + measure(this._shown(last.start, Math.max(last.start, start + trimmed.length)), last.format).width;
        }
        var align = first.align;
        var room = this._box[2] - 2 * gutter - first.leftMargin - first.rightMargin - first.indent;
        var offset = gutter + first.leftMargin + first.indent;
        if (align === "center")
            offset += Math.max(0, (room - width) / 2);
        else if (align === "right" || align === "end")
            offset += Math.max(0, room - width);
        runs.forEach(function (run) { run.x += offset; });
        return {start: start, end: end, runs: runs, width: width, x: offset, ascent: ascent, descent: descent, leading: leading,
            height: ascent + descent + leading};
    }
    /// Auto-sized: the box takes the text's size.
    _fit() {
        if (this._autoSize === "none" || !this._layout || this._fitting)
            return;
        this._fitting = true;
        var layout = this._layout;
        var wide = this._wordWrap ? this._box[2] : Math.ceil(layout.width + 2 * gutter);
        var high = Math.ceil(layout.height + 2 * gutter);
        if (!this._wordWrap) {
            if (this._autoSize === "center")
                this._box[0] += (this._box[2] - wide) / 2;
            else if (this._autoSize === "right")
                this._box[0] += this._box[2] - wide;
            if (wide !== this._box[2]) {
                this._box[2] = wide;
                // Alignment within the box moved with it.
                this._layout = null;
                this._lines();
            }
        }
        this._box[3] = high;
        this._fitting = false;
    }
    _contentBounds(strokes) {
        this._lines();
        return [this._box[0], this._box[1], this._box[0] + this._box[2], this._box[1] + this._box[3]];
    }
    _hitContent(x, y) {
        var box = this._contentBounds(true);
        return x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3];
    }
    _draw(ctx, m, c, r) {
        var layout = this._lines();
        var box = this._box;
        ctx.save();
        ctx.setTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
        if (r.maskPass) {
            ctx.fillStyle = "#000";
            ctx.fillRect(box[0], box[1], box[2], box[3]);
            ctx.restore();
            return;
        }
        if (this._background) {
            ctx.fillStyle = C.css(this._backgroundColor, 1, c);
            ctx.fillRect(box[0], box[1], box[2], box[3]);
        }
        if (this._border) {
            ctx.strokeStyle = C.css(this._borderColor, 1, c);
            ctx.lineWidth = 1;
            ctx.strokeRect(box[0] + 0.5, box[1] + 0.5, box[2] - 1, box[3] - 1);
        }
        // Being typed into: the input laid over it shows the text.
        if (this._editing) {
            ctx.restore();
            return;
        }
        ctx.beginPath();
        ctx.rect(box[0], box[1], box[2], box[3]);
        ctx.clip();
        ctx.textBaseline = "alphabetic";
        var top = box[1] + gutter;
        for (var i = this._scrollV - 1; i < layout.lines.length; ++i) {
            var line = layout.lines[i];
            if (top > box[1] + box[3])
                break;
            var baseline = top + line.ascent;
            for (var k = 0; k < line.runs.length; ++k) {
                var run = line.runs[k];
                ctx.font = cssFont(run.format);
                if ("letterSpacing" in ctx)
                    ctx.letterSpacing = (run.format.letterSpacing || 0) + "px";
                ctx.fillStyle = C.css(run.format.color, 1, c);
                var x = box[0] + run.x - this._scrollH;
                ctx.fillText(run.text, x, baseline);
                if (run.format.underline) {
                    ctx.fillRect(x, baseline + Math.max(1, run.format.size / 14), run.width, Math.max(1, run.format.size / 16));
                }
            }
            top += line.height;
        }
        ctx.restore();
    }
}

function toTextFormat(f) {
    var out = new TextFormat();
    formatKeys.forEach(function (key) { out[key] = f[key] === undefined ? null : f[key]; });
    return out;
}

/// The tags of htmlText Phoenix reads: P, BR, FONT, B, I, U, A, LI,
/// TEXTFORMAT and SPAN (class ignored), with the entities.
function parseHtml(html, base, multiline, condense) {
    var text = "", formats = [];
    var stack = [Object.assign({}, base)];
    var current = function () { return stack[stack.length - 1]; };
    var push = function (change) { stack.push(Object.assign({}, current(), change)); };
    var entities = {amp: "&", lt: "<", gt: ">", quot: "\"", apos: "'", nbsp: " "};
    var add = function (s) {
        s = s.replace(/&(#x[0-9a-f]+|#\d+|\w+);/gi, function (all, name) {
            if (name[0] === "#")
                return String.fromCharCode(name[1] === "x" || name[1] === "X" ? parseInt(name.slice(2), 16) : parseInt(name.slice(1), 10));
            return entities[name.toLowerCase()] !== undefined ? entities[name.toLowerCase()] : all;
        });
        if (condense)
            s = s.replace(/\s+/g, " ");
        var f = current();
        for (var i = 0; i < s.length; ++i) {
            var ch = s[i] === "\n" ? (condense ? " " : "\r") : s[i];
            text += ch;
            formats.push(f);
        }
    };
    var attributes = function (source) {
        var out = {};
        source.replace(/(\w+)\s*=\s*("([^"]*)"|'([^']*)'|(\S+))/g, function (all, key, v, a, b, c) {
            out[key.toLowerCase()] = a !== undefined ? a : b !== undefined ? b : c;
        });
        return out;
    };
    var tag = /<(\/?)(\w+)([^>]*)>/g, last = 0, match;
    var paragraphOpen = false;
    while ((match = tag.exec(html)) !== null) {
        add(html.slice(last, match.index));
        last = tag.lastIndex;
        var closing = !!match[1], name = match[2].toLowerCase(), attrs = attributes(match[3]);
        if (name === "br") {
            text += "\r";
            formats.push(current());
            continue;
        }
        if (closing) {
            if (name === "p" || name === "li") {
                if (stack.length > 1)
                    stack.pop();
                text += "\r";
                formats.push(current());
                paragraphOpen = false;
            } else if (stack.length > 1)
                stack.pop();
            continue;
        }
        switch (name) {
        case "p":
            push({align: (attrs.align || current().align).toLowerCase()});
            paragraphOpen = true;
            break;
        case "font":
            var change = {};
            if (attrs.face) change.font = attrs.face;
            if (attrs.size) {
                var size = attrs.size;
                change.size = size[0] === "+" || size[0] === "-" ? current().size + parseFloat(size) : parseFloat(size);
            }
            if (attrs.color) change.color = parseInt(attrs.color.replace("#", ""), 16) || 0;
            if (attrs.letterspacing) change.letterSpacing = parseFloat(attrs.letterspacing) || 0;
            push(change);
            break;
        case "b": push({bold: true}); break;
        case "i": push({italic: true}); break;
        case "u": push({underline: true}); break;
        case "a": push({url: attrs.href || "", target: attrs.target || ""}); break;
        case "li": push({bullet: true}); break;
        case "textformat":
            var tf = {};
            if (attrs.leading) tf.leading = parseFloat(attrs.leading);
            if (attrs.indent) tf.indent = parseFloat(attrs.indent);
            if (attrs.leftmargin) tf.leftMargin = parseFloat(attrs.leftmargin);
            if (attrs.rightmargin) tf.rightMargin = parseFloat(attrs.rightmargin);
            push(tf);
            break;
        default:
            push({});
            break;
        }
    }
    add(html.slice(last));
    // The paragraph break the last </P> leaves goes.
    if (text.length && text[text.length - 1] === "\r") {
        text = text.slice(0, -1);
        formats.pop();
    }
    return {text: text, formats: formats};
}

class Font {
    constructor() {
        this.fontName = "";
        this.fontStyle = "regular";
        this.fontType = "device";
    }
    hasGlyphs(str) { return true; }
    static enumerateFonts() { return []; }
    static registerFont(font) {}
}

phoenix.defineClass("flash.text.TextField", TextField);
phoenix.defineClass("flash.text.TextFormat", TextFormat);
phoenix.defineClass("flash.text.Font", Font);
[["TextFieldType", TextFieldType], ["TextFieldAutoSize", TextFieldAutoSize], ["TextFormatAlign", TextFormatAlign],
    ["AntiAliasType", AntiAliasType], ["GridFitType", GridFitType]].forEach(function (pair) {
    phoenix.defineClass("flash.text." + pair[0], function () {});
    Object.assign(flash.text[pair[0]], pair[1]);
});
phoenix._.cssFont = cssFont;
})(phoenix);
