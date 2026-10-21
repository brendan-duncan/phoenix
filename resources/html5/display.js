// flash.display: DisplayObject, InteractiveObject, DisplayObjectContainer,
// Sprite, Shape and Stage, and the constants classes. The timeline classes
// (MovieClip, SimpleButton) are in timeline.js, Graphics in graphics.js,
// Bitmap and BitmapData in bitmap.js; the drawing is render.js's.

(function (phoenix) {
"use strict";

var flash = phoenix.root.flash;
var M = phoenix._.M, C = phoenix._.C;
var EventDispatcher = flash.events.EventDispatcher;
var Event = flash.events.Event;
var Point = flash.geom.Point, Rectangle = flash.geom.Rectangle;
var ColorTransform = flash.geom.ColorTransform, Transform = flash.geom.Transform;

function constants(name, values) {
    var cls = function () {};
    Object.assign(cls, values);
    phoenix.defineClass(name, cls);
    return cls;
}
var BlendMode = constants("flash.display.BlendMode", {
    ADD: "add", ALPHA: "alpha", DARKEN: "darken", DIFFERENCE: "difference", ERASE: "erase", HARDLIGHT: "hardlight",
    INVERT: "invert", LAYER: "layer", LIGHTEN: "lighten", MULTIPLY: "multiply", NORMAL: "normal", OVERLAY: "overlay",
    SCREEN: "screen", SHADER: "shader", SUBTRACT: "subtract"
});
constants("flash.display.StageScaleMode", {EXACT_FIT: "exactFit", NO_BORDER: "noBorder", NO_SCALE: "noScale", SHOW_ALL: "showAll"});
constants("flash.display.StageAlign", {BOTTOM: "B", BOTTOM_LEFT: "BL", BOTTOM_RIGHT: "BR", LEFT: "L", RIGHT: "R", TOP: "T",
    TOP_LEFT: "TL", TOP_RIGHT: "TR"});
constants("flash.display.StageQuality", {BEST: "best", HIGH: "high", LOW: "low", MEDIUM: "medium",
    HIGH_8X8: "8x8", HIGH_8X8_LINEAR: "8x8linear", HIGH_16X16: "16x16", HIGH_16X16_LINEAR: "16x16linear"});
constants("flash.display.StageDisplayState", {FULL_SCREEN: "fullScreen", FULL_SCREEN_INTERACTIVE: "fullScreenInteractive", NORMAL: "normal"});
constants("flash.display.LineScaleMode", {HORIZONTAL: "horizontal", NONE: "none", NORMAL: "normal", VERTICAL: "vertical"});
constants("flash.display.CapsStyle", {NONE: "none", ROUND: "round", SQUARE: "square"});
constants("flash.display.JointStyle", {BEVEL: "bevel", MITER: "miter", ROUND: "round"});
constants("flash.display.GradientType", {LINEAR: "linear", RADIAL: "radial"});
constants("flash.display.SpreadMethod", {PAD: "pad", REFLECT: "reflect", REPEAT: "repeat"});
constants("flash.display.InterpolationMethod", {LINEAR_RGB: "linearRGB", RGB: "rgb"});
constants("flash.display.GraphicsPathCommand", {NO_OP: 0, MOVE_TO: 1, LINE_TO: 2, CURVE_TO: 3, WIDE_MOVE_TO: 4,
    WIDE_LINE_TO: 5, CUBIC_CURVE_TO: 6});
constants("flash.display.GraphicsPathWinding", {EVEN_ODD: "evenOdd", NON_ZERO: "nonZero"});
constants("flash.display.PixelSnapping", {ALWAYS: "always", AUTO: "auto", NEVER: "never"});
constants("flash.display.TriangleCulling", {NEGATIVE: "negative", NONE: "none", POSITIVE: "positive"});
constants("flash.display.ActionScriptVersion", {ACTIONSCRIPT2: 2, ACTIONSCRIPT3: 3});

var toRadians = Math.PI / 180;

/// What a timeline's placement or a new library instance carries into the
/// constructor: set just before the object is made, taken by the first
/// DisplayObject constructor to run.
phoenix._.pending = null;
var instanceCount = 0;

class DisplayObject extends EventDispatcher {
    constructor() {
        super();
        var pending = phoenix._.pending;
        phoenix._.pending = null;
        Object.defineProperty(this, "_isDisplayObject", {value: true});
        this._parent = null;
        this._name = pending && pending.name ? pending.name : "instance" + (++instanceCount);
        this._m = pending && pending.m ? pending.m.slice() : [1, 0, 0, 1, 0, 0];
        this._decomposed = null;
        this._colorTransform = new ColorTransform();
        this._visible = true;
        this._blendMode = "normal";
        this._filters = [];
        this._mask = null;
        this._maskOwner = null;
        this._scrollRect = null;
        this._scale9Grid = null;
        this._cacheAsBitmap = false;
        this._opaqueBackground = null;
        this._transform = null;
        this._stage = null;
        /// Where a timeline placed it: its depth, its key among the
        /// timeline's objects, the timeline mask group it is in or masks.
        this._depth = undefined;
        this._timelineKey = null;
        this._clipGroup = null;
        this._maskGroup = null;
        /// What a script has set, which the timeline then leaves alone.
        this._scriptMatrix = false;
        this._scriptColor = false;
        this._placement = pending;
        this._accessibilityProperties = null;
    }

    // Placement.

    _matrix() { return this._m; }
    _setMatrix(m, fromTimeline) {
        this._m = m.slice();
        this._decomposed = null;
        if (!fromTimeline)
            this._scriptMatrix = true;
        this._changed();
    }
    /// The matrix as scale, rotation and skew, kept between sets so that
    /// setting one leaves the others as they were.
    _parts() {
        if (!this._decomposed) {
            var m = this._m;
            var sx = Math.sqrt(m[0] * m[0] + m[1] * m[1]);
            var sy = Math.sqrt(m[2] * m[2] + m[3] * m[3]);
            var rx = Math.atan2(m[1], m[0]);
            var ry = Math.atan2(-m[2], m[3]);
            if (m[0] * m[3] - m[1] * m[2] < 0) {
                // Flipped: one axis turns the other way round.
                sy = -sy;
                ry = Math.atan2(m[2], -m[3]);
            }
            this._decomposed = {sx: sx, sy: sy, rx: rx, ry: ry};
        }
        return this._decomposed;
    }
    _compose(parts) {
        var m = this._m;
        this._m = [parts.sx * Math.cos(parts.rx), parts.sx * Math.sin(parts.rx),
            -parts.sy * Math.sin(parts.ry), parts.sy * Math.cos(parts.ry), m[4], m[5]];
        this._decomposed = parts;
        this._scriptMatrix = true;
        this._changed();
    }
    get x() { return this._m[4]; }
    set x(value) {
        value = +value;
        if (isNaN(value))
            return;
        this._m[4] = value;
        this._scriptMatrix = true;
        this._changed();
    }
    get y() { return this._m[5]; }
    set y(value) {
        value = +value;
        if (isNaN(value))
            return;
        this._m[5] = value;
        this._scriptMatrix = true;
        this._changed();
    }
    get z() { return 0; }
    set z(value) {
        if (+value)
            phoenix.unsupported("DisplayObject.z", "is not supported: objects stay flat");
    }
    get scaleX() { return this._parts().sx; }
    set scaleX(value) {
        var parts = Object.assign({}, this._parts());
        parts.sx = +value;
        this._compose(parts);
    }
    get scaleY() { return this._parts().sy; }
    set scaleY(value) {
        var parts = Object.assign({}, this._parts());
        parts.sy = +value;
        this._compose(parts);
    }
    get scaleZ() { return 1; }
    set scaleZ(value) {}
    get rotation() {
        var degrees = this._parts().rx / toRadians;
        return degrees > 180 ? degrees - 360 : degrees <= -180 ? degrees + 360 : degrees;
    }
    set rotation(value) {
        value = +value;
        if (isNaN(value))
            return;
        var parts = Object.assign({}, this._parts());
        var turn = value * toRadians - parts.rx;
        parts.rx += turn;
        parts.ry += turn;
        this._compose(parts);
    }
    get rotationX() { return 0; }
    set rotationX(value) {
        if (+value)
            phoenix.unsupported("DisplayObject.rotationX", "is not supported: objects stay flat");
    }
    get rotationY() { return 0; }
    set rotationY(value) {
        if (+value)
            phoenix.unsupported("DisplayObject.rotationY", "is not supported: objects stay flat");
    }
    get rotationZ() { return this.rotation; }
    set rotationZ(value) { this.rotation = value; }
    get width() {
        var box = this._boundsIn(this._parent, true, true);
        return box ? box[2] - box[0] : 0;
    }
    set width(value) {
        value = +value;
        var box = this._localBounds(true);
        var wide = box ? box[2] - box[0] : 0;
        if (!wide)
            return;
        // The scale that makes the unturned box this wide.
        var parts = Object.assign({}, this._parts());
        parts.sx = (parts.sx < 0 ? -1 : 1) * value / wide;
        this._compose(parts);
    }
    get height() {
        var box = this._boundsIn(this._parent, true, true);
        return box ? box[3] - box[1] : 0;
    }
    set height(value) {
        value = +value;
        var box = this._localBounds(true);
        var high = box ? box[3] - box[1] : 0;
        if (!high)
            return;
        var parts = Object.assign({}, this._parts());
        parts.sy = (parts.sy < 0 ? -1 : 1) * value / high;
        this._compose(parts);
    }
    get alpha() { return this._colorTransform.alphaMultiplier; }
    set alpha(value) {
        value = +value;
        if (isNaN(value))
            return;
        this._colorTransform.alphaMultiplier = value;
        this._scriptColor = true;
        this._changed();
    }
    get visible() { return this._visible; }
    set visible(value) {
        this._visible = !!value;
        this._changed();
    }
    get name() { return this._name; }
    set name(value) {
        if (this._placedByTimeline())
            throw phoenix.as3Error("Error", 2078, "The name property of a Timeline-placed object cannot be modified.");
        this._name = value === null ? null : String(value);
    }
    _placedByTimeline() { return this._timelineKey !== null && !!this._parent && this._parent._ownsTimelineChild(this); }
    get parent() { return this._parent; }
    get stage() { return this._stage; }
    /// The movie's main timeline, for an object on the stage; off it, the
    /// top of the tree it is in when that is the main timeline, else null.
    get root() {
        var at = this;
        while (at._parent && !(at._parent instanceof Stage))
            at = at._parent;
        return at._parent || at === phoenix._.rootClip ? at : null;
    }
    get loaderInfo() { return phoenix._.loaderInfo || null; }
    get mask() { return this._mask; }
    set mask(value) {
        if (this._mask)
            this._mask._maskOwner = null;
        this._mask = value || null;
        if (this._mask)
            this._mask._maskOwner = this;
        this._changed();
    }
    get blendMode() { return this._blendMode; }
    set blendMode(value) {
        this._blendMode = value ? String(value) : "normal";
        this._changed();
    }
    get filters() { return this._filters.map(function (f) { return f.clone(); }); }
    set filters(value) {
        this._filters = (value || []).map(function (f) { return f.clone(); });
        this._changed();
    }
    get cacheAsBitmap() { return this._cacheAsBitmap || this._filters.length > 0; }
    set cacheAsBitmap(value) { this._cacheAsBitmap = !!value; }
    get cacheAsBitmapMatrix() { return null; }
    set cacheAsBitmapMatrix(value) {}
    get opaqueBackground() { return this._opaqueBackground; }
    set opaqueBackground(value) {
        this._opaqueBackground = value === null || value === undefined ? null : value >>> 0;
        this._changed();
    }
    get scrollRect() { return this._scrollRect ? this._scrollRect.clone() : null; }
    set scrollRect(value) {
        this._scrollRect = value ? value.clone() : null;
        this._changed();
    }
    get scale9Grid() { return this._scale9Grid ? this._scale9Grid.clone() : null; }
    set scale9Grid(value) { this._scale9Grid = value ? value.clone() : null; this._slices = null; this._changed(); }
    get transform() {
        if (!this._transform)
            this._transform = new Transform(this);
        return this._transform;
    }
    set transform(value) {
        this._setMatrix(M.fromMatrix(value.matrix));
        this._setColorTransform(value.colorTransform);
    }
    _setColorTransform(t, fromTimeline) {
        this._colorTransform = t.clone();
        if (!fromTimeline)
            this._scriptColor = true;
        this._changed();
    }
    get accessibilityProperties() { return this._accessibilityProperties; }
    set accessibilityProperties(value) { this._accessibilityProperties = value; }
    get mouseX() { return this._globalToLocal(phoenix._.mouseX || 0, phoenix._.mouseY || 0)[0]; }
    get mouseY() { return this._globalToLocal(phoenix._.mouseX || 0, phoenix._.mouseY || 0)[1]; }

    /// Something that shows changed: the stage draws again.
    _changed() {
        if (phoenix._.requestDraw)
            phoenix._.requestDraw();
    }

    // Space.

    /// From this object's space to the stage's.
    _concatenatedMatrix() {
        var m = this._m;
        for (var at = this._parent; at && !(at instanceof Stage); at = at._parent)
            m = M.multiply(m, at._m);
        return m;
    }
    _localToGlobal(x, y) { return M.apply(this._concatenatedMatrix(), x, y); }
    _globalToLocal(x, y) {
        var inverse = M.invert(this._concatenatedMatrix());
        return inverse ? M.apply(inverse, x, y) : [0, 0];
    }
    localToGlobal(point) {
        var at = this._localToGlobal(point.x, point.y);
        return new Point(at[0], at[1]);
    }
    globalToLocal(point) {
        var at = this._globalToLocal(point.x, point.y);
        return new Point(at[0], at[1]);
    }
    local3DToGlobal(point) { return this.localToGlobal(point); }
    globalToLocal3D(point) { var p = this.globalToLocal(point); return {x: p.x, y: p.y, z: 0}; }

    /// What the object draws, in its own space: [x0, y0, x1, y1] or null.
    /// Strokes count with \a strokes.
    _contentBounds(strokes) { return null; }
    _localBounds(strokes) {
        var box = this._contentBounds(strokes);
        if (this._scrollRect)
            box = [0, 0, this._scrollRect.width, this._scrollRect.height];
        return box;
    }
    /// The box in \a space's coordinates (null: the stage's).
    _boundsIn(space, strokes, own) {
        var box = this._localBounds(strokes);
        if (!box)
            return null;
        var m = this._concatenatedMatrix();
        if (own && space === this._parent)
            m = this._m;
        else if (space && space !== this) {
            var inverse = M.invert(space._concatenatedMatrix());
            m = inverse ? M.multiply(m, inverse) : m;
        } else if (space === this)
            m = M.identity;
        return M.mapBox(m, box);
    }
    getBounds(targetCoordinateSpace) {
        var space = targetCoordinateSpace === undefined ? this : targetCoordinateSpace;
        var box = this._boundsIn(space, true);
        if (!box) {
            // Nothing drawn: an empty box where the object's origin is.
            var origin = space && space !== this
                ? space._globalToLocal.apply(space, this._localToGlobal(0, 0))
                : space === this ? [0, 0] : this._localToGlobal(0, 0);
            return new Rectangle(origin[0], origin[1], 0, 0);
        }
        return new Rectangle(box[0], box[1], box[2] - box[0], box[3] - box[1]);
    }
    getRect(targetCoordinateSpace) {
        var box = this._boundsIn(targetCoordinateSpace === undefined ? this : targetCoordinateSpace, false);
        return box ? new Rectangle(box[0], box[1], box[2] - box[0], box[3] - box[1]) : this.getBounds(targetCoordinateSpace);
    }
    /// Whether the object's content is at (\a x, \a y) in its own space.
    _hitContent(x, y, shapeFlag) {
        var box = this._contentBounds(true);
        return !!box && x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3];
    }
    /// The same, for a point on the stage, through its mask and scroll rect.
    _hitStage(x, y, shapeFlag) {
        var local = this._globalToLocal(x, y);
        if (this._scrollRect) {
            var r = this._scrollRect;
            if (local[0] < 0 || local[1] < 0 || local[0] > r.width || local[1] > r.height)
                return false;
            local = [local[0] + r.x, local[1] + r.y];
        }
        if (this._mask && !this._mask._hitStage(x, y, true))
            return false;
        return this._hitContent(local[0], local[1], shapeFlag);
    }
    hitTestPoint(x, y, shapeFlag) {
        if (!shapeFlag) {
            var box = this._boundsIn(null, true);
            return !!box && x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3];
        }
        return this._hitStage(+x, +y, true);
    }
    hitTestObject(other) {
        var a = this._boundsIn(null, true), b = other && other._boundsIn(null, true);
        return !!a && !!b && a[0] <= b[2] && b[0] <= a[2] && a[1] <= b[3] && b[1] <= a[3];
    }

    // The display list.

    /// Put on the stage or taken off it (\a stage null), with its children.
    _setStage(stage) {
        if (this._stage === stage)
            return;
        var was = this._stage;
        this._stage = stage;
        if (stage && !was) {
            phoenix._.onStage && phoenix._.onStage(this, true);
            this.dispatchEvent(new Event(Event.ADDED_TO_STAGE));
        } else if (!stage && was) {
            phoenix._.onStage && phoenix._.onStage(this, false);
            this.dispatchEvent(new Event(Event.REMOVED_FROM_STAGE));
        }
    }
    /// Frame events go to every object listening for them, on the stage or
    /// not: the player keeps the list.
    _listening(type, on) {
        if (phoenix._.broadcastListening)
            phoenix._.broadcastListening(this, type, on);
    }
    /// The object as the renderer draws it, for render.js to fill in.
    _draw(renderer, m, c) {}
    toString() { return "[object " + phoenix.qualifiedNameOf(this).split("::").pop() + "]"; }
}

