package flash.events {
    public class MouseEvent extends Event {
        public static const CLICK:String = "click";
        public static const DOUBLE_CLICK:String = "doubleClick";
        public static const MOUSE_DOWN:String = "mouseDown";
        public static const MOUSE_MOVE:String = "mouseMove";
        public static const MOUSE_OUT:String = "mouseOut";
        public static const MOUSE_OVER:String = "mouseOver";
        public static const MOUSE_UP:String = "mouseUp";
        public static const MOUSE_WHEEL:String = "mouseWheel";
        public static const ROLL_OUT:String = "rollOut";
        public static const ROLL_OVER:String = "rollOver";

        public var localX:Number;
        public var localY:Number;
        public var relatedObject:Object;
        public var ctrlKey:Boolean;
        public var altKey:Boolean;
        public var shiftKey:Boolean;
        public var buttonDown:Boolean;
        public var delta:int;

        public function MouseEvent(type:String, bubbles:Boolean = true, cancelable:Boolean = false,
                                   localX:Number = 0, localY:Number = 0, relatedObject:Object = null,
                                   ctrlKey:Boolean = false, altKey:Boolean = false, shiftKey:Boolean = false,
                                   buttonDown:Boolean = false, delta:int = 0) {
            super(type, bubbles, cancelable);
            this.localX = localX; this.localY = localY; this.relatedObject = relatedObject;
            this.ctrlKey = ctrlKey; this.altKey = altKey; this.shiftKey = shiftKey;
            this.buttonDown = buttonDown; this.delta = delta;
        }
        public function get stageX():Number { return flash.display.Stage.mouseStageX; }
        public function get stageY():Number { return flash.display.Stage.mouseStageY; }
        public function updateAfterEvent():void {}
        override public function clone():Event {
            return new MouseEvent(type, bubbles, cancelable, localX, localY, relatedObject, ctrlKey, altKey, shiftKey, buttonDown, delta);
        }
    }
}
