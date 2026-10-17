package flash.events {
    // Stand-in: listeners by type and priority; target and bubble phases
    // along the display list (capture listeners run in the bubble order).
    public class EventDispatcher {
        // Made on first use: a subclass may be used before its super() runs.
        private var _listenerTable:Object;
        private function get _listeners():Object {
            if (_listenerTable == null) _listenerTable = {};
            return _listenerTable;
        }
        private var _dispatchTarget:Object;

        public function EventDispatcher(target:Object = null) {
            _dispatchTarget = target;
        }
        // The parent events bubble to: DisplayObject gives its parent.
        public function eventParent():EventDispatcher { return null; }

        public function addEventListener(type:String, listener:Function, useCapture:Boolean = false,
                                         priority:int = 0, useWeakReference:Boolean = false):void {
            var list:Array = _listeners[type];
            if (list == null) { list = []; _listeners[type] = list; }
            for each (var entry:Object in list) if (entry.f == listener && entry.c == useCapture) return;
            var index:int = list.length;
            while (index > 0 && list[index - 1].p < priority) index--;
            list.splice(index, 0, {f: listener, c: useCapture, p: priority});
        }
        public function removeEventListener(type:String, listener:Function, useCapture:Boolean = false):void {
            var list:Array = _listeners[type];
            if (list == null) return;
            for (var i:int = 0; i < list.length; i++) {
                if (list[i].f == listener && list[i].c == useCapture) { list.splice(i, 1); return; }
            }
        }
        public function hasEventListener(type:String):Boolean {
            var list:Array = _listeners[type];
            return list != null && list.length > 0;
        }
        public function willTrigger(type:String):Boolean {
            for (var d:EventDispatcher = this; d != null; d = d.eventParent()) if (d.hasEventListener(type)) return true;
            return false;
        }
        public function listenerCount(type:String):int {
            var list:Array = _listeners[type];
            return list == null ? 0 : list.length;
        }
        public function notify(event:Event):void {
            var list:Array = _listeners[event.type];
            if (list == null) return;
            event._currentTarget = this;
            for each (var entry:Object in list.slice()) {
                entry.f(event);
                if (event._stoppedNow) return;
            }
        }
        public function dispatchEvent(event:Event):Boolean {
            if (event._target != null) event = event.clone();
            event._target = _dispatchTarget != null ? _dispatchTarget : this;
            event._phase = 2;
            notify(event);
            if (event.bubbles) {
                event._phase = 3;
                for (var d:EventDispatcher = eventParent(); d != null && !event._stopped; d = d.eventParent()) {
                    d.notify(event);
                }
            }
            return !event.isDefaultPrevented();
        }
    }
}
