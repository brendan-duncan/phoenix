package fl.data {
    /**
     * An item of a list's dataProvider as a component parameter gives it:
     * a label and data.
     */
    public dynamic class SimpleCollectionItem {
        [Inspectable()]
        public var label:String;

        [Inspectable()]
        public var data:String;

        public function SimpleCollectionItem() {}

        public function toString():String {
            return "[SimpleCollectionItem: " + label + "," + data + "]";
        }
    }
}
