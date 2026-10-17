package fl.controls {
    import flash.events.Event;
    import flash.events.EventDispatcher;

    /** Dispatched when the selection changes. */
    [Event(name="change", type="flash.events.Event")]
    /** Dispatched when one of its buttons is clicked. */
    [Event(name="click", type="flash.events.MouseEvent")]

    /**
     * The radio buttons of one name: at most one selected. Found (and made
     * on first use) by name with getGroup.
     */
    public class RadioButtonGroup extends EventDispatcher {
        private static var groups:Object = {};
        private static var groupCount:uint = 0;

        protected var _name:String;
        protected var radioButtons:Array;
        protected var _selection:RadioButton;

        /** The group of a name, made if there is none. */
        public static function getGroup(name:String):RadioButtonGroup {
            var group:RadioButtonGroup = groups[name];
            if (group == null) {
                group = new RadioButtonGroup(name);
            }
            return group;
        }

        public function RadioButtonGroup(name:String) {
            super();
            _name = name;
            radioButtons = [];
            groups[name] = this;
            groupCount++;
        }

        public function get name():String {
            return _name;
        }

        public function addRadioButton(radioButton:RadioButton):void {
            if (radioButton.groupName != _name && radioButton.groupName != null) {
                radioButton.groupName = _name;
                return;
            }
            if (radioButtons.indexOf(radioButton) >= 0) return;
            radioButtons.push(radioButton);
            if (radioButton.selected) {
                selection = radioButton;
            }
        }

        public function removeRadioButton(radioButton:RadioButton):void {
            var index:int = radioButtons.indexOf(radioButton);
            if (index >= 0) {
                radioButtons.splice(index, 1);
            }
            if (_selection == radioButton) {
                _selection = null;
            }
        }

        /** The selected button, or null. */
        public function get selection():RadioButton {
            return _selection;
        }

        public function set selection(value:RadioButton):void {
            if (_selection == value) return;
            if (value != null && radioButtons.indexOf(value) < 0) return;
            if (_selection != null) {
                _selection.setSelectedFromGroup(false);
            }
            _selection = value;
            if (_selection != null) {
                _selection.setSelectedFromGroup(true);
            }
            dispatchEvent(new Event(Event.CHANGE));
        }

        /** The selected button's value (or label when it has none). */
        public function get selectedData():Object {
            if (_selection == null) return null;
            return _selection.value != null ? _selection.value : _selection.label;
        }

        public function set selectedData(value:Object):void {
            for each (var button:RadioButton in radioButtons) {
                if (button.value == value || button.label == value) {
                    selection = button;
                    return;
                }
            }
        }

        public function getRadioButtonIndex(radioButton:RadioButton):int {
            return radioButtons.indexOf(radioButton);
        }

        public function getRadioButtonAt(index:int):RadioButton {
            return radioButtons[index];
        }

        public function get numRadioButtons():int {
            return radioButtons.length;
        }
    }
}
