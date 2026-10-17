package cases {
    import fl.controls.ComboBox;
    import fl.controls.List;
    import fl.controls.listClasses.CellRenderer;
    import fl.data.DataProvider;
    import flash.display.DisplayObject;
    import flash.display.DisplayObjectContainer;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.MouseEvent;

    public class ComboBoxTest {
        private var log:Array = [];

        public function run():void {
            var stage:Stage = new Stage();
            var cb:ComboBox = new ComboBox();
            stage.addChild(cb);
            check(cb.width == 100 && cb.height == 22 && cb.rowCount == 5 && !cb.editable && cb.selectedIndex == -1 &&
                  cb.length == 0, "defaults");

            // As BP_LayerOptionsPanel does.
            cb.move(1, 3);
            cb.setSize(70, 22);
            var modes:Array = ["normal", "multiply", "screen", "lighten", "darken", "difference", "add", "subtract",
                               "invert", "overlay", "hardlight"];
            for each (var mode:String in modes) {
                cb.addItem({label: mode.charAt(0).toUpperCase() + mode.substr(1), data: mode});
            }
            check(cb.length == 11 && cb.selectedIndex == 0 && cb.selectedItem.data == "normal", "first item selected: " + cb.selectedIndex);
            var found:Boolean = false;
            for (var i:int = 0; i < cb.length; i++) {
                if (cb.getItemAt(i).data == "screen") {
                    cb.selectedItem = cb.getItemAt(i);
                    found = true;
                    break;
                }
            }
            check(found && cb.selectedIndex == 2 && cb.selectedLabel == "Screen" && cb.value == "screen", "selectedItem");

            cb.addEventListener(Event.CHANGE, function(e:Event):void { log.push("change:" + cb.selectedItem.data); });
            cb.addEventListener(Event.OPEN, function(e:Event):void { log.push("open"); });
            cb.addEventListener(Event.CLOSE, function(e:Event):void { log.push("close"); });
            cb.drawNow();
            check(cb.textField.text == "Screen", "shows the selection: " + cb.textField.text);

            // As MultiBrushTool does.
            cb.height = 20;
            cb.rowCount = 10;
            check(cb.height == 20 && cb.rowCount == 10, "height and rowCount");

            // Open: the list on the stage under the box, rowCount rows high.
            var before:int = stage.numChildren;
            cb.open();
            check(stage.numChildren == before + 1 && log.join("|") == "open", "opened " + log.join("|"));
            var list:List = cb.dropdown;
            check(list.parent == stage && list.y == 3 + 20 && list.x == 1 && list.width == 70, "list placed " + list.x + "," + list.y + " " + list.width);
            check(list.height == 10 * 20 + 2, "ten rows " + list.height);

            // Picking a row: closed, selected, CHANGE.
            log = [];
            list.drawNow();
            var row:DisplayObject = rendererFor(list, 6);
            row.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(cb.selectedIndex == 6 && log.join("|") == "close|change:add", "picked " + log.join("|"));
            check(list.parent == null, "list closed");
            cb.drawNow();
            check(cb.textField.text == "Add", "shows the pick: " + cb.textField.text);

            // Setting the selection in code says nothing; close() when closed does nothing.
            log = [];
            cb.selectedIndex = 1;
            cb.close();
            check(log.length == 0, "quiet " + log.join("|"));

            // A click outside closes it.
            cb.open();
            stage.frame();
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            check(list.parent == null && log.join("|") == "open|close", "closed by a click outside " + log.join("|"));

            // The button toggles it.
            log = [];
            var button:DisplayObject = cb.getChildAt(0);
            button.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            button.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            check(log.join("|") == "open|close", "toggled " + log.join("|"));

            // Removed from the stage, it closes.
            cb.open();
            stage.removeChild(cb);
            check(list.parent == null, "closed when removed");

            // A prompt: nothing selected until picked.
            var prompted:ComboBox = new ComboBox();
            prompted.prompt = "Choose";
            prompted.dataProvider = new DataProvider([{label: "A"}, {label: "B"}]);
            prompted.drawNow();
            check(prompted.selectedIndex == -1 && prompted.textField.text == "Choose", "prompt " + prompted.selectedIndex);

            // Editable: the text is the value.
            var edit:ComboBox = new ComboBox();
            edit.editable = true;
            edit.addItem({label: "One", data: 1});
            edit.drawNow();
            check(edit.editable && edit.text == "One", "editable shows the item: " + edit.text);
            edit.removeAll();
            check(edit.length == 0 && edit.selectedIndex == -1, "removeAll");
        }

        private static function rendererFor(list:List, index:int):DisplayObject {
            var found:Array = [];
            collect(list, found);
            for each (var r:CellRenderer in found) {
                if (r.listData.index == index) return r;
            }
            return null;
        }

        private static function collect(c:DisplayObjectContainer, found:Array):void {
            for (var i:int = 0; i < c.numChildren; i++) {
                var child:DisplayObject = c.getChildAt(i);
                if (child is CellRenderer) found.push(child);
                else if (child is DisplayObjectContainer) collect(child as DisplayObjectContainer, found);
            }
        }
    }
}
