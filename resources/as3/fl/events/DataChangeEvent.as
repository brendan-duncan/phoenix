package fl.events {
    import flash.events.Event;

    /**
     * A DataProvider's items are about to change (PRE_DATA_CHANGE) or have
     * changed (DATA_CHANGE): how (DataChangeType), which items, and the
     * range of indices concerned.
     */
    public class DataChangeEvent extends Event {
        public static const DATA_CHANGE:String = "dataChange";
        public static const PRE_DATA_CHANGE:String = "preDataChange";

        protected var _startIndex:uint;
        protected var _endIndex:uint;
        protected var _changeType:String;
        protected var _items:Array;

        public function DataChangeEvent(eventType:String, changeType:String, items:Array,
                                        startIndex:int = -1, endIndex:int = -1) {
            super(eventType);
            _changeType = changeType;
            _startIndex = startIndex;
            _endIndex = endIndex;
            _items = items;
        }

        public function get changeType():String { return _changeType; }
        public function get items():Array { return _items; }
        public function get startIndex():uint { return _startIndex; }
        public function get endIndex():uint { return _endIndex; }

        override public function toString():String {
            return formatToString("DataChangeEvent", "type", "changeType", "startIndex", "endIndex", "bubbles", "cancelable");
        }

        override public function clone():Event {
            return new DataChangeEvent(type, _changeType, _items, _startIndex, _endIndex);
        }
    }
}
