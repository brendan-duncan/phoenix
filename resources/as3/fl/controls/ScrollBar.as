package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ComponentEvent;
    import fl.events.ScrollEvent;
    import flash.display.DisplayObject;
    import flash.events.MouseEvent;

    /** Dispatched when the scroll position changes, by the user or in code. */
    [Event(name="scroll", type="fl.events.ScrollEvent")]

    /**
     * A scroll bar: arrows at the ends, a track between and a thumb that
     * shows the page's share of the whole and where it is. The arrows move
     * by lineScrollSize, a press on the track by a page, and the thumb can
     * be dragged; holding an arrow or the track repeats.
     *
     * It is laid out vertically, 15 pixels wide; a horizontal one is the
     * same turned on its side, so its width is its length.
     */
    public class ScrollBar extends UIComponent {
        public static const WIDTH:Number = 15;

        // The arrows' length along the bar.
        protected static const ARROW_LENGTH:Number = 14;

        private var _pageSize:Number = 10;
        private var _pageScrollSize:Number = 0;
        private var _lineScrollSize:Number = 1;
        private var _minScrollPosition:Number = 0;
        private var _maxScrollPosition:Number = 0;
        private var _scrollPosition:Number = 0;
        private var _direction:String = ScrollBarDirection.VERTICAL;
        private var thumbScrollOffset:Number = 0;
        private var dragStage:Object = null;

        protected var inDrag:Boolean = false;
        protected var upArrow:BaseButton;
        protected var downArrow:BaseButton;
        protected var thumb:LabelButton;
        protected var track:BaseButton;

        private static var defaultStyles:Object = {
            downArrowDisabledSkin: "ScrollArrowDown_disabledSkin",
            downArrowDownSkin: "ScrollArrowDown_downSkin",
            downArrowOverSkin: "ScrollArrowDown_overSkin",
            downArrowUpSkin: "ScrollArrowDown_upSkin",
            thumbDisabledSkin: "ScrollThumb_upSkin",
            thumbDownSkin: "ScrollThumb_downSkin",
            thumbOverSkin: "ScrollThumb_overSkin",
            thumbUpSkin: "ScrollThumb_upSkin",
            trackDisabledSkin: "ScrollTrack_skin",
            trackDownSkin: "ScrollTrack_skin",
            trackOverSkin: "ScrollTrack_skin",
            trackUpSkin: "ScrollTrack_skin",
            upArrowDisabledSkin: "ScrollArrowUp_disabledSkin",
            upArrowDownSkin: "ScrollArrowUp_downSkin",
            upArrowOverSkin: "ScrollArrowUp_overSkin",
            upArrowUpSkin: "ScrollArrowUp_upSkin",
            thumbIcon: "ScrollBar_thumbIcon",
            repeatDelay: 500,
            repeatInterval: 35
        };

        protected static const DOWN_ARROW_STYLES:Object = {
            disabledSkin: "downArrowDisabledSkin",
            downSkin: "downArrowDownSkin",
            overSkin: "downArrowOverSkin",
            upSkin: "downArrowUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };
        protected static const THUMB_STYLES:Object = {
            disabledSkin: "thumbDisabledSkin",
            downSkin: "thumbDownSkin",
            overSkin: "thumbOverSkin",
            upSkin: "thumbUpSkin",
            icon: "thumbIcon",
            textPadding: "thumbTextPadding"
        };
        protected static const TRACK_STYLES:Object = {
            disabledSkin: "trackDisabledSkin",
            downSkin: "trackDownSkin",
            overSkin: "trackOverSkin",
            upSkin: "trackUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };
        protected static const UP_ARROW_STYLES:Object = {
            disabledSkin: "upArrowDisabledSkin",
            downSkin: "upArrowDownSkin",
            overSkin: "upArrowOverSkin",
            upSkin: "upArrowUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function ScrollBar() {
            super();
            setStyles();
            focusEnabled = false;
        }

        override protected function defaultSize():Array {
            return [WIDTH, 100];
        }

        private function get horizontal():Boolean {
            return _direction == ScrollBarDirection.HORIZONTAL;
        }

        /** For a horizontal bar, width is the length and height the thickness. */
        override public function setSize(width:Number, height:Number):void {
            if (horizontal) {
                super.setSize(height, width);
            } else {
                super.setSize(width, height);
            }
        }

        override public function get width():Number {
            return horizontal ? super.height : super.width;
        }

        override public function set width(value:Number):void {
            setSize(value, height);
        }

        override public function get height():Number {
            return horizontal ? super.width : super.height;
        }

        override public function set height(value:Number):void {
            setSize(width, value);
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            var active:Boolean = enabled && _maxScrollPosition > _minScrollPosition;
            downArrow.enabled = active;
            track.enabled = active;
            thumb.enabled = active;
            upArrow.enabled = active;
            updateThumb();
        }

        /** Sets the page size and range of positions together, and enables the bar if there is a range. */
        public function setScrollProperties(pageSize:Number, minScrollPosition:Number, maxScrollPosition:Number,
                                            pageScrollSize:Number = 0):void {
            this.pageSize = pageSize;
            _minScrollPosition = minScrollPosition;
            _maxScrollPosition = maxScrollPosition;
            if (pageScrollSize >= 0) {
                _pageScrollSize = pageScrollSize;
            }
            enabled = (_maxScrollPosition > _minScrollPosition);
            setScrollPosition(_scrollPosition, false);
            updateThumb();
        }

        public function get scrollPosition():Number {
            return _scrollPosition;
        }

        public function set scrollPosition(newScrollPosition:Number):void {
            setScrollPosition(newScrollPosition, true);
        }

        public function get minScrollPosition():Number {
            return _minScrollPosition;
        }

        public function set minScrollPosition(value:Number):void {
            setScrollProperties(_pageSize, value, _maxScrollPosition);
        }

        public function get maxScrollPosition():Number {
            return _maxScrollPosition;
        }

        public function set maxScrollPosition(value:Number):void {
            setScrollProperties(_pageSize, _minScrollPosition, value);
        }

        /** How much is seen at once, in scroll position units. */
        public function get pageSize():Number {
            return _pageSize;
        }

        public function set pageSize(value:Number):void {
            if (value > 0) {
                _pageSize = value;
            }
        }

        /** How far a press on the track moves: the page size unless set. */
        public function get pageScrollSize():Number {
            return _pageScrollSize == 0 ? _pageSize : _pageScrollSize;
        }

        public function set pageScrollSize(value:Number):void {
            if (value >= 0) {
                _pageScrollSize = value;
            }
        }

        /** How far an arrow moves. */
        public function get lineScrollSize():Number {
            return _lineScrollSize;
        }

        public function set lineScrollSize(value:Number):void {
            if (value > 0) {
                _lineScrollSize = value;
            }
        }

        public function get direction():String {
            return _direction;
        }

        public function set direction(value:String):void {
            if (_direction == value) return;
            // The length stays the length.
            var length:Number = super.height;
            var thickness:Number = super.width;
            _direction = value;
            if (horizontal) {
                setScaleX(-1);
                rotation = -90;
            } else {
                rotation = 0;
                setScaleX(1);
            }
            super.setSize(thickness, length);
            invalidate(InvalidationType.SIZE);
        }

        override protected function configUI():void {
            super.configUI();

            track = new BaseButton();
            track.move(0, ARROW_LENGTH);
            track.useHandCursor = false;
            track.autoRepeat = true;
            track.focusEnabled = false;
            addChild(track);

            thumb = new LabelButton();
            thumb.label = "";
            thumb.setSize(WIDTH, 15);
            thumb.move(0, 15);
            thumb.focusEnabled = false;
            thumb.tabEnabled = false;
            addChild(thumb);

            downArrow = new BaseButton();
            downArrow.setSize(WIDTH, ARROW_LENGTH);
            downArrow.autoRepeat = true;
            downArrow.focusEnabled = false;
            addChild(downArrow);

            upArrow = new BaseButton();
            upArrow.setSize(WIDTH, ARROW_LENGTH);
            upArrow.move(0, 0);
            upArrow.autoRepeat = true;
            upArrow.focusEnabled = false;
            addChild(upArrow);

            upArrow.addEventListener(ComponentEvent.BUTTON_DOWN, scrollPressHandler, false, 0, true);
            downArrow.addEventListener(ComponentEvent.BUTTON_DOWN, scrollPressHandler, false, 0, true);
            track.addEventListener(ComponentEvent.BUTTON_DOWN, scrollPressHandler, false, 0, true);
            thumb.addEventListener(MouseEvent.MOUSE_DOWN, thumbPressHandler, false, 0, true);

            enabled = false;
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.SIZE)) {
                var length:Number = super.height;
                upArrow.setSize(WIDTH, ARROW_LENGTH);
                downArrow.setSize(WIDTH, ARROW_LENGTH);
                downArrow.move(0, Math.max(upArrow.height, length - downArrow.height));
                track.move(0, upArrow.height);
                track.setSize(WIDTH, Math.max(0, length - (downArrow.height + upArrow.height)));
                updateThumb();
            }
            if (isInvalid(InvalidationType.STYLES, InvalidationType.STATE)) {
                setStyles();
            }
            downArrow.drawNow();
            upArrow.drawNow();
            track.drawNow();
            thumb.drawNow();
            validate();
        }

        protected function setStyles():void {
            copyStylesToChild(downArrow, DOWN_ARROW_STYLES);
            copyStylesToChild(thumb, THUMB_STYLES);
            copyStylesToChild(track, TRACK_STYLES);
            copyStylesToChild(upArrow, UP_ARROW_STYLES);
        }

        /** Sizes and places the thumb for the page size and position; hides it when there is no range. */
        protected function updateThumb():void {
            var whole:Number = _maxScrollPosition - _minScrollPosition + _pageSize;
            if (track.height <= 12 || _maxScrollPosition <= _minScrollPosition || whole == 0 || isNaN(whole)) {
                thumb.height = 12;
                thumb.visible = false;
            } else {
                thumb.height = Math.max(13, _pageSize / whole * track.height);
                thumb.y = track.y + (track.height - thumb.height) *
                    ((_scrollPosition - _minScrollPosition) / (_maxScrollPosition - _minScrollPosition));
                thumb.visible = enabled;
            }
        }

        protected function scrollPressHandler(event:ComponentEvent):void {
            event.stopImmediatePropagation();
            if (event.currentTarget == upArrow) {
                setScrollPosition(_scrollPosition - _lineScrollSize);
            } else if (event.currentTarget == downArrow) {
                setScrollPosition(_scrollPosition + _lineScrollSize);
            } else {
                var mousePosition:Number = (track.mouseY) / track.height *
                    (_maxScrollPosition - _minScrollPosition) + _minScrollPosition;
                var step:Number = pageScrollSize;
                if (_scrollPosition < mousePosition) {
                    setScrollPosition(Math.min(mousePosition, _scrollPosition + step));
                } else if (_scrollPosition > mousePosition) {
                    setScrollPosition(Math.max(mousePosition, _scrollPosition - step));
                }
            }
        }

        protected function thumbPressHandler(event:MouseEvent):void {
            inDrag = true;
            thumbScrollOffset = mouseY - thumb.y;
            thumb.mouseStateLocked = true;
            mouseChildren = false;
            if (stage != null) {
                dragStage = stage;
                stage.addEventListener(MouseEvent.MOUSE_MOVE, handleThumbDrag, false, 0, true);
                stage.addEventListener(MouseEvent.MOUSE_UP, thumbReleaseHandler, false, 0, true);
            }
        }

        protected function handleThumbDrag(event:MouseEvent):void {
            var room:Number = track.height - thumb.height;
            if (room <= 0) return;
            var position:Number = Math.max(0, Math.min(room, mouseY - track.y - thumbScrollOffset));
            setScrollPosition(position / room * (_maxScrollPosition - _minScrollPosition) + _minScrollPosition);
        }

        protected function thumbReleaseHandler(event:MouseEvent):void {
            inDrag = false;
            mouseChildren = true;
            thumb.mouseStateLocked = false;
            if (dragStage != null) {
                dragStage.removeEventListener(MouseEvent.MOUSE_MOVE, handleThumbDrag);
                dragStage.removeEventListener(MouseEvent.MOUSE_UP, thumbReleaseHandler);
                dragStage = null;
            }
        }

        /** Moves to a position in the range; dispatches ScrollEvent.SCROLL when it moved and fireEvent is set. */
        public function setScrollPosition(newScrollPosition:Number, fireEvent:Boolean = true):void {
            var old:Number = scrollPosition;
            _scrollPosition = Math.max(_minScrollPosition, Math.min(_maxScrollPosition, newScrollPosition));
            if (old == _scrollPosition) return;
            if (fireEvent) {
                dispatchEvent(new ScrollEvent(_direction, scrollPosition - old, scrollPosition));
            }
            updateThumb();
        }
    }
}
