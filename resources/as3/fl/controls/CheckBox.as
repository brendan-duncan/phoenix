package fl.controls {
    import flash.display.DisplayObject;
    import flash.display.Graphics;
    import flash.display.Shape;

    /**
     * A box that a click checks or clears, with a label. Always a toggle:
     * the click flips selected (before the click reaches other listeners)
     * and dispatches Event.CHANGE. Its icons are the styles upIcon
     * "CheckBox_upIcon", overIcon, downIcon, disabledIcon and their selected
     * forms; it has no background, only an invisible area taking clicks
     * around the box and label.
     */
    public class CheckBox extends LabelButton {
        private static var defaultStyles:Object = {
            icon: null,
            upIcon: "CheckBox_upIcon",
            downIcon: "CheckBox_downIcon",
            overIcon: "CheckBox_overIcon",
            disabledIcon: "CheckBox_disabledIcon",
            selectedDisabledIcon: "CheckBox_selectedDisabledIcon",
            focusRectSkin: null,
            focusRectPadding: null,
            selectedUpIcon: "CheckBox_selectedUpIcon",
            selectedDownIcon: "CheckBox_selectedDownIcon",
            selectedOverIcon: "CheckBox_selectedOverIcon",
            textFormat: null,
            disabledTextFormat: null,
            embedFonts: null,
            textPadding: 5
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function CheckBox() {
            super();
        }

        /** Always true: a check box toggles. */
        override public function get toggle():Boolean {
            return true;
        }

        override public function set toggle(value:Boolean):void {
            throw new Error("Warning: You cannot change a CheckBox's toggle.");
        }

        /** Always false. */
        override public function get autoRepeat():Boolean {
            return false;
        }

        override public function set autoRepeat(value:Boolean):void {}

        override protected function configUI():void {
            super.configUI();
            super.toggle = true;
            // The area that takes the clicks, sized by drawLayout.
            var hit:Shape = new Shape();
            var g:Graphics = hit.graphics;
            g.beginFill(0, 0);
            g.drawRect(0, 0, 100, 100);
            g.endFill();
            background = hit;
            addChildAt(background, 0);
        }

        /** The background stays the click area. */
        override protected function drawBackground():void {}

        /**
         * The box at one side and the label next to it (labelPlacement RIGHT,
         * the default: box at the left), or above or below it; the click area
         * around both.
         */
        override protected function drawLayout():void {
            super.drawLayout();
            var padding:Number = Number(getStyleValue("textPadding"));
            if (isNaN(padding)) padding = 0;
            var iconWidth:Number = icon == null ? 0 : icon.width;
            var iconHeight:Number = icon == null ? 0 : icon.height;
            var iconX:Number = 0;
            var iconY:Number = icon == null ? Math.round(height / 2) : icon.y;
            switch (_labelPlacement) {
                case ButtonLabelPlacement.RIGHT:
                    iconX = padding;
                    textField.x = iconX + iconWidth + padding;
                    background.width = textField.x + textField.width + padding;
                    background.height = Math.max(textField.height, iconHeight) + padding * 2;
                    break;
                case ButtonLabelPlacement.LEFT:
                    iconX = width - iconWidth - padding;
                    textField.x = width - iconWidth - padding * 2 - textField.width;
                    background.width = textField.width + iconWidth + padding * 3;
                    background.height = Math.max(textField.height, iconHeight) + padding * 2;
                    break;
                default:
                    iconX = icon == null ? Math.round(width / 2) : icon.x;
                    background.width = Math.max(textField.width, iconWidth) + padding * 2;
                    background.height = textField.height + iconHeight + padding * 3;
                    break;
            }
            if (icon != null) {
                icon.x = iconX;
            }
            background.x = Math.min(iconX - padding, textField.x - padding);
            background.y = Math.min(iconY - padding, textField.y - padding);
        }

        override public function drawFocus(focused:Boolean):void {
            super.drawFocus(focused);
            if (focused && uiFocusRect != null && background != null) {
                // Around the box and label rather than the whole component.
                var padding:Number = Number(getStyleValue("focusRectPadding"));
                if (isNaN(padding)) padding = 0;
                uiFocusRect.x = background.x - padding;
                uiFocusRect.y = background.y - padding;
                uiFocusRect.width = background.width + padding * 2;
                uiFocusRect.height = background.height + padding * 2;
            }
        }
    }
}
