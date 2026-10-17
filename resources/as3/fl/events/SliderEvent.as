package fl.events {
    import flash.events.Event;

    /**
     * Events of a Slider: its value changed, its thumb pressed, dragged or
     * released. CHANGE is "change", the same as Event.CHANGE.
     */
    public class SliderEvent extends Event {
        public static const CHANGE:String = "change";
        public static const THUMB_DRAG:String = "thumbDrag";
        public static const THUMB_PRESS:String = "thumbPress";
        public static const THUMB_RELEASE:String = "thumbRelease";

        protected var _value:Number;
        protected var _clickTarget:String;
        protected var _triggerEvent:String;
        protected var _keyCode:int;

        public function SliderEvent(type:String, value:Number, clickTarget:String, triggerEvent:String, keyCode:int = 0) {
            super(type);
            _value = value;
            _clickTarget = clickTarget;
            _triggerEvent = triggerEvent;
            _keyCode = keyCode;
        }

        /** The slider's value when the event was made. */
        public function get value():Number { return _value; }
        /** SliderEventClickTarget.THUMB or TRACK: what the mouse was on. */
        public function get clickTarget():String { return _clickTarget; }
        /** InteractionInputType.MOUSE or KEYBOARD. */
        public function get triggerEvent():String { return _triggerEvent; }
        public function get keyCode():Number { return _keyCode; }

        override public function toString():String {
            return formatToString("SliderEvent", "type", "value", "bubbles", "cancelable", "keyCode", "triggerEvent", "clickTarget");
        }

        override public function clone():Event {
            return new SliderEvent(type, _value, _clickTarget, _triggerEvent, _keyCode);
        }
    }
}
