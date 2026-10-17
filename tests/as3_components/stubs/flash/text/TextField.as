package flash.text {
    import flash.display.InteractiveObject;
    import flash.events.Event;
    import flash.geom.Rectangle;

    // Stand-in: one line of text measured as size * 0.5 per character and
    // size * 1.25 per line; scrolling by lines over "\r" or "\n" breaks.
    public class TextField extends InteractiveObject {
        private var _text:String = "";
        private var _w:Number = 100;
        private var _h:Number = 100;
        private var _format:TextFormat = new TextFormat("Times New Roman", 12, 0);
        private var _scrollV:int = 1;
        public var type:String = "dynamic";
        public var selectable:Boolean = true;
        public var embedFonts:Boolean = false;
        public var autoSize:String = "none";
        public var wordWrap:Boolean = false;
        public var multiline:Boolean = false;
        public var maxChars:int = 0;
        public var restrict:String = null;
        public var displayAsPassword:Boolean = false;
        public var alwaysShowSelection:Boolean = false;
        public var condenseWhite:Boolean = false;
        public var background:Boolean = false;
        public var backgroundColor:uint = 0xFFFFFF;
        public var border:Boolean = false;
        public var borderColor:uint = 0;
        public var antiAliasType:String = "normal";
        public var gridFitType:String = "pixel";
        public var sharpness:Number = 0;
        public var thickness:Number = 0;
        public var scrollH:Number = 0;
        public var selectionBeginIndex:int = 0;
        public var selectionEndIndex:int = 0;

        public function TextField() { super(); }

        public function get text():String { return _text; }
        public function set text(value:String):void { _text = value == null ? "" : value; }
        public function get htmlText():String { return _text; }
        public function set htmlText(value:String):void { _text = value.replace(/<[^>]*>/g, ""); }
        public function get length():int { return _text.length; }
        public function appendText(value:String):void { _text += value; }
        public function replaceText(begin:int, end:int, value:String):void {
            _text = _text.substring(0, begin) + value + _text.substring(end);
        }
        public function replaceSelectedText(value:String):void {
            replaceText(selectionBeginIndex, selectionEndIndex, value);
        }
        public function setSelection(begin:int, end:int):void { selectionBeginIndex = begin; selectionEndIndex = end; }

        public function get defaultTextFormat():TextFormat { return _format; }
        public function set defaultTextFormat(value:TextFormat):void { merge(value); }
        public function setTextFormat(value:TextFormat, begin:int = -1, end:int = -1):void { merge(value); }
        public function getTextFormat(begin:int = -1, end:int = -1):TextFormat { return _format; }
        private function merge(f:TextFormat):void {
            for each (var key:String in ["font", "size", "color", "bold", "italic", "underline", "align", "leftMargin", "rightMargin", "indent", "leading"]) {
                if (f[key] != null) _format[key] = f[key];
            }
        }
        public function get textColor():uint { return uint(_format.color); }
        public function set textColor(value:uint):void { _format.color = value; }

        private function lines():Array { return _text.split(/\r\n|\r|\n/); }
        public function get textWidth():Number {
            var widest:int = 0;
            for each (var line:String in lines()) widest = Math.max(widest, line.length);
            return widest * Number(_format.size) * 0.5;
        }
        public function get textHeight():Number { return _text.length == 0 ? 0 : lines().length * Number(_format.size) * 1.25; }
        public function get numLines():int { return lines().length; }
        private function visibleLines():int { return Math.max(1, Math.floor((_h - 4) / (Number(_format.size) * 1.25))); }
        public function get maxScrollV():int { return Math.max(1, numLines - visibleLines() + 1); }
        public function get bottomScrollV():int { return Math.min(numLines, _scrollV + visibleLines() - 1); }
        public function get scrollV():int { return _scrollV; }
        public function set scrollV(value:int):void {
            var v:int = Math.max(1, Math.min(maxScrollV, value));
            if (v == _scrollV) return;
            _scrollV = v;
            dispatchEvent(new Event(Event.SCROLL));
        }
        public function get maxScrollH():Number { return Math.max(0, textWidth + 4 - _w); }
        public function getLineMetrics(line:int):TextLineMetrics {
            var size:Number = Number(_format.size);
            return new TextLineMetrics(0, String(lines()[line]).length * size * 0.5, size * 1.25, size, size * 0.25, 0);
        }

        override public function get width():Number { return _w * scaleX; }
        override public function set width(value:Number):void { _w = value; }
        override public function get height():Number { return _h * scaleY; }
        override public function set height(value:Number):void { _h = value; }
        override public function contentBounds():Rectangle { return new Rectangle(0, 0, _w, _h); }
    }
}
