// The player: loads a published movie's content, makes the stage and the
// main timeline, and plays it at the movie's frame rate in Flash Player's
// frame order (enterFrame, the new frame's objects made, frameConstructed,
// frame scripts, exitFrame, render, then drawing). It sends the pointer and
// the keys to the display list, lays the canvas out on the page and lays
// an HTML input over a text field being typed into.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;
var display = flash.display;
var Event = flash.events.Event, MouseEvent = flash.events.MouseEvent;
var KeyboardEvent = flash.events.KeyboardEvent, FocusEvent = flash.events.FocusEvent;

/// What content files and ActionScript files hand the runtime before it
/// starts.
phoenix._.contents = phoenix._.contents || [];
phoenix._.modules = phoenix._.modules || [];
/// A content file (README.md, "Content") calls this with its movie.
phoenix.content = function (movie) {
    phoenix._.contents.push(movie);
};
/// A translated ActionScript file (README.md, "Translated ActionScript")
/// calls this with a function that defines and registers its classes.
phoenix.actionscript = function (module) {
    phoenix._.modules.push(module);
    if (phoenix._.started)
        phoenix._.runModule(module);
};

var stage = null;
var rootClip = null;
var canvas = null, holder = null;
var view = {scale: 1, sx: 1, sy: 1, ox: 0, oy: 0, dpr: 1, width: 0, height: 0};
var options = {};
var drawPending = false;
var running = false;
var timer = null;
var nextTick = 0;
var frameCount = 0;

// Frame events, to every object listening, on the stage or not.
var broadcast = {enterFrame: new Set(), exitFrame: new Set(), frameConstructed: new Set(), render: new Set()};
phoenix._.broadcastListening = function (object, type, on) {
    var set = broadcast[type];
    if (!set)
        return;
    if (on)
        set.add(object);
    else
        set.delete(object);
};
function sendBroadcast(type) {
    var set = broadcast[type];
    if (!set.size)
        return;
    Array.from(set).forEach(function (object) {
        var event = new Event(type);
        event._target = object;
        event._eventPhase = 2;
        object._deliver(event, false);
    });
}

// Frame scripts: queued as clips enter frames, run in order.
var scriptQueue = [];
var inScripts = false, inFrame = false;
phoenix._.queueScript = function (clip) {
    if (!clip._queued) {
        clip._queued = true;
        scriptQueue.push(clip);
    }
};
function runScripts() {
    if (inScripts)
        return;
    inScripts = true;
    try {
        for (var guard = 0; scriptQueue.length && guard < 100000; ++guard) {
            var clip = scriptQueue.shift();
            clip._queued = false;
            if (clip._runFrameScript)
                clip._runFrameScript();
        }
    } finally {
        inScripts = false;
    }
}
/// A script sent a clip to another frame: outside the frame's own work,
/// that frame's script runs now.
phoenix._.afterGoTo = function () {
    if (!inFrame && !inScripts)
        runScripts();
};
/// After events from outside the frame loop (the pointer, timers): the
/// scripts they queued run, and the stage draws.
phoenix._.afterEvents = function () {
    if (!inFrame)
        runScripts();
    requestDraw();
};

/// Every display object on the stage, the stage first, a container before
/// what it holds (and a button's showing state).
function walk(object, visit) {
    visit(object);
    if (object._children)
        for (var i = 0; i < object._children.length; ++i)
            walk(object._children[i], visit);
    else if (object instanceof display.SimpleButton && object._shown)
        walk(object._shown, visit);
}

/// One frame.
function tick() {
    if (!stage)
        return;
    inFrame = true;
    try {
        var advanced = [];
        walk(stage, function (object) {
            if (object instanceof display.MovieClip && object._advance())
                advanced.push(object);
        });
        sendBroadcast("enterFrame");
        advanced.forEach(function (clip) {
            if (clip._timeline)
                clip._construct();
        });
        sendBroadcast("frameConstructed");
        runScripts();
        sendBroadcast("exitFrame");
        if (stage._invalidated) {
            stage._invalidated = false;
            sendBroadcast("render");
        }
    } catch (error) {
        phoenix._.reportError(error, "frame " + (frameCount + 1));
    } finally {
        inFrame = false;
    }
    ++frameCount;
    requestDraw();
}

// Sounds on frames.

