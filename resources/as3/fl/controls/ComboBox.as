package fl.controls {
    import fl.controls.listClasses.ICellRenderer;
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.data.DataProvider;
    import fl.events.ComponentEvent;
    import fl.events.DataChangeEvent;
    import fl.events.ListEvent;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.FocusEvent;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.geom.Point;
    import flash.text.TextField;
    import flash.ui.Keyboard;

    /** Dispatched when the user changes the selection (or, editable, the text). */
    [Event(name="change", type="flash.events.Event")]
    /** Dispatched when the drop-down list opens. */
    [Event(name="open", type="flash.events.Event")]
    /** Dispatched when the drop-down list closes. */
    [Event(name="close", type="flash.events.Event")]
    /** Dispatched when Enter is pressed in an editable combo box. */
    [Event(name="enter", type="fl.events.ComponentEvent")]
    [Event(name="itemRollOver", type="fl.events.ListEvent")]
    [Event(name="itemRollOut", type="fl.events.ListEvent")]

    /**
     * A button showing the selected item that drops a list of the items
     * down (or up, when there is no room below), rowCount (5) rows high.
     * Editable, the text can be typed. 100 by 22 by default.
     *
     * Without a prompt, the first item is selected as soon as there is
     * one. Setting the selection in code dispatches nothing; the user
     * picking a different item dispatches Event.CHANGE.
     */
    public class ComboBox extends UIComponent implements IFocusManagerComponent {
        protected var inputField:TextInput;
        protected var background:BaseButton;
        protected var list:List;
        protected var _rowCount:uint = 5;
        protected var _editable:Boolean = false;
        protected var isOpen:Boolean = false;
        protected var highlightedCell:int = -1;
        protected var editableValue:String;
        protected var _prompt:String;
        protected var isKeyDown:Boolean = false;
        protected var currentIndex:int;
        protected var listOverIndex:uint;
        protected var _dropdownWidth:Number;
        private var listStage:Stage = null;

        private static var defaultStyles:Object = {
            upSkin: "ComboBox_upSkin",
            downSkin: "ComboBox_downSkin",
            overSkin: "ComboBox_overSkin",
            disabledSkin: "ComboBox_disabledSkin",
            focusRectSkin: null,
            focusRectPadding: null,
            textFormat: null,
            disabledTextFormat: null,
            textPadding: 3,
            buttonWidth: 24,
            disabledAlpha: null,
            listSkin: null
        };

        protected static const LIST_STYLES:Object = {
            upSkin: "comboListUpSkin",
            overSkin: "comboListOverSkin",
            downSkin: "comboListDownSkin",
            disabledSkin: "comboListDisabledSkin",
            downArrowDisabledSkin: "downArrowDisabledSkin",
            downArrowDownSkin: "downArrowDownSkin",
            downArrowOverSkin: "downArrowOverSkin",
            downArrowUpSkin: "downArrowUpSkin",
            upArrowDisabledSkin: "upArrowDisabledSkin",
            upArrowDownSkin: "upArrowDownSkin",
            upArrowOverSkin: "upArrowOverSkin",
            upArrowUpSkin: "upArrowUpSkin",
            thumbDisabledSkin: "thumbDisabledSkin",
            thumbDownSkin: "thumbDownSkin",
            thumbOverSkin: "thumbOverSkin",
            thumbUpSkin: "thumbUpSkin",
            thumbIcon: "thumbIcon",
            trackDisabledSkin: "trackDisabledSkin",
            trackDownSkin: "trackDownSkin",
            trackOverSkin: "trackOverSkin",
            trackUpSkin: "trackUpSkin",
            skin: "listSkin",
            cellRenderer: "cellRenderer",
            contentPadding: "contentPadding",
            repeatDelay: "repeatDelay",
            repeatInterval: "repeatInterval",
            disabledAlpha: "disabledAlpha"
        };

        protected static const BACKGROUND_STYLES:Object = {
            overSkin: "overSkin",
            downSkin: "downSkin",
            upSkin: "upSkin",
            disabledSkin: "disabledSkin",
            repeatInterval: "repeatInterval"
        };

        protected static const TEXT_INPUT_STYLES:Object = {
            textFormat: "textFormat",
            disabledTextFormat: "disabledTextFormat",
            embedFonts: "embedFonts"
        };

        public static function getStyleDefinition():Object {
            return mergeStyles(defaultStyles, List.getStyleDefinition());
        }

        public function ComboBox() {
            super();
        }

        override protected function defaultSize():Array {
            return [100, 22];
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            if (!value) close();
            background.enabled = value;
        }

        /** Whether the text can be typed. */
        public function get editable():Boolean {
            return _editable;
        }

        public function set editable(value:Boolean):void {
            _editable = value;
            drawTextField();
        }

        /** The most rows the drop-down shows before it scrolls. */
        public function get rowCount():uint {
            return _rowCount;
        }

        public function set rowCount(value:uint):void {
            _rowCount = value;
            invalidate(InvalidationType.SIZE);
        }

        public function get restrict():String {
            return inputField.restrict;
        }

        public function set restrict(value:String):void {
            inputField.restrict = value;
        }

        public function get selectedIndex():int {
            return list.selectedIndex;
        }

        public function set selectedIndex(value:int):void {
            list.selectedIndex = value;
            highlightCell();
            invalidate(InvalidationType.SELECTED);
        }

        /** The text typed (editable), or the selected item's label. */
        public function get text():String {
            return inputField.text;
        }

        public function set text(value:String):void {
            if (!editable) return;
            inputField.text = value;
        }

        public function get labelField():String {
            return list.labelField;
        }

        public function set labelField(value:String):void {
            list.labelField = value;
            invalidate(InvalidationType.DATA);
        }

        public function get labelFunction():Function {
            return list.labelFunction;
        }

        public function set labelFunction(value:Function):void {
            list.labelFunction = value;
            invalidate(InvalidationType.DATA);
        }

        /** The label an item shows. */
        public function itemToLabel(item:Object):String {
            if (item == null) return "";
            return list.itemToLabel(item);
        }

        public function get selectedItem():Object {
            return list.selectedItem;
        }

        public function set selectedItem(value:Object):void {
            list.selectedItem = value;
            invalidate(InvalidationType.SELECTED);
        }

        /** The list that drops down. */
        public function get dropdown():List {
            return list;
        }

        /** The number of items. */
        public function get length():int {
            return list.length;
        }

        /** The text field the selection shows in (and is typed in, editable). */
        public function get textField():TextInput {
            return inputField;
        }

        /** The text typed (editable), else the selected item's data, or its label when it has none. */
        public function get value():String {
            if (editableValue != null) return editableValue;
            var item:Object = selectedItem;
            if (!_editable && item != null && item.data != null) return item.data;
            return itemToLabel(item);
        }

        public function get dataProvider():DataProvider {
            return list.dataProvider;
        }

        public function set dataProvider(value:DataProvider):void {
            list.dataProvider = value;
            listenToData();
            selectFirst();
            invalidate(InvalidationType.DATA);
        }

        /** The drop-down's width: the combo box's unless set. */
        public function get dropdownWidth():Number {
            return list.width;
        }

        public function set dropdownWidth(value:Number):void {
            _dropdownWidth = value;
            invalidate(InvalidationType.SIZE);
        }

        public function addItem(item:Object):void {
            list.addItem(item);
            invalidate(InvalidationType.DATA);
        }

        public function addItemAt(item:Object, index:uint):void {
            list.addItemAt(item, index);
            invalidate(InvalidationType.DATA);
        }

        public function removeAll():void {
            list.removeAll();
            inputField.text = "";
            invalidate(InvalidationType.DATA);
        }

        public function removeItem(item:Object):Object {
            return list.removeItem(item);
        }

        public function removeItemAt(index:uint):void {
            list.removeItemAt(index);
            invalidate(InvalidationType.DATA);
        }

        public function getItemAt(index:uint):Object {
            return list.getItemAt(index);
        }

        public function replaceItemAt(item:Object, index:uint):Object {
            return list.replaceItemAt(item, index);
        }

        public function sortItems(...sortArgs):* {
            return list.sortItems.apply(list, sortArgs);
        }

        public function sortItemsOn(field:String, options:Object = null):* {
            return list.sortItemsOn(field, options);
        }

        /** Shown, with no item selected, until the user picks one. */
        public function get prompt():String {
            return _prompt;
        }

        public function set prompt(value:String):void {
            _prompt = value == "" ? null : value;
            invalidate(InvalidationType.STATE);
        }

        /** The selected item's label (or the text typed, editable). */
        public function get selectedLabel():String {
            if (editableValue != null) return editableValue;
            if (selectedIndex == -1) return null;
            return itemToLabel(selectedItem);
        }

        override public function get imeMode():String {
            return inputField.imeMode;
        }

        override public function set imeMode(value:String):void {
            inputField.imeMode = value;
        }

        /** Drops the list down. Dispatches Event.OPEN. */
        public function open():void {
            currentIndex = selectedIndex;
            if (isOpen || length == 0 || stage == null) return;
            dispatchEvent(new Event(Event.OPEN));
            isOpen = true;
            // Listening for clicks elsewhere from the next frame, not this click.
            addEventListener(Event.ENTER_FRAME, addCloseListener, false, 0, true);
            listStage = stage;
            drawList();
            list.drawNow();
            positionList();
            listStage.addChild(list);
            list.scrollToSelected();
        }

        /** Closes the drop-down list. Dispatches Event.CLOSE when it was open. */
        public function close():void {
            highlightCell();
            highlightedCell = -1;
            if (!isOpen) return;
            dispatchEvent(new Event(Event.CLOSE));
            removeEventListener(Event.ENTER_FRAME, addCloseListener);
            if (listStage != null) {
                listStage.removeEventListener(MouseEvent.MOUSE_DOWN, onStageClick);
                if (list.parent == listStage) listStage.removeChild(list);
            }
            listStage = null;
            isOpen = false;
        }

        override protected function configUI():void {
            super.configUI();

            background = new BaseButton();
            background.focusEnabled = false;
            copyStylesToChild(background, BACKGROUND_STYLES);
            background.addEventListener(MouseEvent.MOUSE_DOWN, onToggleListVisibility, false, 0, true);
            addChild(background);

            inputField = new TextInput();
            inputField.focusTarget = this as IFocusManagerComponent;
            inputField.focusEnabled = false;
            inputField.addEventListener(Event.CHANGE, onTextInput, false, 0, true);
            addChild(inputField);

            list = new List();
            list.focusEnabled = false;
            copyStylesToChild(list, LIST_STYLES);
            list.addEventListener(Event.CHANGE, onListChange, false, 0, true);
            list.addEventListener(ListEvent.ITEM_CLICK, onListItemClick, false, 0, true);
            list.addEventListener(ListEvent.ITEM_ROLL_OUT, passEvent, false, 0, true);
            list.addEventListener(ListEvent.ITEM_ROLL_OVER, passEvent, false, 0, true);
            listenToData();
            addEventListener(Event.REMOVED_FROM_STAGE, onRemovedFromStage, false, 0, true);
        }

        private function listenToData():void {
            list.dataProvider.addEventListener(DataChangeEvent.DATA_CHANGE, onDataChange, false, 0, true);
        }

        // Without a prompt, an item is always selected while there are any.
        private function selectFirst():void {
            if (_prompt == null && editableValue == null && list.selectedIndex == -1 && list.length > 0) {
                list.selectedIndex = 0;
            }
        }

        private function onDataChange(event:DataChangeEvent):void {
            if (event.target != list.dataProvider) return;
            selectFirst();
            invalidate(InvalidationType.DATA);
        }

        private function onRemovedFromStage(event:Event):void {
            close();
        }

        protected function focusInHandlerOfInput(event:FocusEvent):void {}

        protected function addCloseListener(event:Event):void {
            removeEventListener(Event.ENTER_FRAME, addCloseListener);
            if (!isOpen || listStage == null) return;
            listStage.addEventListener(MouseEvent.MOUSE_DOWN, onStageClick, false, 0, true);
        }

        protected function onTextInput(event:Event):void {
            event.stopPropagation();
            if (!_editable) return;
            editableValue = inputField.text;
            selectedIndex = -1;
            dispatchEvent(new Event(Event.CHANGE));
        }

        protected function calculateAvailableHeight():Number {
            var padding:Number = Number(getStyleValue("contentPadding"));
            if (isNaN(padding)) padding = 0;
            return list.height - padding * 2;
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES)) {
                setStyles();
                setEmbedFonts();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.DATA, InvalidationType.STATE)) {
                drawTextFormat();
                drawLayout();
                invalidate(InvalidationType.DATA, false);
            }
            if (isInvalid(InvalidationType.DATA)) {
                drawList();
            }
            if (isInvalid(InvalidationType.STATE, InvalidationType.DATA, InvalidationType.SELECTED)) {
                drawTextField();
            }
            background.drawNow();
            inputField.drawNow();
            super.draw();
        }

        protected function setEmbedFonts():void {
            var embed:Object = getStyleValue("embedFonts");
            if (embed != null) {
                setNativeProperty(inputField.textField, "embedFonts", Boolean(embed));
            }
        }

        protected function drawTextFormat():void {
            copyStylesToChild(inputField, TEXT_INPUT_STYLES);
        }

        protected function setStyles():void {
            copyStylesToChild(background, BACKGROUND_STYLES);
            copyStylesToChild(list, LIST_STYLES);
            copyStylesToChild(inputField, TEXT_INPUT_STYLES);
        }

        protected function drawLayout():void {
            var buttonWidth:Number = Number(getStyleValue("buttonWidth"));
            var textPadding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(buttonWidth)) buttonWidth = 24;
            if (isNaN(textPadding)) textPadding = 0;
            background.setSize(width, height);
            inputField.x = inputField.y = textPadding;
            inputField.setSize(Math.max(0, width - buttonWidth - textPadding), Math.max(0, height - textPadding));
            list.width = isNaN(_dropdownWidth) ? width : _dropdownWidth;
            background.enabled = enabled;
            background.drawNow();
        }

        protected function drawTextField():void {
            // The combo box's own skin is the field's background.
            inputField.setStyle("upSkin", "");
            inputField.setStyle("disabledSkin", "");
            inputField.setStyle("textPadding", 0);
            inputField.enabled = enabled;
            inputField.editable = _editable;
            inputField.textField.selectable = enabled && _editable;
            inputField.mouseEnabled = inputField.mouseChildren = enabled && _editable;
            inputField.focusEnabled = false;
            if (editableValue != null) {
                inputField.text = editableValue;
            } else if (selectedIndex == -1 && _prompt != null) {
                inputField.text = _prompt;
            } else {
                var label:String = selectedLabel;
                inputField.text = label == null ? "" : label;
            }
            inputField.horizontalScrollPosition = 0;
        }

        protected function drawList():void {
            list.rowCount = Math.max(0, Math.min(_rowCount, list.dataProvider.length));
        }

        /** Below the combo box, or above it when the stage has no room below. */
        protected function positionList():void {
            if (listStage == null) return;
            var p:Point = localToGlobal(new Point(0, 0));
            p = listStage.globalToLocal(p);
            list.x = p.x;
            if (p.y + height + list.height > listStage.stageHeight) {
                list.y = p.y - list.height;
            } else {
                list.y = p.y + height;
            }
        }

        protected function onToggleListVisibility(event:MouseEvent):void {
            event.stopPropagation();
            if (isOpen) {
                close();
            } else {
                open();
            }
        }

        protected function onStageClick(event:MouseEvent):void {
            if (!isOpen) return;
            var target:DisplayObject = event.target as DisplayObject;
            if (target != null && (contains(target) || list.contains(target))) return;
            if (highlightedCell != -1) {
                selectedIndex = highlightedCell;
                dispatchEvent(new Event(Event.CHANGE));
            }
            close();
        }

        protected function passEvent(event:Event):void {
            dispatchEvent(event);
        }

        // A click on a row closes the list; the list then selects the row
        // and, when that is a change, dispatches CHANGE (onListChange).
        protected function onListItemClick(event:ListEvent):void {
            editableValue = null;
            close();
        }

        protected function onListChange(event:Event):void {
            editableValue = null;
            dispatchEvent(new Event(Event.CHANGE));
            invalidate(InvalidationType.DATA);
            if (isKeyDown) return;
            close();
        }

        protected function highlightCell(index:int = -1):void {
            var renderer:ICellRenderer;
            if (highlightedCell > -1) {
                renderer = list.itemToCellRenderer(getItemAt(highlightedCell));
                if (renderer != null) renderer.setMouseState("up");
            }
            if (index == -1) return;
            list.scrollToIndex(index);
            list.drawNow();
            renderer = list.itemToCellRenderer(getItemAt(index));
            if (renderer != null) {
                renderer.setMouseState("over");
                highlightedCell = index;
            }
        }

        override protected function keyDownHandler(event:KeyboardEvent):void {
            isKeyDown = true;
            if (event.ctrlKey) {
                switch (event.keyCode) {
                    case Keyboard.UP:
                        if (highlightedCell > -1) {
                            selectedIndex = highlightedCell;
                            dispatchEvent(new Event(Event.CHANGE));
                        }
                        close();
                        break;
                    case Keyboard.DOWN:
                        open();
                        break;
                }
                return;
            }
            event.stopPropagation();
            var pageSize:int = Math.max((calculateAvailableHeight() / list.rowHeight) << 0, 1);
            var sel:int = selectedIndex;
            var lastSel:int = (highlightedCell == -1) ? selectedIndex : highlightedCell;
            var newSel:int = -1;
            switch (event.keyCode) {
                case Keyboard.SPACE:
                    if (isOpen) close(); else open();
                    return;
                case Keyboard.ESCAPE:
                    if (isOpen) {
                        if (highlightedCell > -1) {
                            selectedIndex = selectedIndex;
                        }
                        close();
                    }
                    return;
                case Keyboard.UP:
                    newSel = Math.max(0, lastSel - 1);
                    break;
                case Keyboard.DOWN:
                    newSel = Math.min(length - 1, lastSel + 1);
                    break;
                case Keyboard.PAGE_UP:
                    newSel = Math.max(lastSel - pageSize, 0);
                    break;
                case Keyboard.PAGE_DOWN:
                    newSel = Math.min(lastSel + pageSize, length - 1);
                    break;
                case Keyboard.HOME:
                    newSel = 0;
                    break;
                case Keyboard.END:
                    newSel = length - 1;
                    break;
                case Keyboard.ENTER:
                    if (_editable && highlightedCell == -1) {
                        editableValue = inputField.text;
                        selectedIndex = -1;
                    } else if (isOpen && highlightedCell > -1) {
                        editableValue = null;
                        selectedIndex = highlightedCell;
                        dispatchEvent(new Event(Event.CHANGE));
                    }
                    dispatchEvent(new ComponentEvent(ComponentEvent.ENTER));
                    close();
                    return;
                default:
                    return;
            }
            if (newSel == -1 || newSel == lastSel) return;
            if (isOpen) {
                highlightCell(newSel);
                inputField.text = list.itemToLabel(getItemAt(newSel));
            } else {
                highlightCell();
                selectedIndex = newSel;
                dispatchEvent(new Event(Event.CHANGE));
            }
        }

        override protected function keyUpHandler(event:KeyboardEvent):void {
            isKeyDown = false;
        }

        override protected function focusOutHandler(event:FocusEvent):void {
            isKeyDown = false;
            // Leaving an editable combo box keeps what was typed.
            if (_editable && inputField.text != selectedLabel) {
                editableValue = inputField.text;
            }
            super.focusOutHandler(event);
        }

        override public function setFocus():void {
            if (stage == null) return;
            stage.focus = _editable ? inputField.textField : this;
        }

        override protected function isOurFocus(target:DisplayObject):Boolean {
            return target == inputField || target == inputField.textField || super.isOurFocus(target);
        }
    }
}
