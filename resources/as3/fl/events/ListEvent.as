package fl.events {
    import flash.events.Event;

    /**
     * Events of the list-based components about one of their items: clicked,
     * double-clicked, rolled over or out of. The item, its index in the
     * data provider, and its row and column as shown.
     */
    public class ListEvent extends Event {
        public static const ITEM_CLICK:String = "itemClick";
        public static const ITEM_DOUBLE_CLICK:String = "itemDoubleClick";
        public static const ITEM_ROLL_OUT:String = "itemRollOut";
        public static const ITEM_ROLL_OVER:String = "itemRollOver";

        protected var _rowIndex:int;
        protected var _columnIndex:int;
        protected var _index:int;
        protected var _item:Object;

        public function ListEvent(type:String, bubbles:Boolean = false, cancelable:Boolean = false,
                                  columnIndex:int = -1, rowIndex:int = -1, index:int = -1, item:Object = null) {
            super(type, bubbles, cancelable);
            _rowIndex = rowIndex;
            _columnIndex = columnIndex;
            _index = index;
            _item = item;
        }

        public function get rowIndex():Object { return _rowIndex; }
        public function get columnIndex():int { return _columnIndex; }
        public function get index():int { return _index; }
        public function get item():Object { return _item; }

        override public function toString():String {
            return formatToString("ListEvent", "type", "bubbles", "cancelable", "columnIndex", "rowIndex", "index", "item");
        }

        override public function clone():Event {
            return new ListEvent(type, bubbles, cancelable, _columnIndex, _rowIndex, _index, _item);
        }
    }
}
