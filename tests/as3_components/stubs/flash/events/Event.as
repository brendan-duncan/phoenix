package flash.events {
    // Stand-in for the player's Event: enough for the components' logic.
    public class Event {
        public static const ACTIVATE:String = "activate";
        public static const ADDED:String = "added";
        public static const ADDED_TO_STAGE:String = "addedToStage";
        public static const CHANGE:String = "change";
        public static const CLOSE:String = "close";
        public static const COMPLETE:String = "complete";
        public static const ENTER_FRAME:String = "enterFrame";
        public static const OPEN:String = "open";
        public static const REMOVED:String = "removed";
        public static const REMOVED_FROM_STAGE:String = "removedFromStage";
        public static const RENDER:String = "render";
        public static const RESIZE:String = "resize";
        public static const SCROLL:String = "scroll";
        public static const SELECT:String = "select";
        public static const TAB_CHILDREN_CHANGE:String = "tabChildrenChange";
        public static const TAB_ENABLED_CHANGE:String = "tabEnabledChange";

        private var _type:String;
        private var _bubbles:Boolean;
        private var _cancelable:Boolean;
        private var _prevented:Boolean = false;
        public var _target:Object = null;
        public var _currentTarget:Object = null;
        public var _phase:uint = 2;
        public var _stopped:Boolean = false;
        public var _stoppedNow:Boolean = false;

        public function Event(type:String, bubbles:Boolean = false, cancelable:Boolean = false) {
            _type = type;
            _bubbles = bubbles;
            _cancelable = cancelable;
        }
        public function get type():String { return _type; }
        public function get bubbles():Boolean { return _bubbles; }
        public function get cancelable():Boolean { return _cancelable; }
        public function get target():Object { return _target; }
        public function get currentTarget():Object { return _currentTarget; }
        public function get eventPhase():uint { return _phase; }
        public function clone():Event { return new Event(_type, _bubbles, _cancelable); }
        public function stopPropagation():void { _stopped = true; }
        public function stopImmediatePropagation():void { _stopped = true; _stoppedNow = true; }
        public function preventDefault():void { if (_cancelable) _prevented = true; }
        public function isDefaultPrevented():Boolean { return _prevented; }
        public function formatToString(className:String, ...names):String {
            var text:String = "[" + className;
            for each (var name:String in names) text += " " + name + "=" + this[name];
            return text + "]";
        }
        public function toString():String { return formatToString("Event", "type", "bubbles", "cancelable"); }
    }
}
