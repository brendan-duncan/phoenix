package fl.controls {
    import fl.core.InvalidationType;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;

    /**
     * A push button with a label (and an optional icon), 100 by 22 by
     * default. Emphasized, it has a border around it (emphasizedSkin), as a
     * form's default button has.
     */
    public class Button extends LabelButton implements IFocusManagerComponent {
        protected var _emphasized:Boolean = false;
        protected var emphasizedBorder:DisplayObject;

        private static var defaultStyles:Object = {
            emphasizedSkin: "Button_emphasizedSkin",
            emphasizedPadding: 2
        };

        public static function getStyleDefinition():Object {
            return mergeStyles(LabelButton.getStyleDefinition(), defaultStyles);
        }

        public function Button() {
            super();
        }

        /** Whether the button has the emphasized border. */
        public function get emphasized():Boolean {
            return _emphasized;
        }

        public function set emphasized(value:Boolean):void {
            _emphasized = value;
            invalidate(InvalidationType.STYLES);
        }

        override protected function draw():void {
            var redraw:Boolean = isInvalid(InvalidationType.STYLES, InvalidationType.SIZE);
            super.draw();
            if (redraw) {
                drawEmphasized();
            }
        }

        protected function drawEmphasized():void {
            if (emphasizedBorder != null && emphasizedBorder.parent == this) {
                removeChild(emphasizedBorder);
            }
            emphasizedBorder = null;
            if (!_emphasized) return;
            emphasizedBorder = getDisplayObjectInstance(getStyleValue("emphasizedSkin"));
            if (emphasizedBorder == null) return;
            var padding:Number = Number(getStyleValue("emphasizedPadding"));
            if (isNaN(padding)) padding = 0;
            emphasizedBorder.x = -padding;
            emphasizedBorder.y = -padding;
            emphasizedBorder.width = width + padding * 2;
            emphasizedBorder.height = height + padding * 2;
            addChildAt(emphasizedBorder, 0);
        }

        override public function drawFocus(focused:Boolean):void {
            super.drawFocus(focused);
            if (focused && uiFocusRect != null && _emphasized) {
                // Around the emphasized border, not under it.
                var padding:Number = Number(getStyleValue("emphasizedPadding")) + Number(getStyleValue("focusRectPadding"));
                if (!isNaN(padding)) {
                    uiFocusRect.x = -padding;
                    uiFocusRect.y = -padding;
                    uiFocusRect.width = width + padding * 2;
                    uiFocusRect.height = height + padding * 2;
                }
            }
        }
    }
}
