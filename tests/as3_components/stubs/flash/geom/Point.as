package flash.geom {
    public class Point {
        public var x:Number;
        public var y:Number;
        public function Point(x:Number = 0, y:Number = 0) { this.x = x; this.y = y; }
        public function clone():Point { return new Point(x, y); }
        public function toString():String { return "(x=" + x + ", y=" + y + ")"; }
    }
}
