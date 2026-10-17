package flash.display {
    import flash.geom.Rectangle;
    // Stand-in: keeps only the bounds of what is drawn, and a count.
    public final class Graphics {
        public var bounds:Rectangle = null;
        public var commands:int = 0;
        private var _px:Number = 0;
        private var _py:Number = 0;

        private function add(x:Number, y:Number, w:Number = 0, h:Number = 0):void {
            var r:Rectangle = new Rectangle(Math.min(x, x + w), Math.min(y, y + h), Math.abs(w), Math.abs(h));
            if (bounds == null) bounds = r;
            else {
                var l:Number = Math.min(bounds.x, r.x), t:Number = Math.min(bounds.y, r.y);
                bounds = new Rectangle(l, t, Math.max(bounds.right, r.right) - l, Math.max(bounds.bottom, r.bottom) - t);
            }
            commands++;
        }
        public function clear():void { bounds = null; commands = 0; }
        public function beginFill(color:uint, alpha:Number = 1):void {}
        public function beginGradientFill(type:String, colors:Array, alphas:Array, ratios:Array, matrix:* = null,
                                          spreadMethod:String = "pad", interpolationMethod:String = "rgb",
                                          focalPointRatio:Number = 0):void {}
        public function beginBitmapFill(bitmap:*, matrix:* = null, repeat:Boolean = true, smooth:Boolean = false):void {}
        public function endFill():void {}
        public function lineStyle(thickness:Number = NaN, color:uint = 0, alpha:Number = 1, pixelHinting:Boolean = false,
                                  scaleMode:String = "normal", caps:String = null, joints:String = null,
                                  miterLimit:Number = 3):void {}
        public function moveTo(x:Number, y:Number):void { _px = x; _py = y; }
        public function lineTo(x:Number, y:Number):void { add(_px, _py, 0, 0); add(x, y); _px = x; _py = y; }
        public function curveTo(cx:Number, cy:Number, x:Number, y:Number):void { add(_px, _py); add(x, y); _px = x; _py = y; }
        public function drawRect(x:Number, y:Number, w:Number, h:Number):void { add(x, y, w, h); }
        public function drawRoundRect(x:Number, y:Number, w:Number, h:Number, ew:Number, eh:Number = NaN):void { add(x, y, w, h); }
        public function drawCircle(x:Number, y:Number, r:Number):void { add(x - r, y - r, 2 * r, 2 * r); }
        public function drawEllipse(x:Number, y:Number, w:Number, h:Number):void { add(x, y, w, h); }
    }
}
