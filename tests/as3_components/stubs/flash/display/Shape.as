package flash.display {
    import flash.geom.Rectangle;
    public class Shape extends DisplayObject {
        private var _graphics:Graphics = new Graphics();
        public function Shape() { super(); }
        public function get graphics():Graphics { return _graphics; }
        override public function contentBounds():Rectangle {
            return _graphics.bounds == null ? new Rectangle() : _graphics.bounds.clone();
        }
    }
}
