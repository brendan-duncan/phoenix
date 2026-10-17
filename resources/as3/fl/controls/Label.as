package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ComponentEvent;
    import flash.events.Event;
    import flash.text.TextField;
    import flash.text.TextFieldAutoSize;
    import flash.text.TextFieldType;
    import flash.text.TextFormat;

    /** Dispatched when autoSize changes the label's size. */
    [Event(name="resize", type="fl.events.ComponentEvent")]

    /**
     * A line (or with wordWrap, lines) of text, 100 by 22 by default. With
     * autoSize the label takes the text's size, kept at its left, center or
     * right.
     */
    public class Label extends UIComponent {
        /** The text field shown. */
        public var textField:TextField;

        protected var actualWidth:Number;
        protected var actualHeight:Number;
        protected var defaultLabel:String = "Label";
        protected var _savedHTML:String;
        protected var _html:Boolean = false;

        private static var defaultStyles:Object = {
            textFormat: null,
            embedFonts: false
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function Label() {
            super();
            text = defaultLabel;
            actualWidth = _width;
            actualHeight = _height;
        }

        override protected function defaultSize():Array {
            return [100, 22];
        }

        public function get text():String {
            return textField.text;
        }

        public function set text(value:String):void {
            if (value == text && !_html) return;
            _html = false;
            textField.text = value == null ? "" : value;
            if (textField.autoSize != TextFieldAutoSize.NONE) invalidate(InvalidationType.SIZE);
            invalidate(InvalidationType.DATA);
        }

        public function get htmlText():String {
            return textField.htmlText;
        }

        public function set htmlText(value:String):void {
            if (value == htmlText) return;
            _html = true;
            _savedHTML = value;
            textField.htmlText = value;
            if (textField.autoSize != TextFieldAutoSize.NONE) invalidate(InvalidationType.SIZE);
            invalidate(InvalidationType.DATA);
        }

        public function get condenseWhite():Boolean {
            return textField.condenseWhite;
        }

        public function set condenseWhite(value:Boolean):void {
            textField.condenseWhite = value;
            if (textField.autoSize != TextFieldAutoSize.NONE) invalidate(InvalidationType.SIZE);
        }

        public function get selectable():Boolean {
            return textField.selectable;
        }

        public function set selectable(value:Boolean):void {
            textField.selectable = value;
        }

        public function get wordWrap():Boolean {
            return textField.wordWrap;
        }

        public function set wordWrap(value:Boolean):void {
            textField.wordWrap = value;
            if (textField.autoSize != TextFieldAutoSize.NONE) invalidate(InvalidationType.SIZE);
        }

        /** TextFieldAutoSize.NONE (the default), LEFT, CENTER or RIGHT. */
        public function get autoSize():String {
            return textField.autoSize;
        }

        public function set autoSize(value:String):void {
            textField.autoSize = value;
            invalidate(InvalidationType.SIZE);
        }

        override public function get width():Number {
            if (textField.autoSize != TextFieldAutoSize.NONE && !textField.wordWrap) {
                return _width;
            }
            return actualWidth;
        }

        override public function set width(value:Number):void {
            actualWidth = value;
            super.width = value;
        }

        override public function get height():Number {
            if (textField.autoSize != TextFieldAutoSize.NONE && textField.wordWrap) {
                return _height;
            }
            return actualHeight;
        }

        override public function setSize(width:Number, height:Number):void {
            actualWidth = width;
            actualHeight = height;
            super.setSize(width, height);
        }

        override protected function configUI():void {
            super.configUI();
            textField = new TextField();
            addChild(textField);
            textField.type = TextFieldType.DYNAMIC;
            textField.selectable = false;
            textField.wordWrap = false;
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES, InvalidationType.STATE)) {
                drawTextFormat();
                var embed:Object = getStyleValue("embedFonts");
                if (embed != null) setNativeProperty(textField, "embedFonts", Boolean(embed));
                if (textField.autoSize != TextFieldAutoSize.NONE) invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.DATA)) {
                drawLayout();
            }
            super.draw();
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
            if (_html) textField.htmlText = _savedHTML;
        }

        /** The field at the label's size; with autoSize, the label at the text's. */
        protected function drawLayout():void {
            var resized:Boolean = false;
            textField.width = width;
            textField.height = height;
            if (textField.autoSize != TextFieldAutoSize.NONE) {
                var textWidth:Number = textField.textWidth + 4;
                var textHeight:Number = textField.textHeight + 4;
                if (!textField.wordWrap) {
                    resized = _width != textWidth;
                    var oldWidth:Number = _width;
                    _width = textWidth;
                    textField.width = textWidth;
                    switch (textField.autoSize) {
                        case TextFieldAutoSize.RIGHT:
                            textField.x = 0;
                            super.x = Math.round(x + oldWidth - textWidth);
                            break;
                        case TextFieldAutoSize.CENTER:
                            textField.x = 0;
                            super.x = Math.round(x + (oldWidth - textWidth) / 2);
                            break;
                        default:
                            textField.x = 0;
                            break;
                    }
                }
                resized = resized || _height != textHeight;
                _height = textHeight;
                textField.height = textHeight;
            } else {
                textField.x = 0;
            }
            if (resized) {
                dispatchEvent(new ComponentEvent(ComponentEvent.RESIZE, true));
            }
        }
    }
}