/// The timeline's sounds on frame \a to, and its streams that frame leaves.
phoenix._.frameEntered = function (clip, from, to) {
    var timeline = clip._timeline;
    if (!timeline || !timeline.sounds)
        return;
    if (clip._streams)
        clip._streams = clip._streams.filter(function (stream) {
            if (to >= stream.from && to < stream.to && to >= (from === undefined ? to : from))
                return true;
            stream.channel.stop();
            return false;
        });
    for (var i = 0; i < timeline.sounds.length; ++i) {
        var sound = timeline.sounds[i];
        if (sound.f !== to)
            continue;
        playFrameSound(clip, sound);
    }
};
function playFrameSound(owner, sound) {
    var sync = sound.y || "event";
    var active = phoenix._.activeSounds || (phoenix._.activeSounds = new Map());
    var list = active.get(sound.s) || [];
    if (sync === "stop") {
        list.forEach(function (channel) { channel.stop(); });
        active.set(sound.s, []);
        return;
    }
    if (sync === "start" && list.some(function (channel) { return !channel._stopped; }))
        return;
    var channel = phoenix._.playLibrarySound(sound.s, sound.l || 1, 0);
    list = list.filter(function (each) { return !each._stopped; });
    list.push(channel);
    active.set(sound.s, list);
    if (sync === "stream") {
        owner._streams = owner._streams || [];
        owner._streams.push({from: sound.f, to: sound.e || sound.f + 1, channel: channel});
    }
}
phoenix._.buttonSound = function (button, frame) {
    var timeline = button._symbolDef && button._symbolDef.timeline;
    if (!timeline || !timeline.sounds)
        return;
    timeline.sounds.forEach(function (sound) {
        if (sound.f === frame)
            playFrameSound(button, sound);
    });
};
phoenix._.onStage = function (object, on) {
    if (!on && object._streams) {
        object._streams.forEach(function (stream) { stream.channel.stop(); });
        object._streams = [];
    }
};

// Drawing and layout.

function requestDraw() {
    if (drawPending || !canvas)
        return;
    drawPending = true;
    var draw = function () {
        drawPending = false;
        drawNow();
    };
    if (typeof requestAnimationFrame !== "undefined" && !options.syncDraw)
        requestAnimationFrame(draw);
    else
        setTimeout(draw, 0);
}
phoenix._.requestDraw = requestDraw;
function drawNow() {
    if (!canvas || !stage)
        return;
    var m = [view.sx * view.dpr, 0, 0, view.sy * view.dpr, view.ox * view.dpr, view.oy * view.dpr];
    try {
        phoenix._.drawStage(stage, canvas, m, Math.sqrt(view.sx * view.sy) * view.dpr);
    } catch (error) {
        phoenix._.reportError(error, "drawing");
    }
    placeEditor();
}
/// The canvas's size and the stage's place on it, as the scale mode and
/// the publish settings say.
function layout() {
    if (!canvas || !stage)
        return;
    var w = stage._stageWidth, h = stage._stageHeight;
    var fit = options.scale !== "none";
    var room = fit ? [Math.max(1, holder.clientWidth || window.innerWidth), Math.max(1, holder.clientHeight || window.innerHeight)] : [w, h];
    var mode = stage._scaleMode;
    var cw = w, ch = h, sx = 1, sy = 1, ox = 0, oy = 0;
    if (fit) {
        cw = room[0];
        ch = room[1];
        if (mode === "noScale") {
            sx = sy = 1;
        } else if (mode === "exactFit") {
            sx = cw / w;
            sy = ch / h;
        } else if (mode === "noBorder") {
            sx = sy = Math.max(cw / w, ch / h);
        } else {
            sx = sy = Math.min(cw / w, ch / h);
            // The canvas is the stage's shape: no bars inside it.
            cw = Math.round(w * sx);
            ch = Math.round(h * sy);
        }
        if (mode === "noScale") {
            var align = stage._align;
            ox = align.indexOf("L") >= 0 ? 0 : align.indexOf("R") >= 0 ? cw - w : (cw - w) / 2;
            oy = align.indexOf("T") >= 0 ? 0 : align.indexOf("B") >= 0 ? ch - h : (ch - h) / 2;
            if (!align)
                ox = oy = 0;
        } else {
            ox = (cw - w * sx) / 2;
            oy = (ch - h * sy) / 2;
        }
    }
    var dpr = options.devicePixelRatio || (typeof devicePixelRatio !== "undefined" ? devicePixelRatio : 1) || 1;
    view = {scale: Math.sqrt(sx * sy), sx: sx, sy: sy, ox: ox, oy: oy, dpr: dpr, width: cw, height: ch};
    canvas.style.width = cw + "px";
    canvas.style.height = ch + "px";
    canvas.width = Math.max(1, Math.round(cw * dpr));
    canvas.height = Math.max(1, Math.round(ch * dpr));
    requestDraw();
}
phoenix._.layout = layout;
phoenix._.viewSize = function () {
    return [Math.round(view.width / view.sx), Math.round(view.height / view.sy)];
};

// The pointer.

