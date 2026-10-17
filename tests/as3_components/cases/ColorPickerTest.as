package cases {
    import fl.controls.ColorPicker;
    import fl.events.ColorPickerEvent;
    import flash.display.DisplayObject;
    import flash.display.DisplayObjectContainer;
    import flash.display.Sprite;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.MouseEvent;
    import flash.geom.ColorTransform;

    public class ColorPickerTest {
        private var picked:Array = [];

        private function onChange(event:ColorPickerEvent):void {
            picked.push(event.color);
        }

        // The swatch sprites of an open palette, in order.
        private static function swatchesOf(stage:Stage):Array {
            var palette:DisplayObjectContainer = stage.getChildAt(stage.numChildren - 1) as DisplayObjectContainer;
            for (var i:int = 0; i < palette.numChildren; i++) {
                var child:DisplayObjectContainer = palette.getChildAt(i) as DisplayObjectContainer;
                if (child != null && child.numChildren > 100) {
                    var list:Array = [];
                    for (var j:int = 0; j < child.numChildren; j++) {
                        if (child.getChildAt(j).name.indexOf("swatch") == 0) list.push(child.getChildAt(j));
                    }
                    return list;
                }
            }
            return [];
        }

        public function run():void {
            var stage:Stage = new Stage();
            var cp:ColorPicker = new ColorPicker();
            stage.addChild(cp);
            var colors:Array = cp.colors;
            check(colors.length == 216, "216 default colors: " + colors.length);
            check(colors[0] == 0x000000 && colors[5] == 0x0000FF && colors[17] == 0x6600FF &&
                  colors[18] == 0x003300 && colors[108] == 0x990000 && colors[215] == 0xFFFFFF,
                  "web palette layout " + colors[17].toString(16) + " " + colors[108].toString(16));
            var unique:Object = {};
            var count:int = 0;
            for each (var c:uint in colors) {
                if (unique[c] == null) count++;
                unique[c] = true;
            }
            check(count == 216, "all different");

            // As BP_ColorButton does: grays pushed onto the shared palette.
            var transform:ColorTransform = new ColorTransform();
            for (var i:int = 0; i < 18; i++) {
                var g:int = (i / 17.0) * 255;
                transform.redOffset = g;
                transform.greenOffset = g;
                transform.blueOffset = g;
                cp.colors.push(transform.color);
            }
            var other:ColorPicker = new ColorPicker();
            check(other.colors.length == 234 && other.colors[233] == 0xFFFFFF && other.colors[216] == 0,
                  "the palette is shared: " + other.colors.length);
            other.colors = [0xFF0000, 0x00FF00];
            check(other.colors.length == 2 && cp.colors.length == 234, "custom colors");

            cp.selectedColor = 0x00FF00;
            check(cp.selectedColor == 0x00FF00 && cp.hexValue == "00FF00", "selectedColor and hexValue " + cp.hexValue);
            cp.selectedColor = 0x1234567;
            check(cp.selectedColor == 0x234567 && cp.hexValue == "234567", "only 24 bits kept");

            // Opening: the palette on the stage with a swatch for each color.
            cp.addEventListener(ColorPickerEvent.CHANGE, onChange);
            var opened:int = 0;
            var closed:int = 0;
            cp.addEventListener(Event.OPEN, function(e:Event):void { opened++; });
            cp.addEventListener(Event.CLOSE, function(e:Event):void { closed++; });
            cp.editable = false;
            cp.showTextField = false;
            cp.width = 50;
            cp.drawNow();
            var before:int = stage.numChildren;
            cp.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            check(stage.numChildren == before + 1 && opened == 1, "palette opened");
            var swatches:Array = swatchesOf(stage);
            check(swatches.length == 234, "a swatch for each color: " + swatches.length);
            // The palette starts below the button.
            var palette:DisplayObject = stage.getChildAt(stage.numChildren - 1);
            check(palette.y == cp.height && palette.x == 0, "palette below " + palette.x + "," + palette.y);

            // Picking: CHANGE with the color, closed.
            (swatches[5] as Sprite).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(picked.length == 1 && picked[0] == 0x0000FF && cp.selectedColor == 0x0000FF, "picked " + picked);
            check(stage.numChildren == before && closed == 1, "closed after the pick");

            // Setting the color in code says nothing.
            cp.selectedColor = 0xABCDEF;
            check(picked.length == 1, "no event when set in code");

            // A click elsewhere closes it (from the next frame on).
            cp.open();
            stage.frame();
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            check(stage.numChildren == before && closed == 2, "closed by a click elsewhere");

            // With the field: typing a color and Enter.
            var entered:Array = [];
            cp.addEventListener(ColorPickerEvent.ENTER, function(e:ColorPickerEvent):void { entered.push(e.color); });
            cp.editable = true;
            cp.showTextField = true;
            cp.drawNow();
            cp.open();
            check(cp.textField != null && cp.textField.text == "ABCDEF", "field shows the color");
            cp.textField.text = "ff8000";
            cp.textField.dispatchEvent(new flash.events.KeyboardEvent(flash.events.KeyboardEvent.KEY_DOWN, true, false, 13, 13));
            check(entered.length == 1 && entered[0] == 0xFF8000 && cp.selectedColor == 0xFF8000, "typed color " + entered);

            cp.enabled = false;
            cp.open();
            check(stage.numChildren == before, "a disabled picker does not open");
        }
    }
}
