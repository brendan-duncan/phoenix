package fl.events {
    /** How a DataProvider's items changed, for DataChangeEvent.changeType. */
    public class DataChangeType {
        public static const ADD:String = "add";
        public static const CHANGE:String = "change";
        public static const INVALIDATE:String = "invalidate";
        public static const INVALIDATE_ALL:String = "invalidateAll";
        public static const REMOVE:String = "remove";
        public static const REMOVE_ALL:String = "removeAll";
        public static const REPLACE:String = "replace";
        public static const SORT:String = "sort";
    }
}
