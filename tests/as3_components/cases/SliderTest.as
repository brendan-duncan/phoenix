package cases {
    import fl.controls.Slider;
    import fl.controls.SliderDirection;
    import fl.events.SliderEvent;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.ui.Keyboard;

    public class SliderTest {
        private var events:Array = [];

        private function record(event:SliderEvent):void {
            events.push(event.type + ":" + event.value + ":" + event.clickTarget + ":" + event.triggerEvent);
        }

        private function listen(s:Slider):void {
            s.addEventListener(SliderEvent.CHANGE, record);
            s.addEventListener(SliderEvent.THUMB_DRAG, record);
            s.addEventListener(SliderEvent.THUMB_PRESS, record);
            s.addEventListener(SliderEvent.THUMB_RELEASE, record);
        }

        public function run():void {
            var stage:Stage = new Stage();
            var s:TestSlider = new TestSlider();
            stage.addChild(s);
            listen(s);
            check(s.minimum == 0 && s.maximum == 10 && s.value == 0 && s.snapInterval == 0 && !s.liveDragging &&
                  s.direction == SliderDirection.HORIZONTAL, "defaults");

            // As BP_SliderControl does: width, then the range, then the value.
            s.width = 190;
            s.move(70, 0);
            s.minimum = -100;
            s.maximum = 100;
            s.value = 37.5;
            check(s.value == 37.5 && events.length == 0, "set in code: kept and no event");
            s.value = 500;
            check(s.value == 100, "clamped to the maximum");
            s.value = -500;
            check(s.value == -100, "clamped to the minimum");
            s.maximum = 50;
            check(s.value == -100 && s.maximum == 50, "maximum");
            s.value = 50;
            s.maximum = 20;
            check(s.value == 20, "lowering the maximum lowers the value");

            // Snapping.
            s.maximum = 100;
            s.snapInterval = 5;
            s.value = 12;
            check(s.value == 10, "snapped to 5: " + s.value);
            s.snapInterval = 0.25;
            s.value = 1.3;
            check(s.value == 1.25, "snapped to 0.25: " + s.value);
            s.snapInterval = 0;
            s.value = 1.3;
            check(s.value == 1.3, "no snapping");

            // The thumb sits at the value along the track.
            s.drawNow();
            check(Math.abs(s.thumbButton.x - (1.3 + 100) / 200 * 190) < 0.001, "thumb position " + s.thumbButton.x);
            check(s.trackButton.width == 190, "track as long as the slider");

            // Dragging: THUMB_DRAG as it moves, CHANGE on release (not live).
            events = [];
            s.value = 0;
            s.thumbButton.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            Stage.mouseStageX = 70 + 190 * 0.75;
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_MOVE));
            check(s.value == 50, "dragged to three quarters: " + s.value);
            Stage.mouseStageX = 70 + 190;
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_MOVE));
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_UP));
            check(events.join("|") == "thumbPress:0:thumb:mouse|thumbDrag:50:thumb:mouse|thumbDrag:100:thumb:mouse|" +
                  "thumbRelease:100:thumb:mouse|change:100:thumb:mouse", "drag events " + events.join("|"));

            // Live: CHANGE as it moves.
            events = [];
            s.liveDragging = true;
            s.thumbButton.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            Stage.mouseStageX = 70 + 190 / 2;
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_MOVE));
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_UP));
            check(events.join("|") == "thumbPress:100:thumb:mouse|change:0:thumb:mouse|thumbDrag:0:thumb:mouse|thumbRelease:0:thumb:mouse",
                  "live drag events " + events.join("|"));
            check(!stage.hasEventListener(MouseEvent.MOUSE_MOVE), "drag listeners removed");

            // A click on the track.
            events = [];
            Stage.mouseStageX = 70 + 190 * 0.25;
            s.trackButton.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(s.value == -50 && events.join("|") == "change:-50:track:mouse", "track click " + events.join("|"));

            // Keys.
            events = [];
            s.snapInterval = 10;
            s.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 0, Keyboard.RIGHT));
            check(s.value == -40 && events.join("|") == "change:-40:null:keyboard", "right key " + events.join("|"));
            s.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 0, Keyboard.END));
            check(s.value == 100, "end key");

            // Disabled.
            s.enabled = false;
            check(!s.trackButton.enabled && !s.thumbButton.enabled, "disabled parts");
            s.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 0, Keyboard.HOME));
            check(s.value == 100, "keys ignored when disabled");

            // An Event.CHANGE listener hears SliderEvent.CHANGE (bunnyPaint listens either way).
            var heard:int = 0;
            var t:Slider = new Slider();
            stage.addChild(t);
            t.maximum = 100;
            t.addEventListener(Event.CHANGE, function(e:Event):void { heard++; });
            Stage.mouseStageX = 40;
            t.drawNow();
            (t.getChildAt(0) as Object).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(heard == 1 && t.value == 50, "Event.CHANGE on a track click: " + heard + " " + t.value);
        }
    }
}

// Names in full: libas3 keeps only the first of several imports outside a package.
class TestSlider extends fl.controls.Slider {
    public function get thumbButton():fl.controls.BaseButton { return thumb; }
    public function get trackButton():fl.controls.BaseButton { return track; }
}
