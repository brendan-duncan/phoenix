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
    import flash.events.TextEvent;
    import flash.text.TextField;
    import flash.text.TextFieldType;
    import flash.text.TextFormat;
    import flash.text.TextLineMetrics;
    import flash.ui.Keyboard;

    /** Dispatched when the user changes the text. */
    [Event(name="change", type="flash.events.Event")]
    /** Dispatched when the user presses Enter. */
    [Event(name="enter", type="fl.events.ComponentEvent")]
    /** Dispatched when the user types, deletes or pastes text. */
    [Event(name="textInput", type="flash.events.TextEvent")]

    /**
     * A one-line text field with a border (upSkin "TextInput_upSkin",
     * disabledSkin "TextInput_disabledSkin"), editable or not.
     */
    public class TextInput extends UIComponent implements IFocusManagerComponent {
        /** The text field inside. */
        public var textField:TextField;

        protected var _editable:Boolean = true;
        protected var background:DisplayObject;
        protected var _html:Boolean = false;
        protected var _savedHTML:String;

        private static var defaultStyles:Object = {
            upSkin: "TextInput_upSkin",
            disabledSkin: "TextInput_disabledSkin",
            focusRectSkin: null,
            focusRectPadding: null,
            textFormat: null,
            disabledTextFormat: null,
            textPadding: 0,
            embedFonts: false
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function TextInput() {
            super();
        }

        override protected function defaultSize():Array {
            return [100, 22];
        }

        public function get text():String {
            return textField.text;
        }

        public function set text(value:String):void {
            textField.text = value == null ? "" : value;
            _html = false;
            invalidate(InvalidationType.DATA);
            invalidate(InvalidationType.STYLES);
        }

        public function get htmlText():String {
            return textField.htmlText;
        }

        public function set htmlText(value:String):void {
            if (value == "") {
                text = "";
                return;
            }
            _html = true;
            _savedHTML = value;
            textField.htmlText = value;
            invalidate(InvalidationType.DATA);
            invalidate(InvalidationType.STYLES);
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            updateTextFieldType();
        }

        public function get editable():Boolean {
            return _editable;
        }

        public function set editable(value:Boolean):void {
            _editable = value;
            updateTextFieldType();
        }

        public function get condenseWhite():Boolean {
            return textField.condenseWhite;
        }

        public function set condenseWhite(value:Boolean):void {
            textField.condenseWhite = value;
        }

        public function get horizontalScrollPosition():int {
            return textField.scrollH;
        }

        public function set horizontalScrollPosition(value:int):void {
            textField.scrollH = value;
        }

        public function get maxHorizontalScrollPosition():int {
            return textField.maxScrollH;
        }

        public function get length():int {
            return textField.length;
        }

        public function get maxChars():int {
            return textField.maxChars;
        }

        public function set maxChars(value:int):void {
            textField.maxChars = value;
        }

        public function get displayAsPassword():Boolean {
            return textField.displayAsPassword;
        }

        public function set displayAsPassword(value:Boolean):void {
            textField.displayAsPassword = value;
        }

        public function get restrict():String {
            return textField.restrict;
        }

        public function set restrict(value:String):void {
            textField.restrict = value;
        }

        public function get selectionBeginIndex():int {
            return textField.selectionBeginIndex;
        }

        public function get selectionEndIndex():int {
            return textField.selectionEndIndex;
        }

        public function get alwaysShowSelection():Boolean {
            return textField.alwaysShowSelection;
        }

        public function set alwaysShowSelection(value:Boolean):void {
            textField.alwaysShowSelection = value;
        }

        public function get textWidth():Number {
            return textField.textWidth;
        }

        public function get textHeight():Number {
            return textField.textHeight;
        }

        public function setSelection(beginIndex:int, endIndex:int):void {
            textField.setSelection(beginIndex, endIndex);
        }

        public function appendText(text:String):void {
            textField.appendText(text);
        }

        public function getLineMetrics(index:int):TextLineMetrics {
            return textField.getLineMetrics(index);
        }

        override public function drawFocus(draw:Boolean):void {
            if (focusTarget != null) {
                focusTarget.drawFocus(draw);
                return;
            }
            super.drawFocus(draw);
        }

        override public function setFocus():void {
            if (stage != null) {
                stage.focus = textField;
            }
        }

        override protected function isOurFocus(target:DisplayObject):Boolean {
            return target == textField || super.isOurFocus(target);
        }

        protected function updateTextFieldType():void {
            textField.type = (enabled && editable) ? TextFieldType.INPUT : TextFieldType.DYNAMIC;
            textField.selectable = enabled;
        }

        protected function handleKeyDown(event:KeyboardEvent):void {
            if (event.keyCode == Keyboard.ENTER) {
                dispatchEvent(new ComponentEvent(ComponentEvent.ENTER, true));
            }
        }

        protected function handleChange(event:Event):void {
            event.stopPropagation();
            dispatchEvent(new Event(Event.CHANGE, true));
        }

        protected function handleTextInput(event:TextEvent):void {
            event.stopPropagation();
            dispatchEvent(new TextEvent(TextEvent.TEXT_INPUT, true, false, event.text));
        }

        protected function setEmbedFont():void {
            var embed:Object = getStyleValue("embedFonts");
            if (embed != null) {
                setNativeProperty(textField, "embedFonts", Boolean(embed));
            }
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES, InvalidationType.STATE)) {
                drawTextFormat();
                drawBackground();
                setEmbedFont();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.DATA)) {
                drawLayout();
            }
            super.draw();
        }

        protected function drawBackground():void {
            var old:DisplayObject = background;
            var made:DisplayObject = getDisplayObjectInstance(getStyleValue(enabled ? "upSkin" : "disabledSkin"));
            background = made;
            if (background != null) {
                addChildAt(background, 0);
            }
            if (old != null && old != background && old.parent == this) {
                removeChild(old);
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
            if (_html) {
                textField.htmlText = _savedHTML;
            }
        }

        /** The text field fills the box less the padding; one line high, in the middle. */
        protected function drawLayout():void {
            var padding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(padding)) padding = 0;
            if (background != null) {
                background.width = width;
                background.height = height;
            }
            textField.width = Math.max(0, width - 2 * padding);
            var lineHeight:Number = textField.textHeight > 0 ? textField.textHeight + 4 : lineHeightOf(textField);
            textField.height = Math.min(Math.max(lineHeight, 0), Math.max(0, height - 2 * padding));
            textField.x = padding;
            textField.y = Math.round((height - textField.height) / 2);
        }

        // The height of a line of the field's format, with the gutter.
        private static function lineHeightOf(field:TextField):Number {
            var format:TextFormat = field.defaultTextFormat;
            var size:Number = format == null || format.size == null ? 12 : Number(format.size);
            return Math.ceil(size * 1.15) + 4;
        }

        override protected function configUI():void {
            super.configUI();
            tabChildren = true;
            textField = new TextField();
            addChild(textField);
            updateTextFieldType();
            textField.addEventListener(TextEvent.TEXT_INPUT, handleTextInput, false, 0, true);
            textField.addEventListener(Event.CHANGE, handleChange, false, 0, true);
            textField.addEventListener(KeyboardEvent.KEY_DOWN, handleKeyDown, false, 0, true);
        }

        override protected function focusInHandler(event:FocusEvent):void {
            if (event.target == this && stage != null) {
                stage.focus = textField;
            }
            var fm:IFocusManager = focusManager;
            if (editable && fm != null) {
                fm.showFocusIndicator = true;
            }
            super.focusInHandler(event);
        }
    }
}
