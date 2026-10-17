package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ComponentEvent;
    import fl.events.ScrollEvent;
    import fl.managers.IFocusManager;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;
    import flash.events.Event;
    import flash.events.FocusEvent;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.events.TextEvent;
    import flash.text.TextField;
    import flash.text.TextFieldType;
    import flash.text.TextFormat;
    import flash.text.TextLineMetrics;
    import flash.ui.Keyboard;

    /** Dispatched when the user changes the text. */
    [Event(name="change", type="flash.events.Event")]
    /** Dispatched when the user types, deletes or pastes text. */
    [Event(name="textInput", type="flash.events.TextEvent")]
    /** Dispatched when the user presses Enter. */
    [Event(name="enter", type="fl.events.ComponentEvent")]
    /** Dispatched when the text scrolls. */
    [Event(name="scroll", type="fl.events.ScrollEvent")]

    /**
     * Several lines of text in a border (upSkin "TextArea_upSkin"), with
     * scroll bars as needed (verticalScrollPolicy, horizontalScrollPolicy),
     * editable or not; wordWrap by default. 100 by 100 by default.
     */
    public class TextArea extends UIComponent implements IFocusManagerComponent {
        /** The text field inside. */
        public var textField:TextField;

        protected var _editable:Boolean = true;
        protected var _wordWrap:Boolean = true;
        protected var _horizontalScrollPolicy:String = ScrollPolicy.AUTO;
        protected var _verticalScrollPolicy:String = ScrollPolicy.AUTO;
        protected var _horizontalScrollBar:UIScrollBar;
        protected var _verticalScrollBar:UIScrollBar;
        protected var background:DisplayObject;
        protected var _html:Boolean = false;
        protected var _savedHTML:String;
        protected var textHasChanged:Boolean = false;

        private static var defaultStyles:Object = {
            upSkin: "TextArea_upSkin",
            disabledSkin: "TextArea_disabledSkin",
            focusRectSkin: null,
            focusRectPadding: null,
            textFormat: null,
            disabledTextFormat: null,
            textPadding: 3,
            embedFonts: false
        };

        protected static const SCROLL_BAR_STYLES:Object = {
            downArrowDisabledSkin: "downArrowDisabledSkin",
            downArrowDownSkin: "downArrowDownSkin",
            downArrowOverSkin: "downArrowOverSkin",
            downArrowUpSkin: "downArrowUpSkin",
            upArrowDisabledSkin: "upArrowDisabledSkin",
            upArrowDownSkin: "upArrowDownSkin",
            upArrowOverSkin: "upArrowOverSkin",
            upArrowUpSkin: "upArrowUpSkin",
            thumbDisabledSkin: "thumbDisabledSkin",
            thumbDownSkin: "thumbDownSkin",
            thumbOverSkin: "thumbOverSkin",
            thumbUpSkin: "thumbUpSkin",
            thumbIcon: "thumbIcon",
            trackDisabledSkin: "trackDisabledSkin",
            trackDownSkin: "trackDownSkin",
            trackOverSkin: "trackOverSkin",
            trackUpSkin: "trackUpSkin",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval"
        };

        public static function getStyleDefinition():Object {
            return UIComponent.mergeStyles(defaultStyles, ScrollBar.getStyleDefinition());
        }

        public function TextArea() {
            super();
        }

        override protected function defaultSize():Array {
            return [100, 100];
        }

        public function get horizontalScrollBar():UIScrollBar {
            return _horizontalScrollBar;
        }

        public function get verticalScrollBar():UIScrollBar {
            return _verticalScrollBar;
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            mouseChildren = enabled;
            invalidate(InvalidationType.STATE);
        }

        public function get text():String {
            return textField.text;
        }

        public function set text(value:String):void {
            if (componentInspectorSetting && value == "") return;
            textField.text = value == null ? "" : value;
            _html = false;
            invalidate(InvalidationType.DATA);
            invalidate(InvalidationType.STYLES);
            textHasChanged = true;
        }

        public function get htmlText():String {
            return textField.htmlText;
        }

        public function set htmlText(value:String):void {
            if (componentInspectorSetting && value == "") return;
            if (value == "") {
                text = "";
                return;
            }
            _html = true;
            _savedHTML = value;
            textField.htmlText = value;
            invalidate(InvalidationType.DATA);
            invalidate(InvalidationType.STYLES);
            textHasChanged = true;
        }

        public function get condenseWhite():Boolean {
            return textField.condenseWhite;
        }

        public function set condenseWhite(value:Boolean):void {
            textField.condenseWhite = value;
            invalidate(InvalidationType.DATA);
        }

        public function get horizontalScrollPolicy():String {
            return _horizontalScrollPolicy;
        }

        public function set horizontalScrollPolicy(value:String):void {
            _horizontalScrollPolicy = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get verticalScrollPolicy():String {
            return _verticalScrollPolicy;
        }

        public function set verticalScrollPolicy(value:String):void {
            _verticalScrollPolicy = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get horizontalScrollPosition():Number {
            return textField.scrollH;
        }

        public function set horizontalScrollPosition(value:Number):void {
            drawNow();
            textField.scrollH = value;
        }

        public function get verticalScrollPosition():Number {
            return textField.scrollV;
        }

        public function set verticalScrollPosition(value:Number):void {
            drawNow();
            textField.scrollV = value;
        }

        public function get textWidth():Number {
            drawNow();
            return textField.textWidth;
        }

        public function get textHeight():Number {
            drawNow();
            return textField.textHeight;
        }

        public function get length():Number {
            return textField.text.length;
        }

        public function get restrict():String {
            return textField.restrict;
        }

        public function set restrict(value:String):void {
            if (componentInspectorSetting && value == "") value = null;
            textField.restrict = value;
        }

        public function get maxChars():int {
            return textField.maxChars;
        }

        public function set maxChars(value:int):void {
            textField.maxChars = value;
        }

        public function get maxHorizontalScrollPosition():int {
            return textField.maxScrollH;
        }

        public function get maxVerticalScrollPosition():int {
            return textField.maxScrollV;
        }

        public function get wordWrap():Boolean {
            return _wordWrap;
        }

        public function set wordWrap(value:Boolean):void {
            _wordWrap = value;
            invalidate(InvalidationType.STATE);
        }

        public function get selectionBeginIndex():int {
            return textField.selectionBeginIndex;
        }

        public function get selectionEndIndex():int {
            return textField.selectionEndIndex;
        }

        public function get displayAsPassword():Boolean {
            return textField.displayAsPassword;
        }

        public function set displayAsPassword(value:Boolean):void {
            textField.displayAsPassword = value;
        }

        public function get editable():Boolean {
            return _editable;
        }

        public function set editable(value:Boolean):void {
            _editable = value;
            invalidate(InvalidationType.STATE);
        }

        public function get alwaysShowSelection():Boolean {
            return textField.alwaysShowSelection;
        }

        public function set alwaysShowSelection(value:Boolean):void {
            textField.alwaysShowSelection = value;
        }

        public function getLineMetrics(lineIndex:int):TextLineMetrics {
            return textField.getLineMetrics(lineIndex);
        }

        public function setSelection(setSelection:int, endIndex:int):void {
            textField.setSelection(setSelection, endIndex);
        }

        public function appendText(text:String):void {
            textField.appendText(text);
            invalidate(InvalidationType.DATA);
        }

        override public function drawFocus(draw:Boolean):void {
            if (focusTarget != null) {
                focusTarget.drawFocus(draw);
                return;
            }
            super.drawFocus(draw);
        }

        override public function setFocus():void {
            if (stage != null) stage.focus = textField;
        }

        override protected function isOurFocus(target:DisplayObject):Boolean {
            return target == textField || super.isOurFocus(target);
        }

        override protected function configUI():void {
            super.configUI();
            tabChildren = true;

            textField = new TextField();
            addChild(textField);
            updateTextFieldType();

            _verticalScrollBar = new UIScrollBar();
            _verticalScrollBar.name = "V";
            _verticalScrollBar.visible = false;
            _verticalScrollBar.focusEnabled = false;
            copyStylesToChild(_verticalScrollBar, SCROLL_BAR_STYLES);
            _verticalScrollBar.addEventListener(ScrollEvent.SCROLL, handleScroll, false, 0, true);
            addChild(_verticalScrollBar);

            _horizontalScrollBar = new UIScrollBar();
            _horizontalScrollBar.name = "H";
            _horizontalScrollBar.visible = false;
            _horizontalScrollBar.focusEnabled = false;
            _horizontalScrollBar.direction = ScrollBarDirection.HORIZONTAL;
            copyStylesToChild(_horizontalScrollBar, SCROLL_BAR_STYLES);
            _horizontalScrollBar.addEventListener(ScrollEvent.SCROLL, handleScroll, false, 0, true);
            addChild(_horizontalScrollBar);

            textField.addEventListener(TextEvent.TEXT_INPUT, handleTextInput, false, 0, true);
            textField.addEventListener(Event.CHANGE, handleChange, false, 0, true);
            textField.addEventListener(KeyboardEvent.KEY_DOWN, handleKeyDown, false, 0, true);
            textField.addEventListener(Event.SCROLL, handleTextScroll, false, 0, true);

            _horizontalScrollBar.scrollTarget = textField;
            _verticalScrollBar.scrollTarget = textField;
            addEventListener(MouseEvent.MOUSE_WHEEL, handleWheel, false, 0, true);
        }

        protected function updateTextFieldType():void {
            textField.type = (enabled && _editable) ? TextFieldType.INPUT : TextFieldType.DYNAMIC;
            textField.selectable = enabled;
            textField.wordWrap = _wordWrap;
            textField.multiline = true;
        }

        protected function handleKeyDown(event:KeyboardEvent):void {
            if (event.keyCode == Keyboard.ENTER) {
                dispatchEvent(new ComponentEvent(ComponentEvent.ENTER, true));
            }
        }

        protected function handleChange(event:Event):void {
            event.stopPropagation();
            dispatchEvent(new Event(Event.CHANGE, true));
            invalidate(InvalidationType.DATA);
        }

        protected function handleTextInput(event:TextEvent):void {
            event.stopPropagation();
            dispatchEvent(new TextEvent(TextEvent.TEXT_INPUT, true, false, event.text));
        }

        protected function handleScroll(event:ScrollEvent):void {
            dispatchEvent(event);
        }

        protected function handleTextScroll(event:Event):void {
            // The scroll bars follow the field through their scrollTarget.
        }

        protected function handleWheel(event:MouseEvent):void {
            if (!enabled || !_verticalScrollBar.visible) return;
            _verticalScrollBar.scrollPosition -= event.delta * _verticalScrollBar.lineScrollSize;
            dispatchEvent(new ScrollEvent(ScrollBarDirection.VERTICAL, event.delta * _verticalScrollBar.lineScrollSize,
                                          _verticalScrollBar.scrollPosition));
        }

        protected function setEmbedFont():void {
            var embed:Object = getStyleValue("embedFonts");
            if (embed != null) setNativeProperty(textField, "embedFonts", Boolean(embed));
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STATE)) {
                updateTextFieldType();
            }
            if (isInvalid(InvalidationType.STYLES)) {
                setStyles();
                setEmbedFont();
            }
            if (isInvalid(InvalidationType.STYLES, InvalidationType.STATE)) {
                drawTextFormat();
                drawBackground();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.DATA)) {
                drawLayout();
            }
            super.draw();
        }

        protected function setStyles():void {
            copyStylesToChild(_verticalScrollBar, SCROLL_BAR_STYLES);
            copyStylesToChild(_horizontalScrollBar, SCROLL_BAR_STYLES);
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
            if (_html) textField.htmlText = _savedHTML;
        }

        protected function drawBackground():void {
            var old:DisplayObject = background;
            background = getDisplayObjectInstance(getStyleValue(enabled ? "upSkin" : "disabledSkin"));
            if (background != null) {
                addChildAt(background, 0);
            }
            if (old != null && old != background && old.parent == this) {
                removeChild(old);
            }
        }

        /** The field inside the padding and beside the scroll bars that are needed. */
        protected function drawLayout():void {
            var padding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(padding)) padding = 0;
            textField.x = textField.y = padding;
            if (background != null) {
                background.width = width;
                background.height = height;
            }
            // Fit the field without bars, then see which bars it needs.
            var availWidth:Number = width - padding * 2;
            var availHeight:Number = height - padding * 2;
            textField.width = Math.max(0, availWidth);
            textField.height = Math.max(0, availHeight);
            var vertical:Boolean = needVScroll();
            if (vertical) {
                availWidth -= ScrollBar.WIDTH;
                textField.width = Math.max(0, availWidth);
            }
            var horizontal:Boolean = needHScroll();
            if (horizontal) {
                availHeight -= ScrollBar.WIDTH;
                textField.height = Math.max(0, availHeight);
                if (!vertical && needVScroll()) {
                    vertical = true;
                    availWidth -= ScrollBar.WIDTH;
                    textField.width = Math.max(0, availWidth);
                }
            }
            _verticalScrollBar.visible = vertical;
            _horizontalScrollBar.visible = horizontal;
            if (vertical) {
                _verticalScrollBar.x = width - ScrollBar.WIDTH;
                _verticalScrollBar.y = 0;
                _verticalScrollBar.setSize(ScrollBar.WIDTH, height - (horizontal ? ScrollBar.WIDTH : 0));
            }
            if (horizontal) {
                _horizontalScrollBar.x = 0;
                _horizontalScrollBar.y = height - ScrollBar.WIDTH;
                _horizontalScrollBar.setSize(width - (vertical ? ScrollBar.WIDTH : 0), ScrollBar.WIDTH);
            }
            _verticalScrollBar.update();
            _horizontalScrollBar.update();
            _verticalScrollBar.drawNow();
            _horizontalScrollBar.drawNow();
        }

        protected function needVScroll():Boolean {
            if (_verticalScrollPolicy == ScrollPolicy.OFF) return false;
            if (_verticalScrollPolicy == ScrollPolicy.ON) return true;
            return textField.maxScrollV > 1;
        }

        protected function needHScroll():Boolean {
            if (_horizontalScrollPolicy == ScrollPolicy.OFF) return false;
            if (_horizontalScrollPolicy == ScrollPolicy.ON) return true;
            return textField.maxScrollH > 0;
        }

        override protected function focusInHandler(event:FocusEvent):void {
            if (event.target == this && stage != null) {
                stage.focus = textField;
            }
            var fm:IFocusManager = focusManager;
            if (fm != null && editable) {
                fm.showFocusIndicator = true;
            }
            super.focusInHandler(event);
        }
    }
}
