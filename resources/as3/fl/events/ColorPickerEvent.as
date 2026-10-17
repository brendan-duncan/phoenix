package fl.events {
    import flash.events.Event;

    /**
     * Events of a ColorPicker with the color concerned: a swatch picked
     * (CHANGE), a color typed and Enter pressed (ENTER), a swatch rolled
     * over or out of.
     */
    public class ColorPickerEvent extends Event {
        public static const CHANGE:String = "change";
        public static const ENTER:String = "enter";
        public static const ITEM_ROLL_OUT:String = "itemRollOut";
        public static const ITEM_ROLL_OVER:String = "itemRollOver";

        protected var _color:uint;

        public function ColorPickerEvent(type:String, color:uint) {
            super(type, true);
            _color = color;
        }

        public function get color():uint { return _color; }

        override public function toString():String {
            return formatToString("ColorPickerEvent", "type", "bubbles", "cancelable", "color");
        }

        override public function clone():Event {
            return new ColorPickerEvent(type, _color);
        }
    }
}