var pointer = {x: 0, y: 0, over: null, overChain: [], pressed: null, down: false, lastClick: null, lastClickTime: 0, inside: false};
var drag = null;
function toStage(event) {
    var box = canvas.getBoundingClientRect();
    var x = (event.clientX - box.left - view.ox) / view.sx;
    var y = (event.clientY - box.top - view.oy) / view.sy;
    return [x, y];
}
/// What the pointer finds at a point on the stage: the deepest object that
/// takes the mouse, or null where there is only the stage. A shape or
/// bitmap hit passes the hit up to its container.
function pick(object, x, y) {
    if (!object._visible || object._maskOwner || object._maskGroup !== null)
        return null;
    if (object instanceof display.SimpleButton) {
        if (!object._mouseEnabled || !object._hitStageButton(x, y))
            return null;
        return {target: object};
    }
    if (object._mask && !object._mask._hitStage(x, y, true))
        return null;
    if (object._scrollRect) {
        var local = object._globalToLocal(x, y);
        var r = object._scrollRect;
        if (local[0] < 0 || local[1] < 0 || local[0] > r.width || local[1] > r.height)
            return null;
    }
    if (object instanceof display.DisplayObjectContainer) {
        if (object._hitArea) {
            if (!object._hitArea._hitStage(x, y, true))
                return null;
            return {target: object._mouseEnabled ? object : null};
        }
        var children = object._children;
        for (var i = children.length - 1; i >= 0; --i) {
            var child = children[i];
            if (child._clipGroup !== null && child._clipGroup !== undefined) {
                // Masked by a timeline mask: found only inside the mask.
                var group = child._clipGroup, inside = false;
                for (var k = 0; k < children.length; ++k)
                    if (children[k]._maskGroup === group && children[k]._hitStage(x, y, true))
                        inside = true;
                if (!inside)
                    continue;
            }
            var found = pick(child, x, y);
            if (!found)
                continue;
            if (!object._mouseChildren)
                return {target: object._mouseEnabled ? object : null};
            if (found.target)
                return found;
            return {target: object._mouseEnabled && !(object instanceof display.Stage) ? object : null};
        }
        var self = object._hitOwn && object._hitOwn.apply(object, object._globalToLocal(x, y).concat([true]));
        if (self)
            return {target: object._mouseEnabled && !(object instanceof display.Stage) ? object : null};
        return null;
    }
    if (!object._hitStage(x, y, true))
        return null;
    if (object instanceof display.InteractiveObject)
        return {target: object._mouseEnabled ? object : null};
    return {target: null};
}
display.SimpleButton.prototype._hitStageButton = function (x, y) {
    if (!this._hitTestState)
        return false;
    var local = this._globalToLocal(x, y);
    return this._hitContent(local[0], local[1], true);
};
function chainOf(object) {
    var chain = [];
    for (var at = object; at && !(at instanceof display.Stage); at = at._parent)
        chain.push(at);
    return chain;
}
function mouseEvent(type, target, related, event, bubbles) {
    var e = new MouseEvent(type, bubbles === undefined ? true : bubbles, false, 0, 0, related || null,
        event ? event.ctrlKey : false, event ? event.altKey : false, event ? event.shiftKey : false, pointer.down,
        type === "mouseWheel" && event ? -Math.sign(event.deltaY) * 3 : 0);
    e._stageX = pointer.x;
    e._stageY = pointer.y;
    var local = target._globalToLocal ? target._globalToLocal(pointer.x, pointer.y) : [pointer.x, pointer.y];
    e._localX = local[0];
    e._localY = local[1];
    target.dispatchEvent(e);
}
function targetAt(x, y) {
    var found = pick(stage, x, y);
    return found && found.target ? found.target : null;
}
/// The pointer moved, or what is under it did: over, out and roll events
/// for what it is over now, and buttons' states.
function hover(event) {
    var target = pointer.inside ? targetAt(pointer.x, pointer.y) : null;
    var was = pointer.over;
    if (target !== was) {
        var oldChain = pointer.overChain, newChain = chainOf(target);
        if (was && was._stage)
            mouseEvent("mouseOut", was, target, event);
        for (var i = 0; i < oldChain.length; ++i)
            if (newChain.indexOf(oldChain[i]) < 0 && oldChain[i]._stage)
                mouseEvent("rollOut", oldChain[i], target, event, false);
        for (var j = newChain.length - 1; j >= 0; --j)
            if (oldChain.indexOf(newChain[j]) < 0)
                mouseEvent("rollOver", newChain[j], was, event, false);
        if (target)
            mouseEvent("mouseOver", target, was, event);
        pointer.over = target;
        pointer.overChain = newChain;
    }
    updateButtons();
    updateCursor();
}
function updateButtons() {
    walk(stage, function (object) {
        if (!(object instanceof display.SimpleButton))
            return;
        var over = pointer.overChain.indexOf(object) >= 0;
        var state = object._enabled && over ? (pointer.down && pointer.pressed && chainOf(pointer.pressed).indexOf(object) >= 0 ? "down" : "over")
            : (object._enabled && pointer.down && pointer.pressed && chainOf(pointer.pressed).indexOf(object) >= 0 && object.trackAsMenu ? "over" : "up");
        object._setState(state);
    });
}
function updateCursor() {
    if (!canvas)
        return;
    var mouse = phoenix._.mouse;
    var cursor = "default";
    if (mouse && mouse._hidden)
        cursor = "none";
    else if (mouse && mouse._cursor !== "auto")
        cursor = mouse._cursor === "ibeam" ? "text" : mouse._cursor === "arrow" ? "default" : "pointer";
    else {
        for (var i = 0; i < pointer.overChain.length; ++i) {
            var at = pointer.overChain[i];
            if ((at instanceof display.SimpleButton && at._useHandCursor && at._enabled) ||
                (at instanceof display.Sprite && at._buttonMode && at._useHandCursor)) {
                cursor = "pointer";
                break;
            }
            // A CSS cursor of its own (CreateJS's cursor).
            if (at._cssCursor) {
                cursor = at._cssCursor;
                break;
            }
            if (at instanceof flash.text.TextField && (at._type === "input" || at._selectable)) {
                cursor = at._type === "input" ? "text" : "default";
                break;
            }
        }
    }
    canvas.style.cursor = cursor;
}
phoenix._.updateCursor = updateCursor;
phoenix._.startDrag = function (object, lockCenter, bounds) {
    var parent = object._parent;
    var at = parent ? parent._globalToLocal(pointer.x, pointer.y) : [pointer.x, pointer.y];
    drag = {object: object, dx: lockCenter ? 0 : object.x - at[0], dy: lockCenter ? 0 : object.y - at[1], bounds: bounds};
    moveDragged();
};
phoenix._.stopDrag = function (object) {
    if (drag && drag.object === object) {
        var under = targetAt(pointer.x, pointer.y);
        object._dropTarget = under === object ? null : under;
    }
    drag = null;
};
function moveDragged() {
    if (!drag)
        return;
    var object = drag.object, parent = object._parent;
    var at = parent ? parent._globalToLocal(pointer.x, pointer.y) : [pointer.x, pointer.y];
    var x = at[0] + drag.dx, y = at[1] + drag.dy;
    if (drag.bounds) {
        var b = drag.bounds;
        x = Math.max(Math.min(b.x, b.x + b.width), Math.min(Math.max(b.x, b.x + b.width), x));
        y = Math.max(Math.min(b.y, b.y + b.height), Math.min(Math.max(b.y, b.y + b.height), y));
    }
    object.x = x;
    object.y = y;
}
function onPointerMove(event) {
    var at = toStage(event);
    pointer.x = phoenix._.mouseX = at[0];
    pointer.y = phoenix._.mouseY = at[1];
    pointer.inside = true;
    moveDragged();
    hover(event);
    mouseEvent("mouseMove", pointer.over || stage, null, event);
    phoenix._.afterEvents();
}
function onPointerDown(event) {
    if (event.button !== undefined && event.button !== 0)
        return;
    phoenix._.resumeAudio && phoenix._.resumeAudio();
    var at = toStage(event);
    pointer.x = phoenix._.mouseX = at[0];
    pointer.y = phoenix._.mouseY = at[1];
    pointer.inside = true;
    pointer.down = true;
    hover(event);
    var target = pointer.over || stage;
    pointer.pressed = target;
    // A press puts the keyboard's focus on what it hit.
    if (target instanceof flash.text.TextField && (target._type === "input" || target._selectable))
        setFocus(target);
    else if (target !== stage && target instanceof display.InteractiveObject)
        setFocus(target, true);
    else
        setFocus(null);
    mouseEvent("mouseDown", target, null, event);
    updateButtons();
    if (canvas.setPointerCapture && event.pointerId !== undefined)
        try { canvas.setPointerCapture(event.pointerId); } catch (error) {}
    phoenix._.afterEvents();
    if (!(focus && focus._type === "input"))
        event.preventDefault();
}
function onPointerUp(event) {
    if (event.button !== undefined && event.button !== 0)
        return;
    var at = toStage(event);
    pointer.x = phoenix._.mouseX = at[0];
    pointer.y = phoenix._.mouseY = at[1];
    pointer.down = false;
    var target = targetAt(pointer.x, pointer.y) || stage;
    mouseEvent("mouseUp", target, null, event);
    var pressed = pointer.pressed;
    pointer.pressed = null;
    if (pressed && pressed === target) {
        var now = phoenix.now();
        var doubled = pointer.lastClick === target && now - pointer.lastClickTime < 500 && target._doubleClickEnabled;
        if (doubled) {
            mouseEvent("doubleClick", target, null, event);
            pointer.lastClick = null;
        } else {
            mouseEvent("click", target, null, event);
            pointer.lastClick = target;
            pointer.lastClickTime = now;
        }
    } else if (pressed && pressed !== stage && pressed._stage)
        mouseEvent("releaseOutside", pressed, null, event);
    hover(event);
    phoenix._.afterEvents();
}
function onPointerLeave(event) {
    pointer.inside = false;
    hover(event);
    stage.dispatchEvent(new Event(Event.MOUSE_LEAVE));
    phoenix._.afterEvents();
}
function onWheel(event) {
    var target = pointer.over || stage;
    mouseEvent("mouseWheel", target, null, event);
    phoenix._.afterEvents();
}

