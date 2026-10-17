package fl.managers {
    import fl.controls.Button;
    import flash.display.InteractiveObject;

    /** What a focus manager does; FocusManager is the one there is. */
    public interface IFocusManager {
        function get defaultButton():Button;
        function set defaultButton(value:Button):void;
        function get defaultButtonEnabled():Boolean;
        function set defaultButtonEnabled(value:Boolean):void;
        function get nextTabIndex():int;
        function get showFocusIndicator():Boolean;
        function set showFocusIndicator(value:Boolean):void;
        function getFocus():InteractiveObject;
        function setFocus(component:InteractiveObject):void;
        function showFocus():void;
        function hideFocus():void;
        function activate():void;
        function deactivate():void;
        function findFocusManagerComponent(component:InteractiveObject):InteractiveObject;
        function getNextFocusManagerComponent(backward:Boolean = false):InteractiveObject;
    }
}
