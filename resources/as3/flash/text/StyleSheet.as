package flash.text
{
    import flash.events.EventDispatcher;

    /// Styles for HTML text, by selector (a tag's name, or a class as
    /// .name): each an object of CSS properties, which transform makes a
    /// TextFormat. A text field with one takes its styles as its HTML text
    /// is set.
    public dynamic class StyleSheet extends EventDispatcher
    {
        // Copies of the objects given, by selector in lower case.
        private var _styles:Object = {};

        public function StyleSheet()
        {
        }

        public function get styleNames():Array
        {
            var names:Array = [];
            for (var name:String in _styles)
                names.push(name);
            return names;
        }

        public function clear():void
        {
            _styles = {};
        }

        private static function copyOf(original:*):Object
        {
            var copy:Object = {};
            for (var key:String in original)
                copy[key] = original[key];
            return copy;
        }

        public function getStyle(styleName:String):Object
        {
            return copyOf(_styles[styleName.toLowerCase()]);
        }

        public function setStyle(styleName:String, styleObject:Object):void
        {
            _styles[styleName.toLowerCase()] = copyOf(styleObject);
        }

        /// The styles \a CSSText has, each over any the selector had; none
        /// at all when it does not parse.
        public function parseCSS(CSSText:String):void
        {
            var parsed:Array = __parseCSS(CSSText);
            if (!parsed)
                return;
            for (var i:int = 0; i < parsed.length; i += 2)
                setStyle(parsed[i], parsed[i + 1]);
        }

        /// The TextFormat a style's properties make.
        public function transform(formatObject:Object):TextFormat
        {
            if (!formatObject)
                return null;
            var format:TextFormat = new TextFormat();
            if (formatObject.color)
                format.color = __parseColor(formatObject.color);
            if (formatObject.display)
                format.display = formatObject.display;
            if (formatObject.fontFamily)
                format.font = __fontList(formatObject.fontFamily);
            if (formatObject.fontSize)
            {
                var size:Number = parseInt(formatObject.fontSize);
                if (size > 0)
                    format.size = size;
            }
            if (formatObject.fontStyle == "italic")
                format.italic = true;
            else if (formatObject.fontStyle == "normal")
                format.italic = false;
            if (formatObject.fontWeight == "bold")
                format.bold = true;
            else if (formatObject.fontWeight == "normal")
                format.bold = false;
            // Kerning is always set: true, false or a number.
            if (formatObject.kerning == "true")
                format.kerning = true;
            else if (formatObject.kerning == "false")
                format.kerning = false;
            else
                format.kerning = parseInt(formatObject.kerning);
            if (formatObject.leading)
                format.leading = parseInt(formatObject.leading);
            if (formatObject.letterSpacing)
                format.letterSpacing = parseFloat(formatObject.letterSpacing);
            if (formatObject.marginLeft)
                format.leftMargin = parseFloat(formatObject.marginLeft);
            if (formatObject.marginRight)
                format.rightMargin = parseFloat(formatObject.marginRight);
            if (formatObject.textAlign)
                format.align = formatObject.textAlign;
            if (formatObject.textDecoration == "underline")
                format.underline = true;
            else if (formatObject.textDecoration == "none")
                format.underline = false;
            if (formatObject.textIndent)
                format.indent = parseInt(formatObject.textIndent);
            return format;
        }
    }
}