// The keys and the focus.

var focus = null;
function setFocus(object, quiet) {
    if (object === focus)
        return;
    var was = focus;
    focus = object;
    stage._focus = object;
    if (was) {
        if (was._editing)
            closeEditor(was);
        was.dispatchEvent(new FocusEvent(FocusEvent.FOCUS_OUT, true, false, object));
    }
    if (object) {
        object.dispatchEvent(new FocusEvent(FocusEvent.FOCUS_IN, true, false, was));
        if (object instanceof flash.text.TextField && object._type === "input")
            openEditor(object);
    }
}
phoenix._.setFocus = function (object) { setFocus(object); };
function keyEvent(type, event) {
    var target = focus && focus._stage ? focus : stage;
    var charCode = event.key && event.key.length === 1 ? event.key.charCodeAt(0) : event.key === "Enter" ? 13 : 0;
    var keyCode = event.keyCode || 0;
    var location = event.location === 1 ? 1 : event.location === 2 ? 2 : event.location === 3 ? 3 : 0;
    target.dispatchEvent(new KeyboardEvent(type, true, false, charCode, keyCode, location, event.ctrlKey, event.altKey,
        event.shiftKey));
    phoenix._.afterEvents();
}
function onKeyDown(event) {
    phoenix._.resumeAudio && phoenix._.resumeAudio();
    keyEvent("keyDown", event);
    // Arrows and space would scroll the page under a game.
    if (!(focus && focus._editing) && [32, 37, 38, 39, 40].indexOf(event.keyCode) >= 0)
        event.preventDefault();
}
function onKeyUp(event) { keyEvent("keyUp", event); }

