package fl.managers {
    /**
     * A component one of a group (radio buttons): Tab goes to the group's
     * selected member only.
     */
    public interface IFocusManagerGroup {
        function get groupName():String;
        function set groupName(value:String):void;
        function get selected():Boolean;
        function set selected(value:Boolean):void;
    }
}
