package flash.geom {
    public class Rectangle {
        public var x:Number;
        public var y:Number;
        public var width:Number;
        public var height:Number;
        public function Rectangle(x:Number = 0, y:Number = 0, width:Number = 0, height:Number = 0) {
            this.x = x; this.y = y; this.width = width; this.height = height;
        }
        public function get left():Number { return x; }
        public function get top():Number { return y; }
        public function get right():Number { return x + width; }
        public function get bottom():Number { return y + height; }
        public function isEmpty():Boolean { return width <= 0 || height <= 0; }
        public function contains(px:Number, py:Number):Boolean {
            return px >= x && px < x + width && py >= y && py < y + height;
        }
        public function union(r:Rectangle):Rectangle {
            if (isEmpty()) return r.clone();
            if (r.isEmpty()) return clone();
            var l:Number = Math.min(x, r.x), t:Number = Math.min(y, r.y);
            return new Rectangle(l, t, Math.max(right, r.right) - l, Math.max(bottom, r.bottom) - t);
        }
        public function clone():Rectangle { return new Rectangle(x, y, width, height); }
        public function toString():String { return "(x=" + x + ", y=" + y + ", w=" + width + ", h=" + height + ")"; }
    }
}