// Typing into an input text field: an HTML input over it.

var editor = null;
function openEditor(field) {
    if (!holder || typeof document === "undefined")
        return;
    closeEditor();
    var element = document.createElement(field._multiline ? "textarea" : "input");
    if (!field._multiline)
        element.type = field._password ? "password" : "text";
    var f = field._formats[0] || field._default;
    element.value = field._text.replace(/\r/g, "\n");
    if (field._maxChars)
        element.maxLength = field._maxChars;
    var style = element.style;
    style.position = "absolute";
    style.margin = "0";
    style.border = "none";
    style.outline = "none";
    style.background = "transparent";
    style.resize = "none";
    style.overflow = "hidden";
    style.color = C.css(f.color, 1, null);
    style.textAlign = f.align === "center" ? "center" : f.align === "right" ? "right" : "left";
    style.boxSizing = "border-box";
    style.whiteSpace = field._wordWrap ? "pre-wrap" : "pre";
    style.transformOrigin = "0 0";
    element.spellcheck = false;
    holder.appendChild(element);
    editor = {element: element, field: field};
    field._editing = true;
    element.addEventListener("input", function () {
        var value = element.value;
        if (field._restrict !== null)
            value = restricted(value, field._restrict);
        if (value !== element.value)
            element.value = value;
        var added = value.length > field._text.length ? value.slice(field._text.length) : "";
        if (added) {
            var textEvent = new flash.events.TextEvent("textInput", true, true, added);
            if (!field.dispatchEvent(textEvent)) {
                element.value = field._text.replace(/\r/g, "\n");
                return;
            }
        }
        var format = field._formats.length ? field._formats[field._formats.length - 1] : field._default;
        field._text = value.replace(/\n/g, "\r");
        field._formats = [];
        for (var i = 0; i < field._text.length; ++i)
            field._formats.push(format);
        field._textChanged();
        field.dispatchEvent(new Event(Event.CHANGE, true));
        phoenix._.afterEvents();
    });
    element.addEventListener("blur", function () {
        if (editor && editor.field === field && focus === field)
            setFocus(null);
    });
    placeEditor();
    setTimeout(function () { element.focus(); }, 0);
}
function restricted(value, restrict) {
    if (!restrict)
        return value;
    try {
        var negate = restrict[0] === "^";
        var set = new RegExp("[" + (negate ? "^" : "") + restrict.replace(/^\^/, "").replace(/\\/g, "\\\\").replace(/]/g, "\\]") + "]");
        return value.split("").filter(function (ch) { return set.test(ch); }).join("");
    } catch (error) {
        return value;
    }
}
function closeEditor() {
    if (!editor)
        return;
    editor.field._editing = false;
    editor.field._changed();
    if (editor.element.parentNode)
        editor.element.parentNode.removeChild(editor.element);
    editor = null;
}
/// Keeps the input over its field as the field moves or the page scales.
function placeEditor() {
    if (!editor || !canvas)
        return;
    var field = editor.field;
    if (!field._stage) {
        closeEditor();
        return;
    }
    var m = M.multiply(field._concatenatedMatrix(), [view.sx, 0, 0, view.sy, view.ox, view.oy]);
    var box = field._box;
    var f = field._formats[0] || field._default;
    var style = editor.element.style;
    style.left = (canvas.offsetLeft + 0) + "px";
    style.top = (canvas.offsetTop + 0) + "px";
    style.width = box[2] + "px";
    style.height = box[3] + "px";
    style.padding = "2px";
    style.font = phoenix._.cssFont(f);
    style.lineHeight = "normal";
    style.transform = "matrix(" + [m[0], m[1], m[2], m[3], m[4] + m[0] * box[0] + m[2] * box[1], m[5] + m[1] * box[0] + m[3] * box[1]].join(",") + ")";
}

