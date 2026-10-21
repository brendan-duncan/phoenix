package flash.text
{
    /// A run of characters of one format, as TextField.getTextRuns gives
    /// them.
    public class TextRun
    {
        public var beginIndex:int;
        public var endIndex:int;
        public var textFormat:TextFormat;

        public function TextRun(beginIndex:int, endIndex:int, textFormat:TextFormat)
        {
            this.beginIndex = beginIndex;
            this.endIndex = endIndex;
            this.textFormat = textFormat;
        }
    }
}
