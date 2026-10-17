package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ComponentEvent;
    import flash.display.DisplayObject;
    import flash.events.Event;
    import flash.events.MouseEvent;
    import flash.events.TimerEvent;
    import flash.utils.Timer;

    /** Dispatched while pressed: once on press, then repeatedly when autoRepeat is set. */
    [Event(name="buttonDown", type="fl.events.ComponentEvent")]
    /** Dispatched when selected changes by a click (a toggle button). */
    [Event(name="change", type="flash.events.Event")]

    /**
     * A button without a label: a background skin for each mouse state
     * (up, over, down, disabled, and their selected forms), which it shows
     * at its size, and press-and-hold repeating (autoRepeat) for scroll
     * arrows and steppers.
     *
     * The skins are styles naming symbols exported for ActionScript:
     * upSkin "Button_upSkin", overSkin, downSkin, disabledSkin,
     * selectedUpSkin, selectedOverSkin, selectedDownSkin and
     * selectedDisabledSkin.
     */
    public class BaseButton extends UIComponent {
        protected var background:DisplayObject;
        protected var mouseState:String;
        protected var _selected:Boolean = false;
        protected var _autoRepeat:Boolean = false;
        protected var pressTimer:Timer;
        private var _mouseStateLocked:Boolean = false;
        private var unlockedMouseState:String;
        private var pressStage:Object = null;

        private static var defaultStyles:Object = {
            upSkin: "Button_upSkin",
            downSkin: "Button_downSkin",
            overSkin: "Button_overSkin",
            disabledSkin: "Button_disabledSkin",
            selectedDisabledSkin: "Button_selectedDisabledSkin",
            selectedUpSkin: "Button_selectedUpSkin",
            selectedDownSkin: "Button_selectedDownSkin",
            selectedOverSkin: "Button_selectedOverSkin",
            focusRectSkin: null,
            focusRectPadding: null,
            repeatDelay: 500,
            repeatInterval: 35
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function BaseButton() {
            super();
            buttonMode = true;
            mouseChildren = false;
            useHandCursor = false;
            setupMouseEvents();
            setMouseState("up");
            pressTimer = new Timer(1, 0);
            pressTimer.addEventListener(TimerEvent.TIMER, buttonDown, false, 0, true);
        }

        override protected function defaultSize():Array {
            return [100, 22];
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            mouseEnabled = value;
        }

        public function get selected():Boolean {
            return _selected;
        }

        public function set selected(value:Boolean):void {
            if (_selected == value) return;
            _selected = value;
            invalidate(InvalidationType.STATE);
        }

        /** Whether holding the button down repeats buttonDown events. */
        public function get autoRepeat():Boolean {
            return _autoRepeat;
        }

        public function set autoRepeat(value:Boolean):void {
            _autoRepeat = value;
        }

        /** Keeps the look of the current mouse state while true (a dragged thumb). */
        public function set mouseStateLocked(value:Boolean):void {
            _mouseStateLocked = value;
            if (value == false) {
                setMouseState(unlockedMouseState);
            } else {
                unlockedMouseState = mouseState;
            }
        }

        /** Shows the look of a mouse state: "up", "over" or "down". */
        public function setMouseState(state:String):void {
            if (_mouseStateLocked) {
                unlockedMouseState = state;
                return;
            }
            if (mouseState == state) return;
            mouseState = state;
            invalidate(InvalidationType.STATE);
        }

        protected function setupMouseEvents():void {
            addEventListener(MouseEvent.ROLL_OVER, mouseEventHandler, false, 0, true);
            addEventListener(MouseEvent.MOUSE_DOWN, mouseEventHandler, false, 0, true);
            addEventListener(MouseEvent.MOUSE_UP, mouseEventHandler, false, 0, true);
            addEventListener(MouseEvent.ROLL_OUT, mouseEventHandler, false, 0, true);
        }

        protected function mouseEventHandler(event:MouseEvent):void {
            if (event.type == MouseEvent.MOUSE_DOWN) {
                setMouseState("down");
                startPress();
            } else if (event.type == MouseEvent.ROLL_OVER || event.type == MouseEvent.MOUSE_UP) {
                setMouseState("over");
                endPress();
            } else if (event.type == MouseEvent.ROLL_OUT) {
                setMouseState("up");
                endPress();
            }
        }

        protected function startPress():void {
            if (_autoRepeat) {
                pressTimer.delay = Number(getStyleValue("repeatDelay"));
                pressTimer.start();
            }
            // Released anywhere, the press ends.
            if (stage != null && pressStage == null) {
                pressStage = stage;
                stage.addEventListener(MouseEvent.MOUSE_UP, stageMouseUp, false, 0, true);
            }
            dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
        }

        private function stageMouseUp(event:MouseEvent):void {
            endPress();
            if (mouseState == "down") setMouseState("up");
        }

        protected function buttonDown(event:TimerEvent):void {
            if (!_autoRepeat) {
                endPress();
                return;
            }
            if (pressTimer.currentCount == 1) {
                pressTimer.delay = Number(getStyleValue("repeatInterval"));
            }
            dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
        }

        protected function endPress():void {
            pressTimer.reset();
            if (pressStage != null) {
                pressStage.removeEventListener(MouseEvent.MOUSE_UP, stageMouseUp);
                pressStage = null;
            }
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES, InvalidationType.STATE)) {
                drawBackground();
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.SIZE)) {
                drawLayout();
            }
            super.draw();
        }

        /** The name of the skin style for the current state: "upSkin", "selectedOverSkin", "disabledSkin"... */
        protected function stateStyleName(suffix:String):String {
            var name:String = enabled ? mouseState : "disabled";
            if (selected) {
                name = "selected" + name.substr(0, 1).toUpperCase() + name.substr(1);
            }
            return name + suffix;
        }

        protected function drawBackground():void {
            var old:DisplayObject = background;
            var made:DisplayObject = getDisplayObjectInstance(getStyleValue(stateStyleName("Skin")));
            background = made;
            if (background != null) {
                addChildAt(background, 0);
            }
            if (old != null && old != background && old.parent == this) {
                removeChild(old);
            }
        }

        protected function drawLayout():void {
            if (background != null) {
                background.width = width;
                background.height = height;
            }
        }
    }
}
