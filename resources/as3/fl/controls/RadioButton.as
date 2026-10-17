package fl.controls {
    import fl.managers.IFocusManagerGroup;
    import flash.display.Graphics;
    import flash.display.Shape;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.ui.Keyboard;

    /**
     * One of a group of choices: selecting it clears the others of its
     * group (RadioButtonGroup, by groupName). A click selects it; clicking
     * it again leaves it selected. Arrow keys move the selection along the
     * group.
     */
    public class RadioButton extends LabelButton implements IFocusManagerGroup {
        protected var _group:RadioButtonGroup;
        protected var _value:Object;
        protected var defaultGroupName:String = "RadioButtonGroup";

        private static var defaultStyles:Object = {
            icon: null,
            upIcon: "RadioButton_upIcon",
            downIcon: "RadioButton_downIcon",
            overIcon: "RadioButton_overIcon",
            disabledIcon: "RadioButton_disabledIcon",
            selectedDisabledIcon: "RadioButton_selectedDisabledIcon",
            selectedUpIcon: "RadioButton_selectedUpIcon",
            selectedDownIcon: "RadioButton_selectedDownIcon",
            selectedOverIcon: "RadioButton_selectedOverIcon",
            focusRectSkin: null,
            focusRectPadding: null,
            textFormat: null,
            disabledTextFormat: null,
            embedFonts: null,
            textPadding: 5
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function RadioButton() {
            super();
            mode = "border";
            groupName = defaultGroupName;
        }

        override public function get toggle():Boolean {
            return true;
        }

        override public function set toggle(value:Boolean):void {
            throw new Error("Warning: You cannot change a RadioButton's toggle.");
        }

        override public function get autoRepeat():Boolean {
            return false;
        }

        override public function set autoRepeat(value:Boolean):void {}

        /** The name of its group; buttons of one name are one group. */
        public function get groupName():String {
            return _group == null ? null : _group.name;
        }

        public function set groupName(value:String):void {
            if (_group != null) {
                _group.removeRadioButton(this);
                _group.removeEventListener(Event.CHANGE, handleChange);
            }
            _group = value == null ? null : RadioButtonGroup.getGroup(value);
            if (_group != null) {
                _group.addRadioButton(this);
                _group.addEventListener(Event.CHANGE, handleChange, false, 0, true);
            }
        }

        public function get group():RadioButtonGroup {
            return _group;
        }

        public function set group(value:RadioButtonGroup):void {
            groupName = value == null ? null : value.name;
        }

        /** Data that goes with the choice (RadioButtonGroup.selectedData). */
        public function get value():Object {
            return _value;
        }

        public function set value(value:Object):void {
            _value = value;
        }

        override public function get selected():Boolean {
            return super.selected;
        }

        override public function set selected(value:Boolean):void {
            if (_group == null) {
                super.selected = value;
            } else if (value) {
                _group.selection = this;
            } else if (_group.selection == this) {
                _group.selection = null;
            }
        }

        /** Called by the group as its selection changes. */
        internal function setSelectedFromGroup(value:Boolean):void {
            super.selected = value;
        }

        override protected function configUI():void {
            super.configUI();
            super.toggle = true;
            var hit:Shape = new Shape();
            var g:Graphics = hit.graphics;
            g.beginFill(0, 0);
            g.drawRect(0, 0, 100, 100);
            g.endFill();
            background = hit;
            addChildAt(background, 0);
            addEventListener(MouseEvent.CLICK, handleClick, false, 0, true);
        }

        override protected function drawBackground():void {}

        // A click selects (the toggle would also clear it).
        override protected function toggleSelected(event:MouseEvent):void {}

        private function handleClick(event:MouseEvent):void {
            if (_group == null) return;
            selected = true;
            _group.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
        }

        private function handleChange(event:Event):void {
            dispatchEvent(new Event(Event.CHANGE, true));
        }

        override protected function drawLayout():void {
            super.drawLayout();
            var padding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(padding)) padding = 0;
            var iconWidth:Number = icon == null ? 0 : icon.width;
            var iconHeight:Number = icon == null ? 0 : icon.height;
            var iconX:Number = 0;
            var iconY:Number = icon == null ? Math.round(height / 2) : icon.y;
            switch (_labelPlacement) {
                case ButtonLabelPlacement.RIGHT:
                    iconX = padding;
                    textField.x = iconX + iconWidth + padding;
                    background.width = textField.x + textField.width + padding;
                    background.height = Math.max(textField.height, iconHeight) + padding * 2;
                    break;
                case ButtonLabelPlacement.LEFT:
                    iconX = width - iconWidth - padding;
                    textField.x = width - iconWidth - padding * 2 - textField.width;
                    background.width = textField.width + iconWidth + padding * 3;
                    background.height = Math.max(textField.height, iconHeight) + padding * 2;
                    break;
                default:
                    iconX = icon == null ? Math.round(width / 2) : icon.x;
                    background.width = Math.max(textField.width, iconWidth) + padding * 2;
                    background.height = textField.height + iconHeight + padding * 3;
                    break;
            }
            if (icon != null) {
                icon.x = iconX;
            }
            background.x = Math.min(iconX - padding, textField.x - padding);
            background.y = Math.min(iconY - padding, textField.y - padding);
        }

        override protected function keyDownHandler(event:KeyboardEvent):void {
            switch (event.keyCode) {
                case Keyboard.DOWN:
                case Keyboard.RIGHT:
                    moveSelection(1);
                    break;
                case Keyboard.UP:
                case Keyboard.LEFT:
                    moveSelection(-1);
                    break;
                default:
                    super.keyDownHandler(event);
                    break;
            }
        }

        private function moveSelection(step:int):void {
            if (_group == null) return;
            var count:int = _group.numRadioButtons;
            var index:int = _group.getRadioButtonIndex(this);
            for (var i:int = 1; i < count; i++) {
                var next:RadioButton = _group.getRadioButtonAt((index + step * i + count * 2) % count);
                if (next.enabled) {
                    next.setFocus();
                    next.selected = true;
                    next.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
                    return;
                }
            }
        }
    }
}
