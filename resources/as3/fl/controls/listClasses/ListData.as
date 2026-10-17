package fl.controls.listClasses {
    import fl.core.UIComponent;

    /**
     * What a cell renderer shows for its item: the label and icon the list
     * made of the item, the list, and the item's index and place.
     */
    public class ListData {
        protected var _icon:Object = null;
        protected var _label:String;
        protected var _owner:UIComponent;
        protected var _index:uint;
        protected var _row:uint;
        protected var _column:uint;

        public function ListData(label:String, icon:Object, owner:UIComponent, index:uint, row:uint, col:uint = 0) {
            _label = label;
            _icon = icon;
            _owner = owner;
            _index = index;
            _row = row;
            _column = col;
        }

        public function get label():String { return _label; }
        public function get icon():Object { return _icon; }
        public function get owner():UIComponent { return _owner; }
        /** The item's index in the data provider. */
        public function get index():uint { return _index; }
        public function get row():uint { return _row; }
        public function get column():uint { return _column; }
    }
}
