package fl.events {
    import flash.events.Event;

    /**
     * Events a component dispatches about itself: moved, resized, shown,
     * hidden, its label changed, a button pressed, Enter typed.
     */
    public class ComponentEvent extends Event {
        public static const BUTTON_DOWN:String = "buttonDown";
        public static const ENTER:String = "enter";
        public static const HIDE:String = "hide";
        public static const LABEL_CHANGE:String = "labelChange";
        public static const MOVE:String = "move";
        public static const RESIZE:String = "resize";
        public static const SHOW:String = "show";

        public function ComponentEvent(type:String, bubbles:Boolean = false, cancelable:Boolean = false) {
            super(type, bubbles, cancelable);
        }

        override public function toString():String {
            return formatToString("ComponentEvent", "type", "bubbles", "cancelable");
        }

        override public function clone():Event {
            return new ComponentEvent(type, bubbles, cancelable);
        }
    }
}
