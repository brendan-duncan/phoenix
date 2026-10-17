package fl.core {
    import fl.events.ComponentEvent;
    import fl.managers.FocusManager;
    import fl.managers.IFocusManager;
    import fl.managers.IFocusManagerComponent;
    import fl.managers.StyleManager;
    import fl.skins.DefaultSkin;
    import flash.display.DisplayObject;
    import flash.display.Sprite;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.FocusEvent;
    import flash.events.KeyboardEvent;
    import flash.text.TextFormat;
    import flash.text.TextFormatAlign;
    import flash.utils.Dictionary;
    import flash.utils.getDefinitionByName;

    /** Dispatched after the component moves. */
    [Event(name="move", type="fl.events.ComponentEvent")]
    /** Dispatched after the component is resized. */
    [Event(name="resize", type="fl.events.ComponentEvent")]
    /** Dispatched when the component becomes visible. */
    [Event(name="show", type="fl.events.ComponentEvent")]
    /** Dispatched when the component becomes invisible. */
    [Event(name="hide", type="fl.events.ComponentEvent")]

    /**
     * The base of every component: a size of its own (width and height do
     * not scale it), styles, focus, and drawing put off until the frame is
     * rendered, so that many changes in a row draw once.
     *
     * Changing a property marks what it affects as invalid (invalidate) and
     * asks for draw() to run later: on the stage's next render event, or the
     * next enter frame, whichever comes first. draw() redraws what is
     * invalid and calls validate(). drawNow() draws at once.
     *
     * Placed on a timeline, a component's symbol shows an "avatar" shape at
     * the size it was given there: the constructor reads that size (and the
     * scale the instance was given) as the component's size, sets the scale
     * back to 1 and removes the avatar. Made in code without one, it takes
     * its class's default size.
     */
    public class UIComponent extends Sprite {
        public var version:String = "3.0.3.1";

        /** True while the components draw what they put off. */
        public static var inCallLaterPhase:Boolean = false;

        /** The component the focus manager gives the focus to for this one. */
        public var focusTarget:IFocusManagerComponent;

        protected var isLivePreview:Boolean = false;
        protected var instanceStyles:Object;
        protected var sharedStyles:Object;
        protected var callLaterMethods:Array;
        protected var invalidateFlag:Boolean = false;
        protected var _enabled:Boolean = true;
        protected var invalidHash:Object;
        protected var uiFocusRect:DisplayObject;
        protected var isFocused:Boolean = false;
        protected var _width:Number = 0;
        protected var _height:Number = 0;
        protected var _x:Number = NaN;
        protected var _y:Number = NaN;
        protected var startWidth:Number = 0;
        protected var startHeight:Number = 0;
        protected var _imeMode:String = null;
        protected var _oldIMEMode:String = null;
        protected var errorCaught:Boolean = false;
        protected var _inspector:Boolean = false;

        private var _focusEnabled:Boolean = true;
        private var _mouseFocusEnabled:Boolean = true;
        // While this component draws, invalidating it asks for no new draw:
        // validate() at the end of draw() clears what was marked.
        private var _drawing:Boolean = false;
        private var _callLaterHooked:Boolean = false;
        private var _renderStage:Stage = null;

        // A focus manager for each stage (or container given one).
        private static var focusManagers:Dictionary = new Dictionary(true);
        // Skin class name -> the class, or false when the movie has none.
        private static var skinClasses:Object = {};

        private static var defaultStyles:Object = {
            focusRectSkin: "focusRectSkin",
            focusRectPadding: 2,
            textFormat: new TextFormat("_sans", 11, 0x000000, false, false, false, "", "", TextFormatAlign.LEFT, 0, 0, 0, 0),
            disabledTextFormat: new TextFormat("_sans", 11, 0x999999, false, false, false, "", "", TextFormatAlign.LEFT, 0, 0, 0, 0),
            defaultTextFormat: new TextFormat("_sans", 11, 0x000000, false, false, false, "", "", TextFormatAlign.LEFT, 0, 0, 0, 0),
            defaultDisabledTextFormat: new TextFormat("_sans", 11, 0x999999, false, false, false, "", "", TextFormatAlign.LEFT, 0, 0, 0, 0)
        };

        /** The default styles of UIComponent; each component class has its own. */
        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        /**
         * The styles of several style objects in one: where more than one
         * has a style, the first one's.
         */
        public static function mergeStyles(...list):Object {
            var styles:Object = {};
            for (var i:int = 0; i < list.length; i++) {
                var source:Object = list[i];
                for (var name:String in source) {
                    if (styles[name] != null) continue;
                    styles[name] = source[name];
                }
            }
            return styles;
        }

        public function UIComponent() {
            super();
            instanceStyles = {};
            sharedStyles = {};
            invalidHash = {};
            callLaterMethods = [];
            StyleManager.registerInstance(this);
            configUI();
            invalidate(InvalidationType.ALL);
            tabEnabled = (this is IFocusManagerComponent);
            setNativeProperty(this, "focusRect", false);
            if (tabEnabled) {
                addEventListener(FocusEvent.FOCUS_IN, focusInHandler);
                addEventListener(FocusEvent.FOCUS_OUT, focusOutHandler);
                addEventListener(KeyboardEvent.KEY_DOWN, keyDownHandler);
                addEventListener(KeyboardEvent.KEY_UP, keyUpHandler);
            }
            initializeFocusManager();
        }

        /**
         * Sets a property of the player's classes that a component can do
         * without, so that a player lacking it does not stop the component.
         */
        protected static function setNativeProperty(target:Object, name:String, value:*):void {
            try {
                target[name] = value;
            } catch (e:Error) {
            }
        }

        /**
         * True while the authoring tool's (or the player's) component
         * parameters are being set: the movie sets it before applying an
         * instance's parameters and clears it after.
         */
        public function get componentInspectorSetting():Boolean {
            return _inspector;
        }

        public function set componentInspectorSetting(value:Boolean):void {
            _inspector = value;
            if (_inspector) {
                beforeComponentParametersSet();
            } else {
                afterComponentParametersSet();
            }
        }

        protected function beforeComponentParametersSet():void {}

        protected function afterComponentParametersSet():void {}

        public function get enabled():Boolean {
            return _enabled;
        }

        public function set enabled(value:Boolean):void {
            if (value == _enabled) return;
            _enabled = value;
            invalidate(InvalidationType.STATE);
        }

        /** Sets the size: width and height, without scaling. Dispatches RESIZE. */
        public function setSize(width:Number, height:Number):void {
            _width = width;
            _height = height;
            invalidate(InvalidationType.SIZE);
            dispatchEvent(new ComponentEvent(ComponentEvent.RESIZE, false));
        }

        override public function get width():Number {
            return _width;
        }

        override public function set width(value:Number):void {
            if (_width == value) return;
            setSize(value, height);
        }

        override public function get height():Number {
            return _height;
        }

        override public function set height(value:Number):void {
            if (_height == value) return;
            setSize(width, value);
        }

        /** Scaling a component sizes it: the scale of its size when placed. */
        override public function set scaleX(value:Number):void {
            setSize(startWidth * value, height);
        }

        override public function get scaleX():Number {
            return startWidth == 0 ? 1 : width / startWidth;
        }

        override public function set scaleY(value:Number):void {
            setSize(width, startHeight * value);
        }

        override public function get scaleY():Number {
            return startHeight == 0 ? 1 : height / startHeight;
        }

        protected function getScaleY():Number {
            return super.scaleY;
        }

        protected function setScaleY(value:Number):void {
            super.scaleY = value;
        }

        protected function getScaleX():Number {
            return super.scaleX;
        }

        protected function setScaleX(value:Number):void {
            super.scaleX = value;
        }

        override public function get visible():Boolean {
            return super.visible;
        }

        override public function set visible(value:Boolean):void {
            if (super.visible == value) return;
            super.visible = value;
            var type:String = value ? ComponentEvent.SHOW : ComponentEvent.HIDE;
            dispatchEvent(new ComponentEvent(type, true));
        }

        /** Moves to (x, y), on whole pixels. Dispatches MOVE. */
        public function move(x:Number, y:Number):void {
            _x = x;
            _y = y;
            super.x = Math.round(x);
            super.y = Math.round(y);
            dispatchEvent(new ComponentEvent(ComponentEvent.MOVE));
        }

        override public function set x(value:Number):void {
            move(value, isNaN(_y) ? super.y : _y);
        }

        override public function get x():Number {
            return isNaN(_x) ? super.x : _x;
        }

        override public function set y(value:Number):void {
            move(isNaN(_x) ? super.x : _x, value);
        }

        override public function get y():Number {
            return isNaN(_y) ? super.y : _y;
        }

        /** Draws everything now. */
        public function validateNow():void {
            invalidate(InvalidationType.ALL, false);
            drawNow();
        }

        /** Draws what is invalid now, rather than when the frame renders. */
        public function drawNow():void {
            var was:Boolean = _drawing;
            _drawing = true;
            try {
                draw();
            } finally {
                _drawing = was;
            }
        }

        /**
         * Marks part of the component (an InvalidationType) for drawing, by
         * default when the frame is rendered.
         */
        public function invalidate(property:String = InvalidationType.ALL, callLater:Boolean = true):void {
            invalidHash[property] = true;
            if (callLater && !_drawing) {
                this.callLater(draw);
            }
        }

        public function setSharedStyle(name:String, style:Object):void {
            if (sharedStyles[name] === style && !(style is TextFormat)) return;
            sharedStyles[name] = style;
            if (instanceStyles[name] == null) invalidate(InvalidationType.STYLES);
        }

        /** Sets a style of this instance (null to fall back on the shared one). */
        public function setStyle(style:String, value:Object):void {
            if (instanceStyles[style] === value && !(value is TextFormat)) return;
            instanceStyles[style] = value;
            invalidate(InvalidationType.STYLES);
        }

        public function clearStyle(style:String):void {
            setStyle(style, null);
        }

        /** The style as set on this instance (null if it is not). */
        public function getStyle(style:String):Object {
            return instanceStyles[style];
        }

        public function get focusEnabled():Boolean {
            return _focusEnabled;
        }

        public function set focusEnabled(value:Boolean):void {
            _focusEnabled = value;
        }

        public function get mouseFocusEnabled():Boolean {
            return _mouseFocusEnabled;
        }

        public function set mouseFocusEnabled(value:Boolean):void {
            _mouseFocusEnabled = value;
        }

        /** The focus manager of the component's stage (or of a container above it given one). */
        public function get focusManager():IFocusManager {
            var o:DisplayObject = this;
            while (o != null) {
                if (focusManagers[o] != null) return IFocusManager(focusManagers[o]);
                o = o.parent;
            }
            return null;
        }

        public function set focusManager(value:IFocusManager):void {
            focusManagers[this] = value;
        }

        /** Draws (or removes) the focus rectangle around the component. */
        public function drawFocus(focused:Boolean):void {
            isFocused = focused;
            if (uiFocusRect != null && contains(uiFocusRect)) {
                removeChild(uiFocusRect);
                uiFocusRect = null;
            }
            if (focused) {
                uiFocusRect = getDisplayObjectInstance(getStyleValue("focusRectSkin"));
                if (uiFocusRect == null) return;
                var padding:Number = Number(getStyleValue("focusRectPadding"));
                if (isNaN(padding)) padding = 0;
                uiFocusRect.x = -padding;
                uiFocusRect.y = -padding;
                uiFocusRect.width = width + padding * 2;
                uiFocusRect.height = height + padding * 2;
                addChildAt(uiFocusRect, 0);
            }
        }

        public function setFocus():void {
            if (stage != null) {
                stage.focus = this;
            }
        }

        public function getFocus():Object {
            if (stage != null) {
                return stage.focus;
            }
            return null;
        }

        /** The input method editor mode (kept; there is none to switch). */
        public function get imeMode():String {
            return _imeMode;
        }

        public function set imeMode(value:String):void {
            _imeMode = value;
        }

        protected function setIMEMode(enabled:Boolean):void {}

        /** True when the property, or any of the others, or all, is invalid. */
        protected function isInvalid(property:String, ...properties):Boolean {
            if (invalidHash[property] || invalidHash[InvalidationType.ALL]) return true;
            while (properties.length > 0) {
                if (invalidHash[properties.pop()]) return true;
            }
            return false;
        }

        /** Marks everything drawn. */
        protected function validate():void {
            invalidHash = {};
        }

        /** Draws what is invalid; subclasses draw their parts and call super.draw(). */
        protected function draw():void {
            if (isInvalid(InvalidationType.SIZE, InvalidationType.STYLES)) {
                if (isFocused) {
                    var fm:IFocusManager = focusManager;
                    if (fm != null && fm.showFocusIndicator) drawFocus(true);
                }
            }
            validate();
        }

        /**
         * Reads the size the component was placed at (its avatar's, scaled),
         * sets the scale back to 1 and removes the avatar. Subclasses make
         * their parts here, after calling it.
         */
        protected function configUI():void {
            var r:Number = rotation;
            rotation = 0;
            var w:Number = super.width;
            var h:Number = super.height;
            super.scaleX = 1;
            super.scaleY = 1;
            if (numChildren > 0) {
                removeChildAt(0);
            }
            if (w == 0 && h == 0) {
                // Made in code, with no avatar: the class's default size.
                var size:Array = defaultSize();
                w = size[0];
                h = size[1];
            }
            setSize(w, h);
            move(super.x, super.y);
            rotation = r;
            startWidth = w;
            startHeight = h;
        }

        /** The size a component made in code starts at: [width, height]. */
        protected function defaultSize():Array {
            return [100, 100];
        }

        /** True in the authoring tool's live preview; never in a movie. */
        protected function checkLivePreview():Boolean {
            return false;
        }

        protected function focusInHandler(event:FocusEvent):void {
            if (isOurFocus(event.target as DisplayObject)) {
                var fm:IFocusManager = focusManager;
                if (fm != null && fm.showFocusIndicator) {
                    drawFocus(true);
                }
                isFocused = true;
            }
        }

        protected function focusOutHandler(event:FocusEvent):void {
            if (isOurFocus(event.target as DisplayObject)) {
                drawFocus(false);
                isFocused = false;
            }
        }

        protected function keyDownHandler(event:KeyboardEvent):void {}

        protected function keyUpHandler(event:KeyboardEvent):void {}

        protected function isOurFocus(target:DisplayObject):Boolean {
            return target == this;
        }

        /**
         * The display object a style names: a DisplayObject as it is, a
         * Class made, or a class name (a symbol exported for ActionScript,
         * as the component skins are) found and made. When the movie has no
         * class of a skin's name, a DefaultSkin draws it.
         */
        protected function getDisplayObjectInstance(skin:Object):DisplayObject {
            if (skin == null) return null;
            if (skin is DisplayObject) {
                (skin as DisplayObject).x = 0;
                (skin as DisplayObject).y = 0;
                return skin as DisplayObject;
            }
            if (skin is Class) {
                try {
                    return new (skin as Class)() as DisplayObject;
                } catch (e:Error) {
                    return null;
                }
            }
            var name:String = String(skin);
            if (name == "") return null;
            var cls:Object = skinClass(name);
            if (cls != null) {
                try {
                    var made:DisplayObject = new cls() as DisplayObject;
                    if (made != null) return made;
                } catch (e2:Error) {
                }
            }
            return DefaultSkin.handles(name) ? new DefaultSkin(name) : null;
        }

        /** The class exported under a skin's name, or null (remembered either way). */
        protected static function skinClass(name:String):Object {
            var known:Object = skinClasses[name];
            if (known === false) return null;
            if (known != null) return known;
            var cls:Object = null;
            try {
                cls = getDefinitionByName(name);
            } catch (e:Error) {
                cls = null;
            }
            skinClasses[name] = cls == null ? false : cls;
            return cls;
        }

        /** A style: this instance's, else the shared one. */
        protected function getStyleValue(name:String):Object {
            var value:Object = instanceStyles[name];
            if (value != null) return value;
            value = sharedStyles[name];
            if (value != null) return value;
            return StyleManager.getSharedStyle(this, name);
        }

        /** Gives a child component styles of this one: child style name -> this one's style name. */
        protected function copyStylesToChild(child:UIComponent, styleMap:Object):void {
            for (var name:String in styleMap) {
                child.setStyle(name, getStyleValue(styleMap[name]));
            }
        }

        /** Runs a method when the frame is rendered (or next enters), once however often asked. */
        protected function callLater(fn:Function):void {
            if (callLaterMethods.indexOf(fn) < 0) {
                callLaterMethods.push(fn);
            }
            if (_callLaterHooked) {
                if (_renderStage != null) _renderStage.invalidate();
                return;
            }
            _callLaterHooked = true;
            addEventListener(Event.ENTER_FRAME, callLaterDispatcher, false, 0, true);
            if (stage != null) {
                _renderStage = stage;
                _renderStage.addEventListener(Event.RENDER, callLaterDispatcher, false, 0, true);
                _renderStage.invalidate();
            } else {
                addEventListener(Event.ADDED_TO_STAGE, callLaterDispatcher, false, 0, true);
            }
        }

        private function callLaterDispatcher(event:Event):void {
            if (event.type == Event.ADDED_TO_STAGE) {
                removeEventListener(Event.ADDED_TO_STAGE, callLaterDispatcher);
                if (stage != null) {
                    _renderStage = stage;
                    _renderStage.addEventListener(Event.RENDER, callLaterDispatcher, false, 0, true);
                    _renderStage.invalidate();
                }
                return;
            }
            removeEventListener(Event.ENTER_FRAME, callLaterDispatcher);
            removeEventListener(Event.ADDED_TO_STAGE, callLaterDispatcher);
            if (_renderStage != null) {
                _renderStage.removeEventListener(Event.RENDER, callLaterDispatcher);
                _renderStage = null;
            }
            _callLaterHooked = false;
            var methods:Array = callLaterMethods;
            callLaterMethods = [];
            var was:Boolean = inCallLaterPhase;
            inCallLaterPhase = true;
            _drawing = true;
            try {
                for each (var method:Function in methods) {
                    method();
                }
            } finally {
                _drawing = false;
                inCallLaterPhase = was;
            }
        }

        protected function initializeFocusManager():void {
            if (stage == null) {
                addEventListener(Event.ADDED_TO_STAGE, addedHandler, false, 0, true);
            } else {
                createFocusManager();
            }
        }

        private function addedHandler(event:Event):void {
            removeEventListener(Event.ADDED_TO_STAGE, addedHandler);
            initializeFocusManager();
        }

        protected function createFocusManager():void {
            if (stage != null && focusManagers[stage] == null) {
                focusManagers[stage] = new FocusManager(stage);
            }
        }
    }
}
