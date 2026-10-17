package flash.display {
    import flash.geom.Rectangle;
    public class Sprite extends DisplayObjectContainer {
        private var _graphics:Graphics = new Graphics();
        public var buttonMode:Boolean = false;
        public var useHandCursor:Boolean = true;
        public var hitArea:Sprite = null;
        public function Sprite() { super(); }
        public function get graphics():Graphics { return _graphics; }
        override public function ownBounds():Rectangle {
            return _graphics.bounds == null ? new Rectangle() : _graphics.bounds.clone();
        }
        public function startDrag(lockCenter:Boolean = false, bounds:Rectangle = null):void {}
        public function stopDrag():void {}
    }
}