class InteractiveObject extends DisplayObject {
    constructor() {
        super();
        this._mouseEnabled = true;
        this._doubleClickEnabled = false;
        this._tabEnabled = undefined;
        this._tabIndex = -1;
        this.focusRect = null;
        this.contextMenu = null;
        this.needsSoftKeyboard = false;
        this.softKeyboardInputAreaOfInterest = null;
    }
    get mouseEnabled() { return this._mouseEnabled; }
    set mouseEnabled(value) { this._mouseEnabled = !!value; }
    get doubleClickEnabled() { return this._doubleClickEnabled; }
    set doubleClickEnabled(value) { this._doubleClickEnabled = !!value; }
    get tabEnabled() { return this._tabEnabled === undefined ? false : this._tabEnabled; }
    set tabEnabled(value) { this._tabEnabled = !!value; }
    get tabIndex() { return this._tabIndex; }
    set tabIndex(value) { this._tabIndex = value | 0; }
    requestSoftKeyboard() { return false; }
}

class DisplayObjectContainer extends InteractiveObject {
    constructor() {
        super();
        this._children = [];
        this._mouseChildren = true;
        this._tabChildren = true;
    }
    get numChildren() { return this._children.length; }
    get mouseChildren() { return this._mouseChildren; }
    set mouseChildren(value) { this._mouseChildren = !!value; }
    get tabChildren() { return this._tabChildren; }
    set tabChildren(value) { this._tabChildren = !!value; }
    get textSnapshot() { return null; }
    /// Whether \a child is one this container's timeline placed and still
    /// holds.
    _ownsTimelineChild(child) { return false; }
    _checkChild(child) {
        if (!child)
            throw phoenix.as3Error("TypeError", 2007, "Parameter child must be non-null.");
        if (child === this)
            throw phoenix.as3Error("ArgumentError", 2024, "An object cannot be added as a child of itself.");
        if (child instanceof DisplayObjectContainer && child.contains(this))
            throw phoenix.as3Error("ArgumentError", 2150, "An object cannot be added as a child to one of it's children (or children's children, etc.).");
    }
    addChild(child) {
        return this.addChildAt(child, this._children.length - (child && child._parent === this ? 1 : 0));
    }
    addChildAt(child, index) {
        this._checkChild(child);
        if (index < 0 || index > this._children.length - (child._parent === this ? 1 : 0))
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        if (child._parent === this) {
            this.setChildIndex(child, index);
            return child;
        }
        if (child._parent)
            child._parent.removeChild(child);
        this._insert(child, index);
        return child;
    }
    /// Puts \a child in at \a index and tells it so.
    _insert(child, index) {
        this._children.splice(index, 0, child);
        child._parent = this;
        child.dispatchEvent(new Event(Event.ADDED, true));
        if (this._stage)
            child._setStageDeep(this._stage);
        this._changed();
    }
    contains(child) {
        for (var at = child; at; at = at._parent)
            if (at === this)
                return true;
        return false;
    }
    getChildAt(index) {
        if (index < 0 || index >= this._children.length)
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        return this._children[index];
    }
    getChildByName(name) {
        for (var i = 0; i < this._children.length; ++i)
            if (this._children[i]._name === name)
                return this._children[i];
        return null;
    }
    getChildIndex(child) {
        var index = this._children.indexOf(child);
        if (index < 0)
            throw phoenix.as3Error("ArgumentError", 2025, "The supplied DisplayObject must be a child of the caller.");
        return index;
    }
    getObjectsUnderPoint(point) {
        var found = [];
        var visit = function (object) {
            if (!object._visible)
                return;
            if (object instanceof DisplayObjectContainer)
                object._children.forEach(visit);
            else if (object._hitStage(point.x, point.y, true))
                found.push(object);
        };
        this._children.forEach(visit);
        return found;
    }
    areInaccessibleObjectsUnderPoint() { return false; }
    removeChild(child) {
        var index = this._children.indexOf(child);
        if (index < 0)
            throw phoenix.as3Error("ArgumentError", 2025, "The supplied DisplayObject must be a child of the caller.");
        this._removeAt(index);
        return child;
    }
    removeChildAt(index) {
        if (index < 0 || index >= this._children.length)
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        var child = this._children[index];
        this._removeAt(index);
        return child;
    }
    _removeAt(index) {
        var child = this._children[index];
        child.dispatchEvent(new Event(Event.REMOVED, true));
        // A listener may have moved it already.
        index = this._children.indexOf(child);
        if (index < 0)
            return;
        if (child._stage)
            child._setStageDeep(null);
        this._children.splice(index, 1);
        child._parent = null;
        this._childGone(child);
        this._changed();
    }
    /// A child went: a timeline forgets it.
    _childGone(child) {}
    removeChildren(beginIndex, endIndex) {
        beginIndex = beginIndex === undefined ? 0 : beginIndex | 0;
        endIndex = endIndex === undefined ? 0x7fffffff : endIndex | 0;
        endIndex = Math.min(endIndex, this._children.length - 1);
        if (beginIndex < 0 || (this._children.length && beginIndex > endIndex))
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        for (var i = endIndex; i >= beginIndex; --i)
            this._removeAt(i);
    }
    setChildIndex(child, index) {
        var at = this.getChildIndex(child);
        if (index < 0 || index >= this._children.length)
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        if (at === index)
            return;
        this._children.splice(at, 1);
        this._children.splice(index, 0, child);
        this._changed();
    }
    swapChildren(a, b) { this.swapChildrenAt(this.getChildIndex(a), this.getChildIndex(b)); }
    swapChildrenAt(i, j) {
        if (i < 0 || j < 0 || i >= this._children.length || j >= this._children.length)
            throw phoenix.as3Error("RangeError", 2006, "The supplied index is out of bounds.");
        var t = this._children[i];
        this._children[i] = this._children[j];
        this._children[j] = t;
        this._changed();
    }
    stopAllMovieClips() {
        this._children.forEach(function (child) {
            if (child.stop && child._timeline)
                child.stop();
            if (child.stopAllMovieClips)
                child.stopAllMovieClips();
        });
        if (this._timeline && this.stop)
            this.stop();
    }
    _contentBounds(strokes) {
        var box = this._ownBounds(strokes);
        for (var i = 0; i < this._children.length; ++i) {
            var child = this._children[i];
            if (!child._visible && !phoenix._.measuringHidden)
                continue;
            if (child._maskGroup !== null)
                continue;
            var inner = child._localBounds(strokes);
            if (inner)
                box = M.union(box, M.mapBox(child._m, inner));
        }
        return box;
    }
    /// What the container draws itself (a Sprite's graphics).
    _ownBounds(strokes) { return null; }
    _hitContent(x, y, shapeFlag) {
        if (this._hitOwn(x, y, shapeFlag))
            return true;
        for (var i = this._children.length - 1; i >= 0; --i) {
            var child = this._children[i];
            if (!child._visible || child._maskOwner || child._maskGroup !== null)
                continue;
            var inverse = M.invert(child._m);
            if (!inverse)
                continue;
            var local = M.apply(inverse, x, y);
            if (child._hitContent(local[0], local[1], shapeFlag))
                return true;
        }
        return false;
    }
    _hitOwn(x, y, shapeFlag) { return false; }
    _setStageDeep(stage) {
        this._setStage(stage);
        for (var i = 0; i < this._children.length; ++i)
            this._children[i]._setStageDeep(stage);
    }
}
DisplayObject.prototype._setStageDeep = function (stage) { this._setStage(stage); };