// Starting.

/// Loads the published pictures; resolves when all have come (or failed).
function loadImages(images, base) {
    phoenix._.images = [];
    return Promise.all((images || []).map(function (desc, index) {
        return new Promise(function (resolve) {
            if (typeof Image === "undefined") {
                resolve();
                return;
            }
            var image = new Image();
            image.onload = function () {
                phoenix._.images[index] = image;
                resolve();
            };
            image.onerror = function () {
                phoenix._.reportError(new Error("could not load " + desc.src), "loading");
                resolve();
            };
            image.src = (base || "") + desc.src;
        });
    }));
}
/// Loads a translated ActionScript file, when there is one.
function loadScript(url) {
    return new Promise(function (resolve) {
        if (!url || typeof document === "undefined") {
            resolve(false);
            return;
        }
        var script = document.createElement("script");
        script.src = url;
        script.onload = function () { resolve(true); };
        script.onerror = function () { resolve(false); };
        document.head.appendChild(script);
    });
}

/// The library as the timeline classes see it, and a class for each
/// linkage name a movie's code may name.
function buildLibrary(movie) {
    var symbols = (movie.symbols || []).map(function (symbol, index) {
        return Object.assign({index: index}, symbol);
    });
    phoenix._.library = {symbols: symbols, art: movie.art || [], texts: movie.texts || [], movie: movie};
    phoenix._.rootDef = {index: -1, name: "", type: "mc", timeline: movie.root || {n: 1, states: [[]], map: [0]}};
    symbols.forEach(function (def) {
        if (!def.linkage || phoenix.classByName(def.linkage))
            return;
        var Base = def.base ? phoenix.classByName(def.base) : null;
        if (!Base)
            Base = def.type === "button" ? display.SimpleButton : display.MovieClip;
        var made = {[def.linkage.split(".").pop()]: class extends Base {}}[def.linkage.split(".").pop()];
        phoenix.defineClass(def.linkage, made);
        linkClass(def, made);
    });
    (movie.sounds || []).forEach(function (sound, index) {
        if (!sound.linkage || phoenix.classByName(sound.linkage))
            return;
        var made = class extends flash.media.Sound {};
        phoenix.defineClass(sound.linkage, made);
        phoenix._.soundForClass.set(made, index);
    });
    (movie.images || []).forEach(function (image, index) {
        if (!image.linkage || phoenix.classByName(image.linkage))
            return;
        var made = class extends display.BitmapData {};
        phoenix.defineClass(image.linkage, made);
        phoenix._.bitmapForClass.set(made, index);
    });
}
function linkClass(def, cls) {
    phoenix._.classForSymbol.set(def.index, cls);
    phoenix._.symbolForClass.set(cls, def);
}

/// What translated ActionScript registers its classes through (README.md,
/// "Translated ActionScript").
var as3 = {
    ns: phoenix.ns,
    flash: flash,
    trace: phoenix.trace,
    defineClass: phoenix.defineClass,
    classByName: phoenix.classByName,
    /// Binds \a cls to the library item whose linkage class (or, failing
    /// that, library name) is \a name: a symbol, a sound or a bitmap.
    linkClass: function (name, cls) {
        var library = phoenix._.library;
        var movie = library.movie;
        var def = library.symbols.filter(function (s) { return s.linkage === name; })[0] ||
            library.symbols.filter(function (s) { return s.name === name; })[0];
        if (def) {
            linkClass(def, cls);
            if (def.linkage && phoenix.classByName(def.linkage) !== cls)
                phoenix.defineClass(def.linkage, cls);
            return true;
        }
        var sounds = movie.sounds || [];
        for (var i = 0; i < sounds.length; ++i)
            if (sounds[i].linkage === name || sounds[i].name === name) {
                phoenix._.soundForClass.set(cls, i);
                return true;
            }
        var images = movie.images || [];
        for (var j = 0; j < images.length; ++j)
            if (images[j].linkage === name || images[j].name === name) {
                phoenix._.bitmapForClass.set(cls, j);
                return true;
            }
        return false;
    },
    /// The main timeline's class: the document class, or the class made
    /// for the main timeline's frame scripts.
    documentClass: function (cls) {
        phoenix._.documentClass = cls;
        phoenix._.symbolForClass.set(cls, phoenix._.rootDef);
    },
    /// Frame scripts for a timeline with no class of its own: \a symbol is
    /// a library name or linkage (empty for the main timeline), \a scripts
    /// functions by frame (from 0), each called with the clip as this.
    frameScripts: function (symbol, scripts) {
        var library = phoenix._.library;
        var def = !symbol ? phoenix._.rootDef : library.symbols.filter(function (s) {
            return s.linkage === symbol || s.name === symbol;
        })[0];
        if (!def)
            return false;
        def.frameScripts = Object.assign(def.frameScripts || {}, scripts);
        return true;
    },
    get stage() { return stage; },
    get root() { return rootClip; },
    library: function () {
        return phoenix._.library.symbols.map(function (s) { return {name: s.name, linkage: s.linkage || null, type: s.type}; });
    }
};
phoenix.as3 = as3;
phoenix._.runModule = function (module) {
    try {
        module(as3);
    } catch (error) {
        phoenix._.reportError(error, "ActionScript");
    }
};

