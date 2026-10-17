package flash.events {
    public class FocusEvent extends Event {
        public static const FOCUS_IN:String = "focusIn";
        public static const FOCUS_OUT:String = "focusOut";
        public static const KEY_FOCUS_CHANGE:String = "keyFocusChange";
        public static const MOUSE_FOCUS_CHANGE:String = "mouseFocusChange";
        public var relatedObject:Object;
        public var shiftKey:Boolean;
        public var keyCode:uint;
        public function FocusEvent(type:String, bubbles:Boolean = true, cancelable:Boolean = false,
                                   relatedObject:Object = null, shiftKey:Boolean = false, keyCode:uint = 0) {
            super(type, bubbles, cancelable);
            this.relatedObject = relatedObject; this.shiftKey = shiftKey; this.keyCode = keyCode;
        }
        override public function clone():Event {
            return new FocusEvent(type, bubbles, cancelable, relatedObject, shiftKey, keyCode);
        }
    }
}
