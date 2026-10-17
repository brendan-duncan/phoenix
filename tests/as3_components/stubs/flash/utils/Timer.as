package flash.utils {
    import flash.events.EventDispatcher;
    import flash.events.TimerEvent;
    // Stand-in: never fires on its own; tests call fire().
    public class Timer extends EventDispatcher {
        public var delay:Number;
        public var repeatCount:int;
        public var currentCount:int = 0;
        private var _running:Boolean = false;
        public function Timer(delay:Number, repeatCount:int = 0) {
            this.delay = delay; this.repeatCount = repeatCount;
        }
        public function get running():Boolean { return _running; }
        public function start():void { _running = true; }
        public function stop():void { _running = false; }
        public function reset():void { _running = false; currentCount = 0; }
        public function fire():void {
            if (!_running) return;
            currentCount++;
            dispatchEvent(new TimerEvent(TimerEvent.TIMER));
        }
    }
}
