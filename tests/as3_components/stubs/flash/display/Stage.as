package flash.display {
    import flash.events.Event;
    import flash.events.FocusEvent;

    // Stand-in: the root of the display list, with helpers for tests:
    // Stage.mouseStageX/Y is where the mouse is, frame() runs one frame
    // (enter frame listeners, then render ones when invalidated).
    public class Stage extends DisplayObjectContainer {
        public static var mouseStageX:Number = 0;
        public static var mouseStageY:Number = 0;
        private static var watched:Array = [];

        public var stageWidth:int = 550;
        public var stageHeight:int = 400;
        public var frameRate:Number = 24;
        private var _focus:InteractiveObject = null;
        private var _invalid:Boolean = false;

        public function Stage() { super(); name = "stage"; }

        public static function watchFrames(d:DisplayObject):void {
            if (watched.indexOf(d) < 0) watched.push(d);
        }
        public function invalidate():void { _invalid = true; }
        public function frame():void {
            for each (var d:DisplayObject in watched.slice()) {
                if (d.hasEventListener(Event.ENTER_FRAME)) d.dispatchEvent(new Event(Event.ENTER_FRAME));
            }
            if (_invalid) {
                _invalid = false;
                dispatchEvent(new Event(Event.RENDER));
            }
        }
        public function get focus():InteractiveObject { return _focus; }
        public function set focus(value:InteractiveObject):void {
            if (_focus == value) return;
            var old:InteractiveObject = _focus;
            _focus = value;
            if (old != null) old.dispatchEvent(new FocusEvent(FocusEvent.FOCUS_OUT, true, false, value));
            if (value != null) value.dispatchEvent(new FocusEvent(FocusEvent.FOCUS_IN, true, false, old));
        }
    }
}
