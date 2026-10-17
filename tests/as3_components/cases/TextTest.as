package cases {
    import fl.controls.Label;
    import fl.controls.TextArea;
    import fl.controls.TextInput;
    import fl.events.ComponentEvent;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.text.TextFieldAutoSize;
    import flash.ui.Keyboard;

    public class TextTest {
        public function run():void {
            var stage:Stage = new Stage();

            var input:TextInput = new TextInput();
            stage.addChild(input);
            input.text = "hello";
            check(input.text == "hello" && input.length == 5 && input.textField.type == "input", "text input");
            input.editable = false;
            check(input.textField.type == "dynamic", "not editable");
            var entered:int = 0;
            input.addEventListener(ComponentEvent.ENTER, function(e:Event):void { entered++; });
            input.textField.dispatchEvent(new KeyboardEvent(KeyboardEvent.KEY_DOWN, true, false, 13, Keyboard.ENTER));
            check(entered == 1, "enter");
            var changed:int = 0;
            input.addEventListener(Event.CHANGE, function(e:Event):void { changed++; });
            input.textField.dispatchEvent(new Event(Event.CHANGE, true));
            check(changed == 1, "change from the field");
            input.setSize(120, 22);
            input.drawNow();
            check(input.textField.width == 120 && input.textField.x == 0, "layout " + input.textField.width);

            var label:Label = new Label();
            stage.addChild(label);
            check(label.text == "Label" && label.width == 100 && label.height == 22, "label defaults");
            label.autoSize = TextFieldAutoSize.LEFT;
            label.text = "Longer text here";
            label.drawNow();
            check(label.width == label.textField.textWidth + 4, "auto sized " + label.width);

            var area:TextArea = new TextArea();
            stage.addChild(area);
            check(area.width == 100 && area.height == 100 && area.wordWrap && area.editable, "text area defaults");
            var lines:Array = [];
            for (var i:int = 0; i < 40; i++) lines.push("line " + i);
            area.text = lines.join("\n");
            area.drawNow();
            check(area.verticalScrollBar.visible && area.verticalScrollBar.maxScrollPosition == area.textField.maxScrollV,
                  "a scroll bar for long text " + area.verticalScrollBar.visible + " " + area.verticalScrollBar.maxScrollPosition);
            check(area.textField.width == 100 - 6 - 15, "field beside the bar " + area.textField.width);
            area.verticalScrollBar.scrollPosition = 5;
            check(area.verticalScrollPosition == 5, "scrolled by the bar");
            area.text = "short";
            area.drawNow();
            check(!area.verticalScrollBar.visible, "no bar for short text");
        }
    }
}
