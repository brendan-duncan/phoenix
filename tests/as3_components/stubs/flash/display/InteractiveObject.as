package flash.display {
    public class InteractiveObject extends DisplayObject {
        public var mouseEnabled:Boolean = true;
        public var doubleClickEnabled:Boolean = false;
        public var tabEnabled:Boolean = false;
        public var tabIndex:int = -1;
        public var focusRect:Object = null;
        public var contextMenu:Object = null;
        public function InteractiveObject() { super(); }
    }
}
