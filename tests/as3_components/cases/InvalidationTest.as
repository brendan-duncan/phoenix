package cases {
    import fl.controls.Button;
    import fl.controls.CheckBox;
    import fl.controls.TextInput;
    import fl.core.InvalidationType;
    import fl.managers.FocusManager;
    import fl.managers.IFocusManager;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.FocusEvent;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.ui.Keyboard;

    public class InvalidationTest {
        public function run():void {
            var stage:Stage = new Stage();

            // Drawing waits for the frame: many changes, one draw.
            var c:CountingButton = new CountingButton();
            stage.addChild(c);
            c.label = "One";
            c.label = "Two";
            c.setSize(50, 20);
            check(c.draws == 0, "nothing drawn yet");
            stage.frame();
            check(c.draws == 1 && c.textField.text == "Two", "drawn once on the frame: " + c.draws);
            stage.frame();
            check(c.draws == 1, "nothing to draw again");
            c.label = "Three";
            c.drawNow();
            check(c.draws == 2 && c.textField.text == "Three", "drawNow");
            stage.frame();
            check(c.draws == 3, "the frame still draws what was asked for (nothing left invalid)");
            c.validateNow();
            check(c.draws == 4 && !c.invalid(InvalidationType.ALL), "validateNow");

            // Off the stage, it draws on the next frame anyway.
            var off:CountingButton = new CountingButton();
            off.label = "Off";
            stage.frame();
            check(off.draws == 1, "drawn off the stage: " + off.draws);

            // Render events draw it when the stage is invalidated first.
            var r:CountingButton = new CountingButton();
            stage.addChild(r);
            r.label = "R";
            stage.dispatchEvent(new Event(Event.RENDER));
            check(r.draws == 1, "drawn on render: " + r.draws);

            // The focus manager: one for the stage, made as a component joins it.
            var fm:IFocusManager = c.focusManager;
            check(fm is FocusManager && r.focusManager == fm, "a focus manager for the stage");
            var box:CheckBox = new CheckBox();
            var input:TextInput = new TextInput();
            stage.addChild(box);
            stage.addChild(input);
            c.focusEnabled = true;
            // Tab moves along the components in display order.
            stage.focus = c;
            stage.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 9, Keyboard.TAB));
            check(fm.getFocus() == r, "tab to the next: " + fm.getFocus());
            stage.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 9, Keyboard.TAB));
            stage.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 9, Keyboard.TAB));
            check(stage.focus == input.textField && fm.getFocus() == input, "a text input takes it in its field");
            stage.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 9, Keyboard.TAB, 0, false, false, true));
            check(fm.getFocus() == box, "shift tab back: " + fm.getFocus());
            // A click gives the focus to what is clicked.
            fm.showFocusIndicator = true;
            r.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            check(fm.getFocus() == r && !fm.showFocusIndicator, "clicked: focused without the rectangle");

            // The default button gets Enter, and is emphasized.
            var clicks:int = 0;
            var ok:Button = new Button();
            stage.addChild(ok);
            ok.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void { clicks++; });
            fm.defaultButton = ok;
            check(ok.emphasized, "default button emphasized");
            stage.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 13, Keyboard.ENTER));
            check(clicks == 1, "enter clicks the default button");
        }
    }
}

// Names in full: libas3 keeps only the first of several imports outside a package.
class CountingButton extends fl.controls.Button {
    public var draws:int = 0;
    override protected function draw():void {
        draws++;
        super.draw();
    }
    public function invalid(what:String):Boolean { return isInvalid(what); }
}
