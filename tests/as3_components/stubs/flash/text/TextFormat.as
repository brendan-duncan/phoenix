package flash.text {
    public class TextFormat {
        public var font:String, size:Object, color:Object, bold:Object, italic:Object, underline:Object;
        public var url:String, target:String, align:String;
        public var leftMargin:Object, rightMargin:Object, indent:Object, leading:Object;
        public var letterSpacing:Object = null, kerning:Object = null, blockIndent:Object = null;
        public function TextFormat(font:String = null, size:Object = null, color:Object = null, bold:Object = null,
                                   italic:Object = null, underline:Object = null, url:String = null,
                                   target:String = null, align:String = null, leftMargin:Object = null,
                                   rightMargin:Object = null, indent:Object = null, leading:Object = null) {
            this.font = font; this.size = size; this.color = color; this.bold = bold; this.italic = italic;
            this.underline = underline; this.url = url; this.target = target; this.align = align;
            this.leftMargin = leftMargin; this.rightMargin = rightMargin; this.indent = indent; this.leading = leading;
        }
    }
}