class Sprite extends DisplayObjectContainer {
    constructor() {
        super();
        this._graphics = null;
        this._buttonMode = false;
        this._useHandCursor = true;
        this._hitArea = null;
        this._dropTarget = null;
        this._soundTransform = null;
        // A library symbol's object: its timeline, and its first frame
        // (timeline.js).
        if (phoenix._.initTimeline)
            phoenix._.initTimeline(this, new.target);
    }
    get soundTransform() {
        if (!this._soundTransform)
            this._soundTransform = new flash.media.SoundTransform();
        return this._soundTransform;
    }
    set soundTransform(value) { this._soundTransform = value; }
    get graphics() {
        if (!this._graphics)
            this._graphics = new flash.display.Graphics(this);
        return this._graphics;
    }
    get buttonMode() { return this._buttonMode; }
    set buttonMode(value) { this._buttonMode = !!value; }
    get useHandCursor() { return this._useHandCursor; }
    set useHandCursor(value) { this._useHandCursor = !!value; }
    get hitArea() { return this._hitArea; }
    set hitArea(value) { this._hitArea = value || null; }
    get dropTarget() { return this._dropTarget; }
    startDrag(lockCenter, bounds) {
        phoenix._.startDrag && phoenix._.startDrag(this, !!lockCenter, bounds || null);
    }
    stopDrag() {
        phoenix._.stopDrag && phoenix._.stopDrag(this);
    }
    startTouchDrag(touchPointID, lockCenter, bounds) { this.startDrag(lockCenter, bounds); }
    stopTouchDrag() { this.stopDrag(); }
    _ownBounds(strokes) { return this._graphics ? this._graphics._bounds(strokes) : null; }
    _hitOwn(x, y, shapeFlag) { return !!this._graphics && this._graphics._hit(x, y, shapeFlag); }
}

