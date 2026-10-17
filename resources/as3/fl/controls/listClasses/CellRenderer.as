package fl.controls.listClasses {
    import fl.controls.LabelButton;
    import flash.events.MouseEvent;

    /**
     * The row of a list: a background for each state (CellRenderer_upSkin,
     * overSkin, downSkin, selectedUpSkin ...), the item's icon at the left
     * and its label after it. The list sets it selected; a click does not.
     */
    public class CellRenderer extends LabelButton implements ICellRenderer {
        protected var _listData:ListData;
        protected var _data:Object;

        private static var defaultStyles:Object = {
            upSkin: "CellRenderer_upSkin",
            downSkin: "CellRenderer_downSkin",
            overSkin: "CellRenderer_overSkin",
            disabledSkin: "CellRenderer_disabledSkin",
            selectedDisabledSkin: "CellRenderer_selectedDisabledSkin",
            selectedUpSkin: "CellRenderer_selectedUpSkin",
            selectedDownSkin: "CellRenderer_selectedDownSkin",
            selectedOverSkin: "CellRenderer_selectedOverSkin",
            textFormat: null,
            disabledTextFormat: null,
            embedFonts: null,
            textPadding: 5
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function CellRenderer() {
            super();
            toggle = true;
            focusEnabled = false;
        }

        override protected function defaultSize():Array {
            return [152, 22];
        }

        override public function setSize(width:Number, height:Number):void {
            super.setSize(width, height);
        }

        public function get listData():ListData {
            return _listData;
        }

        public function set listData(value:ListData):void {
            _listData = value;
            label = _listData.label;
            setStyle("icon", _listData.icon);
        }

        public function get data():Object {
            return _data;
        }

        public function set data(value:Object):void {
            _data = value;
        }

        override public function get selected():Boolean {
            return super.selected;
        }

        override public function set selected(value:Boolean):void {
            super.selected = value;
        }

        // The list selects; a click alone does not.
        override protected function toggleSelected(event:MouseEvent):void {}

        /** The icon at the left, the label after it, both in the middle of the row. */
        override protected function drawLayout():void {
            var padding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(padding)) padding = 0;
            var textX:Number = 0;
            if (icon != null) {
                icon.x = padding;
                icon.y = Math.round((height - icon.height) / 2);
                textX = icon.width + padding;
            }
            if (label.length > 0) {
                textField.visible = true;
                textField.width = Math.max(0, width - textX - padding * 2);
                textField.height = textField.textHeight + 4;
                textField.x = textX + padding;
                textField.y = Math.round((height - textField.height) / 2);
            } else {
                textField.visible = false;
            }
            if (background != null) {
                background.width = width;
                background.height = height;
            }
        }
    }
}
