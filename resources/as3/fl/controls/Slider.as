package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.InteractionInputType;
    import fl.events.SliderEvent;
    import fl.events.SliderEventClickTarget;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;
    import flash.display.Sprite;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.ui.Keyboard;

    /** Dispatched when the value changes by the user (not when set in code). */
    [Event(name="change", type="fl.events.SliderEvent")]
    /** Dispatched while the thumb is dragged. */
    [Event(name="thumbDrag", type="fl.events.SliderEvent")]
    /** Dispatched when the thumb is pressed. */
    [Event(name="thumbPress", type="fl.events.SliderEvent")]
    /** Dispatched when the thumb is released. */
    [Event(name="thumbRelease", type="fl.events.SliderEvent")]

    /**
     * A value between minimum and maximum, chosen by dragging a thumb along
     * a track, clicking the track or with the arrow keys. With snapInterval
     * the value goes in steps of it; with tickInterval, ticks mark the track.
     *
     * Setting value in code dispatches nothing. The user dragging the thumb
     * dispatches THUMB_DRAG as it moves, and CHANGE as it moves when
     * liveDragging is set, else once when released; a click on the track or
     * a key dispatches CHANGE.
     *
     * The track is as long as the slider's width and as thick as its skin;
     * the thumb's registration point is its tip, at the value along the track.
     */
    public class Slider extends UIComponent implements IFocusManagerComponent {
        protected var _direction:String = SliderDirection.HORIZONTAL;
        protected var _minimum:Number = 0;
        protected var _maximum:Number = 10;
        protected var _value:Number = 0;
        protected var _tickInterval:Number = 0;
        protected var _snapInterval:Number = 0;
        protected var _liveDragging:Boolean = false;
        protected var tickContainer:Sprite;
        protected var thumb:BaseButton;
        protected var track:BaseButton;
        private var dragStage:Object = null;
        private var dragStartValue:Number = 0;

        protected static var defaultStyles:Object = {
            thumbUpSkin: "SliderThumb_upSkin",
            thumbOverSkin: "SliderThumb_overSkin",
            thumbDownSkin: "SliderThumb_downSkin",
            thumbDisabledSkin: "SliderThumb_disabledSkin",
            sliderTrackSkin: "SliderTrack_skin",
            sliderTrackDisabledSkin: "SliderTrack_disabledSkin",
            tickSkin: "SliderTick_skin",
            focusRectSkin: null,
            focusRectPadding: null
        };

        protected static const TRACK_STYLES:Object = {
            upSkin: "sliderTrackSkin",
            overSkin: "sliderTrackSkin",
            downSkin: "sliderTrackSkin",
            disabledSkin: "sliderTrackDisabledSkin"
        };

        protected static const THUMB_STYLES:Object = {
            upSkin: "thumbUpSkin",
            overSkin: "thumbOverSkin",
            downSkin: "thumbDownSkin",
            disabledSkin: "thumbDisabledSkin"
        };

        protected static const TICK_STYLES:Object = {
            upSkin: "tickSkin"
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function Slider() {
            super();
            setStyles();
        }

        override protected function defaultSize():Array {
            return [80, 3];
        }

        /** SliderDirection.HORIZONTAL (the default) or VERTICAL (the minimum at the bottom). */
        public function get direction():String {
            return _direction;
        }

        public function set direction(value:String):void {
            _direction = value;
            var vertical:Boolean = _direction == SliderDirection.VERTICAL;
            // Vertical, the track goes up from the slider's origin.
            rotation = vertical ? -90 : 0;
            invalidate(InvalidationType.SIZE);
        }

        public function get minimum():Number {
            return _minimum;
        }

        public function set minimum(value:Number):void {
            _minimum = value;
            this.value = Math.max(value, this.value);
            invalidate(InvalidationType.DATA);
        }

        public function get maximum():Number {
            return _maximum;
        }

        public function set maximum(value:Number):void {
            _maximum = value;
            this.value = Math.min(value, this.value);
            invalidate(InvalidationType.DATA);
        }

        /** The spacing of tick marks along the track (0 for none). */
        public function get tickInterval():Number {
            return _tickInterval;
        }

        public function set tickInterval(value:Number):void {
            _tickInterval = value;
            invalidate(InvalidationType.SIZE);
        }

        /** The step values go in (0 for any value). */
        public function get snapInterval():Number {
            return _snapInterval;
        }

        public function set snapInterval(value:Number):void {
            _snapInterval = value;
        }

        /** Whether CHANGE is dispatched as the thumb is dragged, rather than when it is released. */
        public function get liveDragging():Boolean {
            return _liveDragging;
        }

        public function set liveDragging(value:Boolean):void {
            _liveDragging = value;
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            if (enabled == value) return;
            super.enabled = value;
            track.enabled = thumb.enabled = value;
        }

        override public function setSize(w:Number, h:Number):void {
            if (_direction == SliderDirection.VERTICAL && !isLivePreview) {
                super.setSize(h, w);
            } else {
                super.setSize(w, h);
            }
            invalidate(InvalidationType.SIZE);
        }

        public function get value():Number {
            return _value;
        }

        public function set value(value:Number):void {
            doSetValue(value);
        }

        /** Snaps a value to the snap interval (when there is one) and the range. */
        protected function snapValue(value:Number):Number {
            if (_snapInterval != 0 && !isNaN(_snapInterval)) {
                var places:int = decimalPlaces(_snapInterval);
                var scale:Number = Math.pow(10, places);
                var stepped:Number = Math.round(value / _snapInterval) * _snapInterval;
                value = Math.round(stepped * scale) / scale;
            }
            if (value > _maximum) value = _maximum;
            if (value < _minimum) value = _minimum;
            return value;
        }

        // The number of digits after the point a number has, as written.
        private static function decimalPlaces(n:Number):int {
            var text:String = String(n);
            var point:int = text.indexOf(".");
            return point < 0 ? 0 : text.length - point - 1;
        }

        /**
         * Sets the value, snapped and kept in range, and moves the thumb.
         * With an interaction type (the user's doing) it dispatches CHANGE
         * when the value changed: for the thumb only when liveDragging.
         */
        protected function doSetValue(val:Number, interactionType:String = null, clickTarget:String = null,
                                      keyCode:int = 0):void {
            if (isNaN(val)) return;
            var old:Number = _value;
            _value = snapValue(val);
            if (old != _value && interactionType != null &&
                (clickTarget != SliderEventClickTarget.THUMB || _liveDragging)) {
                dispatchEvent(new SliderEvent(SliderEvent.CHANGE, value, clickTarget, interactionType, keyCode));
            }
            positionThumb();
        }

        protected function setStyles():void {
            copyStylesToChild(thumb, THUMB_STYLES);
            copyStylesToChild(track, TRACK_STYLES);
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES)) {
                setStyles();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE)) {
                track.setSize(_width, trackThickness());
                track.drawNow();
                thumb.setSize(thumbSize()[0], thumbSize()[1]);
                thumb.drawNow();
            }
            if (_tickInterval > 0) {
                drawTicks();
            } else {
                clearTicks();
            }
            positionThumb();
            super.draw();
        }

        // The track skin's own thickness.
        private function trackThickness():Number {
            var skin:DisplayObject = getDisplayObjectInstance(getStyleValue(enabled ? "sliderTrackSkin" : "sliderTrackDisabledSkin"));
            return skin == null || skin.height <= 0 ? 3 : skin.height;
        }

        // The thumb skin's own size.
        private function thumbSize():Array {
            var skin:DisplayObject = getDisplayObjectInstance(getStyleValue("thumbUpSkin"));
            if (skin == null || skin.width <= 0) return [13, 13];
            return [skin.width, skin.height];
        }

        protected function positionThumb():void {
            var range:Number = _maximum - _minimum;
            thumb.x = range == 0 ? 0 : ((_value - _minimum) / range) * _width;
            thumb.y = 0;
        }

        protected function drawTicks():void {
            clearTicks();
            tickContainer = new Sprite();
            tickContainer.mouseEnabled = false;
            var range:Number = _maximum - _minimum;
            if (range <= 0) return;
            var count:Number = range / _tickInterval;
            var spacing:Number = _width / count;
            for (var i:int = 0; i <= Math.floor(count) && i < 1000; i++) {
                var tick:DisplayObject = getDisplayObjectInstance(getStyleValue("tickSkin"));
                if (tick == null) break;
                tick.x = spacing * i;
                tick.y = -tick.height - 2;
                tickContainer.addChild(tick);
            }
            addChild(tickContainer);
        }

        protected function clearTicks():void {
            if (tickContainer == null) return;
            if (tickContainer.parent == this) removeChild(tickContainer);
            tickContainer = null;
        }

        protected function calculateValue(pos:Number, interactionType:String, clickTarget:String, keyCode:int = 0):void {
            var range:Number = _maximum - _minimum;
            var val:Number = _width == 0 ? _minimum : (pos / _width) * range + _minimum;
            doSetValue(val, interactionType, clickTarget, keyCode);
        }

        protected function doDrag(event:MouseEvent):void {
            var position:Number = Math.max(0, Math.min(_width, mouseX));
            calculateValue(position, InteractionInputType.MOUSE, SliderEventClickTarget.THUMB);
            dispatchEvent(new SliderEvent(SliderEvent.THUMB_DRAG, value, SliderEventClickTarget.THUMB, InteractionInputType.MOUSE));
        }

        protected function thumbPressHandler(event:MouseEvent):void {
            dragStartValue = _value;
            if (stage != null) {
                dragStage = stage;
                stage.addEventListener(MouseEvent.MOUSE_MOVE, doDrag, false, 0, true);
                stage.addEventListener(MouseEvent.MOUSE_UP, thumbReleasedHandler, false, 0, true);
            }
            thumb.mouseStateLocked = true;
            dispatchEvent(new SliderEvent(SliderEvent.THUMB_PRESS, value, SliderEventClickTarget.THUMB, InteractionInputType.MOUSE));
        }

        protected function thumbReleasedHandler(event:MouseEvent):void {
            if (dragStage != null) {
                dragStage.removeEventListener(MouseEvent.MOUSE_MOVE, doDrag);
                dragStage.removeEventListener(MouseEvent.MOUSE_UP, thumbReleasedHandler);
                dragStage = null;
            }
            thumb.mouseStateLocked = false;
            dispatchEvent(new SliderEvent(SliderEvent.THUMB_RELEASE, value, SliderEventClickTarget.THUMB, InteractionInputType.MOUSE));
            if (!_liveDragging && _value != dragStartValue) {
                dispatchEvent(new SliderEvent(SliderEvent.CHANGE, value, SliderEventClickTarget.THUMB, InteractionInputType.MOUSE));
            }
        }

        protected function onTrackClick(event:MouseEvent):void {
            calculateValue(track.mouseX, InteractionInputType.MOUSE, SliderEventClickTarget.TRACK);
        }

        override protected function keyDownHandler(event:KeyboardEvent):void {
            if (!enabled) return;
            var step:Number = _snapInterval > 0 ? _snapInterval : 1;
            var vertical:Boolean = _direction == SliderDirection.VERTICAL;
            var newValue:Number;
            switch (event.keyCode) {
                case Keyboard.END:
                    newValue = _maximum;
                    break;
                case Keyboard.HOME:
                    newValue = _minimum;
                    break;
                case Keyboard.UP:
                    if (!vertical) return;
                    newValue = _value + step;
                    break;
                case Keyboard.DOWN:
                    if (!vertical) return;
                    newValue = _value - step;
                    break;
                case Keyboard.LEFT:
                    if (vertical) return;
                    newValue = _value - step;
                    break;
                case Keyboard.RIGHT:
                    if (vertical) return;
                    newValue = _value + step;
                    break;
                default:
                    return;
            }
            doSetValue(newValue, InteractionInputType.KEYBOARD, null, event.keyCode);
        }

        override protected function configUI():void {
            super.configUI();
            thumb = new BaseButton();
            thumb.setSize(13, 13);
            thumb.autoRepeat = false;
            thumb.focusEnabled = false;
            addChild(thumb);
            thumb.addEventListener(MouseEvent.MOUSE_DOWN, thumbPressHandler, false, 0, true);

            track = new BaseButton();
            track.move(0, 0);
            track.setSize(80, 4);
            track.autoRepeat = false;
            track.useHandCursor = false;
            track.focusEnabled = false;
            track.addEventListener(MouseEvent.CLICK, onTrackClick, false, 0, true);
            addChildAt(track, 0);
        }
    }
}
