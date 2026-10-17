package flash.display {
    import flash.events.Event;
    import flash.events.EventDispatcher;
    import flash.geom.Point;
    import flash.geom.Rectangle;
    import flash.geom.Transform;

    // Stand-in: position and scale (no rotation in the math), bounds from
    // what subclasses report, the parent chain, and enter-frame listeners
    // kept for Stage.frame().
    public class DisplayObject extends EventDispatcher {
        private var _x:Number = 0;
        private var _y:Number = 0;
        private var _scaleX:Number = 1;
        private var _scaleY:Number = 1;
        private var _rotation:Number = 0;
        private var _transform:Transform = new Transform();
        public var _parent:DisplayObjectContainer = null;
        public var name:String = "";
        public var visible:Boolean = true;
        public var alpha:Number = 1;
        public var filters:Array = [];
        public var mask:DisplayObject = null;
        public var scrollRect:Rectangle = null;
        public var cacheAsBitmap:Boolean = false;
        public var blendMode:String = "normal";
        public var scale9Grid:Rectangle = null;

        private static var instances:int = 0;

        public function DisplayObject() {
            super();
            name = "instance" + (++instances);
        }
        override public function eventParent():EventDispatcher { return _parent; }

        public function get x():Number { return _x; }
        public function set x(value:Number):void { _x = value; }
        public function get y():Number { return _y; }
        public function set y(value:Number):void { _y = value; }
        public function get scaleX():Number { return _scaleX; }
        public function set scaleX(value:Number):void { _scaleX = value; }
        public function get scaleY():Number { return _scaleY; }
        public function set scaleY(value:Number):void { _scaleY = value; }
        public function get rotation():Number { return _rotation; }
        public function set rotation(value:Number):void { _rotation = value; }
        public function get transform():Transform { return _transform; }
        public function set transform(value:Transform):void { _transform = value; }

        // The bounds of the content in its own space; subclasses report.
        public function contentBounds():Rectangle { return new Rectangle(); }

        public function get width():Number { return contentBounds().width * Math.abs(_scaleX); }
        public function set width(value:Number):void {
            var w:Number = contentBounds().width;
            if (w > 0) _scaleX = value / w;
        }
        public function get height():Number { return contentBounds().height * Math.abs(_scaleY); }
        public function set height(value:Number):void {
            var h:Number = contentBounds().height;
            if (h > 0) _scaleY = value / h;
        }

        public function get parent():DisplayObjectContainer { return _parent; }
        public function get root():DisplayObject {
            var d:DisplayObject = this;
            while (d._parent != null && !(d._parent is Stage)) d = d._parent;
            return d;
        }
        public function get stage():Stage {
            var d:DisplayObject = this;
            while (d._parent != null) d = d._parent;
            return d as Stage;
        }
        public function get loaderInfo():Object { return null; }

        public function localToGlobal(p:Point):Point {
            var q:Point = new Point(p.x, p.y);
            for (var d:DisplayObject = this; d != null; d = d._parent) {
                q.x = q.x * d._scaleX + d._x;
                q.y = q.y * d._scaleY + d._y;
            }
            return q;
        }
        public function globalToLocal(p:Point):Point {
            var chain:Array = [];
            for (var d:DisplayObject = this; d != null; d = d._parent) chain.unshift(d);
            var q:Point = new Point(p.x, p.y);
            for each (var e:DisplayObject in chain) {
                q.x = (q.x - e._x) / e._scaleX;
                q.y = (q.y - e._y) / e._scaleY;
            }
            return q;
        }
        public function get mouseX():Number { return globalToLocal(new Point(Stage.mouseStageX, Stage.mouseStageY)).x; }
        public function get mouseY():Number { return globalToLocal(new Point(Stage.mouseStageX, Stage.mouseStageY)).y; }

        public function getBounds(target:DisplayObject):Rectangle {
            var b:Rectangle = contentBounds();
            var tl:Point = localToGlobal(new Point(b.x, b.y));
            var br:Point = localToGlobal(new Point(b.right, b.bottom));
            if (target != null) { tl = target.globalToLocal(tl); br = target.globalToLocal(br); }
            return new Rectangle(Math.min(tl.x, br.x), Math.min(tl.y, br.y), Math.abs(br.x - tl.x), Math.abs(br.y - tl.y));
        }
        public function getRect(target:DisplayObject):Rectangle { return getBounds(target); }
        public function hitTestPoint(x:Number, y:Number, shapeFlag:Boolean = false):Boolean {
            return getBounds(null).contains(x, y);
        }
        public function hitTestObject(other:DisplayObject):Boolean { return false; }

        override public function addEventListener(type:String, listener:Function, useCapture:Boolean = false,
                                                  priority:int = 0, useWeakReference:Boolean = false):void {
            super.addEventListener(type, listener, useCapture, priority, useWeakReference);
            if (type == Event.ENTER_FRAME) Stage.watchFrames(this);
        }
    }
}
