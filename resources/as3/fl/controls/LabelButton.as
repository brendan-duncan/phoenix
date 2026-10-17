package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ComponentEvent;
    import fl.managers.IFocusManager;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.text.TextField;
    import flash.text.TextFieldType;
    import flash.text.TextFormat;
    import flash.ui.Keyboard;

    /** Dispatched when the label changes. */
    [Event(name="labelChange", type="fl.events.ComponentEvent")]
    /** Dispatched when a toggle button is clicked and its selected state changes. */
    [Event(name="change", type="flash.events.Event")]
    /** Dispatched when clicked. */
    [Event(name="click", type="flash.events.MouseEvent")]

    /**
     * A button with a label and an optional icon: the icon is a style for
     * each state (upIcon, selectedOverIcon, ... or icon for all), a class, a
     * class name or a display object. A toggle button flips selected when
     * clicked. The space bar presses it when it has the focus.
     */
    public class LabelButton extends BaseButton implements IFocusManagerComponent {
        /** The label's text field. */
        public var textField:TextField;

        protected var _labelPlacement:String = ButtonLabelPlacement.RIGHT;
        protected var _toggle:Boolean = false;
        protected var icon:DisplayObject;
        protected var oldMouseState:String;
        protected var _label:String = "Label";
        protected var mode:String = "center";

        private static var defaultStyles:Object = {
            icon: null,
            upIcon: null,
            downIcon: null,
            overIcon: null,
            disabledIcon: null,
            selectedDisabledIcon: null,
            selectedUpIcon: null,
            selectedDownIcon: null,
            selectedOverIcon: null,
            textFormat: null,
            disabledTextFormat: null,
            textPadding: 5,
            embedFonts: false
        };

        public static function getStyleDefinition():Object {
            return mergeStyles(defaultStyles, BaseButton.getStyleDefinition());
        }

        public function LabelButton() {
            super();
        }

        /** The text shown. */
        public function get label():String {
            return _label;
        }

        public function set label(value:String):void {
            _label = value == null ? "" : value;
            if (textField.text != _label) {
                textField.text = _label;
                dispatchEvent(new ComponentEvent(ComponentEvent.LABEL_CHANGE));
            }
            invalidate(InvalidationType.SIZE);
            invalidate(InvalidationType.STYLES);
        }

        /** Where the label is, next to the icon: ButtonLabelPlacement.RIGHT (the default), LEFT, TOP or BOTTOM. */
        public function get labelPlacement():String {
            return _labelPlacement;
        }

        public function set labelPlacement(value:String):void {
            _labelPlacement = value;
            invalidate(InvalidationType.SIZE);
        }

        /** Whether a click flips selected. */
        public function get toggle():Boolean {
            return _toggle;
        }

        public function set toggle(value:Boolean):void {
            if (!value && super.selected) {
                selected = false;
            }
            _toggle = value;
            if (_toggle) {
                addEventListener(MouseEvent.CLICK, toggleSelected, false, 0, true);
            } else {
                removeEventListener(MouseEvent.CLICK, toggleSelected);
            }
            invalidate(InvalidationType.STATE);
        }

        protected function toggleSelected(event:MouseEvent):void {
            selected = !selected;
            dispatchEvent(new Event(Event.CHANGE, true));
        }

        /** Selected, for a toggle button; always false for another. */
        override public function get selected():Boolean {
            return _toggle ? _selected : false;
        }

        override public function set selected(value:Boolean):void {
            _selected = value;
            if (_toggle) {
                invalidate(InvalidationType.STATE);
            }
        }

        override protected function configUI():void {
            super.configUI();
            textField = new TextField();
            textField.type = TextFieldType.DYNAMIC;
            textField.selectable = false;
            textField.text = _label;
            addChild(textField);
        }

        override protected function draw():void {
            if (textField.text != _label) {
                label = _label;
            }
            if (isInvalid(InvalidationType.STYLES, InvalidationType.STATE)) {
                drawBackground();
                drawIcon();
                drawTextFormat();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE)) {
                drawLayout();
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.STYLES)) {
                if (isFocused) {
                    var fm:IFocusManager = focusManager;
                    if (fm != null && fm.showFocusIndicator) drawFocus(true);
                }
            }
            validate();
        }

        protected function drawIcon():void {
            var oldIcon:DisplayObject = icon;
            var iconStyle:Object = getStyleValue(stateStyleName("Icon"));
            if (iconStyle == null) {
                iconStyle = getStyleValue("icon");
            }
            icon = iconStyle == null ? null : getDisplayObjectInstance(iconStyle);
            if (icon != null) {
                addChildAt(icon, Math.min(1, numChildren));
            }
            if (oldIcon != null && oldIcon != icon && oldIcon.parent == this) {
                removeChild(oldIcon);
            }
        }

        protected function drawTextFormat():void {
            var uiStyles:Object = UIComponent.getStyleDefinition();
            var defaultFormat:TextFormat = (enabled ? uiStyles.defaultTextFormat : uiStyles.defaultDisabledTextFormat) as TextFormat;
            textField.setTextFormat(defaultFormat);
            var format:TextFormat = getStyleValue(enabled ? "textFormat" : "disabledTextFormat") as TextFormat;
            if (format != null) {
                textField.setTextFormat(format);
            } else {
                format = defaultFormat;
            }
            textField.defaultTextFormat = format;
            setEmbedFont();
        }

        protected function setEmbedFont():void {
            var embed:Object = getStyleValue("embedFonts");
            if (embed != null) {
                setNativeProperty(textField, "embedFonts", Boolean(embed));
            }
        }

        /**
         * Lays out the label and icon: side by side (label to the right or
         * left of the icon) or one over the other, centered in the button,
         * the label cut to the room there is.
         */
        override protected function drawLayout():void {
            var padding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(padding)) padding = 0;
            var placement:String = (icon == null && mode == "center") ? ButtonLabelPlacement.TOP : _labelPlacement;
            textField.height = textField.textHeight + 4;
            var textWidth:Number = textField.textWidth + 4;
            var textHeight:Number = textField.textHeight + 4;
            var iconWidth:Number = icon == null ? 0 : icon.width + padding;
            var iconHeight:Number = icon == null ? 0 : icon.height + padding;
            textField.visible = label.length > 0;

            if (icon != null) {
                icon.x = Math.round((width - icon.width) / 2);
                icon.y = Math.round((height - icon.height) / 2);
            }

            if (!textField.visible) {
                textField.width = 0;
                textField.height = 0;
            } else if (placement == ButtonLabelPlacement.BOTTOM || placement == ButtonLabelPlacement.TOP) {
                textWidth = Math.max(0, Math.min(textWidth, width - 2 * padding));
                textHeight = height - 2 > textHeight ? textHeight : height - 2;
                textField.width = textWidth;
                textField.height = textHeight;
                textField.x = Math.round((width - textWidth) / 2);
                textField.y = Math.round((height - textField.height - iconHeight) / 2 +
                                         (placement == ButtonLabelPlacement.BOTTOM ? iconHeight : 0));
                if (icon != null) {
                    icon.y = Math.round(placement == ButtonLabelPlacement.BOTTOM ? textField.y - iconHeight
                                                                                 : textField.y + textField.height + padding);
                }
            } else {
                textWidth = Math.max(0, Math.min(textWidth, width - iconWidth - 2 * padding));
                textField.width = textWidth;
                textField.x = Math.round((width - textWidth - iconWidth) / 2 +
                                         (placement != ButtonLabelPlacement.LEFT ? iconWidth : 0));
                textField.y = Math.round((height - textField.height) / 2);
                if (icon != null) {
                    icon.x = Math.round(placement != ButtonLabelPlacement.LEFT ? textField.x - iconWidth
                                                                              : textField.x + textWidth + padding);
                }
            }
            super.drawLayout();
        }

        override protected function keyDownHandler(event:KeyboardEvent):void {
            if (!enabled) return;
            if (event.keyCode == Keyboard.SPACE) {
                if (oldMouseState == null) {
                    oldMouseState = mouseState;
                }
                setMouseState("down");
                startPress();
            }
        }

        override protected function keyUpHandler(event:KeyboardEvent):void {
            if (!enabled) return;
            if (event.keyCode == Keyboard.SPACE) {
                setMouseState(oldMouseState == null ? "up" : oldMouseState);
                oldMouseState = null;
                endPress();
                dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            }
        }
    }
}
