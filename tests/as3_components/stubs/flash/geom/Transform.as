package flash.geom {
    public class Transform {
        private var _color:ColorTransform = new ColorTransform();
        public function Transform(owner:Object = null) {}
        public function get colorTransform():ColorTransform { return _color; }
        public function set colorTransform(value:ColorTransform):void { _color = value; }
    }
}
