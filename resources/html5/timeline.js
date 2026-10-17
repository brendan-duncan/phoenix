// The timeline classes: MovieClip, SimpleButton, Scene and FrameLabel,
// and Graphic, Phoenix's own container for a graphic symbol (it shows the
// frame its parent's timeline says, rather than playing by itself).
//
// A symbol's timeline comes from the published content (README.md,
// "Timelines"): for each frame the list of what it shows, in depth order,
// each with a key that says when an object on one frame is the same object
// on the next. Going to a frame brings the clip's children into line with
// that list: what is no longer there goes, what stays is moved, what is
// new is made.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;
var display = flash.display;
var Event = flash.events.Event;
var ColorTransform = flash.geom.ColorTransform;

/// Classes by the library symbol they are linked to (by index), and the
/// symbol each linked class makes.
phoenix._.classForSymbol = new Map();
phoenix._.symbolForClass = new Map();

function library() { return phoenix._.library || {symbols: [], art: [], texts: []}; }

/// The symbol a new object is made from: the one its timeline placement
/// names, or the one its class (or a class it extends) is linked to.
function symbolOf(object, cls) {
    if (object._placement && object._placement.def)
        return object._placement.def;
    for (var at = cls; at && at !== display.Sprite && at !== display.MovieClip && at !== Function.prototype; at = Object.getPrototypeOf(at)) {
        var def = phoenix._.symbolForClass.get(at);
        if (def)
            return def;
    }
    return null;
}

/// Called by Sprite's constructor: a symbol's object gets its timeline,
/// and its first frame's children are made.
phoenix._.initTimeline = function (object, cls) {
    var def = symbolOf(object, cls);
    object._symbolDef = def;
    object._timeline = def ? def.timeline : null;
    object._frame = 0;
    object._playing = !!def && def.timeline.n > 1;
    object._frameScripts = null;
    object._stateIndex = -1;
    object._timelineChildren = new Map();
    object._entered = false;
    object._scriptFrame = -1;
    if (def && def.scale9)
        object._scale9Grid = new flash.geom.Rectangle(def.scale9[0], def.scale9[1], def.scale9[2] - def.scale9[0], def.scale9[3] - def.scale9[1]);
    if (def) {
        // Its frame script runs before those of the children it makes.
        if (object instanceof MovieClip)
            phoenix._.queueScript && phoenix._.queueScript(object);
        applyFrame(object, 0);
        if (phoenix._.premakeNamed)
            premake(object);
    }
};
/// The named instances of frames still to come, made now and set as the
/// clip's properties, for scripts that expect every instance from the
/// start (an HTML5 Canvas document's: its published clips make them all at
/// once); the frame that places one uses it.
function premake(object) {
    var made = null;
    (object._timeline.states || []).forEach(function (state) {
        state.forEach(function (entry) {
            if (!entry.n || object._timelineChildren.has(entry.k) || (made && made.has(entry.k)) || object[entry.n])
                return;
            var child = makeObject(entry);
            if (!child)
                return;
            (made || (made = new Map())).set(entry.k, child);
            setNamed(object, entry.n, child);
        });
    });
    object._premade = made;
}

