package flash.filters {
    public class DropShadowFilter {
        public var distance:Number, angle:Number, color:uint, alpha:Number, blurX:Number, blurY:Number;
        public var strength:Number, quality:int, inner:Boolean, knockout:Boolean, hideObject:Boolean;
        public function DropShadowFilter(distance:Number = 4, angle:Number = 45, color:uint = 0, alpha:Number = 1,
                                         blurX:Number = 4, blurY:Number = 4, strength:Number = 1, quality:int = 1,
                                         inner:Boolean = false, knockout:Boolean = false, hideObject:Boolean = false) {
            this.distance = distance; this.angle = angle; this.color = color; this.alpha = alpha;
            this.blurX = blurX; this.blurY = blurY; this.strength = strength; this.quality = quality;
            this.inner = inner; this.knockout = knockout; this.hideObject = hideObject;
        }
    }
}
