package flash.geom {
    public class ColorTransform {
        public var redMultiplier:Number, greenMultiplier:Number, blueMultiplier:Number, alphaMultiplier:Number;
        public var redOffset:Number, greenOffset:Number, blueOffset:Number, alphaOffset:Number;
        public function ColorTransform(rm:Number = 1, gm:Number = 1, bm:Number = 1, am:Number = 1,
                                       ro:Number = 0, go:Number = 0, bo:Number = 0, ao:Number = 0) {
            redMultiplier = rm; greenMultiplier = gm; blueMultiplier = bm; alphaMultiplier = am;
            redOffset = ro; greenOffset = go; blueOffset = bo; alphaOffset = ao;
        }
        public function get color():uint {
            return (uint(redOffset) << 16) | (uint(greenOffset) << 8) | uint(blueOffset);
        }
        public function set color(value:uint):void {
            redMultiplier = greenMultiplier = blueMultiplier = 0;
            redOffset = (value >> 16) & 0xFF;
            greenOffset = (value >> 8) & 0xFF;
            blueOffset = value & 0xFF;
        }
    }
}