/// The class to make a symbol's object with, for an instance that behaves
/// as \a type (its own, which may not be its symbol's).
function classOf(def, type) {
    type = type || def.type;
    var linked = type === def.type ? phoenix._.classForSymbol.get(def.index) : null;
    if (linked)
        return linked;
    if (type === "graphic")
        return Graphic;
    if (type === "button")
        return display.SimpleButton;
    return display.MovieClip;
}
/// What an entry places, to tell whether an object already there is the
/// one it means: the kind, the library item, and how an instance behaves.
function characterOf(entry) {
    return entry.t + entry.id + (entry.it || "");
}
/// A new object for a timeline's entry: a symbol's, a shape, a text
/// field or a bitmap.
function makeObject(entry) {
    var lib = library();
    var object;
    switch (entry.t) {
    case "s":
        var def = lib.symbols[entry.id];
        if (!def)
            return null;
        var type = entry.it || def.type;
        var Cls = classOf(def, type);
        phoenix._.pending = {name: entry.n || null, m: entry.m || null, def: def, cp: entry.cp || null};
        try {
            object = new Cls();
        } catch (error) {
            phoenix._.pending = null;
            phoenix._.reportError(error, "constructing " + (def.linkage || def.name));
            phoenix._.pending = {name: entry.n || null, m: entry.m || null, def: def};
            object = type === "graphic" ? new Graphic() : type === "button" ? new display.SimpleButton() : new MovieClip();
        }
        phoenix._.pending = null;
        break;
    case "a":
        phoenix._.pending = {m: entry.m || null};
        object = new display.Shape();
        var art = lib.art[entry.id];
        if (art)
            object.graphics._setArt(art);
        break;
    case "x":
        phoenix._.pending = {name: entry.n || null, m: entry.m || null};
        object = new flash.text.TextField();
        var desc = lib.texts[entry.id];
        if (desc)
            object._fromTimeline(desc);
        break;
    case "b":
        phoenix._.pending = {m: entry.m || null, image: entry.id};
        object = new display.Bitmap();
        object.smoothing = true;
        break;
    default:
        return null;
    }
    phoenix._.pending = null;
    object._character = characterOf(entry);
    if (entry.n)
        object._name = entry.n;
    return object;
}
function colorOf(entry) {
    var c = entry.ct || C.identity;
    return new ColorTransform(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
}
/// Moves, colors and filters \a object as \a entry says, leaving alone
/// what a script has set.
function update(object, entry, fresh) {
    if (!object._scriptMatrix || fresh) {
        object._m = (entry.m || M.identity).slice();
        object._decomposed = null;
        object._scriptMatrix = false;
    }
    if (!object._scriptColor || fresh) {
        var c = entry.ct || C.identity;
        var t = object._colorTransform;
        t.redMultiplier = c[0]; t.greenMultiplier = c[1]; t.blueMultiplier = c[2]; t.alphaMultiplier = c[3];
        t.redOffset = c[4]; t.greenOffset = c[5]; t.blueOffset = c[6]; t.alphaOffset = c[7];
        object._scriptColor = false;
    }
    var filters = entry.f ? JSON.stringify(entry.f) : "";
    if (object._timelineFilters !== filters) {
        object._timelineFilters = filters;
        object._filters = (entry.f || []).map(phoenix._.filterOf).filter(function (f) { return !!f; });
    }
    object._blendMode = entry.bm || "normal";
    if (entry.v === 0)
        object._visible = false;
    else if (fresh)
        object._visible = true;
    if (entry.cab)
        object._cacheAsBitmap = true;
    object._maskGroup = entry.mk === undefined ? null : entry.mk;
    object._clipGroup = entry.mb === undefined ? null : entry.mb;
    if (object instanceof Graphic)
        object._showFrame(entry.gf || 0);
}
/// Where a new timeline object goes among \a container's children: above
/// the timeline's objects at lower depths, below those at higher ones, and
/// below what scripts added on top.
function indexForDepth(container, depth) {
    var children = container._children, lastLower = -1;
    for (var i = 0; i < children.length; ++i) {
        var d = children[i]._depth;
        if (d === undefined)
            continue;
        if (d > depth)
            return i;
        lastLower = i;
    }
    return lastLower + 1;
}

/// Brings \a object's children into line with frame \a frame (from 0) of
/// its timeline.
function applyFrame(object, frame) {
    var timeline = object._timeline;
    if (!timeline)
        return;
    var index = timeline.map ? timeline.map[Math.min(frame, timeline.map.length - 1)] : frame;
    applyState(object, index === undefined ? -1 : index);
    // A scene's camera colors the whole picture.
    if (timeline.cc && !object._scriptColor) {
        var c = timeline.cc[Math.min(frame, timeline.cc.length - 1)] || C.identity;
        var t = object._colorTransform;
        if (t.redMultiplier !== c[0] || t.greenMultiplier !== c[1] || t.blueMultiplier !== c[2] ||
            t.alphaMultiplier !== c[3] || t.redOffset !== c[4] || t.greenOffset !== c[5] || t.blueOffset !== c[6] ||
            t.alphaOffset !== c[7]) {
            t.redMultiplier = c[0]; t.greenMultiplier = c[1]; t.blueMultiplier = c[2]; t.alphaMultiplier = c[3];
            t.redOffset = c[4]; t.greenOffset = c[5]; t.blueOffset = c[6]; t.alphaOffset = c[7];
            object._changed();
        }
    }
}
function applyState(object, index) {
    if (index === object._stateIndex)
        return;
    object._stateIndex = index;
    var entries = index >= 0 && object._timeline.states[index] || [];
    var wanted = new Map();
    for (var i = 0; i < entries.length; ++i)
        wanted.set(entries[i].k, entries[i]);
    // What the frame no longer has goes.
    var gone = [];
    object._timelineChildren.forEach(function (child, key) {
        var entry = wanted.get(key);
        if (!entry || child._character !== characterOf(entry))
            gone.push(key);
    });
    gone.forEach(function (key) {
        var child = object._timelineChildren.get(key);
        object._timelineChildren.delete(key);
        if (child._parent === object)
            object.removeChild(child);
        child._timelineKey = null;
        child._depth = undefined;
        if (child._name && object[child._name] === child)
            setNamed(object, child._name, null);
    });
    // What stays moves; what is new is made.
    for (var j = 0; j < entries.length; ++j) {
        var entry = entries[j];
        var child = object._timelineChildren.get(entry.k);
        if (child) {
            update(child, entry, false);
            continue;
        }
        child = object._premade && object._premade.get(entry.k);
        if (child && child._character === characterOf(entry))
            object._premade.delete(entry.k);
        else
            child = makeObject(entry);
        if (!child)
            continue;
        child._timelineKey = entry.k;
        child._depth = entry.d === undefined ? j : entry.d;
        update(child, entry, true);
        object._timelineChildren.set(entry.k, child);
        object._insert(child, indexForDepth(object, child._depth));
        if (entry.n)
            setNamed(object, entry.n, child);
    }
    object._changed();
}
/// The instance names the timeline places: properties of their parent, as
/// declared stage instances are.
function setNamed(object, name, child) {
    try {
        var described = null;
        for (var at = object; at && !described; at = Object.getPrototypeOf(at))
            described = Object.getOwnPropertyDescriptor(at, name);
        if (described && !described.writable && !described.set)
            return;
        object[name] = child;
    } catch (error) {}
}

class FrameLabel {
    constructor(name, frame) {
        this._name = String(name);
        this._frame = frame | 0;
    }
    get name() { return this._name; }
    get frame() { return this._frame; }
}
class Scene {
    constructor(name, labels, numFrames) {
        this._name = String(name);
        this._labels = labels || [];
        this._numFrames = numFrames | 0;
    }
    get name() { return this._name; }
    get labels() { return this._labels.slice(); }
    get numFrames() { return this._numFrames; }
}

class MovieClip extends display.Sprite {
    constructor() {
        super();
        this._enabled = true;
        this.trackAsMenu = false;
    }
    get enabled() { return this._enabled; }
    set enabled(value) { this._enabled = !!value; }
    get isPlaying() { return !!this._playing; }
    get totalFrames() { return this._timeline ? this._timeline.n : 1; }
    get framesLoaded() { return this.totalFrames; }
    /// The scene the playhead is in, and its first frame (from 0).
    _sceneAt(frame) {
        var scenes = this._timeline && this._timeline.scenes;
        if (!scenes || !scenes.length)
            return {name: "Scene 1", at: 0, n: this.totalFrames, index: 0};
        var found = scenes[0], index = 0;
        for (var i = 0; i < scenes.length; ++i)
            if (scenes[i].at <= frame) {
                found = scenes[i];
                index = i;
            }
        return {name: found.name, at: found.at, n: found.n, index: index};
    }
    get currentFrame() { return this._frame - this._sceneAt(this._frame).at + 1; }
    get currentScene() {
        var scene = this._sceneAt(this._frame);
        return new Scene(scene.name, this._labelsIn(scene), scene.n);
    }
    get scenes() {
        var self = this;
        var scenes = this._timeline && this._timeline.scenes;
        if (!scenes || !scenes.length)
            return [this.currentScene];
        return scenes.map(function (s) { return new Scene(s.name, self._labelsIn(s), s.n); });
    }
    _labelsIn(scene) {
        var labels = this._timeline ? this._timeline.labels || [] : [];
        return labels.filter(function (l) { return l[0] >= scene.at && l[0] < scene.at + scene.n; })
            .map(function (l) { return new FrameLabel(l[1], l[0] - scene.at + 1); });
    }
    get currentLabels() { return this._labelsIn(this._sceneAt(this._frame)); }
    get currentLabel() {
        var scene = this._sceneAt(this._frame), found = null;
        var labels = this._timeline ? this._timeline.labels || [] : [];
        for (var i = 0; i < labels.length; ++i)
            if (labels[i][0] >= scene.at && labels[i][0] <= this._frame)
                found = labels[i][1];
        return found;
    }
    get currentFrameLabel() {
        var labels = this._timeline ? this._timeline.labels || [] : [];
        for (var i = 0; i < labels.length; ++i)
            if (labels[i][0] === this._frame)
                return labels[i][1];
        return null;
    }
    /// A frame (from 0, on the whole timeline) from a number (from 1, in
    /// the scene) or a label; -1 when there is none.
    _frameOf(frame, scene) {
        var sceneInfo = this._sceneAt(this._frame);
        if (scene !== undefined && scene !== null) {
            var scenes = this._timeline && this._timeline.scenes || [];
            var named = scenes.filter(function (s) { return s.name === String(scene); })[0];
            if (!named)
                throw phoenix.as3Error("ArgumentError", 2108, "Scene " + scene + " was not found.");
            sceneInfo = named;
        }
        if (typeof frame === "string" && isNaN(Number(frame))) {
            var labels = this._timeline ? this._timeline.labels || [] : [];
            for (var i = 0; i < labels.length; ++i)
                if (labels[i][1] === frame && (scene === undefined || scene === null || (labels[i][0] >= sceneInfo.at && labels[i][0] < sceneInfo.at + sceneInfo.n)))
                    return labels[i][0];
            throw phoenix.as3Error("ArgumentError", 2109, "Frame label " + frame + " not found in scene " + sceneInfo.name + ".");
        }
        var number = Math.floor(Number(frame));
        if (isNaN(number))
            return -1;
        return Math.max(0, Math.min(this.totalFrames - 1, sceneInfo.at + number - 1));
    }
    play() { this._playing = this.totalFrames > 1 || this._playing; }
    stop() { this._playing = false; }
    gotoAndPlay(frame, scene) {
        this._playing = true;
        this._goTo(this._frameOf(frame, scene));
    }
    gotoAndStop(frame, scene) {
        this._playing = false;
        this._goTo(this._frameOf(frame, scene));
    }
    nextFrame() {
        this._playing = false;
        if (this._frame + 1 < this.totalFrames)
            this._goTo(this._frame + 1);
    }
    prevFrame() {
        this._playing = false;
        if (this._frame > 0)
            this._goTo(this._frame - 1);
    }
    nextScene() {
        var scenes = this._timeline && this._timeline.scenes || [];
        var at = this._sceneAt(this._frame).index;
        if (at + 1 < scenes.length)
            this.gotoAndPlay(1, scenes[at + 1].name);
    }
    prevScene() {
        var scenes = this._timeline && this._timeline.scenes || [];
        var at = this._sceneAt(this._frame).index;
        if (at > 0)
            this.gotoAndPlay(1, scenes[at - 1].name);
    }
    /// Goes to \a frame (from 0) now: its children made, its sounds started,
    /// and its script run once the script that sent it there is done.
    _goTo(frame) {
        if (frame < 0 || !this._timeline)
            return;
        if (frame === this._frame && this._stateIndex >= 0)
            return;
        var from = this._frame;
        this._frame = frame;
        this._scriptFrame = -1;
        phoenix._.queueScript && phoenix._.queueScript(this);
        applyFrame(this, frame);
        phoenix._.frameEntered && phoenix._.frameEntered(this, from, frame);
        phoenix._.afterGoTo && phoenix._.afterGoTo();
    }
    /// The playhead moves on a frame (the player's first step each frame);
    /// the new frame's children are made later, in _construct.
    _advance() {
        if (!this._playing || !this._timeline || this._timeline.n <= 1)
            return false;
        var next = this._frame + 1;
        if (next >= this._timeline.n) {
            if (this._timeline.once) {
                this._playing = false;
                return false;
            }
            next = 0;
        }
        this._advancedFrom = this._frame;
        this._frame = next;
        return true;
    }
    _construct() {
        var from = this._advancedFrom;
        this._advancedFrom = undefined;
        this._scriptFrame = -1;
        phoenix._.queueScript && phoenix._.queueScript(this);
        applyFrame(this, this._frame);
        phoenix._.frameEntered && phoenix._.frameEntered(this, from, this._frame);
    }
    addFrameScript() {
        if (!this._frameScripts)
            this._frameScripts = {};
        for (var i = 0; i + 1 < arguments.length; i += 2) {
            var frame = arguments[i] | 0, script = arguments[i + 1];
            if (typeof script === "function")
                this._frameScripts[frame] = script;
            else
                delete this._frameScripts[frame];
        }
        phoenix._.queueScript && phoenix._.queueScript(this);
    }
    /// Runs the current frame's script, once for each time the frame is
    /// entered.
    _runFrameScript() {
        if (this._scriptFrame === this._frame)
            return;
        this._scriptFrame = this._frame;
        var script = this._frameScripts && this._frameScripts[this._frame];
        if (script) {
            try {
                script.call(this);
            } catch (error) {
                phoenix._.reportError(error, "frame " + (this._frame + 1) + " of " + this.name);
            }
        }
    }
}

/// A graphic symbol's object: it shows the frame its parent's timeline
/// gives it, and lets the pointer through to its parent.
class Graphic extends display.Sprite {
    constructor() {
        super();
        this._mouseEnabled = false;
    }
    _showFrame(frame) {
        if (!this._timeline)
            return;
        frame = Math.max(0, Math.min(this._timeline.n - 1, frame | 0));
        this._frame = frame;
        applyFrame(this, frame);
    }
}

/// flash.display.SimpleButton: four states, each a picture of a frame of
/// its symbol (up, over, down and hit), shown as the pointer has it.
class SimpleButton extends display.InteractiveObject {
    constructor(upState, overState, downState, hitTestState) {
        super();
        var def = this._placement && this._placement.def;
        if (!def)
            for (var at = new.target; at && at !== SimpleButton && !def; at = Object.getPrototypeOf(at))
                def = phoenix._.symbolForClass.get(at) || null;
        this._upState = upState || null;
        this._overState = overState || null;
        this._downState = downState || null;
        this._hitTestState = hitTestState || null;
        this._state = "up";
        this._enabled = true;
        this._useHandCursor = true;
        this.trackAsMenu = false;
        this._soundTransform = null;
        this._symbolDef = def || null;
        if (def) {
            var self = this;
            var states = ["_upState", "_overState", "_downState", "_hitTestState"];
            states.forEach(function (key, frame) {
                if (frame < def.timeline.n)
                    self[key] = buttonState(def, frame, self);
            });
            if (!this._overState)
                this._overState = this._upState;
            if (!this._downState)
                this._downState = this._overState;
            if (!this._hitTestState)
                this._hitTestState = this._upState;
        }
        this._adopt();
    }
    $construct(upState, overState, downState, hitTestState) {
        super.$construct();
        if (upState)
            this._upState = upState;
        if (overState)
            this._overState = overState;
        if (downState)
            this._downState = downState;
        if (hitTestState)
            this._hitTestState = hitTestState;
        this._adopt();
    }
    /// The states draw in the button's space and see its stage.
    _adopt() {
        var self = this;
        [this._upState, this._overState, this._downState, this._hitTestState].forEach(function (state) {
            if (state)
                state._parent = self;
        });
    }
    get upState() { return this._upState; }
    set upState(value) { this._upState = value; this._adopt(); this._changed(); }
    get overState() { return this._overState; }
    set overState(value) { this._overState = value; this._adopt(); this._changed(); }
    get downState() { return this._downState; }
    set downState(value) { this._downState = value; this._adopt(); this._changed(); }
    get hitTestState() { return this._hitTestState; }
    set hitTestState(value) { this._hitTestState = value; this._adopt(); }
    get enabled() { return this._enabled; }
    set enabled(value) { this._enabled = !!value; }
    get useHandCursor() { return this._useHandCursor; }
    set useHandCursor(value) { this._useHandCursor = !!value; }
    get soundTransform() {
        if (!this._soundTransform)
            this._soundTransform = new flash.media.SoundTransform();
        return this._soundTransform;
    }
    set soundTransform(value) { this._soundTransform = value; }
    get _shown() {
        return this._state === "down" ? this._downState : this._state === "over" ? this._overState : this._upState;
    }
    /// The pointer has it up, over or down: the state shown, and the sounds
    /// on that frame of its symbol played.
    _setState(state) {
        if (this._state === state)
            return;
        this._state = state;
        if (this._symbolDef && phoenix._.buttonSound)
            phoenix._.buttonSound(this, state === "down" ? 2 : state === "over" ? 1 : 0);
        this._changed();
    }
    _contentBounds(strokes) {
        var state = this._shown;
        if (!state)
            return null;
        var box = state._localBounds(strokes);
        return box ? M.mapBox(state._m, box) : null;
    }
    _hitContent(x, y, shapeFlag) {
        var hit = this._hitTestState;
        if (!hit)
            return false;
        var inverse = M.invert(hit._m);
        if (!inverse)
            return false;
        var local = M.apply(inverse, x, y);
        return hit._hitContent(local[0], local[1], true);
    }
    _setStageDeep(stage) {
        this._setStage(stage);
        [this._upState, this._overState, this._downState, this._hitTestState].forEach(function (state) {
            if (state)
                state._setStageDeep(stage);
        });
    }
    _draw(ctx, m, c, r) {
        var state = this._shown;
        if (state)
            phoenix._.renderObject(ctx, state, m, c, r);
    }
}
/// A button's state: a sprite holding one frame of its symbol.
function buttonState(def, frame, button) {
    var holder = new display.Sprite();
    holder._timeline = def.timeline;
    holder._timelineChildren = new Map();
    holder._stateIndex = -1;
    holder._ownsTimelineChild = function (child) { return holder._timelineChildren.get(child._timelineKey) === child; };
    applyFrame(holder, frame);
    holder._mouseEnabled = false;
    return holder;
}

display.Sprite.prototype._ownsTimelineChild = function (child) {
    return !!this._timelineChildren && this._timelineChildren.get(child._timelineKey) === child;
};
display.Sprite.prototype._childGone = function (child) {
    if (this._ownsTimelineChild(child)) {
        // A script took it off: the timeline forgets it, and makes a new one
        // should a later frame place it again.
        this._timelineChildren.delete(child._timelineKey);
        child._timelineKey = null;
        child._depth = undefined;
    }
};

phoenix.defineClass("flash.display.MovieClip", MovieClip);
phoenix.defineClass("flash.display.SimpleButton", SimpleButton);
phoenix.defineClass("flash.display.FrameLabel", FrameLabel);
phoenix.defineClass("flash.display.Scene", Scene);
phoenix.Graphic = Graphic;
phoenix._.applyFrame = applyFrame;
phoenix._.makeObject = makeObject;
})(phoenix);
