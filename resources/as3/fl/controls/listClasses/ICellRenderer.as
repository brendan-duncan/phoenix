package fl.controls.listClasses {
    /**
     * What a list needs of the objects that show its items (the
     * cellRenderer style): placed, sized, given its item and its list data,
     * selected or not.
     */
    public interface ICellRenderer {
        function set y(y:Number):void;
        function set x(x:Number):void;
        function setSize(width:Number, height:Number):void;
        function get listData():ListData;
        function set listData(value:ListData):void;
        function get data():Object;
        function set data(value:Object):void;
        function get selected():Boolean;
        function set selected(value:Boolean):void;
        function setMouseState(state:String):void;
    }
}
