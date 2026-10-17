package cases {
    import fl.controls.NumericStepper;
    import fl.events.ComponentEvent;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.ui.Keyboard;

    public class NumericStepperTest {
        private var changes:int = 0;

        private function changed(event:Event):void {
            changes++;
        }

        public function run():void {
            var stage:Stage = new Stage();
            var n:TestStepper = new TestStepper();
            stage.addChild(n);
            n.addEventListener(Event.CHANGE, changed);
            check(n.minimum == 0 && n.maximum == 10 && n.stepSize == 1 && n.value == 1, "defaults");
            check(n.width == 80 && n.height == 22, "default size");

            // As BP_SliderControl does.
            n.width = 55;
            n.minimum = -100;
            n.maximum = 100;
            n.value = 0.8 * 100;
            check(n.value == 80 && changes == 0, "set in code: " + n.value + ", no event");
            n.value = 0.65;
            check(n.value == 1, "rounded to the step: " + n.value);
            n.value = 1000;
            check(n.value == 100, "clamped to the maximum");
            n.value = -1000;
            check(n.value == -100, "clamped to the minimum");
            n.value = NaN;
            check(n.value == -100, "NaN ignored");

            n.stepSize = 0.1;
            n.value = 0.35;
            check(n.value == 0.4 || n.value == 0.3, "a tenth: " + n.value);
            n.value = 0.3;
            check(n.nextValue == 0.4 && n.previousValue == 0.2, "next and previous: " + n.nextValue + " " + n.previousValue);
            n.stepSize = 1;

            // The arrows step and dispatch CHANGE.
            n.value = 99;
            n.up.dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
            check(n.value == 100 && changes == 1, "up arrow: " + n.value + " " + changes);
            n.up.dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
            check(n.value == 100 && changes == 1, "no step past the maximum");
            n.down.dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
            check(n.value == 99 && changes == 2, "down arrow");

            // Keys.
            n.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 0, Keyboard.HOME));
            check(n.value == -100 && changes == 3, "home key");
            n.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 0, Keyboard.UP));
            check(n.value == -99 && changes == 4, "up key");

            // The field shows the value once drawn; typed text and Enter set it.
            n.drawNow();
            check(n.textField.text == "-99", "text " + n.textField.text);
            n.textField.text = "42";
            n.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 0, Keyboard.ENTER));
            check(n.value == 42 && changes == 5, "typed value " + n.value);

            // Layout: arrows on the right, the field the rest.
            n.drawNow();
            check(n.up.x == 55 - n.up.width && n.down.y == 11 && n.textField.width == 55 - n.up.width,
                  "layout " + n.up.x + " " + n.down.y + " " + n.textField.width);

            // Lowering the maximum below the value moves the value, without an event.
            n.maximum = 10;
            check(n.value == 10 && changes == 5, "maximum lowered");

            n.enabled = false;
            check(!n.up.enabled && !n.down.enabled && !n.textField.enabled, "disabled parts");
        }
    }
}

// Names in full: libas3 keeps only the first of several imports outside a package.
class TestStepper extends fl.controls.NumericStepper {
    public function get up():fl.controls.BaseButton { return upArrow; }
    public function get down():fl.controls.BaseButton { return downArrow; }
}
