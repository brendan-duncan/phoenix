package fl.controls {
    import fl.controls.listClasses.ICellRenderer;
    import fl.controls.listClasses.ListData;
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import flash.display.DisplayObject;
    import flash.events.Event;
    import flash.events.MouseEvent;
    import flash.geom.Rectangle;
    import flash.ui.Keyboard;
    import flash.utils.Dictionary;

    /**
     * A scrolling list of items, one row each: the label (the item's
     * labelField, "label", or labelFunction) and an icon (iconField, "icon",
     * or iconFunction: a class, class name or display object). Rows are
     * rowHeight (20) high; 100 by 100 by default.
     */
    public class List extends SelectableList {
        protected var _rowHeight:Number = 20;
        protected var _cellRenderer:Object;
        protected var _labelField:String = "label";
        protected var _labelFunction:Function;
        protected var _iconField:String = "icon";
        protected var _iconFunction:Function;

        private static var defaultStyles:Object = {
            focusRectSkin: null,
            focusRectPadding: null
        };

        public static function getStyleDefinition():Object {
            return mergeStyles(defaultStyles, SelectableList.getStyleDefinition());
        }

        public function List() {
            super();
        }

        /** The item property shown as the label. */
        public function get labelField():String {
            return _labelField;
        }

        public function set labelField(value:String):void {
            if (value == _labelField) return;
            _labelField = value;
            invalidate(InvalidationType.DATA);
        }

        /** A function of an item giving its label, used instead of labelField. */
        public function get labelFunction():Function {
            return _labelFunction;
        }

        public function set labelFunction(value:Function):void {
            if (_labelFunction == value) return;
            _labelFunction = value;
            invalidate(InvalidationType.DATA);
        }

        /** The item property giving its icon. */
        public function get iconField():String {
            return _iconField;
        }

        public function set iconField(value:String):void {
            if (value == _iconField) return;
            _iconField = value;
            invalidate(InvalidationType.DATA);
        }

        /** A function of an item giving its icon, used instead of iconField. */
        public function get iconFunction():Function {
            return _iconFunction;
        }

        public function set iconFunction(value:Function):void {
            if (_iconFunction == value) return;
            _iconFunction = value;
            invalidate(InvalidationType.DATA);
        }

        /** The rows that fit; setting it sizes the list to show that many. */
        override public function get rowCount():uint {
            return Math.ceil(calculateAvailableHeight() / rowHeight);
        }

        public function set rowCount(value:uint):void {
            var padding:Number = Number(getStyleValue("contentPadding"));
            if (isNaN(padding)) padding = 0;
            var scrollBarHeight:Number = (_horizontalScrollPolicy == ScrollPolicy.ON ||
                (_horizontalScrollPolicy == ScrollPolicy.AUTO && _maxHorizontalScrollPosition > 0)) ? 15 : 0;
            height = rowHeight * value + 2 * padding + scrollBarHeight;
        }

        public function get rowHeight():Number {
            return _rowHeight;
        }

        public function set rowHeight(value:Number):void {
            _rowHeight = value;
            invalidate(InvalidationType.SIZE);
        }

        /** The label an item shows. */
        public function itemToLabel(item:Object):String {
            if (_labelFunction != null) {
                return String(_labelFunction(item));
            }
            if (item == null) return "";
            var value:* = item[_labelField];
            return value != null ? String(value) : "";
        }

        /** The icon an item shows, or null. */
        protected function itemToIcon(item:Object):Object {
            if (_iconFunction != null) {
                return _iconFunction(item);
            }
            if (item == null || !(item is Object)) return null;
            var value:* = null;
            try {
                value = item[_iconField];
            } catch (e:Error) {
                value = null;
            }
            return value == undefined ? null : value;
        }

        override public function scrollToIndex(newCaretIndex:int):void {
            drawNow();
            var lastVisible:int = Math.floor((_verticalScrollPosition + availableHeight) / rowHeight) - 1;
            var firstVisible:int = Math.ceil(_verticalScrollPosition / rowHeight);
            if (newCaretIndex < firstVisible) {
                verticalScrollPosition = newCaretIndex * rowHeight;
            } else if (newCaretIndex > lastVisible) {
                verticalScrollPosition = (newCaretIndex + 1) * rowHeight - availableHeight;
            }
        }

        override protected function configUI():void {
            useFixedHorizontalScrolling = true;
            _horizontalScrollPolicy = ScrollPolicy.AUTO;
            _verticalScrollPolicy = ScrollPolicy.AUTO;
            super.configUI();
        }

        protected function calculateAvailableHeight():Number {
            var padding:Number = Number(getStyleValue("contentPadding"));
            if (isNaN(padding)) padding = 0;
            return height - padding * 2 - ((_horizontalScrollPolicy == ScrollPolicy.ON ||
                (_horizontalScrollPolicy == ScrollPolicy.AUTO && _maxHorizontalScrollPosition > 0)) ? 15 : 0);
        }

        override protected function setHorizontalScrollPosition(value:Number, fireEvent:Boolean = false):void {
            list.x = -value;
            super.setHorizontalScrollPosition(value, true);
        }

        override protected function draw():void {
            var contentHeightChanged:Boolean = contentHeight != rowHeight * length;
            contentHeight = rowHeight * length;

            if (isInvalid(InvalidationType.STYLES)) {
                setStyles();
                drawBackground();
                if (contentPadding != Number(getStyleValue("contentPadding"))) {
                    invalidate(InvalidationType.SIZE, false);
                }
                if (_cellRenderer != getStyleValue("cellRenderer")) {
                    _invalidateList();
                    _cellRenderer = getStyleValue("cellRenderer");
                }
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.STATE) || contentHeightChanged) {
                drawLayout();
            }
            if (isInvalid(InvalidationType.RENDERER_STYLES)) {
                updateRendererStyles();
            }
            if (isInvalid(InvalidationType.STYLES, InvalidationType.SIZE, InvalidationType.DATA,
                          InvalidationType.SCROLL, InvalidationType.SELECTED)) {
                drawList();
            }
            updateChildren();
            validate();
        }

        override protected function drawLayout():void {
            super.drawLayout();
            listHolder.x = listHolder.y = contentPadding;
        }

        /** Shows the rows in view: a renderer for each, reused where it can be. */
        protected function drawList():void {
            listHolder.x = listHolder.y = contentPadding;
            var rect:Rectangle = listHolder.scrollRect;
            if (rect == null) rect = new Rectangle(0, 0, availableWidth, availableHeight);
            rect.x = _horizontalScrollPosition;
            rect.y = Math.floor(_verticalScrollPosition) % rowHeight;
            rect.width = availableWidth;
            rect.height = availableHeight;
            listHolder.scrollRect = rect;

            var startIndex:int = Math.floor(_verticalScrollPosition / rowHeight);
            var endIndex:int = Math.min(length, startIndex + rowCount + 1);

            // The renderers showing items still in view keep them.
            var oldRenderers:Dictionary = new Dictionary(true);
            var renderer:ICellRenderer;
            while (activeCellRenderers.length > 0) {
                renderer = activeCellRenderers.pop() as ICellRenderer;
                var shown:Object = renderer.data;
                if (shown != null && oldRenderers[shown] == null && invalidItems[shown] == null) {
                    oldRenderers[shown] = renderer;
                } else {
                    availableCellRenderers.push(renderer);
                }
            }
            var newRendered:Dictionary = new Dictionary(true);
            var items:Array = [];
            var i:int;
            for (i = startIndex; i < endIndex; i++) {
                var item:Object = _dataProvider.getItemAt(i);
                items.push(item);
                newRendered[item] = true;
            }
            // Renderers whose items left the view are free.
            for (var key:Object in oldRenderers) {
                if (newRendered[key] == null) {
                    availableCellRenderers.push(oldRenderers[key]);
                    delete oldRenderers[key];
                }
            }
            renderedItems = newRendered;
            invalidItems = new Dictionary(true);

            var rowWidth:Number = availableWidth + _maxHorizontalScrollPosition;
            for (i = 0; i < items.length; i++) {
                var index:int = startIndex + i;
                var data:Object = items[i];
                var reused:Boolean = oldRenderers[data] != null;
                if (reused) {
                    renderer = oldRenderers[data] as ICellRenderer;
                    delete oldRenderers[data];
                } else if (availableCellRenderers.length > 0) {
                    renderer = availableCellRenderers.pop() as ICellRenderer;
                } else {
                    renderer = makeRenderer();
                    if (renderer == null) break;
                }
                var display:DisplayObject = renderer as DisplayObject;
                if (display != null && display.parent != list) {
                    list.addChild(display);
                }
                activeCellRenderers.push(renderer);
                renderer.y = rowHeight * i;
                renderer.x = 0;
                renderer.setSize(rowWidth, rowHeight);
                renderer.listData = new ListData(itemToLabel(data), itemToIcon(data), this, index, index, 0);
                renderer.data = data;
                renderer.selected = _selectedIndices.indexOf(index) != -1;
                if (renderer is UIComponent) {
                    (renderer as UIComponent).enabled = enabled;
                    (renderer as UIComponent).drawNow();
                }
            }
            // Free renderers leave the list.
            for (i = 0; i < availableCellRenderers.length; i++) {
                var free:DisplayObject = availableCellRenderers[i] as DisplayObject;
                if (free != null && free.parent == list) list.removeChild(free);
            }
        }

        // A new renderer of the cellRenderer style, listening to the mouse.
        private function makeRenderer():ICellRenderer {
            var style:Object = getStyleValue("cellRenderer");
            var made:Object = null;
            if (style is Class) {
                made = new (style as Class)();
            } else if (style != null) {
                made = getDisplayObjectInstance(style);
            }
            var renderer:ICellRenderer = made as ICellRenderer;
            if (renderer == null) return null;
            var display:DisplayObject = made as DisplayObject;
            if (display != null) {
                display.addEventListener(MouseEvent.CLICK, handleCellRendererClick, false, 0, true);
                display.addEventListener(MouseEvent.ROLL_OVER, handleCellRendererMouseEvent, false, 0, true);
                display.addEventListener(MouseEvent.ROLL_OUT, handleCellRendererMouseEvent, false, 0, true);
                display.addEventListener(Event.CHANGE, handleCellRendererChange, false, 0, true);
                display.addEventListener(MouseEvent.DOUBLE_CLICK, handleCellRendererDoubleClick, false, 0, true);
                setNativeProperty(display, "doubleClickEnabled", true);
            }
            var component:UIComponent = made as UIComponent;
            if (component != null) {
                for (var name:String in rendererStyles) {
                    component.setStyle(name, rendererStyles[name]);
                }
            }
            return renderer;
        }

        override protected function moveSelectionVertically(code:uint, shiftKey:Boolean, ctrlKey:Boolean):void {
            var pageSize:int = Math.max(Math.floor(calculateAvailableHeight() / rowHeight), 1);
            var index:int = selectedIndex;
            switch (code) {
                case Keyboard.UP: index = index < 0 ? 0 : index - 1; break;
                case Keyboard.DOWN: index = index + 1; break;
                case Keyboard.PAGE_UP: index = index - pageSize; break;
                case Keyboard.PAGE_DOWN: index = index + pageSize; break;
                case Keyboard.HOME: index = 0; break;
                case Keyboard.END: index = length - 1; break;
            }
            selectByKeyboard(index);
        }

        override protected function defaultSize():Array {
            return [100, 100];
        }
    }
}
