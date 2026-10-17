// The ActionScript 2 components (mx.controls: Button, CheckBox, RadioButton,
// TextInput, TextArea, Label, ComboBox, List, NumericStepper) for movies
// that have no classes of their own for them: each instance draws itself in
// the components' Halo look, set up from the parameters the content gives
// its entry ("cp"), with the properties, methods and events scripts use
// (addEventListener with a function or a listener object, on(click)
// handlers). mx.utils.Delegate and mx.events.EventDispatcher come too.
// Joined after as2.js into phoenix-as2.js.
(function (phoenix) {
"use strict";

var A = phoenix._.as2;
if (!A)
    return;
var $ = A.avm1, G = $.global;
var display = flash.display, text = flash.text;

var colors = {face: 0xf7f7f7, faceLow: 0xd9d9d9, border: 0x919999, halo: 0x80ff4d, text: 0x0b333c, disabled: 0x848384,
    selection: 0xcdffc1, over: 0xe3ffd6, field: 0xffffff};
// Each component's size where the instance has no art to take it from.
var sizes = {Button: [100, 22], CheckBox: [100, 22], RadioButton: [100, 22], TextInput: [100, 22], TextArea: [100, 100],
    Label: [100, 20], ComboBox: [100, 22], List: [100, 100], NumericStepper: [50, 22]};
var rowHeight = 20;

function D(self) { return A.displayOf(self); }
function str(v) { return v === undefined || v === null ? "" : $.s(v); }
function num(v, fallback) { var n = $.n(v); return isFinite(n) ? n : fallback; }
function bool(v) { return !!$.b(v); }
function report(error, where) { phoenix._.reportError(error, where); }

// ---- Events: mx.events.EventDispatcher

function listenersOf(o) {
    if (!Object.prototype.hasOwnProperty.call(o, "__listeners"))
        Object.defineProperty(o, "__listeners", {value: Object.create(null), writable: true, configurable: true});
    return o.__listeners;
}
function addEventListener(type, listener) {
    var list = listenersOf(this)[str(type)] || (listenersOf(this)[str(type)] = []);
    if (listener && list.indexOf(listener) < 0)
        list.push(listener);
}
function removeEventListener(type, listener) {
    var list = listenersOf(this)[str(type)];
    var at = list ? list.indexOf(listener) : -1;
    if (at >= 0)
        list.splice(at, 1);
}
/// Sends \a event to the listeners of its type: a function is called with
/// it, an object has its method of the event's name (or handleEvent)
/// called; then the instance's own on(type) handlers.
function dispatchEvent(event) {
    var self = this;
    if (event.target === undefined)
        event.target = self;
    var list = (listenersOf(self)[str(event.type)] || []).slice();
    list.forEach(function (listener) {
        try {
            if (typeof listener === "function")
                listener.call(undefined, event);
            else if (listener && typeof listener[event.type] === "function")
                listener[event.type](event);
            else if (listener && typeof listener.handleEvent === "function")
                listener.handleEvent(event);
        } catch (error) {
            report(error, "the " + event.type + " listener");
        }
    });
    var d = D(self);
    if (d)
        A.onEvent(d, event.type);
    return true;
}
var EventDispatcher = function EventDispatcher() {};
EventDispatcher.initialize = function (o) {
    if (!o)
        return;
    o.addEventListener = addEventListener;
    o.removeEventListener = removeEventListener;
    o.dispatchEvent = dispatchEvent;
    $.hide(o, ["addEventListener", "removeEventListener", "dispatchEvent"]);
};
var Delegate = function Delegate() {};
Delegate.create = function (target, fn) {
    return function () { return typeof fn === "function" ? fn.apply(target, arguments) : undefined; };
};

// ---- Drawing

function field(d, size, color) {
    var t = new text.TextField();
    t.selectable = false;
    t.autoSize = "left";
    var f = new text.TextFormat("_sans", size || 11, color === undefined ? colors.text : color);
    t.defaultTextFormat = f;
    d.addChild(t);
    return t;
}
function setText(t, value, color) {
    var f = t.defaultTextFormat;
    if (color !== undefined)
        f.color = color;
    t.defaultTextFormat = f;
    t.text = value;
}
/// A raised box: the face's gradient in its border, haloed when the pointer
/// is over it, darker while pressed.
function box(g, x, y, w, h, state, round) {
    var top = state === "down" ? colors.faceLow : colors.field, bottom = state === "down" ? colors.face : colors.faceLow;
    var m = new flash.geom.Matrix();
    m.createGradientBox(w, h, Math.PI / 2, x, y);
    g.lineStyle(1, state === "over" || state === "down" ? 0x5ab52f : colors.border, 1, true);
    g.beginGradientFill("linear", [top, bottom], [1, 1], [0, 255], m);
    if (round)
        g.drawRoundRect(x + 0.5, y + 0.5, w - 1, h - 1, round, round);
    else
        g.drawRect(x + 0.5, y + 0.5, w - 1, h - 1);
    g.endFill();
    if (state === "over") {
        g.lineStyle(2, colors.halo, 0.6, true);
        if (round)
            g.drawRoundRect(x + 1.5, y + 1.5, w - 3, h - 3, round, round);
        else
            g.drawRect(x + 1.5, y + 1.5, w - 3, h - 3);
    }
}
/// A sunken field's border and white inside.
function well(g, w, h, enabled) {
    g.lineStyle(1, colors.border, 1, true);
    g.beginFill(enabled ? colors.field : 0xeeeeee);
    g.drawRect(0.5, 0.5, w - 1, h - 1);
    g.endFill();
}
function arrow(g, x, y, size, down, color) {
    g.lineStyle();
    g.beginFill(color === undefined ? colors.text : color);
    if (down) {
        g.moveTo(x - size, y - size / 2);
        g.lineTo(x + size, y - size / 2);
        g.lineTo(x, y + size / 2);
    } else {
        g.moveTo(x - size, y + size / 2);
        g.lineTo(x + size, y + size / 2);
        g.lineTo(x, y - size / 2);
    }
    g.endFill();
}

// ---- The components

/// Items as ActionScript 2 lists have them: strings, or objects with a
/// label (and data).
function labelOf(item, field) {
    if (item === undefined || item === null)
        return "";
    if (typeof item === "object") {
        if (field && item[field] !== undefined)
            return str(item[field]);
        if (item.label !== undefined)
            return str(item.label);
    }
    return str(item);
}
function itemsFrom(p) {
    var labels = p.labels || p.dataProvider || [], data = p.data || [];
    return labels.map(function (label, i) {
        return data[i] === undefined || data[i] === "" ? label : {label: label, data: data[i]};
    });
}

/// Makes the component class \a name: \a build sets up its parts once,
/// \a draw draws it at its size; \a props and \a meths are its own.
function component(name, build, draw, props, meths) {
    var cls = function () {
        var self = this, d = D(self);
        if (!d || d._c)
            return;
        // The size the instance is placed at: its art's, else the
        // component's own size, scaled.
        var w = d.width, h = d.height;
        if (!(w > 1 && h > 1)) {
            w = sizes[name][0] * Math.abs(d.scaleX || 1);
            h = sizes[name][1] * Math.abs(d.scaleY || 1);
        }
        // Its symbol's own art (avatar and skins) gives way to what the
        // component draws.
        if (d instanceof display.MovieClip) {
            d._timeline = {n: 1, states: [[]], map: [0]};
            d.gotoAndStop(1);
        }
        d.scaleX = 1;
        d.scaleY = 1;
        var c = d._c = {self: self, name: name, w: w, h: h, enabled: true, state: "up", bg: new display.Shape()};
        d.addChild(c.bg);
        c.redraw = function () {
            try {
                c.bg.graphics.clear();
                draw(d, c);
            } catch (error) {
                report(error, "drawing " + name);
            }
        };
        try {
            build(d, c, d._placement && d._placement.cp || {});
        } catch (error) {
            report(error, "making " + name);
        }
        c.redraw();
    };
    $.extend(cls, G.MovieClip);
    var p = cls.prototype;
    A.properties(p, Object.assign({
        enabled: [function (d) { return d._c ? d._c.enabled : true; },
            function (d, v) { if (d._c) { d._c.enabled = bool(v); d.mouseEnabled = d._c.enabled; d._c.redraw(); } }],
        width: [function (d) { return d._c ? d._c.w : d.width; }],
        height: [function (d) { return d._c ? d._c.h : d.height; }],
        _width: [function (d) { return d._c ? d._c.w : d.width; }, function (d, v) { resize(d, num(v, 0), null); }],
        _height: [function (d) { return d._c ? d._c.h : d.height; }, function (d, v) { resize(d, null, num(v, 0)); }],
        className: [function () { return name; }]
    }, props || {}));
    A.methods(p, Object.assign({
        setSize: function (w, h) { resize(D(this), num(w, 0), num(h, 0)); },
        move: function (x, y) { var d = D(this); if (d) { d.x = num(x, d.x); d.y = num(y, d.y); } },
        addEventListener: addEventListener,
        removeEventListener: removeEventListener,
        dispatchEvent: dispatchEvent,
        setFocus: function () { var d = D(this); if (d && d._c && d._c.input && d.stage) d.stage.focus = d._c.input; },
        invalidate: function () { var d = D(this); if (d && d._c) d._c.redraw(); },
        redraw: function () { var d = D(this); if (d && d._c) d._c.redraw(); }
    }, meths || {}));
    A.builtins["mx.controls." + name] = cls;
    return cls;
}
function resize(d, w, h) {
    if (!d || !d._c)
        return;
    if (w !== null)
        d._c.w = Math.max(1, w);
    if (h !== null)
        d._c.h = Math.max(1, h);
    d._c.redraw();
}
function emit(c, type, extra) {
    var event = Object.assign({type: type, target: c.self}, extra || {});
    dispatchEvent.call(c.self, event);
}
/// Pointer states for a pressable component: over, down, and \a action on
/// a click.
function pressable(d, c, action) {
    d.buttonMode = true;
    d.useHandCursor = true;
    d.addEventListener("rollOver", function () { if (c.enabled) { c.state = "over"; c.redraw(); } });
    d.addEventListener("rollOut", function () { c.state = "up"; c.redraw(); });
    d.addEventListener("mouseDown", function () { if (c.enabled) { c.state = "down"; c.redraw(); } });
    d.addEventListener("mouseUp", function () { if (c.enabled) { c.state = "over"; c.redraw(); } });
    d.addEventListener("click", function () { if (c.enabled) action(); });
}

// Button: label, toggle, selected, labelPlacement, icon (not drawn).
var Button = component("Button", function (d, c, p) {
    c.label = p.label === undefined ? "Button" : str(p.label);
    c.toggle = !!p.toggle;
    c.selected = !!p.selected;
    c.placement = p.labelPlacement || "right";
    c.text = field(d);
    pressable(d, c, function () {
        if (c.toggle)
            c.selected = !c.selected;
        c.redraw();
        emit(c, "click");
    });
}, function (d, c) {
    box(c.bg.graphics, 0, 0, c.w, c.h, c.toggle && c.selected ? "down" : c.state, 6);
    setText(c.text, c.label, c.enabled ? colors.text : colors.disabled);
    c.text.x = Math.round((c.w - c.text.width) / 2);
    c.text.y = Math.round((c.h - c.text.height) / 2);
}, {
    label: [function (d) { return d._c.label; }, function (d, v) { d._c.label = str(v); d._c.redraw(); }],
    selected: [function (d) { return d._c.selected; }, function (d, v) { d._c.selected = bool(v); d._c.redraw(); }],
    toggle: [function (d) { return d._c.toggle; }, function (d, v) { d._c.toggle = bool(v); d._c.redraw(); }],
    labelPlacement: [function (d) { return d._c.placement; }, function (d, v) { d._c.placement = str(v); d._c.redraw(); }]
});

/// CheckBox and RadioButton: a box or a circle beside their label.
function checkable(name, round) {
    return component(name, function (d, c, p) {
        c.label = p.label === undefined ? name : str(p.label);
        c.selected = !!p.selected;
        c.placement = p.labelPlacement || "right";
        if (round) {
            c.group = p.groupName === undefined ? "radioGroup" : str(p.groupName);
            c.data = p.data;
        }
        c.text = field(d);
        pressable(d, c, function () {
            if (round) {
                if (c.selected)
                    return;
                select(d, true);
            } else {
                c.selected = !c.selected;
                c.redraw();
            }
            emit(c, "click");
        });
        if (round && c.selected)
            select(d, true);
    }, function (d, c) {
        var g = c.bg.graphics, s = 13;
        setText(c.text, c.label, c.enabled ? colors.text : colors.disabled);
        var left = c.placement === "left";
        var bx = left ? Math.max(0, c.w - s) : 0, by = Math.round((c.h - s) / 2);
        g.lineStyle(1, c.state === "over" ? 0x5ab52f : colors.border, 1, true);
        g.beginFill(c.state === "down" ? colors.faceLow : colors.field);
        if (round)
            g.drawCircle(bx + s / 2, by + s / 2, s / 2 - 0.5);
        else
            g.drawRect(bx + 0.5, by + 0.5, s - 1, s - 1);
        g.endFill();
        if (c.selected) {
            g.lineStyle();
            g.beginFill(c.enabled ? colors.text : colors.disabled);
            if (round)
                g.drawCircle(bx + s / 2, by + s / 2, 3);
            else {
                g.moveTo(bx + 3, by + 6);
                g.lineTo(bx + 5.5, by + 9);
                g.lineTo(bx + 10.5, by + 3);
                g.lineTo(bx + 10.5, by + 5);
                g.lineTo(bx + 5.5, by + 11);
                g.lineTo(bx + 3, by + 8);
            }
            g.endFill();
        }
        // The whole of it takes the pointer.
        g.lineStyle();
        g.beginFill(0, 0);
        g.drawRect(0, 0, c.w, c.h);
        g.endFill();
        c.text.x = left ? Math.max(0, bx - 4 - c.text.width) : s + 4;
        c.text.y = Math.round((c.h - c.text.height) / 2);
    }, Object.assign({
        label: [function (d) { return d._c.label; }, function (d, v) { d._c.label = str(v); d._c.redraw(); }],
        labelPlacement: [function (d) { return d._c.placement; }, function (d, v) { d._c.placement = str(v); d._c.redraw(); }],
        selected: [function (d) { return d._c.selected; }, function (d, v) {
            if (round)
                select(d, bool(v));
            else {
                d._c.selected = bool(v);
                d._c.redraw();
            }
        }]
    }, round ? {
        groupName: [function (d) { return d._c.group; }, function (d, v) { d._c.group = str(v); }],
        data: [function (d) { return d._c.data; }, function (d, v) { d._c.data = v; }],
        selection: [function (d) { var s = selectedIn(d); return s ? s._c.self : undefined; },
            function (d, v) { var o = v && D(v); if (o && o._c) select(o, true); }]
    } : {}));
}
var CheckBox = checkable("CheckBox", false);
var RadioButton = checkable("RadioButton", true);
/// The radio buttons of \a d's group: those with its group name under the
/// same parent.
function groupOf(d) {
    var parent = d.parent;
    if (!parent)
        return [d];
    return parent._children.filter(function (o) { return o._c && o._c.name === "RadioButton" && o._c.group === d._c.group; });
}
function selectedIn(d) {
    var found = null;
    groupOf(d).forEach(function (o) { if (o._c.selected) found = o; });
    return found;
}
function select(d, on) {
    if (on)
        groupOf(d).forEach(function (o) {
            if (o !== d && o._c.selected) {
                o._c.selected = false;
                o._c.redraw();
            }
        });
    d._c.selected = on;
    d._c.redraw();
}

/// TextInput and TextArea: a field to type in.
function typed(name, multiline) {
    return component(name, function (d, c, p) {
        var t = c.input = new text.TextField();
        t.type = p.editable === false ? "dynamic" : "input";
        t.multiline = multiline;
        t.wordWrap = multiline && p.wordWrap !== false;
        t.displayAsPassword = !!p.password;
        if (p.maxChars)
            t.maxChars = num(p.maxChars, 0);
        if (p.restrict)
            t.restrict = str(p.restrict);
        t.defaultTextFormat = new text.TextFormat("_sans", 11, colors.text);
        if (p.html && p.text !== undefined)
            t.htmlText = str(p.text);
        else
            t.text = p.text === undefined ? "" : str(p.text);
        d.addChild(t);
        t.addEventListener("change", function () { emit(c, "change"); });
        t.addEventListener("keyDown", function (event) { if (event.keyCode === 13 && !multiline) emit(c, "enter"); });
        t.addEventListener("focusIn", function () { emit(c, "focusIn"); });
        t.addEventListener("focusOut", function () { emit(c, "focusOut"); });
    }, function (d, c) {
        well(c.bg.graphics, c.w, c.h, c.enabled);
        c.input.x = 2;
        c.input.y = 1;
        c.input.width = Math.max(1, c.w - 4);
        c.input.height = Math.max(1, c.h - 2);
        c.input.type = c.enabled && c.editable !== false ? c.input.type : "dynamic";
    }, {
        text: [function (d) { return d._c.input.text; }, function (d, v) { d._c.input.text = str(v); }],
        htmlText: [function (d) { return d._c.input.htmlText; }, function (d, v) { d._c.input.htmlText = str(v); }],
        length: [function (d) { return d._c.input.length; }],
        editable: [function (d) { return d._c.input.type === "input"; },
            function (d, v) { d._c.input.type = bool(v) ? "input" : "dynamic"; }],
        password: [function (d) { return d._c.input.displayAsPassword; }, function (d, v) { d._c.input.displayAsPassword = bool(v); }],
        maxChars: [function (d) { return d._c.input.maxChars || null; }, function (d, v) { d._c.input.maxChars = num(v, 0); }],
        restrict: [function (d) { return d._c.input.restrict; }, function (d, v) { d._c.input.restrict = v === null ? null : str(v); }],
        wordWrap: [function (d) { return d._c.input.wordWrap; }, function (d, v) { d._c.input.wordWrap = bool(v); }],
        vPosition: [function (d) { return d._c.input.scrollV - 1; }, function (d, v) { d._c.input.scrollV = num(v, 0) + 1; }],
        maxVPosition: [function (d) { return d._c.input.maxScrollV - 1; }]
    });
}
var TextInput = typed("TextInput", false);
var TextArea = typed("TextArea", true);

// Label: text, autoSize, html.
var Label = component("Label", function (d, c, p) {
    c.text = field(d);
    c.text.autoSize = "none";
    c.html = !!p.html;
    c.auto = p.autoSize || "none";
    c.value = p.text === undefined ? "Label" : str(p.text);
}, function (d, c) {
    if (c.html)
        c.text.htmlText = c.value;
    else
        setText(c.text, c.value, c.enabled ? colors.text : colors.disabled);
    c.text.autoSize = c.auto === "none" || c.auto === false ? "none" : c.auto;
    if (c.text.autoSize === "none") {
        c.text.width = c.w;
        c.text.height = c.h;
    }
}, {
    text: [function (d) { return d._c.value; }, function (d, v) { d._c.value = str(v); d._c.redraw(); }],
    htmlText: [function (d) { return d._c.value; }, function (d, v) { d._c.html = true; d._c.value = str(v); d._c.redraw(); }],
    html: [function (d) { return d._c.html; }, function (d, v) { d._c.html = bool(v); d._c.redraw(); }],
    autoSize: [function (d) { return d._c.auto; }, function (d, v) { d._c.auto = str(v); d._c.redraw(); }]
});

/// List and ComboBox's list: rows of items, one (or more) selected.
function listMethods(rows) {
    return {
        addItem: function (label, data) {
            var c = D(this)._c;
            c.items.push(data === undefined && typeof label === "object" ? label : data === undefined ? label : {label: label, data: data});
            c.redraw();
        },
        addItemAt: function (index, label, data) {
            var c = D(this)._c;
            c.items.splice(num(index, 0), 0, data === undefined ? label : {label: label, data: data});
            c.redraw();
        },
        removeItemAt: function (index) {
            var c = D(this)._c;
            var gone = c.items.splice(num(index, 0), 1)[0];
            if (c.selected >= c.items.length)
                c.selected = -1;
            c.redraw();
            return gone;
        },
        removeAll: function () { var c = D(this)._c; c.items = []; c.selected = -1; c.scroll = 0; c.redraw(); },
        replaceItemAt: function (index, label, data) {
            var c = D(this)._c;
            c.items[num(index, 0)] = data === undefined ? label : {label: label, data: data};
            c.redraw();
        },
        getItemAt: function (index) { return D(this)._c.items[num(index, 0)]; },
        sortItemsBy: function (field, order) {
            var c = D(this)._c, f = str(field), down = str(order).toUpperCase() === "DESC";
            c.items.sort(function (a, b) {
                var x = labelOf(a, f), y = labelOf(b, f);
                return (x < y ? -1 : x > y ? 1 : 0) * (down ? -1 : 1);
            });
            c.redraw();
        }
    };
}
var listProps = {
    dataProvider: [function (d) { return d._c.items; }, function (d, v) {
        d._c.items = v && typeof v.length === "number" ? v : [];
        d._c.selected = -1;
        d._c.redraw();
    }],
    length: [function (d) { return d._c.items.length; }],
    selectedIndex: [function (d) { return d._c.selected < 0 ? undefined : d._c.selected; },
        function (d, v) { d._c.selected = Math.min(d._c.items.length - 1, num(v, -1)); d._c.redraw(); }],
    selectedItem: [function (d) { return d._c.items[d._c.selected]; }],
    value: [function (d) {
        var item = d._c.items[d._c.selected];
        return item && typeof item === "object" && item.data !== undefined ? item.data : labelOf(item);
    }],
    labelField: [function (d) { return d._c.labelField || "label"; }, function (d, v) { d._c.labelField = str(v); d._c.redraw(); }],
    rowHeight: [function (d) { return d._c.rowHeight; }, function (d, v) { d._c.rowHeight = Math.max(4, num(v, rowHeight)); d._c.redraw(); }],
    rowCount: [function (d) { return d._c.rowCount; }, function (d, v) { d._c.rowCount = Math.max(1, num(v, 5)); d._c.redraw(); }]
};
/// Draws rows of \a c's items from its scroll position into \a holder,
/// \a w by \a h, the selected one marked; returns the row under a point.
function drawRows(holder, c, w, h) {
    var g = holder._rows.graphics;
    g.clear();
    well(g, w, h, c.enabled);
    var rh = c.rowHeight, shown = Math.max(1, Math.floor((h - 2) / rh));
    c.scroll = Math.max(0, Math.min(c.scroll || 0, c.items.length - shown));
    holder._fields.forEach(function (t) { holder.removeChild(t); });
    holder._fields = [];
    for (var i = 0; i < shown && c.scroll + i < c.items.length; ++i) {
        var at = c.scroll + i;
        var selected = c.multiple ? c.chosen.indexOf(at) >= 0 : at === c.selected;
        if (selected || at === c.hover) {
            g.lineStyle();
            g.beginFill(selected ? colors.selection : colors.over);
            g.drawRect(1, 1 + i * rh, w - 2, rh);
            g.endFill();
        }
        var t = field(holder, 11, c.enabled ? colors.text : colors.disabled);
        setText(t, labelOf(c.items[at], c.labelField), c.enabled ? colors.text : colors.disabled);
        t.x = 4;
        t.y = 1 + i * rh + Math.round((rh - t.height) / 2);
        t.mouseEnabled = false;
        holder._fields.push(t);
    }
    // A scroll bar's thumb when there are more rows than show.
    if (c.items.length > shown) {
        var track = h - 2, thumb = Math.max(10, track * shown / c.items.length);
        g.lineStyle();
        g.beginFill(colors.faceLow);
        g.drawRect(w - 7, 1 + (track - thumb) * c.scroll / (c.items.length - shown), 6, thumb);
        g.endFill();
    }
    return shown;
}
function rowsIn(holder, c) {
    holder._rows = new display.Shape();
    holder.addChild(holder._rows);
    holder._fields = [];
}
function rowAt(c, y) { return c.scroll + Math.floor((y - 1) / c.rowHeight); }

// List: rows to choose from; multipleSelection.
var List = component("List", function (d, c, p) {
    c.items = itemsFrom(p);
    c.selected = -1;
    c.chosen = [];
    c.multiple = !!p.multipleSelection;
    c.rowHeight = num(p.rowHeight, rowHeight);
    c.scroll = 0;
    c.hover = -1;
    rowsIn(d, c);
    d.addEventListener("mouseMove", function () { var r = rowAt(c, d.mouseY); if (r !== c.hover) { c.hover = r; c.redraw(); } });
    d.addEventListener("rollOut", function () { c.hover = -1; c.redraw(); });
    d.addEventListener("mouseWheel", function (event) { c.scroll -= event.delta > 0 ? 1 : -1; c.redraw(); });
    d.addEventListener("click", function (event) {
        var r = rowAt(c, d.mouseY);
        if (!c.enabled || r < 0 || r >= c.items.length)
            return;
        if (c.multiple && event.ctrlKey) {
            var at = c.chosen.indexOf(r);
            if (at >= 0)
                c.chosen.splice(at, 1);
            else
                c.chosen.push(r);
        } else
            c.chosen = [r];
        c.selected = r;
        c.redraw();
        emit(c, "change");
    });
}, function (d, c) {
    drawRows(d, c, c.w, c.h);
}, Object.assign({}, listProps, {
    multipleSelection: [function (d) { return d._c.multiple; }, function (d, v) { d._c.multiple = bool(v); }],
    selectedIndices: [function (d) { return d._c.multiple ? d._c.chosen.slice() : d._c.selected < 0 ? undefined : [d._c.selected]; },
        function (d, v) { d._c.chosen = (v || []).map(function (n) { return num(n, 0); }); d._c.selected = d._c.chosen[0] === undefined ? -1 : d._c.chosen[0]; d._c.redraw(); }],
    selectedItems: [function (d) { var c = d._c; return (c.multiple ? c.chosen : [c.selected]).map(function (i) { return c.items[i]; }); }],
    vPosition: [function (d) { return d._c.scroll; }, function (d, v) { d._c.scroll = num(v, 0); d._c.redraw(); }]
}), listMethods());

// ComboBox: the selected item and a button that drops its list down.
var ComboBox = component("ComboBox", function (d, c, p) {
    c.items = itemsFrom(p);
    c.selected = c.items.length ? 0 : -1;
    c.rowHeight = rowHeight;
    c.rowCount = num(p.rowCount, 5);
    c.scroll = 0;
    c.hover = -1;
    c.editable = !!p.editable;
    c.text = field(d);
    c.text.mouseEnabled = false;
    pressable(d, c, function () { if (c.open) close(c); else open(d, c); });
}, function (d, c) {
    var g = c.bg.graphics, b = Math.min(c.h, 22);
    well(g, c.w, c.h, c.enabled);
    box(g, c.w - b, 0, b, c.h, c.state, 0);
    arrow(g, c.w - b / 2, c.h / 2, 4, true, c.enabled ? colors.text : colors.disabled);
    setText(c.text, labelOf(c.items[c.selected], c.labelField), c.enabled ? colors.text : colors.disabled);
    c.text.x = 4;
    c.text.y = Math.round((c.h - c.text.height) / 2);
    if (c.open)
        drawRows(c.open, c, c.w, c.open._h);
}, Object.assign({}, listProps, {
    editable: [function (d) { return d._c.editable; }, function (d, v) { d._c.editable = bool(v); }],
    text: [function (d) { return labelOf(d._c.items[d._c.selected], d._c.labelField); }],
    selectedIndex: [function (d) { return d._c.selected < 0 ? undefined : d._c.selected; },
        function (d, v) { d._c.selected = Math.min(d._c.items.length - 1, num(v, -1)); d._c.redraw(); }]
}), Object.assign(listMethods(), {
    open: function () { var d = D(this); if (d && !d._c.open) open(d, d._c); },
    close: function () { var d = D(this); if (d) close(d._c); }
}));
/// The drop-down list, over everything on the stage, under the box.
function open(d, c) {
    var stage = d.stage;
    if (!stage || !c.items.length)
        return;
    var list = c.open = new display.Sprite();
    var rows = Math.min(c.rowCount, c.items.length);
    list._h = rows * c.rowHeight + 2;
    rowsIn(list, c);
    var at = d.localToGlobal(new flash.geom.Point(0, c.h));
    list.x = at.x;
    list.y = at.y;
    c.scroll = Math.max(0, Math.min(c.selected, c.items.length - rows));
    list.addEventListener("mouseMove", function () { var r = rowAt(c, list.mouseY); if (r !== c.hover) { c.hover = r; c.redraw(); } });
    list.addEventListener("mouseWheel", function (event) { c.scroll -= event.delta > 0 ? 1 : -1; c.redraw(); });
    list.addEventListener("click", function () {
        var r = rowAt(c, list.mouseY);
        close(c);
        if (r >= 0 && r < c.items.length && r !== c.selected) {
            c.selected = r;
            c.redraw();
            emit(c, "change");
        }
    });
    stage.addChild(list);
    // A press anywhere else closes it.
    c.away = function (event) {
        var t = event.target;
        while (t && t !== list && t !== d)
            t = t.parent;
        if (!t)
            close(c);
    };
    stage.addEventListener("mouseDown", c.away);
    c.redraw();
    emit(c, "open");
}
function close(c) {
    if (!c.open)
        return;
    var list = c.open;
    c.open = null;
    if (list.parent)
        list.parent.removeChild(list);
    if (c.away && list.stage)
        list.stage.removeEventListener("mouseDown", c.away);
    c.hover = -1;
    c.redraw();
    emit(c, "close");
}

// NumericStepper: a number and arrows that step it within its range.
var NumericStepper = component("NumericStepper", function (d, c, p) {
    c.min = num(p.minimum, 0);
    c.max = num(p.maximum, 10);
    c.step = num(p.stepSize, 1);
    c.value = Math.max(c.min, Math.min(c.max, num(p.value, 0)));
    var t = c.input = new text.TextField();
    t.type = "input";
    t.restrict = "0-9.\\-";
    t.defaultTextFormat = new text.TextFormat("_sans", 11, colors.text);
    d.addChild(t);
    var commit = function () { stepTo(c, num(t.text, c.value)); };
    t.addEventListener("keyDown", function (event) { if (event.keyCode === 13) commit(); });
    t.addEventListener("focusOut", commit);
    c.arrows = new display.Sprite();
    c.arrows.buttonMode = true;
    d.addChild(c.arrows);
    c.arrows.addEventListener("click", function () {
        if (c.enabled)
            stepTo(c, c.value + (c.arrows.mouseY < c.h / 2 ? c.step : -c.step));
    });
}, function (d, c) {
    var b = 16;
    well(c.bg.graphics, c.w - b, c.h, c.enabled);
    var g = c.arrows.graphics;
    g.clear();
    box(g, 0, 0, b, c.h / 2, "up", 0);
    box(g, 0, c.h / 2, b, c.h / 2, "up", 0);
    arrow(g, b / 2, c.h / 4, 3, false);
    arrow(g, b / 2, c.h * 3 / 4, 3, true);
    c.arrows.x = c.w - b;
    c.input.x = 2;
    c.input.y = 1;
    c.input.width = Math.max(1, c.w - b - 4);
    c.input.height = Math.max(1, c.h - 2);
    c.input.text = String(c.value);
}, {
    value: [function (d) { return d._c.value; }, function (d, v) { stepTo(d._c, num(v, d._c.value), true); }],
    minimum: [function (d) { return d._c.min; }, function (d, v) { d._c.min = num(v, 0); stepTo(d._c, d._c.value, true); }],
    maximum: [function (d) { return d._c.max; }, function (d, v) { d._c.max = num(v, 10); stepTo(d._c, d._c.value, true); }],
    stepSize: [function (d) { return d._c.step; }, function (d, v) { d._c.step = num(v, 1); }],
    nextValue: [function (d) { return Math.min(d._c.max, d._c.value + d._c.step); }],
    previousValue: [function (d) { return Math.max(d._c.min, d._c.value - d._c.step); }]
});
function stepTo(c, value, quiet) {
    var v = Math.max(c.min, Math.min(c.max, value));
    // On the steps from the minimum, as the component keeps it.
    if (c.step > 0)
        v = Math.round((v - c.min) / c.step) * c.step + c.min;
    v = Math.min(c.max, Math.round(v * 1e6) / 1e6);
    var changed = v !== c.value;
    c.value = v;
    c.redraw();
    if (changed && !quiet)
        emit(c, "change");
}

// ---- Where scripts find them: _global.mx...

function namespace(path) {
    var at = G;
    path.split(".").forEach(function (part) {
        if (!at[part] || (typeof at[part] !== "object" && typeof at[part] !== "function"))
            at[part] = {};
        at = at[part];
    });
    return at;
}
function offer(path, name, value) {
    var ns = namespace(path);
    if (ns[name] === undefined)
        ns[name] = value;
}
var classes = {Button: Button, CheckBox: CheckBox, RadioButton: RadioButton, TextInput: TextInput, TextArea: TextArea,
    Label: Label, ComboBox: ComboBox, List: List, NumericStepper: NumericStepper};
Object.keys(classes).forEach(function (name) { offer("mx.controls", name, classes[name]); });
offer("mx.utils", "Delegate", Delegate);
offer("mx.events", "EventDispatcher", EventDispatcher);
})(phoenix);
