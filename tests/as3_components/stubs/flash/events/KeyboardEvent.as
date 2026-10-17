package flash.events {
    public class KeyboardEvent extends Event {
        public static const KEY_DOWN:String = "keyDown";
        public static const KEY_UP:String = "keyUp";
        public var charCode:uint;
        public var keyCode:uint;
        public var keyLocation:uint;
        public var ctrlKey:Boolean;
        public var altKey:Boolean;
        public var shiftKey:Boolean;
        public function KeyboardEvent(type:String, bubbles:Boolean = true, cancelable:Boolean = false,
                                      charCode:uint = 0, keyCode:uint = 0, keyLocation:uint = 0,
                                      ctrlKey:Boolean = false, altKey:Boolean = false, shiftKey:Boolean = false) {
            super(type, bubbles, cancelable);
            this.charCode = charCode; this.keyCode = keyCode; this.keyLocation = keyLocation;
            this.ctrlKey = ctrlKey; this.altKey = altKey; this.shiftKey = shiftKey;
        }
        override public function clone():Event {
            return new KeyboardEvent(type, bubbles, cancelable, charCode, keyCode, keyLocation, ctrlKey, altKey, shiftKey);
        }
    }
}