class Shape extends DisplayObject {
    constructor() {
        super();
        this._graphics = null;
    }
    get graphics() {
        if (!this._graphics)
            this._graphics = new flash.display.Graphics(this);
        return this._graphics;
    }
    _contentBounds(strokes) { return this._graphics ? this._graphics._bounds(strokes) : null; }
    _hitContent(x, y, shapeFlag) {
        if (!this._graphics)
            return false;
        if (!shapeFlag) {
            var box = this._graphics._bounds(true);
            return !!box && x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3];
        }
        return this._graphics._hit(x, y, true);
    }
}

/// The page's stage: the root of the display list. Its size, frame rate
/// and scale mode are the movie's; the player draws it.
class Stage extends DisplayObjectContainer {
    constructor() {
        super();
        this._stage = this;
        this._name = null;
        this._frameRate = 24;
        this._stageWidth = 550;
        this._stageHeight = 400;
        this._scaleMode = "showAll";
        this._align = "";
        this._quality = "high";
        this._color = 0xffffff;
        this._focus = null;
        this._displayState = "normal";
        this._invalidated = false;
        this.showDefaultContextMenu = true;
        this.stageFocusRect = true;
        this.mouseChildren = true;
    }
    get frameRate() { return this._frameRate; }
    set frameRate(value) {
        value = +value;
        if (!(value > 0))
            return;
        this._frameRate = Math.min(1000, Math.max(0.01, value));
        phoenix._.frameRateChanged && phoenix._.frameRateChanged(this._frameRate);
    }
    get stageWidth() { return this._scaleMode === "noScale" && phoenix._.viewSize ? phoenix._.viewSize()[0] : this._stageWidth; }
    set stageWidth(value) {}
    get stageHeight() { return this._scaleMode === "noScale" && phoenix._.viewSize ? phoenix._.viewSize()[1] : this._stageHeight; }
    set stageHeight(value) {}
    get width() { var box = this._contentBounds(true); return box ? box[2] - box[0] : 0; }
    set width(value) {}
    get height() { var box = this._contentBounds(true); return box ? box[3] - box[1] : 0; }
    set height(value) {}
    get scaleMode() { return this._scaleMode; }
    set scaleMode(value) {
        this._scaleMode = String(value);
        phoenix._.layout && phoenix._.layout();
    }
    get align() { return this._align; }
    set align(value) {
        this._align = String(value || "").toUpperCase();
        phoenix._.layout && phoenix._.layout();
    }
    get quality() { return this._quality; }
    set quality(value) { this._quality = String(value).toLowerCase(); }
    get color() { return this._color; }
    set color(value) { this._color = value >>> 0; this._changed(); }
    get focus() { return this._focus; }
    set focus(value) { phoenix._.setFocus ? phoenix._.setFocus(value || null) : (this._focus = value || null); }
    get displayState() { return this._displayState; }
    set displayState(value) { this._displayState = String(value); }
    get fullScreenWidth() { return typeof screen !== "undefined" ? screen.width : this._stageWidth; }
    get fullScreenHeight() { return typeof screen !== "undefined" ? screen.height : this._stageHeight; }
    get allowsFullScreen() { return false; }
    get contentsScaleFactor() { return typeof devicePixelRatio !== "undefined" ? devicePixelRatio : 1; }
    get mouseX() { return phoenix._.mouseX || 0; }
    get mouseY() { return phoenix._.mouseY || 0; }
    get stage() { return this; }
    get root() { return this; }
    get name() { return null; }
    set name(value) { throw phoenix.as3Error("IllegalOperationError", 2071, "The Stage class does not implement this property or method."); }
    get x() { return 0; }
    set x(value) {}
    get y() { return 0; }
    set y(value) {}
    /// Asks for Event.RENDER before the next draw.
    invalidate() { this._invalidated = true; }
    isFocusInaccessible() { return false; }
    _concatenatedMatrix() { return [1, 0, 0, 1, 0, 0]; }
}

