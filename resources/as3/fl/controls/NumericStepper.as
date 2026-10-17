package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ComponentEvent;
    import fl.managers.IFocusManager;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;
    import flash.events.Event;
    import flash.events.FocusEvent;
    import flash.events.KeyboardEvent;
    import flash.text.TextField;
    import flash.ui.Keyboard;

    /** Dispatched when the user changes the value (not when it is set in code). */
    [Event(name="change", type="flash.events.Event")]

    /**
     * A number with arrows to step it up and down: typed in, stepped by the
     * arrows (held, they repeat) or the arrow keys, always a multiple of
     * stepSize between minimum and maximum. 80 by 22 by default; minimum 0,
     * maximum 10, stepSize 1, value 1.
     *
     * Setting value in code dispatches nothing; the user changing it
     * dispatches Event.CHANGE.
     */
    public class NumericStepper extends UIComponent implements IFocusManagerComponent {
        protected var inputField:TextInput;
        protected var upArrow:BaseButton;
        protected var downArrow:BaseButton;
        protected var _maximum:Number = 10;
        protected var _minimum:Number = 0;
        protected var _value:Number = 1;
        protected var _stepSize:Number = 1;
        protected var _precision:Number = 0;

        private static var defaultStyles:Object = {
            downArrowDisabledSkin: "NumericStepperDownArrow_disabledSkin",
            downArrowDownSkin: "NumericStepperDownArrow_downSkin",
            downArrowOverSkin: "NumericStepperDownArrow_overSkin",
            downArrowUpSkin: "NumericStepperDownArrow_upSkin",
            upArrowDisabledSkin: "NumericStepperUpArrow_disabledSkin",
            upArrowDownSkin: "NumericStepperUpArrow_downSkin",
            upArrowOverSkin: "NumericStepperUpArrow_overSkin",
            upArrowUpSkin: "NumericStepperUpArrow_upSkin",
            upSkin: "TextInput_upSkin",
            disabledSkin: "TextInput_disabledSkin",
            textFormat: null,
            disabledTextFormat: null,
            textPadding: 3,
            embedFonts: false,
            focusRectSkin: null,
            focusRectPadding: null,
            repeatDelay: 500,
            repeatInterval: 35
        };

        protected static const DOWN_ARROW_STYLES:Object = {
            disabledSkin: "downArrowDisabledSkin",
            downSkin: "downArrowDownSkin",
            overSkin: "downArrowOverSkin",
            upSkin: "downArrowUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };

        protected static const UP_ARROW_STYLES:Object = {
            disabledSkin: "upArrowDisabledSkin",
            downSkin: "upArrowDownSkin",
            overSkin: "upArrowOverSkin",
            upSkin: "upArrowUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };

        protected static const TEXT_INPUT_STYLES:Object = {
            upSkin: "upSkin",
            disabledSkin: "disabledSkin",
            textPadding: "textPadding",
            textFormat: "textFormat",
            disabledTextFormat: "disabledTextFormat",
            embedFonts: "embedFonts"
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function NumericStepper() {
            super();
            setStyles();
            stepSize = _stepSize;
        }

        override protected function defaultSize():Array {
            return [80, 22];
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            if (value == enabled) return;
            super.enabled = value;
            upArrow.enabled = downArrow.enabled = inputField.enabled = value;
        }

        public function get maximum():Number {
            return _maximum;
        }

        public function set maximum(value:Number):void {
            _maximum = value;
            if (_value > _maximum) {
                setValue(_maximum, false);
            }
        }

        public function get minimum():Number {
            return _minimum;
        }

        public function set minimum(value:Number):void {
            _minimum = value;
            if (_value < _minimum) {
                setValue(_minimum, false);
            }
        }

        /** The value one step up (or the maximum). */
        public function get nextValue():Number {
            var next:Number = _value + _stepSize;
            return inRange(next) ? next : _value;
        }

        /** The value one step down (or the minimum). */
        public function get previousValue():Number {
            var previous:Number = _value - _stepSize;
            return inRange(previous) ? previous : _value;
        }

        /** The steps the value goes in; values are multiples of it. */
        public function get stepSize():Number {
            return _stepSize;
        }

        public function set stepSize(value:Number):void {
            _stepSize = value;
            _precision = getPrecision();
            setValue(_value);
        }

        public function get value():Number {
            return _value;
        }

        public function set value(value:Number):void {
            setValue(value, false);
        }

        /** The text field the number is typed in. */
        public function get textField():TextInput {
            return inputField;
        }

        override public function get imeMode():String {
            return inputField.imeMode;
        }

        override public function set imeMode(value:String):void {
            inputField.imeMode = value;
        }

        override protected function configUI():void {
            super.configUI();
            upArrow = new BaseButton();
            copyStylesToChild(upArrow, UP_ARROW_STYLES);
            upArrow.autoRepeat = true;
            upArrow.setSize(21, 12);
            upArrow.focusEnabled = false;
            addChild(upArrow);

            downArrow = new BaseButton();
            copyStylesToChild(downArrow, DOWN_ARROW_STYLES);
            downArrow.autoRepeat = true;
            downArrow.setSize(21, 12);
            downArrow.focusEnabled = false;
            addChild(downArrow);

            inputField = new TextInput();
            copyStylesToChild(inputField, TEXT_INPUT_STYLES);
            inputField.restrict = "0-9\\-\\.\\,";
            inputField.text = String(_value);
            inputField.setSize(21, 24);
            inputField.focusTarget = this as IFocusManagerComponent;
            inputField.focusEnabled = false;
            addChild(inputField);

            inputField.addEventListener(Event.CHANGE, onTextChange, false, 0, true);
            upArrow.addEventListener(ComponentEvent.BUTTON_DOWN, stepperPressHandler, false, 0, true);
            downArrow.addEventListener(ComponentEvent.BUTTON_DOWN, stepperPressHandler, false, 0, true);
        }

        /** Sets the value (made valid); dispatches CHANGE when it changed and fireEvent is set. */
        protected function setValue(value:Number, fireEvent:Boolean = true):void {
            if (value == _value) return;
            var old:Number = _value;
            _value = getValidValue(value);
            if (old != _value && fireEvent) {
                dispatchEvent(new Event(Event.CHANGE));
            }
            invalidate(InvalidationType.DATA);
        }

        protected function setStyles():void {
            copyStylesToChild(downArrow, DOWN_ARROW_STYLES);
            copyStylesToChild(upArrow, UP_ARROW_STYLES);
            copyStylesToChild(inputField, TEXT_INPUT_STYLES);
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES)) {
                setStyles();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE)) {
                drawLayout();
            }
            if (isFocused && focusManager != null && focusManager.showFocusIndicator) {
                drawFocus(true);
            }
            validate();
        }

        /** The arrows at the right, the up one over the down one; the field takes the rest. */
        protected function drawLayout():void {
            var arrowWidth:Number = upArrow.width;
            var upHeight:Number = Math.floor(height / 2);
            var downHeight:Number = height - upHeight;
            inputField.setSize(Math.max(0, width - arrowWidth), height);
            upArrow.height = upHeight;
            downArrow.height = downHeight;
            upArrow.move(width - arrowWidth, 0);
            downArrow.move(width - arrowWidth, upHeight);
            inputField.drawNow();
            upArrow.drawNow();
            downArrow.drawNow();
        }

        override public function drawNow():void {
            updateText();
            super.drawNow();
        }

        override protected function validate():void {
            updateText();
            super.validate();
        }

        // Shows the value in the field when it is not being typed.
        private function updateText():void {
            if (inputField != null && Number(inputField.text) != _value) {
                inputField.text = String(_value);
            }
        }

        override protected function focusOutHandler(event:FocusEvent):void {
            // The field inside lost the focus: what was typed is the value.
            if (event.target != this) {
                setValue(Number(inputField.text));
            }
            super.focusOutHandler(event);
        }

        protected function onTextChange(event:Event):void {
            event.stopImmediatePropagation();
        }

        override public function setFocus():void {
            if (stage != null) {
                stage.focus = inputField.textField;
            }
        }

        override protected function isOurFocus(target:DisplayObject):Boolean {
            return target == inputField || target == inputField.textField || super.isOurFocus(target);
        }

        protected function stepperPressHandler(event:ComponentEvent):void {
            setValue(Number(inputField.text), false);
            switch (event.currentTarget) {
                case upArrow:
                    setValue(nextValue);
                    break;
                case downArrow:
                    setValue(previousValue);
                    break;
            }
            inputField.setFocus();
            inputField.textField.setSelection(0, 0);
        }

        override protected function keyDownHandler(event:KeyboardEvent):void {
            if (!enabled) return;
            event.stopImmediatePropagation();
            var typed:Number = Number(inputField.text);
            switch (event.keyCode) {
                case Keyboard.END:
                    setValue(maximum);
                    break;
                case Keyboard.HOME:
                    setValue(minimum);
                    break;
                case Keyboard.UP:
                    setValue(nextValue);
                    break;
                case Keyboard.DOWN:
                    setValue(previousValue);
                    break;
                case Keyboard.ENTER:
                    setValue(typed);
                    break;
            }
        }

        protected function inRange(num:Number):Boolean {
            return num >= _minimum && num <= _maximum;
        }

        /** As close a multiple of stepSize as there is, kept in range; the value as is for NaN. */
        protected function getValidValue(num:Number):Number {
            if (isNaN(num)) return _value;
            var closest:Number = Number((_stepSize * Math.round(num / _stepSize)).toFixed(_precision));
            if (closest > maximum) return maximum;
            if (closest < minimum) return minimum;
            return closest;
        }

        // The digits after the point the step size has.
        protected function getPrecision():Number {
            var text:String = String(_stepSize);
            var point:int = text.indexOf(".");
            return point == -1 ? 0 : text.length - point - 1;
        }
    }
}