/// Timelines with frame scripts registered without a class get them as
/// each of their clips is made.
var initTimeline = phoenix._.initTimeline;
phoenix._.initTimeline = function (object, cls) {
    initTimeline(object, cls);
    var def = object._symbolDef;
    if (def && def.frameScripts && object instanceof display.MovieClip) {
        var args = [];
        Object.keys(def.frameScripts).forEach(function (frame) { args.push(+frame, def.frameScripts[frame]); });
        object.addFrameScript.apply(object, args);
    }
};

/// Starts the movie (README.md, "Pages"). \a settings: container (an
/// element or its id) or canvas, content (a movie; else the last content
/// file loaded), base (where its files are), scale ("fit" or "none"),
/// actionscript (a translated file to load first, or false), frame (stop
/// on that frame, from 1: for tests and stills), trace (an element to
/// write trace output and errors into).
phoenix.start = function (settings) {
    settings = settings || {};
    options = settings;
    var movie = settings.content || phoenix._.contents[phoenix._.contents.length - 1];
    if (!movie)
        throw new Error("phoenix.start: no content");
    var hash = typeof location !== "undefined" ? location.hash : "";
    var stopAt = settings.frame || 0;
    var hashFrame = /[#&]frame=(\d+)/.exec(hash);
    if (hashFrame)
        stopAt = +hashFrame[1];
    if (/[#&]trace\b/.test(hash) && !settings.trace)
        settings.trace = "phoenix-trace";
    var traceOut = typeof settings.trace === "string" && typeof document !== "undefined" ? document.getElementById(settings.trace) : settings.trace;
    var hashScale = /[#&]scale=(fit|none)/.exec(hash);
    if (hashScale)
        settings.scale = hashScale[1];
    if (settings.trace && typeof document !== "undefined" && !traceOut) {
        // Out of the page's way: for tests that read the page back.
        traceOut = document.createElement("pre");
        traceOut.id = settings.trace;
        traceOut.style.cssText = "position: fixed; left: 0; top: 0; margin: 0; opacity: 0; pointer-events: none;";
        document.body.appendChild(traceOut);
    }
    if (traceOut) {
        phoenix.traceListeners.push(function (line) { traceOut.textContent += line + "\n"; });
        // What scripts write to the console comes too (trace writes there
        // already).
        if (typeof console !== "undefined")
            ["log", "info", "warn", "error"].forEach(function (level) {
                var original = console[level];
                console[level] = function () {
                    original.apply(console, arguments);
                    if (!phoenix._.tracing)
                        traceOut.textContent += Array.prototype.map.call(arguments, phoenix.show).join(" ") + "\n";
                };
            });
        var report = phoenix._.reportError;
        phoenix._.reportError = function (error, where) {
            report(error, where);
            traceOut.textContent += "Error in " + where + ": " + String(error) + "\n";
        };
    }
    buildLibrary(movie);
    phoenix._.loadSounds(movie.sounds, settings.base);
    var scriptUrl = settings.actionscript === undefined ? null : settings.actionscript;
    return Promise.all([loadImages(movie.images, settings.base), loadScript(scriptUrl)]).then(function () {
        phoenix._.started = true;
        phoenix._.modules.forEach(phoenix._.runModule);
        if (settings.prepare)
            settings.prepare(movie);
        begin(movie, settings);
        if (stopAt > 0) {
            // Played to that frame at once, and held there.
            for (var i = 1; i < stopAt; ++i)
                tick();
            drawNow();
            phoenix._.ready = true;
            return phoenix.player;
        }
        play();
        phoenix._.ready = true;
        return phoenix.player;
    });
};

function begin(movie, settings) {
    stage = new display.Stage();
    phoenix._.stage = stage;
    stage._stageWidth = movie.width || 550;
    stage._stageHeight = movie.height || 400;
    stage._frameRate = movie.frameRate || 24;
    stage._color = movie.background === undefined ? 0xffffff : movie.background;
    phoenix._.loaderInfo = new display.LoaderInfo(null);
    if (typeof document !== "undefined") {
        holder = typeof settings.container === "string" ? document.getElementById(settings.container) : settings.container;
        canvas = settings.canvas || null;
        if (!canvas) {
            canvas = document.createElement("canvas");
            canvas.className = "phoenix-stage";
            (holder || document.body).appendChild(canvas);
        }
        holder = holder || canvas.parentNode;
        canvas.tabIndex = 0;
        canvas.style.display = "block";
        canvas.style.touchAction = "none";
        canvas.style.outline = "none";
        canvas.addEventListener("pointermove", onPointerMove);
        canvas.addEventListener("pointerdown", onPointerDown);
        canvas.addEventListener("pointerup", onPointerUp);
        canvas.addEventListener("pointerleave", onPointerLeave);
        canvas.addEventListener("wheel", onWheel, {passive: true});
        canvas.addEventListener("contextmenu", function (event) { event.preventDefault(); });
        window.addEventListener("keydown", onKeyDown);
        window.addEventListener("keyup", onKeyUp);
        window.addEventListener("resize", function () {
            layout();
            if (stage._scaleMode === "noScale")
                stage.dispatchEvent(new Event(Event.RESIZE));
        });
        layout();
    }
    phoenix._.frameRateChanged = function () {
        if (running) {
            stop();
            play();
        }
    };
    // The main timeline: made on the stage, so its constructor sees it. Its
    // class is the one registered as the document class, or the class
    // defined under the name the publish settings give it.
    var named = movie.documentClass ? phoenix.classByName(movie.documentClass) : null;
    if (!phoenix._.documentClass && named && named.prototype instanceof display.DisplayObject)
        as3.documentClass(named);
    var Cls = phoenix._.documentClass || display.MovieClip;
    phoenix._.pending = {def: phoenix._.rootDef, name: "root1"};
    var made;
    inFrame = true;
    try {
        made = makeRoot(Cls);
    } catch (error) {
        phoenix._.reportError(error, "the document class");
        phoenix._.pending = {def: phoenix._.rootDef, name: "root1"};
        made = makeRoot(display.MovieClip);
    } finally {
        inFrame = false;
    }
    rootClip = made;
    phoenix._.rootClip = made;
    phoenix._.loaderInfo._content = made;
    // What layers over the runtime (CreateJS) set up before the first
    // frame's scripts run.
    (phoenix._.onRoot || []).forEach(function (hook) {
        try {
            hook(stage, made);
        } catch (error) {
            phoenix._.reportError(error, "starting");
        }
    });
    inFrame = true;
    try {
        sendBroadcast("frameConstructed");
        runScripts();
        sendBroadcast("exitFrame");
    } finally {
        inFrame = false;
    }
    try {
        phoenix._.frameEntered(made, undefined, 0);
    } catch (error) {
        phoenix._.reportError(error, "the first frame's sounds");
    }
    requestDraw();
}
/// The root is on the stage while it is made: its children see the stage
/// as they are added, and its constructor finds stage set.
function makeRoot(Cls) {
    var hook = phoenix._.initTimeline;
    var root = null;
    phoenix._.initTimeline = function (object, cls) {
        if (!root && object._placement && object._placement.def === phoenix._.rootDef) {
            root = object;
            object._parent = stage;
            object._stage = stage;
            stage._children.push(object);
        }
        hook(object, cls);
    };
    try {
        var made = new Cls();
        if (!root) {
            stage.addChild(made);
            root = made;
        }
        return root;
    } finally {
        phoenix._.initTimeline = hook;
    }
}

function play() {
    if (running)
        return;
    running = true;
    nextTick = phoenix.now();
    schedule();
}
function schedule() {
    var interval = 1000 / (stage ? stage._frameRate : 24);
    nextTick += interval;
    var wait = Math.max(0, nextTick - phoenix.now());
    // Far behind (a hidden tab): start the count again rather than race.
    if (wait === 0 && phoenix.now() - nextTick > interval * 4)
        nextTick = phoenix.now();
    timer = setTimeout(function () {
        if (!running)
            return;
        tick();
        schedule();
    }, wait);
}
function stop() {
    running = false;
    if (timer !== null)
        clearTimeout(timer);
    timer = null;
}

/// The player, for pages and tests: step frames, pause and go on.
phoenix.player = {
    tick: function () { tick(); drawNow(); },
    play: play,
    pause: stop,
    get frame() { return frameCount + 1; },
    get stage() { return stage; },
    get root() { return rootClip; },
    draw: drawNow,
    errors: function () { return (phoenix._.errors || []).slice(); }
};
})(phoenix);
