package flash.text {
    public class TextLineMetrics {
        public var x:Number, width:Number, height:Number, ascent:Number, descent:Number, leading:Number;
        public function TextLineMetrics(x:Number, width:Number, height:Number, ascent:Number, descent:Number, leading:Number) {
            this.x = x; this.width = width; this.height = height;
            this.ascent = ascent; this.descent = descent; this.leading = leading;
        }
    }
}
