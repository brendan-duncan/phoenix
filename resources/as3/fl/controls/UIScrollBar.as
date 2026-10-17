package fl.controls {
    import fl.core.InvalidationType;
    import fl.events.ScrollEvent;
    import flash.events.Event;
    import flash.events.TextEvent;
    import flash.text.TextField;

    /**
     * A scroll bar for a text field: set scrollTarget (or, placed on a
     * timeline, scrollTargetName: the name of a text field beside it) and
     * it follows the field's scrollV (or scrollH when horizontal) and
     * scrolls it.
     */
    public class UIScrollBar extends ScrollBar {
        protected var _scrollTarget:TextField;
        protected var inEdit:Boolean = false;
        protected var inScroll:Boolean = false;
        protected var _targetScrollProperty:String = "scrollV";
        protected var _targetMaxScrollProperty:String = "maxScrollV";
        // What the target showed when last looked at, to see it change.
        private var seen:String = "";

        private static var defaultStyles:Object = {};

        public static function getStyleDefinition():Object {
            return mergeStyles(defaultStyles, ScrollBar.getStyleDefinition());
        }

        public function UIScrollBar() {
            super();
        }

        override public function set minScrollPosition(minScrollPosition:Number):void {
            super.minScrollPosition = minScrollPosition < 0 ? 0 : minScrollPosition;
        }

        override public function get minScrollPosition():Number {
            return super.minScrollPosition;
        }

        override public function set maxScrollPosition(maxScrollPosition:Number):void {
            var max:Number = maxScrollPosition;
            if (_scrollTarget != null) {
                max = Math.min(max, Number(_scrollTarget[_targetMaxScrollProperty]));
            }
            super.maxScrollPosition = max;
        }

        override public function get maxScrollPosition():Number {
            return super.maxScrollPosition;
        }

        /** The text field scrolled. */
        public function get scrollTarget():TextField {
            return _scrollTarget;
        }

        public function set scrollTarget(target:TextField):void {
            if (_scrollTarget != null) {
                _scrollTarget.removeEventListener(Event.CHANGE, handleTargetChange);
                _scrollTarget.removeEventListener(TextEvent.TEXT_INPUT, handleTargetChange);
                _scrollTarget.removeEventListener(Event.SCROLL, handleTargetScroll);
                removeEventListener(Event.ENTER_FRAME, watchTarget);
            }
            _scrollTarget = target;
            seen = "";
            if (_scrollTarget != null) {
                setTargetProperties();
                _scrollTarget.addEventListener(Event.CHANGE, handleTargetChange, false, 0, true);
                _scrollTarget.addEventListener(TextEvent.TEXT_INPUT, handleTargetChange, false, 0, true);
                _scrollTarget.addEventListener(Event.SCROLL, handleTargetScroll, false, 0, true);
                // Text set in code changes the field without an event.
                addEventListener(Event.ENTER_FRAME, watchTarget, false, 0, true);
            }
            invalidate(InvalidationType.DATA);
        }

        /** The scroll target by instance name, found among the bar's siblings. */
        public function get scrollTargetName():String {
            return _scrollTarget == null ? "" : _scrollTarget.name;
        }

        public function set scrollTargetName(target:String):void {
            var found:TextField = null;
            try {
                found = parent.getChildByName(target) as TextField;
            } catch (error:Error) {
                found = null;
            }
            if (found == null) {
                throw new Error("ScrollTarget not found, or is not a TextField");
            }
            scrollTarget = found;
        }

        override public function get direction():String {
            return super.direction;
        }

        override public function set direction(dir:String):void {
            super.direction = dir;
            setTargetProperties();
            invalidate(InvalidationType.DATA);
        }

        /** Takes the target's scroll range and position again. */
        public function update():void {
            inEdit = true;
            updateScrollTargetProperties();
            inEdit = false;
        }

        override protected function configUI():void {
            super.configUI();
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.DATA)) {
                updateScrollTargetProperties();
            }
            super.draw();
        }

        protected function updateScrollTargetProperties():void {
            if (_scrollTarget == null) {
                setScrollProperties(pageSize, minScrollPosition, maxScrollPosition, pageScrollSize);
                scrollPosition = 0;
                return;
            }
            var isHorizontal:Boolean = direction == ScrollBarDirection.HORIZONTAL;
            var page:Number = isHorizontal ? _scrollTarget.width
                                           : _scrollTarget.bottomScrollV - _scrollTarget.scrollV + 1;
            setScrollProperties(page, isHorizontal ? 0 : 1, Number(_scrollTarget[_targetMaxScrollProperty]));
            setScrollPosition(Number(_scrollTarget[_targetScrollProperty]), false);
            seen = snapshot();
        }

        override public function setScrollProperties(pageSize:Number, minScrollPosition:Number,
                                                     maxScrollPosition:Number, pageScrollSize:Number = 0):void {
            var max:Number = maxScrollPosition;
            var min:Number = minScrollPosition < 0 ? 0 : minScrollPosition;
            if (_scrollTarget != null) {
                max = Math.min(maxScrollPosition, Number(_scrollTarget[_targetMaxScrollProperty]));
            }
            super.setScrollProperties(pageSize, min, max, pageScrollSize);
        }

        override public function setScrollPosition(scrollPosition:Number, fireEvent:Boolean = true):void {
            super.setScrollPosition(scrollPosition, fireEvent);
            if (_scrollTarget == null || inEdit) {
                inScroll = false;
                return;
            }
            inScroll = true;
            var position:Number = direction == ScrollBarDirection.HORIZONTAL ? this.scrollPosition
                                                                             : Math.round(this.scrollPosition);
            if (_scrollTarget[_targetScrollProperty] != position) {
                _scrollTarget[_targetScrollProperty] = position;
            }
            inScroll = false;
        }

        protected function setTargetProperties():void {
            if (direction == ScrollBarDirection.HORIZONTAL) {
                _targetScrollProperty = "scrollH";
                _targetMaxScrollProperty = "maxScrollH";
            } else {
                _targetScrollProperty = "scrollV";
                _targetMaxScrollProperty = "maxScrollV";
            }
        }

        protected function handleTargetChange(event:Event):void {
            update();
        }

        protected function handleTargetScroll(event:Event):void {
            if (inDrag || inScroll) return;
            update();
        }

        // The target's scroll range and position as one string.
        private function snapshot():String {
            if (_scrollTarget == null) return "";
            return _scrollTarget[_targetScrollProperty] + "/" + _scrollTarget[_targetMaxScrollProperty] + "/" +
                _scrollTarget.bottomScrollV + "/" + _scrollTarget.width;
        }

        private function watchTarget(event:Event):void {
            if (_scrollTarget == null || inDrag) return;
            if (snapshot() != seen) update();
        }
    }
}
