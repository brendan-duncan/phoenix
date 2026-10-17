package flash.display {
    public dynamic class MovieClip extends Sprite {
        public function MovieClip() { super(); }
        public function get currentFrame():int { return 1; }
        public function get totalFrames():int { return 1; }
        public function stop():void {}
        public function play():void {}
        public function gotoAndStop(frame:Object, scene:String = null):void {}
        public function gotoAndPlay(frame:Object, scene:String = null):void {}
    }
}
