package fl.controls {
    import fl.containers.BaseScrollPane;
    import fl.controls.listClasses.CellRenderer;
    import fl.controls.listClasses.ICellRenderer;
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.data.DataProvider;
    import fl.events.DataChangeEvent;
    import fl.events.DataChangeType;
    import fl.events.ListEvent;
    import fl.managers.IFocusManagerComponent;
    import flash.display.Sprite;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.geom.Rectangle;
    import flash.ui.Keyboard;
    import flash.utils.Dictionary;

    /** Dispatched when the user changes the selection (not when it is set in code). */
    [Event(name="change", type="flash.events.Event")]
    /** Dispatched when an item is clicked; cancel it to keep the selection. */
    [Event(name="itemClick", type="fl.events.ListEvent")]
    [Event(name="itemDoubleClick", type="fl.events.ListEvent")]
    [Event(name="itemRollOver", type="fl.events.ListEvent")]
    [Event(name="itemRollOut", type="fl.events.ListEvent")]

    /**
     * The base of the list components: items from a DataProvider shown by
     * cell renderers (the cellRenderer style), one or several of them
     * selected, scrolling.
     *
     * The selection is a list of indices into the data provider; it
     * follows the items as the provider adds, removes and sorts them.
     * Setting it in code dispatches nothing; the user selecting by click or
     * key dispatches Event.CHANGE (a click on the one item already
     * selected changes nothing and dispatches nothing).
     */
    public class SelectableList extends BaseScrollPane implements IFocusManagerComponent {
        protected var listHolder:Sprite;
        protected var list:Sprite;
        protected var _dataProvider:DataProvider;
        protected var activeCellRenderers:Array = [];
        protected var availableCellRenderers:Array = [];
        protected var renderedItems:Dictionary = new Dictionary(true);
        protected var invalidItems:Dictionary = new Dictionary(true);
        protected var _horizontalScrollPosition:Number = 0;
        protected var _verticalScrollPosition:Number = 0;
        protected var _allowMultipleSelection:Boolean = false;
        protected var _selectable:Boolean = true;
        protected var _selectedIndices:Array = [];
        protected var caretIndex:int = -1;
        protected var lastCaretIndex:int = -1;
        protected var preChangeItems:Array;
        protected var rendererStyles:Object = {};
        protected var updatedRendererStyles:Object = {};

        private static var defaultStyles:Object = {
            skin: "List_skin",
            cellRenderer: CellRenderer,
            contentPadding: null,
            disabledAlpha: null
        };

        public static function getStyleDefinition():Object {
            return mergeStyles(defaultStyles, BaseScrollPane.getStyleDefinition());
        }

        public function SelectableList() {
            super();
            if (_dataProvider == null) {
                dataProvider = new DataProvider();
            }
            verticalScrollPolicy = ScrollPolicy.AUTO;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            list.mouseChildren = _enabled;
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        /** The items. Setting it clears the selection. */
        public function get dataProvider():DataProvider {
            return _dataProvider;
        }

        public function set dataProvider(value:DataProvider):void {
            if (_dataProvider != null) {
                _dataProvider.removeEventListener(DataChangeEvent.DATA_CHANGE, handleDataChange);
                _dataProvider.removeEventListener(DataChangeEvent.PRE_DATA_CHANGE, onPreChange);
            }
            _dataProvider = value;
            _dataProvider.addEventListener(DataChangeEvent.DATA_CHANGE, handleDataChange, false, 0, true);
            _dataProvider.addEventListener(DataChangeEvent.PRE_DATA_CHANGE, onPreChange, false, 0, true);
            clearSelection();
            invalidateList();
        }

        override public function get maxHorizontalScrollPosition():Number {
            return _maxHorizontalScrollPosition;
        }

        public function set maxHorizontalScrollPosition(value:Number):void {
            _maxHorizontalScrollPosition = value;
            invalidate(InvalidationType.SIZE);
        }

        /** The number of items. */
        public function get length():uint {
            return _dataProvider.length;
        }

        public function get allowMultipleSelection():Boolean {
            return _allowMultipleSelection;
        }

        public function set allowMultipleSelection(value:Boolean):void {
            if (value == _allowMultipleSelection) return;
            _allowMultipleSelection = value;
            if (!value && _selectedIndices.length > 1) {
                _selectedIndices = [_selectedIndices.pop()];
                invalidate(InvalidationType.DATA);
            }
        }

        /** Whether the user can select items. */
        public function get selectable():Boolean {
            return _selectable;
        }

        public function set selectable(value:Boolean):void {
            if (value == _selectable) return;
            if (!value) {
                selectedIndices = [];
            }
            _selectable = value;
        }

        /** The index of the selected item (the last one selected, with several); -1 for none. */
        public function get selectedIndex():int {
            return _selectedIndices.length == 0 ? -1 : _selectedIndices[_selectedIndices.length - 1];
        }

        public function set selectedIndex(value:int):void {
            selectedIndices = value == -1 ? null : [value];
        }

        public function get selectedIndices():Array {
            return _selectedIndices.concat();
        }

        public function set selectedIndices(value:Array):void {
            if (!_selectable) return;
            _selectedIndices = value == null ? [] : value.concat();
            invalidate(InvalidationType.SELECTED);
        }

        /** The selected item, or null. Set to an item not in the list, it clears the selection. */
        public function get selectedItem():Object {
            return _selectedIndices.length == 0 ? null : _dataProvider.getItemAt(selectedIndex);
        }

        public function set selectedItem(value:Object):void {
            selectedIndex = _dataProvider.getItemIndex(value);
        }

        public function get selectedItems():Array {
            var items:Array = [];
            for (var i:int = 0; i < _selectedIndices.length; i++) {
                items.push(_dataProvider.getItemAt(_selectedIndices[i]));
            }
            return items;
        }

        public function set selectedItems(value:Array):void {
            if (value == null) {
                selectedIndices = null;
                return;
            }
            var indices:Array = [];
            for (var i:int = 0; i < value.length; i++) {
                var index:int = _dataProvider.getItemIndex(value[i]);
                if (index != -1) indices.push(index);
            }
            selectedIndices = indices;
        }

        /** The number of rows that fit. */
        public function get rowCount():uint {
            return 0;
        }

        public function clearSelection():void {
            selectedIndex = -1;
        }

        /** The renderer showing an item, if it is shown. */
        public function itemToCellRenderer(item:Object):ICellRenderer {
            if (item == null) return null;
            for each (var renderer:ICellRenderer in activeCellRenderers) {
                if (renderer.data == item) return renderer;
            }
            return null;
        }

        public function addItem(item:Object):void {
            _dataProvider.addItem(item);
            invalidateList();
        }

        public function addItemAt(item:Object, index:uint):void {
            _dataProvider.addItemAt(item, index);
            invalidateList();
        }

        public function removeAll():void {
            _dataProvider.removeAll();
        }

        public function getItemAt(index:uint):Object {
            return _dataProvider.getItemAt(index);
        }

        public function removeItem(item:Object):Object {
            return _dataProvider.removeItem(item);
        }

        public function removeItemAt(index:uint):Object {
            return _dataProvider.removeItemAt(index);
        }

        public function replaceItemAt(item:Object, index:uint):Object {
            return _dataProvider.replaceItemAt(item, index);
        }

        /** Redraws every row. */
        public function invalidateList():void {
            _invalidateList();
            invalidate(InvalidationType.DATA);
        }

        /** Redraws the row of an item, if shown. */
        public function invalidateItem(item:Object):void {
            if (renderedItems[item] == null) return;
            invalidItems[item] = true;
            invalidate(InvalidationType.DATA);
        }

        public function invalidateItemAt(index:uint):void {
            var item:Object = _dataProvider.getItemAt(index);
            if (item != null) invalidateItem(item);
        }

        public function sortItems(...sortArgs):* {
            return _dataProvider.sort.apply(_dataProvider, sortArgs);
        }

        public function sortItemsOn(field:String, options:Object = null):* {
            return _dataProvider.sortOn(field, options);
        }

        public function isItemSelected(item:Object):Boolean {
            return selectedItems.indexOf(item) > -1;
        }

        /** Scrolls the selected item into view. */
        public function scrollToSelected():void {
            scrollToIndex(selectedIndex);
        }

        /** Scrolls the item at an index into view. */
        public function scrollToIndex(newCaretIndex:int):void {}

        public function getRendererStyle(name:String, column:int = -1):Object {
            return rendererStyles[name];
        }

        /** A style for every cell renderer. */
        public function setRendererStyle(name:String, style:Object, column:uint = 0):void {
            if (rendererStyles[name] == style) return;
            updatedRendererStyles[name] = style;
            rendererStyles[name] = style;
            invalidate(InvalidationType.RENDERER_STYLES);
        }

        public function clearRendererStyle(name:String, column:int = -1):void {
            delete rendererStyles[name];
            updatedRendererStyles[name] = null;
            invalidate(InvalidationType.RENDERER_STYLES);
        }

        override protected function configUI():void {
            super.configUI();
            listHolder = new Sprite();
            addChild(listHolder);
            listHolder.scrollRect = contentScrollRect;
            list = new Sprite();
            listHolder.addChild(list);
        }

        protected function _invalidateList():void {
            availableCellRenderers = [];
            while (activeCellRenderers.length > 0) {
                var renderer:Object = activeCellRenderers.pop();
                if (renderer.parent == list) list.removeChild(renderer);
            }
        }

        protected function handleDataChange(event:DataChangeEvent):void {
            var startIndex:int = event.startIndex;
            var endIndex:int = event.endIndex;
            var changeType:String = event.changeType;
            var i:int;
            if (changeType == DataChangeType.INVALIDATE_ALL) {
                clearSelection();
                invalidateList();
            } else if (changeType == DataChangeType.INVALIDATE) {
                for (i = 0; i < event.items.length; i++) {
                    invalidateItem(event.items[i]);
                }
            } else if (changeType == DataChangeType.ADD) {
                var added:int = event.items.length;
                for (i = 0; i < _selectedIndices.length; i++) {
                    if (_selectedIndices[i] >= startIndex) {
                        _selectedIndices[i] += added;
                    }
                }
            } else if (changeType == DataChangeType.REMOVE) {
                var removed:int = event.items.length;
                var kept:Array = [];
                for (i = 0; i < _selectedIndices.length; i++) {
                    var index:int = _selectedIndices[i];
                    if (index < startIndex) {
                        kept.push(index);
                    } else if (index >= startIndex + removed) {
                        kept.push(index - removed);
                    }
                }
                _selectedIndices = kept;
            } else if (changeType == DataChangeType.REMOVE_ALL) {
                clearSelection();
            } else if (changeType == DataChangeType.REPLACE) {
                // The same index, a new item.
            } else {
                // Sorted: the same items selected, at their new indices.
                selectedItems = preChangeItems;
                preChangeItems = null;
            }
            invalidate(InvalidationType.DATA);
        }

        protected function onPreChange(event:DataChangeEvent):void {
            switch (event.changeType) {
                case DataChangeType.REMOVE:
                case DataChangeType.ADD:
                case DataChangeType.INVALIDATE:
                case DataChangeType.REMOVE_ALL:
                case DataChangeType.REPLACE:
                case DataChangeType.INVALIDATE_ALL:
                    break;
                default:
                    preChangeItems = selectedItems;
                    break;
            }
        }

        protected function handleCellRendererMouseEvent(event:MouseEvent):void {
            var renderer:ICellRenderer = event.currentTarget as ICellRenderer;
            var type:String = event.type == MouseEvent.ROLL_OVER ? ListEvent.ITEM_ROLL_OVER : ListEvent.ITEM_ROLL_OUT;
            dispatchEvent(new ListEvent(type, false, false, renderer.listData.column, renderer.listData.row,
                                        renderer.listData.index, renderer.data));
        }

        /**
         * A click on a row: ITEM_CLICK (which can be cancelled), then the
         * selection (with Ctrl and Shift for several, when allowed) and
         * CHANGE.
         */
        protected function handleCellRendererClick(event:MouseEvent):void {
            if (!_enabled) return;
            var renderer:ICellRenderer = event.currentTarget as ICellRenderer;
            var itemIndex:int = renderer.listData.index;
            var clicked:ListEvent = new ListEvent(ListEvent.ITEM_CLICK, false, true, renderer.listData.column,
                                                  renderer.listData.row, itemIndex, renderer.data);
            if (!dispatchEvent(clicked) || !_selectable) return;

            var selectIndex:int = _selectedIndices.indexOf(itemIndex);
            var i:int;
            if (!_allowMultipleSelection) {
                if (selectIndex != -1) {
                    return;
                }
                renderer.selected = true;
                _selectedIndices = [itemIndex];
                lastCaretIndex = caretIndex = itemIndex;
            } else if (event.shiftKey) {
                var oldIndex:int = _selectedIndices.length > 0 ? _selectedIndices[0] : itemIndex;
                _selectedIndices = [];
                if (oldIndex > itemIndex) {
                    for (i = oldIndex; i >= itemIndex; i--) _selectedIndices.push(i);
                } else {
                    for (i = oldIndex; i <= itemIndex; i++) _selectedIndices.push(i);
                }
                caretIndex = itemIndex;
            } else if (event.ctrlKey) {
                if (selectIndex != -1) {
                    renderer.selected = false;
                    _selectedIndices.splice(selectIndex, 1);
                } else {
                    renderer.selected = true;
                    _selectedIndices.push(itemIndex);
                }
                caretIndex = itemIndex;
            } else {
                _selectedIndices = [itemIndex];
                lastCaretIndex = caretIndex = itemIndex;
            }
            dispatchEvent(new Event(Event.CHANGE));
            invalidate(InvalidationType.DATA);
        }

        protected function handleCellRendererChange(event:Event):void {
            var renderer:ICellRenderer = event.currentTarget as ICellRenderer;
            var itemIndex:uint = renderer.listData.index;
            _dataProvider.invalidateItemAt(itemIndex);
        }

        protected function handleCellRendererDoubleClick(event:MouseEvent):void {
            if (!_enabled) return;
            var renderer:ICellRenderer = event.currentTarget as ICellRenderer;
            dispatchEvent(new ListEvent(ListEvent.ITEM_DOUBLE_CLICK, false, true, renderer.listData.column,
                                        renderer.listData.row, renderer.listData.index, renderer.data));
        }

        override protected function setHorizontalScrollPosition(scroll:Number, fireEvent:Boolean = false):void {
            if (scroll == _horizontalScrollPosition) return;
            _horizontalScrollPosition = scroll;
            invalidate(InvalidationType.SCROLL);
        }

        override protected function setVerticalScrollPosition(scroll:Number, fireEvent:Boolean = false):void {
            if (scroll == _verticalScrollPosition) return;
            _verticalScrollPosition = scroll;
            invalidate(InvalidationType.SCROLL);
        }

        override protected function draw():void {
            super.draw();
        }

        override protected function drawLayout():void {
            super.drawLayout();
            contentScrollRect = listHolder.scrollRect;
            if (contentScrollRect == null) contentScrollRect = new Rectangle();
            contentScrollRect.width = availableWidth;
            contentScrollRect.height = availableHeight;
            listHolder.scrollRect = contentScrollRect;
        }

        /** Gives the renderers the styles set for them since the last draw. */
        protected function updateRendererStyles():void {
            var renderers:Array = availableCellRenderers.concat(activeCellRenderers);
            for (var i:int = 0; i < renderers.length; i++) {
                if (renderers[i].setStyle == null) continue;
                for (var name:String in updatedRendererStyles) {
                    renderers[i].setStyle(name, updatedRendererStyles[name]);
                }
                renderers[i].drawNow();
            }
            updatedRendererStyles = {};
        }

        /** Moves the selection by the arrow keys (lists override). */
        protected function moveSelectionHorizontally(code:uint, shiftKey:Boolean, ctrlKey:Boolean):void {}

        protected function moveSelectionVertically(code:uint, shiftKey:Boolean, ctrlKey:Boolean):void {}

        override protected function keyDownHandler(event:KeyboardEvent):void {
            if (!selectable) return;
            switch (event.keyCode) {
                case Keyboard.UP:
                case Keyboard.DOWN:
                case Keyboard.END:
                case Keyboard.HOME:
                case Keyboard.PAGE_UP:
                case Keyboard.PAGE_DOWN:
                    moveSelectionVertically(event.keyCode, event.shiftKey && _allowMultipleSelection,
                                            event.ctrlKey && _allowMultipleSelection);
                    event.stopPropagation();
                    break;
                case Keyboard.LEFT:
                case Keyboard.RIGHT:
                    moveSelectionHorizontally(event.keyCode, event.shiftKey && _allowMultipleSelection,
                                              event.ctrlKey && _allowMultipleSelection);
                    event.stopPropagation();
                    break;
            }
        }

        /** Selects an index by the keyboard, scrolling it into view; dispatches CHANGE. */
        protected function selectByKeyboard(index:int):void {
            if (length == 0) return;
            index = Math.max(0, Math.min(length - 1, index));
            scrollToIndex(index);
            if (_selectedIndices.length == 1 && _selectedIndices[0] == index) return;
            _selectedIndices = [index];
            lastCaretIndex = caretIndex = index;
            dispatchEvent(new Event(Event.CHANGE));
            invalidate(InvalidationType.DATA);
        }
    }
}
