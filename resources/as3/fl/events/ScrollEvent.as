package fl.events {
    import flash.events.Event;

    /**
     * A scroll bar or scrolling component moved: in which direction
     * (ScrollBarDirection), by how much, and where it is now.
     */
    public class ScrollEvent extends Event {
        public static const SCROLL:String = "scroll";

        private var _direction:String;
        private var _delta:Number;
        private var _position:Number;

        public function ScrollEvent(direction:String, delta:Number, position:Number) {
            super(ScrollEvent.SCROLL, false, false);
            _direction = direction;
            _delta = delta;
            _position = position;
        }

        public function get direction():String { return _direction; }
        public function get delta():Number { return _delta; }
        public function get position():Number { return _position; }

        override public function toString():String {
            return formatToString("ScrollEvent", "type", "bubbles", "cancelable", "direction", "delta", "position");
        }

        override public function clone():Event {
            return new ScrollEvent(_direction, _delta, _position);
        }
    }
}