class LoaderInfo extends EventDispatcher {
    constructor(content) {
        super();
        this._content = content || null;
        this.parameters = {};
        this.url = typeof location !== "undefined" ? String(location.href) : "";
        this.loaderURL = this.url;
        this.contentType = "application/x-shockwave-flash";
        this.actionScriptVersion = 3;
        this.swfVersion = 32;
        this.bytesLoaded = 1;
        this.bytesTotal = 1;
        this.sameDomain = true;
        this.childAllowsParent = true;
        this.parentAllowsChild = true;
        this.sharedEvents = new EventDispatcher();
        this.uncaughtErrorEvents = new EventDispatcher();
        if (typeof location !== "undefined" && location.search)
            location.search.slice(1).split("&").forEach(function (pair) {
                if (!pair)
                    return;
                var at = pair.indexOf("=");
                var key = decodeURIComponent(at < 0 ? pair : pair.slice(0, at));
                this.parameters[key] = at < 0 ? "" : decodeURIComponent(pair.slice(at + 1).replace(/\+/g, " "));
            }, this);
    }
    get content() { return this._content; }
    get frameRate() { return phoenix._.stage ? phoenix._.stage.frameRate : 24; }
    get width() { return phoenix._.stage ? phoenix._.stage._stageWidth : 0; }
    get height() { return phoenix._.stage ? phoenix._.stage._stageHeight : 0; }
    get loader() { return null; }
    get applicationDomain() { return flash.system.ApplicationDomain.currentDomain; }
}

phoenix.defineClass("flash.display.DisplayObject", DisplayObject);
phoenix.defineClass("flash.display.InteractiveObject", InteractiveObject);
phoenix.defineClass("flash.display.DisplayObjectContainer", DisplayObjectContainer);
phoenix.defineClass("flash.display.Sprite", Sprite);
phoenix.defineClass("flash.display.Shape", Shape);
phoenix.defineClass("flash.display.Stage", Stage);
phoenix.defineClass("flash.display.LoaderInfo", LoaderInfo);
phoenix.defineClass("flash.display.IBitmapDrawable", function IBitmapDrawable() {});
})(phoenix);
