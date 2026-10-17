package fl.managers {
    /**
     * A component the focus manager may give the focus to, with Tab or a
     * click.
     */
    public interface IFocusManagerComponent {
        function get focusEnabled():Boolean;
        function set focusEnabled(value:Boolean):void;
        function get mouseFocusEnabled():Boolean;
        function get tabEnabled():Boolean;
        function get tabIndex():int;
        function setFocus():void;
        function drawFocus(draw:Boolean):void;
    }
}
