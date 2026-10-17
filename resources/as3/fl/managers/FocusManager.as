package fl.managers {
    import fl.controls.Button;
    import fl.core.UIComponent;
    import flash.display.DisplayObject;
    import flash.display.DisplayObjectContainer;
    import flash.display.InteractiveObject;
    import flash.display.Stage;
    import flash.events.FocusEvent;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.ui.Keyboard;

    /**
     * Moves the focus among the components of a container (the stage, made
     * by the first component placed on it): Tab and Shift+Tab go to the
     * next and previous component (by tabIndex where given, else in display
     * order), a click gives the focus to the component clicked, and Enter
     * clicks the default button. The focus rectangle shows when the focus
     * moved by the keyboard, not by the mouse.
     */
    public class FocusManager implements IFocusManager {
        private var _form:DisplayObjectContainer;
        private var activated:Boolean = false;
        private var _showFocusIndicator:Boolean = true;
        private var _defaultButton:Button = null;
        private var _defaultButtonEnabled:Boolean = true;
        private var lastFocus:InteractiveObject = null;

        public function FocusManager(container:DisplayObjectContainer) {
            _form = container;
            activate();
        }

        public function get form():DisplayObjectContainer {
            return _form;
        }

        public function set form(value:DisplayObjectContainer):void {
            deactivate();
            _form = value;
            activate();
        }

        private function get theStage():Stage {
            if (_form is Stage) return _form as Stage;
            return _form == null ? null : _form.stage;
        }

        public function activate():void {
            if (activated) return;
            var s:Stage = theStage;
            if (s == null) return;
            s.addEventListener(MouseEvent.MOUSE_DOWN, mouseDownHandler, true, 0, true);
            s.addEventListener(KeyboardEvent.KEY_DOWN, keyDownHandler, true, 0, true);
            s.addEventListener(FocusEvent.KEY_FOCUS_CHANGE, keyFocusChangeHandler, false, 0, true);
            s.addEventListener(FocusEvent.FOCUS_IN, focusInHandler, true, 0, true);
            activated = true;
        }

        public function deactivate():void {
            if (!activated) return;
            var s:Stage = theStage;
            if (s != null) {
                s.removeEventListener(MouseEvent.MOUSE_DOWN, mouseDownHandler, true);
                s.removeEventListener(KeyboardEvent.KEY_DOWN, keyDownHandler, true);
                s.removeEventListener(FocusEvent.KEY_FOCUS_CHANGE, keyFocusChangeHandler);
                s.removeEventListener(FocusEvent.FOCUS_IN, focusInHandler, true);
            }
            activated = false;
        }

        public function get defaultButton():Button {
            return _defaultButton;
        }

        public function set defaultButton(value:Button):void {
            if (_defaultButton == value) return;
            if (_defaultButton != null) _defaultButton.emphasized = false;
            _defaultButton = value;
            if (_defaultButton != null) _defaultButton.emphasized = true;
        }

        public function get defaultButtonEnabled():Boolean {
            return _defaultButtonEnabled;
        }

        public function set defaultButtonEnabled(value:Boolean):void {
            _defaultButtonEnabled = value;
        }

        public function get showFocusIndicator():Boolean {
            return _showFocusIndicator;
        }

        public function set showFocusIndicator(value:Boolean):void {
            if (_showFocusIndicator == value) return;
            _showFocusIndicator = value;
            var focused:IFocusManagerComponent = getFocus() as IFocusManagerComponent;
            if (focused != null) focused.drawFocus(value);
        }

        /** One more than the highest tabIndex in the form. */
        public function get nextTabIndex():int {
            var highest:int = 0;
            for each (var o:InteractiveObject in focusableObjects()) {
                if (o.tabIndex > highest) highest = o.tabIndex;
            }
            return highest + 1;
        }

        /** The component with the focus (or the object, if not in a component). */
        public function getFocus():InteractiveObject {
            var s:Stage = theStage;
            if (s == null) return null;
            var focused:InteractiveObject = s.focus;
            if (focused == null) return null;
            return findFocusManagerComponent(focused);
        }

        public function setFocus(component:InteractiveObject):void {
            if (component is IFocusManagerComponent) {
                (component as IFocusManagerComponent).setFocus();
            } else if (theStage != null) {
                theStage.focus = component;
            }
        }

        public function showFocus():void {
            showFocusIndicator = true;
        }

        public function hideFocus():void {
            showFocusIndicator = false;
        }

        /** The focusable component an object is part of, or the object itself. */
        public function findFocusManagerComponent(component:InteractiveObject):InteractiveObject {
            var o:DisplayObject = component;
            while (o != null) {
                if (o is IFocusManagerComponent && (o as IFocusManagerComponent).focusEnabled) {
                    return o as InteractiveObject;
                }
                o = o.parent;
            }
            return component;
        }

        /** The component Tab (or Shift+Tab when backward) goes to from the focused one. */
        public function getNextFocusManagerComponent(backward:Boolean = false):InteractiveObject {
            var list:Array = focusableObjects();
            if (list.length == 0) return null;
            var current:InteractiveObject = getFocus();
            var index:int = current == null ? -1 : list.indexOf(current);
            var count:int = list.length;
            for (var step:int = 1; step <= count; step++) {
                var i:int;
                if (index < 0) {
                    i = backward ? count - step : step - 1;
                } else {
                    i = backward ? (index - step + count * 2) % count : (index + step) % count;
                }
                var candidate:InteractiveObject = list[i];
                // Of a group (radio buttons), only the selected one.
                if (candidate is IFocusManagerGroup && !(candidate as IFocusManagerGroup).selected) {
                    var group:String = (candidate as IFocusManagerGroup).groupName;
                    var other:IFocusManagerGroup = selectedOfGroup(list, group);
                    if (other != null && other != candidate) continue;
                }
                return candidate;
            }
            return null;
        }

        private static function selectedOfGroup(list:Array, group:String):IFocusManagerGroup {
            for each (var o:Object in list) {
                var member:IFocusManagerGroup = o as IFocusManagerGroup;
                if (member != null && member.groupName == group && member.selected) return member;
            }
            return null;
        }

        // The components Tab may go to, in tab order: those with a tabIndex
        // first, by it, then the rest in display order.
        private function focusableObjects():Array {
            var found:Array = [];
            if (_form != null) collect(_form, found);
            var indexed:Array = [];
            var rest:Array = [];
            for each (var o:InteractiveObject in found) {
                if (o.tabIndex > 0) indexed.push(o);
                else rest.push(o);
            }
            indexed.sortOn("tabIndex", Array.NUMERIC);
            return indexed.concat(rest);
        }

        private function collect(container:DisplayObjectContainer, found:Array):void {
            for (var i:int = 0; i < container.numChildren; i++) {
                var child:DisplayObject = container.getChildAt(i);
                if (child == null || !child.visible) continue;
                if (child is IFocusManagerComponent) {
                    var component:IFocusManagerComponent = child as IFocusManagerComponent;
                    var enabled:Boolean = !(child is UIComponent) || (child as UIComponent).enabled;
                    if (component.focusEnabled && component.tabEnabled && enabled) {
                        found.push(child);
                    }
                    continue;
                }
                var inner:DisplayObjectContainer = child as DisplayObjectContainer;
                if (inner != null && inner.tabChildren) collect(inner, found);
            }
        }

        private function mouseDownHandler(event:MouseEvent):void {
            var target:InteractiveObject = event.target as InteractiveObject;
            if (target == null) return;
            var component:InteractiveObject = findFocusManagerComponent(target);
            var focusable:IFocusManagerComponent = component as IFocusManagerComponent;
            if (focusable == null || !focusable.focusEnabled || !focusable.mouseFocusEnabled) return;
            if (component is UIComponent && !(component as UIComponent).enabled) return;
            _showFocusIndicator = false;
            if (getFocus() != component) setFocus(component);
        }

        private function keyDownHandler(event:KeyboardEvent):void {
            if (event.keyCode == Keyboard.TAB) {
                var next:InteractiveObject = getNextFocusManagerComponent(event.shiftKey);
                _showFocusIndicator = true;
                if (next != null) {
                    setFocus(next);
                    var focusable:IFocusManagerComponent = next as IFocusManagerComponent;
                    if (focusable != null) focusable.drawFocus(true);
                }
                return;
            }
            if (event.keyCode == Keyboard.ENTER && _defaultButton != null && _defaultButtonEnabled &&
                _defaultButton.enabled && _defaultButton.stage != null) {
                _defaultButton.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            }
        }

        // The player's own Tab handling is replaced by keyDownHandler's.
        private function keyFocusChangeHandler(event:FocusEvent):void {
            if (event.keyCode == Keyboard.TAB || event.keyCode == 0) {
                event.preventDefault();
            }
        }

        private function focusInHandler(event:FocusEvent):void {
            var target:InteractiveObject = event.target as InteractiveObject;
            if (target != null) lastFocus = findFocusManagerComponent(target);
        }
    }
}
