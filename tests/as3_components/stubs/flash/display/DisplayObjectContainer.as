package flash.display {
    import flash.events.Event;
    import flash.geom.Rectangle;

    public class DisplayObjectContainer extends InteractiveObject {
        private var _children:Array = [];
        public var mouseChildren:Boolean = true;
        public var tabChildren:Boolean = true;

        public function DisplayObjectContainer() { super(); }

        public function get numChildren():int { return _children.length; }
        public function addChild(child:DisplayObject):DisplayObject { return addChildAt(child, _children.length); }
        public function addChildAt(child:DisplayObject, index:int):DisplayObject {
            if (child == null) throw new TypeError("Error #2007: Parameter child must be non-null.");
            if (index < 0 || index > _children.length) throw new RangeError("Error #2006: The supplied index is out of bounds.");
            if (child._parent == this) {
                _children.splice(_children.indexOf(child), 1);
                if (index > _children.length) index = _children.length;
                _children.splice(index, 0, child);
                return child;
            }
            if (child._parent != null) child._parent.removeChild(child);
            _children.splice(index, 0, child);
            child._parent = this;
            child.dispatchEvent(new Event(Event.ADDED, true));
            if (stage != null) announce(child, Event.ADDED_TO_STAGE);
            return child;
        }
        private static function announce(d:DisplayObject, type:String):void {
            d.dispatchEvent(new Event(type));
            var c:DisplayObjectContainer = d as DisplayObjectContainer;
            if (c != null) for (var i:int = 0; i < c.numChildren; i++) announce(c.getChildAt(i), type);
        }
        public function removeChild(child:DisplayObject):DisplayObject {
            var index:int = _children.indexOf(child);
            if (index < 0) throw new ArgumentError("Error #2025: The supplied DisplayObject must be a child of the caller.");
            return removeChildAt(index);
        }
        public function removeChildAt(index:int):DisplayObject {
            if (index < 0 || index >= _children.length) throw new RangeError("Error #2006: The supplied index is out of bounds.");
            var child:DisplayObject = _children[index];
            child.dispatchEvent(new Event(Event.REMOVED, true));
            if (stage != null) announce(child, Event.REMOVED_FROM_STAGE);
            _children.splice(_children.indexOf(child), 1);
            child._parent = null;
            return child;
        }
        public function getChildAt(index:int):DisplayObject {
            if (index < 0 || index >= _children.length) throw new RangeError("Error #2006: The supplied index is out of bounds.");
            return _children[index];
        }
        public function getChildIndex(child:DisplayObject):int {
            var index:int = _children.indexOf(child);
            if (index < 0) throw new ArgumentError("Error #2025: The supplied DisplayObject must be a child of the caller.");
            return index;
        }
        public function setChildIndex(child:DisplayObject, index:int):void { addChildAt(child, index); }
        public function getChildByName(name:String):DisplayObject {
            for each (var c:DisplayObject in _children) if (c.name == name) return c;
            return null;
        }
        public function contains(child:DisplayObject):Boolean {
            for (var d:DisplayObject = child; d != null; d = d._parent) if (d == this) return true;
            return false;
        }
        public function swapChildren(a:DisplayObject, b:DisplayObject):void {
            var ia:int = getChildIndex(a), ib:int = getChildIndex(b);
            _children[ia] = b; _children[ib] = a;
        }
        override public function contentBounds():Rectangle {
            var b:Rectangle = ownBounds();
            for each (var c:DisplayObject in _children) {
                if (!c.visible) continue;
                var r:Rectangle = c.getBounds(this);
                if (r.width == 0 && r.height == 0) continue;
                b = b.union(r);
            }
            return b;
        }
        public function ownBounds():Rectangle { return new Rectangle(); }
    }
}
