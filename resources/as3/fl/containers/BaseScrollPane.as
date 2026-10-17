package fl.containers {
    import fl.controls.ScrollBar;
    import fl.controls.ScrollBarDirection;
    import fl.controls.ScrollPolicy;
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ScrollEvent;
    import flash.display.DisplayObject;
    import flash.display.Graphics;
    import flash.display.Shape;
    import flash.events.MouseEvent;
    import flash.geom.Rectangle;

    /** Dispatched when the content scrolls. */
    [Event(name="scroll", type="fl.events.ScrollEvent")]

    /**
     * The base of the components that scroll content inside a border
     * (lists, scroll panes): a background (the skin style), scroll bars
     * shown always, never or when the content is bigger than the room
     * (horizontalScrollPolicy, verticalScrollPolicy), the room inside the
     * padding and scroll bars (availableWidth, availableHeight), and the
     * mouse wheel.
     */
    public class BaseScrollPane extends UIComponent {
        protected var _verticalScrollBar:ScrollBar;
        protected var _horizontalScrollBar:ScrollBar;
        protected var contentScrollRect:Rectangle;
        protected var disabledOverlay:Shape;
        protected var background:DisplayObject;
        protected var contentWidth:Number = 0;
        protected var contentHeight:Number = 0;
        protected var _horizontalScrollPolicy:String = ScrollPolicy.AUTO;
        protected var _verticalScrollPolicy:String = ScrollPolicy.AUTO;
        protected var contentPadding:Number = 0;
        protected var availableWidth:Number = 0;
        protected var availableHeight:Number = 0;
        protected var vOffset:Number = 0;
        protected var vScrollBar:Boolean = false;
        protected var hScrollBar:Boolean = false;
        protected var _maxHorizontalScrollPosition:Number = 0;
        protected var _horizontalPageScrollSize:Number = 0;
        protected var _verticalPageScrollSize:Number = 0;
        protected var defaultLineScrollSize:Number = 4;
        protected var useFixedHorizontalScrolling:Boolean = false;
        protected var _useBitmpScrolling:Boolean = false;

        private static var defaultStyles:Object = {
            repeatDelay: 500,
            repeatInterval: 35,
            skin: "ScrollPane_upSkin",
            contentPadding: 0,
            disabledAlpha: 0.5
        };

        protected static const SCROLL_BAR_STYLES:Object = {
            upArrowDisabledSkin: "upArrowDisabledSkin",
            upArrowDownSkin: "upArrowDownSkin",
            upArrowOverSkin: "upArrowOverSkin",
            upArrowUpSkin: "upArrowUpSkin",
            downArrowDisabledSkin: "downArrowDisabledSkin",
            downArrowDownSkin: "downArrowDownSkin",
            downArrowOverSkin: "downArrowOverSkin",
            downArrowUpSkin: "downArrowUpSkin",
            thumbDisabledSkin: "thumbDisabledSkin",
            thumbDownSkin: "thumbDownSkin",
            thumbOverSkin: "thumbOverSkin",
            thumbUpSkin: "thumbUpSkin",
            thumbIcon: "thumbIcon",
            trackDisabledSkin: "trackDisabledSkin",
            trackDownSkin: "trackDownSkin",
            trackOverSkin: "trackOverSkin",
            trackUpSkin: "trackUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };

        public static function getStyleDefinition():Object {
            return mergeStyles(defaultStyles, ScrollBar.getStyleDefinition());
        }

        public function BaseScrollPane() {
            super();
        }

        override protected function defaultSize():Array {
            return [100, 100];
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            if (enabled == value) return;
            _verticalScrollBar.enabled = value;
            _horizontalScrollBar.enabled = value;
            super.enabled = value;
        }

        public function get horizontalScrollPolicy():String {
            return _horizontalScrollPolicy;
        }

        public function set horizontalScrollPolicy(value:String):void {
            _horizontalScrollPolicy = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get verticalScrollPolicy():String {
            return _verticalScrollPolicy;
        }

        public function set verticalScrollPolicy(value:String):void {
            _verticalScrollPolicy = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get horizontalLineScrollSize():Number {
            return _horizontalScrollBar.lineScrollSize;
        }

        public function set horizontalLineScrollSize(value:Number):void {
            _horizontalScrollBar.lineScrollSize = value;
        }

        public function get verticalLineScrollSize():Number {
            return _verticalScrollBar.lineScrollSize;
        }

        public function set verticalLineScrollSize(value:Number):void {
            _verticalScrollBar.lineScrollSize = value;
        }

        public function get horizontalScrollPosition():Number {
            return _horizontalScrollBar.scrollPosition;
        }

        public function set horizontalScrollPosition(value:Number):void {
            drawNow();
            _horizontalScrollBar.scrollPosition = value;
            setHorizontalScrollPosition(_horizontalScrollBar.scrollPosition, false);
        }

        public function get verticalScrollPosition():Number {
            return _verticalScrollBar.scrollPosition;
        }

        public function set verticalScrollPosition(value:Number):void {
            drawNow();
            _verticalScrollBar.scrollPosition = value;
            setVerticalScrollPosition(_verticalScrollBar.scrollPosition, false);
        }

        public function get maxHorizontalScrollPosition():Number {
            drawNow();
            return Math.max(0, contentWidth - availableWidth);
        }

        public function get maxVerticalScrollPosition():Number {
            drawNow();
            return Math.max(0, contentHeight - availableHeight);
        }

        /** Kept for compatibility; scrolling draws the same either way. */
        public function get useBitmapScrolling():Boolean {
            return _useBitmpScrolling;
        }

        public function set useBitmapScrolling(value:Boolean):void {
            _useBitmpScrolling = value;
            invalidate(InvalidationType.STATE);
        }

        public function get horizontalPageScrollSize():Number {
            if (isNaN(availableWidth)) drawNow();
            return (_horizontalPageScrollSize == 0 && !isNaN(availableWidth)) ? availableWidth : _horizontalPageScrollSize;
        }

        public function set horizontalPageScrollSize(value:Number):void {
            _horizontalPageScrollSize = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get verticalPageScrollSize():Number {
            if (isNaN(availableHeight)) drawNow();
            return (_verticalPageScrollSize == 0 && !isNaN(availableHeight)) ? availableHeight : _verticalPageScrollSize;
        }

        public function set verticalPageScrollSize(value:Number):void {
            _verticalPageScrollSize = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get horizontalScrollBar():ScrollBar {
            return _horizontalScrollBar;
        }

        public function get verticalScrollBar():ScrollBar {
            return _verticalScrollBar;
        }

        override protected function configUI():void {
            super.configUI();
            contentScrollRect = new Rectangle(0, 0, 85, 85);

            _verticalScrollBar = new ScrollBar();
            _verticalScrollBar.addEventListener(ScrollEvent.SCROLL, handleScroll, false, 0, true);
            _verticalScrollBar.visible = false;
            _verticalScrollBar.lineScrollSize = defaultLineScrollSize;
            addChild(_verticalScrollBar);
            copyStylesToChild(_verticalScrollBar, SCROLL_BAR_STYLES);

            _horizontalScrollBar = new ScrollBar();
            _horizontalScrollBar.direction = ScrollBarDirection.HORIZONTAL;
            _horizontalScrollBar.addEventListener(ScrollEvent.SCROLL, handleScroll, false, 0, true);
            _horizontalScrollBar.visible = false;
            _horizontalScrollBar.lineScrollSize = defaultLineScrollSize;
            addChild(_horizontalScrollBar);
            copyStylesToChild(_horizontalScrollBar, SCROLL_BAR_STYLES);

            disabledOverlay = new Shape();
            var g:Graphics = disabledOverlay.graphics;
            g.beginFill(0xFFFFFF);
            g.drawRect(0, 0, width, height);
            g.endFill();

            addEventListener(MouseEvent.MOUSE_WHEEL, handleWheel, false, 0, true);
        }

        /** Tells the pane how big its content is, for the scroll bars. */
        protected function setContentSize(width:Number, height:Number):void {
            if ((contentWidth == width || useFixedHorizontalScrolling) && contentHeight == height) return;
            contentWidth = width;
            contentHeight = height;
            invalidate(InvalidationType.SIZE);
        }

        protected function handleScroll(event:ScrollEvent):void {
            if (event.target == _verticalScrollBar) {
                setVerticalScrollPosition(event.position);
            } else {
                setHorizontalScrollPosition(event.position);
            }
            dispatchEvent(new ScrollEvent(event.direction, event.delta, event.position));
        }

        protected function handleWheel(event:MouseEvent):void {
            if (!enabled || !_verticalScrollBar.visible || contentHeight <= availableHeight) return;
            // The bar's scroll event moves the content.
            _verticalScrollBar.scrollPosition -= event.delta * verticalLineScrollSize;
        }

        /** Scrolls the content horizontally; subclasses move their content. */
        protected function setHorizontalScrollPosition(scroll:Number, fireEvent:Boolean = false):void {}

        /** Scrolls the content vertically; subclasses move their content. */
        protected function setVerticalScrollPosition(scroll:Number, fireEvent:Boolean = false):void {}

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES)) {
                setStyles();
                drawBackground();
                if (contentPadding != Number(getStyleValue("contentPadding"))) {
                    invalidate(InvalidationType.SIZE, false);
                }
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.STATE)) {
                drawLayout();
            }
            updateChildren();
            super.draw();
        }

        protected function setStyles():void {
            copyStylesToChild(_verticalScrollBar, SCROLL_BAR_STYLES);
            copyStylesToChild(_horizontalScrollBar, SCROLL_BAR_STYLES);
        }

        protected function drawBackground():void {
            var old:DisplayObject = background;
            var made:DisplayObject = getDisplayObjectInstance(getStyleValue("skin"));
            if (made != null) {
                background = made;
                background.width = width;
                background.height = height;
                addChildAt(background, 0);
            }
            if (old != null && old != background && old.parent == this) {
                removeChild(old);
            }
        }

        /** Places the background, scroll bars and disabled overlay for the size and content. */
        protected function drawLayout():void {
            calculateAvailableSize();
            calculateContentWidth();

            if (background != null) {
                background.width = width;
                background.height = height;
            }

            if (vScrollBar) {
                _verticalScrollBar.visible = true;
                _verticalScrollBar.x = width - ScrollBar.WIDTH - contentPadding;
                _verticalScrollBar.y = contentPadding;
                _verticalScrollBar.height = availableHeight;
            } else {
                _verticalScrollBar.visible = false;
            }
            _verticalScrollBar.setScrollProperties(availableHeight, 0, contentHeight - availableHeight, verticalPageScrollSize);
            setVerticalScrollPosition(_verticalScrollBar.scrollPosition, false);

            if (hScrollBar) {
                _horizontalScrollBar.visible = true;
                _horizontalScrollBar.x = contentPadding;
                _horizontalScrollBar.y = height - ScrollBar.WIDTH - contentPadding;
                _horizontalScrollBar.width = availableWidth;
            } else {
                _horizontalScrollBar.visible = false;
            }
            _horizontalScrollBar.setScrollProperties(availableWidth, 0,
                useFixedHorizontalScrolling ? _maxHorizontalScrollPosition : contentWidth - availableWidth,
                horizontalPageScrollSize);
            setHorizontalScrollPosition(_horizontalScrollBar.scrollPosition, false);

            drawDisabledOverlay();
        }

        protected function drawDisabledOverlay():void {
            if (enabled) {
                if (disabledOverlay.parent == this) removeChild(disabledOverlay);
                return;
            }
            disabledOverlay.x = disabledOverlay.y = contentPadding;
            disabledOverlay.width = availableWidth;
            disabledOverlay.height = availableHeight;
            var alpha:Number = Number(getStyleValue("disabledAlpha"));
            disabledOverlay.alpha = isNaN(alpha) ? 0.5 : alpha;
            addChild(disabledOverlay);
        }

        /** Works out the room inside the padding and the scroll bars, and which bars show. */
        protected function calculateAvailableSize():void {
            var barWidth:Number = ScrollBar.WIDTH;
            var padding:Number = Number(getStyleValue("contentPadding"));
            if (isNaN(padding)) padding = 0;
            contentPadding = padding;

            var availHeight:Number = height - 2 * padding - vOffset;
            vScrollBar = (_verticalScrollPolicy == ScrollPolicy.ON) ||
                (_verticalScrollPolicy == ScrollPolicy.AUTO && contentHeight > availHeight);
            var availWidth:Number = width - (vScrollBar ? barWidth : 0) - 2 * padding;
            var maxHScroll:Number = useFixedHorizontalScrolling ? _maxHorizontalScrollPosition : contentWidth - availWidth;
            hScrollBar = (_horizontalScrollPolicy == ScrollPolicy.ON) ||
                (_horizontalScrollPolicy == ScrollPolicy.AUTO && maxHScroll > 0);
            if (hScrollBar) {
                availHeight -= barWidth;
            }
            // The horizontal bar can leave too little room, needing the vertical one after all.
            if (hScrollBar && !vScrollBar && _verticalScrollPolicy == ScrollPolicy.AUTO && contentHeight > availHeight) {
                vScrollBar = true;
                availWidth -= barWidth;
            }
            availableHeight = availHeight + vOffset;
            availableWidth = availWidth;
        }

        protected function calculateContentWidth():void {}

        protected function updateChildren():void {
            _verticalScrollBar.enabled = _horizontalScrollBar.enabled = enabled;
            _verticalScrollBar.drawNow();
            _horizontalScrollBar.drawNow();
        }
    }
}
