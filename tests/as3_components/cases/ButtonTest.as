package cases {
    import fl.controls.Button;
    import fl.controls.ButtonLabelPlacement;
    import fl.controls.CheckBox;
    import fl.controls.LabelButton;
    import fl.controls.RadioButton;
    import fl.controls.RadioButtonGroup;
    import fl.events.ComponentEvent;
    import flash.display.DisplayObject;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.geom.Point;
    import flash.ui.Keyboard;

    public class ButtonTest {
        private var log:Array = [];

        public function run():void {
            var stage:Stage = new Stage();

            // Button, as BP_EffectsWindow makes its Ok button.
            var ok:Button = new Button();
            stage.addChild(ok);
            check(ok.label == "Label" && ok.width == 100 && ok.height == 22 && !ok.emphasized && !ok.toggle, "button defaults");
            ok.label = "Ok";
            ok.emphasized = true;
            ok.x = 500 - ok.width - 5;
            ok.y = 400 - ok.height - 5;
            check(ok.x == 395 && ok.y == 373, "placed by its size");
            var clicks:int = 0;
            ok.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void { clicks++; });
            ok.drawNow();
            check(ok.textField.text == "Ok" && ok.textField.visible, "label shown");
            // The label in the middle.
            check(Math.abs(ok.textField.x + ok.textField.width / 2 - 50) <= 1, "label centered " + ok.textField.x);
            // The emphasized border around it, behind.
            var border:DisplayObject = ok.getChildAt(0);
            check(border.x == -2 && border.y == -2 && border.width == 104 && border.height == 26, "emphasized border " +
                  border.x + " " + border.width);
            // Mouse states pick the skins.
            ok.dispatchEvent(new MouseEvent(MouseEvent.ROLL_OVER));
            ok.drawNow();
            check(skinName(ok) == "Button_overSkin", "over skin " + skinName(ok));
            ok.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            ok.drawNow();
            check(skinName(ok) == "Button_downSkin", "down skin " + skinName(ok));
            ok.enabled = false;
            ok.drawNow();
            check(skinName(ok) == "Button_disabledSkin" && !ok.mouseEnabled, "disabled skin " + skinName(ok));
            ok.enabled = true;
            // Space presses it.
            ok.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 32, Keyboard.SPACE));
            ok.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_UP, true, false, 32, Keyboard.SPACE));
            check(clicks == 1, "space clicks");
            // A toggle button.
            ok.toggle = true;
            ok.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            ok.drawNow();
            check(ok.selected && skinName(ok).indexOf("Button_selected") == 0, "toggled " + skinName(ok));

            // Size and move events; width does not scale.
            log = [];
            ok.addEventListener(ComponentEvent.RESIZE, function(e:Event):void { log.push("resize"); });
            ok.addEventListener(ComponentEvent.MOVE, function(e:Event):void { log.push("move"); });
            ok.setSize(80, 30);
            ok.move(10.6, 20.2);
            check(log.join() == "resize,move" && ok.x == 10.6 && ok.localToGlobal(new Point(0, 0)).x == 11 && ok.scaleX == 0.8, "events and rounding " +
                  log.join() + " " + ok.localToGlobal(new Point(0, 0)).x + " " + ok.scaleX);

            // CheckBox, as the tools make them: the click flips selected before other listeners hear it.
            var fill:CheckBox = new CheckBox();
            stage.addChild(fill);
            check(fill.toggle && fill.width == 100 && fill.height == 22 && !fill.selected, "check box defaults");
            fill.x = 15;
            fill.y = 5;
            fill.selected = true;
            fill.label = "Fill";
            var seen:Array = [];
            fill.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void { seen.push(e.target.selected); });
            var changes:int = 0;
            fill.addEventListener(Event.CHANGE, function(e:Event):void { changes++; });
            fill.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(seen.join() == "false" && changes == 1 && !fill.selected, "toggled before the listener " + seen.join());
            fill.drawNow();
            check(iconName(fill) == "CheckBox_upIcon", "icon " + iconName(fill));
            fill.selected = true;
            fill.drawNow();
            check(iconName(fill) == "CheckBox_selectedUpIcon", "selected icon " + iconName(fill));
            // The box at the left, the label after it.
            var box:DisplayObject = iconOf(fill);
            check(box.x == 5 && fill.textField.x == 5 + 14 + 5, "right placement " + box.x + " " + fill.textField.x);
            var threw:Boolean = false;
            try {
                fill.toggle = false;
            } catch (e:Error) {
                threw = true;
            }
            check(threw && fill.toggle, "toggle cannot be changed");

            // The stage's lockPainting: 175 wide, label at the left of the box.
            var lock:CheckBox = new CheckBox();
            stage.addChild(lock);
            lock.setSize(175, 22);
            lock.label = "Lock Painting From Quotes:";
            lock.labelPlacement = ButtonLabelPlacement.LEFT;
            lock.selected = false;
            lock.drawNow();
            box = iconOf(lock);
            check(box.x == 175 - 14 - 5, "box at the right " + box.x);
            check(Math.abs(lock.textField.x + lock.textField.width - (box.x - 5)) < 0.001, "label ends before the box " +
                  lock.textField.x + "+" + lock.textField.width);

            // Radio buttons of one group.
            var a:RadioButton = new RadioButton();
            var b:RadioButton = new RadioButton();
            a.groupName = b.groupName = "size";
            a.value = "small";
            b.value = "large";
            var group:RadioButtonGroup = RadioButtonGroup.getGroup("size");
            check(group.numRadioButtons == 2 && group.selection == null, "group");
            a.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(a.selected && !b.selected && group.selectedData == "small", "a selected");
            b.selected = true;
            check(!a.selected && b.selected && group.selection == b, "b selected");
            b.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(b.selected, "a click leaves it selected");
        }

        private static function skinName(button:LabelButton):String {
            for (var i:int = 0; i < button.numChildren; i++) {
                var child:Object = button.getChildAt(i);
                if (child.hasOwnProperty("skinName") && String(child.skinName).indexOf("Skin") > 0 &&
                    String(child.skinName).indexOf("emphasized") < 0) {
                    return child.skinName;
                }
            }
            return "";
        }

        private static function iconOf(button:LabelButton):DisplayObject {
            for (var i:int = 0; i < button.numChildren; i++) {
                var child:Object = button.getChildAt(i);
                if (child.hasOwnProperty("skinName") && String(child.skinName).indexOf("Icon") > 0) return child as DisplayObject;
            }
            return null;
        }

        private static function iconName(button:LabelButton):String {
            var icon:Object = iconOf(button);
            return icon == null ? "" : icon.skinName;
        }
    }
}
