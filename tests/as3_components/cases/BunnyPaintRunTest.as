package cases {
    import com.hulabunny.bunnyPaint.BP_AdjustColorFilter;
    import com.hulabunny.bunnyPaint.BP_Canvas;
    import com.hulabunny.bunnyPaint.BP_ColorButton;
    import com.hulabunny.bunnyPaint.BP_ColorControl;
    import com.hulabunny.bunnyPaint.BP_EffectsWindow;
    import com.hulabunny.bunnyPaint.BP_LayerOptionsPanel;
    import com.hulabunny.bunnyPaint.BP_SliderControl;
    import fl.controls.ComboBox;
    import fl.controls.List;
    import fl.controls.NumericStepper;
    import fl.controls.Slider;
    import fl.controls.listClasses.CellRenderer;
    import fl.events.ComponentEvent;
    import flash.display.DisplayObject;
    import flash.display.DisplayObjectContainer;
    import flash.display.Sprite;
    import flash.display.Stage;
    import flash.events.MouseEvent;
    import flash.geom.Point;

    // bunnyPaint's own panels, run with the components (and a stand-in
    // canvas, tests/as3_components/bunnypaint).
    public class BunnyPaintRunTest {
        public function run():void {
            var stage:Stage = new Stage();
            stage.stageWidth = 800;
            stage.stageHeight = 600;
            var canvas:BP_Canvas = new BP_Canvas();
            stage.addChild(canvas);

            // A slider and stepper kept together.
            var control:BP_SliderControl = new BP_SliderControl("Opacity:", 0.8 * 100.0, 0, 100);
            stage.addChild(control);
            stage.frame();
            check(control.slider.value == 80 && control.edit.value == 80, "both at the value: " +
                  control.slider.value + " " + control.edit.value);
            check(control.slider.width == 190 && control.edit.width == 55, "sizes");
            // Dragging the slider moves the stepper.
            var thumb:DisplayObject = control.slider.getChildAt(1);
            thumb.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            Stage.mouseStageX = 70 + 190 * 0.25;
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_MOVE));
            stage.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_UP));
            check(control.slider.value == 25 && control.edit.value == 25, "stepper follows the slider: " + control.edit.value);
            // Stepping moves the slider.
            var up:DisplayObject = control.edit.getChildAt(0);
            up.dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
            check(control.edit.value == 26 && control.slider.value == 26, "slider follows the stepper: " + control.slider.value);

            // A color control.
            var color:BP_ColorControl = new BP_ColorControl("Shadow Color:", 0x00FF00);
            stage.addChild(color);
            check(color.colorPicker.selectedColor == 0x00FF00 && color.colorPicker.width == 50 && color.colorPicker.x == 80,
                  "color control");

            // The color buttons: the first adds the grays to the palette.
            var fg:BP_ColorButton = new BP_ColorButton(BP_ColorButton.kBrushColor, canvas);
            var bg:BP_ColorButton = new BP_ColorButton(BP_ColorButton.kBackgroundColor, canvas);
            stage.addChild(fg);
            stage.addChild(bg);
            fg.x = 105;
            fg.y = 30;
            check(fg.selectedColor == 0x112233 && bg.selectedColor == 0xFFFFFF, "colors from the canvas");
            check(fg.colors.length == 234 && bg.colors.length == 234, "grays added once: " + fg.colors.length);
            check(!fg.editable && !fg.showTextField, "no field");
            stage.frame();
            fg.dispatchEvent(new MouseEvent(MouseEvent.MOUSE_DOWN));
            var palette:DisplayObjectContainer = stage.getChildAt(stage.numChildren - 1) as DisplayObjectContainer;
            var swatch:DisplayObject = findNamed(palette, "swatch230");
            check(swatch != null, "a swatch for the grays");
            swatch.dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(canvas.brushColor == fg.colors[230] && fg.selectedColor == fg.colors[230], "brush color picked: " +
                  canvas.brushColor.toString(16));

            // The layer panel: blend modes, opacity and the layers.
            var panel:BP_LayerOptionsPanel = new BP_LayerOptionsPanel(canvas, 1, 19, 200, 300);
            stage.addChild(panel);
            stage.frame();
            var modes:ComboBox = find(panel, ComboBox) as ComboBox;
            var opacity:NumericStepper = find(panel, NumericStepper) as NumericStepper;
            var layers:List = find(panel, List) as List;
            check(modes.length == 11 && modes.selectedItem.data == "screen", "blend mode shown: " + modes.selectedItem.label);
            check(opacity.value == 75, "opacity shown: " + opacity.value);
            check(layers.length == 3 && layers.selectedItem.index == 1 && layers.getItemAt(0).index == 2, "layers, top first");
            // Picking a mode.
            modes.open();
            modes.dropdown.drawNow();
            rendererAt(modes.dropdown, 1).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(canvas.currentLayer.blendMode == "multiply", "blend mode set: " + canvas.currentLayer.blendMode);
            // Stepping the opacity.
            opacity.getChildAt(1).dispatchEvent(new ComponentEvent(ComponentEvent.BUTTON_DOWN, true));
            check(Math.abs(canvas.currentLayer.opacity - 0.74) < 1e-9, "opacity set: " + canvas.currentLayer.opacity);
            // Clicking a layer's name selects it; clicking its eye hides it too.
            layers.drawNow();
            Stage.mouseStageX = panel.x + layers.x + 100;
            rendererAt(layers, 0).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(canvas.currentLayerIndex == 2 && canvas.layers[2].visible, "layer 2 current");
            Stage.mouseStageX = panel.x + layers.x + 10;
            rendererAt(layers, 2).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(canvas.currentLayerIndex == 0 && !canvas.layers[0].visible, "layer 0 current and hidden");

            // The effects window: the filters with their eyes, the selected one's controls.
            var effects:BP_EffectsWindow = new BP_EffectsWindow(canvas, 1);
            stage.addChild(effects);
            stage.frame();
            var list:List = find(effects, List) as List;
            check(list.length == 5 && list.selectedItem.index == 1 && list.iconField == "iconSource", "effects list");
            var sliders:Array = findAll(effects, Slider);
            check(sliders.length == 6, "drop shadow sliders: " + sliders.length);
            // Clicking the blur filter's row shows its two sliders.
            list.drawNow();
            Stage.mouseStageX = effects.x + 2 + 100;
            rendererAt(list, 2).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(list.selectedItem.index == 2 && findAll(effects, Slider).length == 2, "blur controls: " + findAll(effects, Slider).length);
            // Its eye turns it on.
            Stage.mouseStageX = effects.x + 2 + 10;
            list.drawNow();
            rendererAt(list, 2).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(canvas.currentLayer.isFilterEnabled(2), "blur turned on by its eye");

            // The adjust color filter's matrix.
            var adjust:BP_AdjustColorFilter = new BP_AdjustColorFilter();
            adjust.brightness = 50;
            check(Math.abs(adjust.filter.matrix[4] - 127.5) < 1e-9 && adjust.filter.matrix[0] == 1, "adjust color matrix " + adjust.filter.matrix);
        }

        private static function rendererAt(list:List, index:int):DisplayObject {
            for each (var r:CellRenderer in findAll(list, CellRenderer)) {
                if (r.listData.index == index) return r;
            }
            return null;
        }

        private static function find(c:DisplayObjectContainer, type:Class):DisplayObject {
            var all:Array = findAll(c, type);
            return all.length > 0 ? all[0] : null;
        }

        private static function findAll(c:DisplayObjectContainer, type:Class, found:Array = null):Array {
            if (found == null) found = [];
            for (var i:int = 0; i < c.numChildren; i++) {
                var child:DisplayObject = c.getChildAt(i);
                if (child is type) found.push(child);
                if (child is DisplayObjectContainer) findAll(child as DisplayObjectContainer, type, found);
            }
            return found;
        }

        private static function findNamed(c:DisplayObjectContainer, name:String):DisplayObject {
            for (var i:int = 0; i < c.numChildren; i++) {
                var child:DisplayObject = c.getChildAt(i);
                if (child.name == name) return child;
                if (child is DisplayObjectContainer) {
                    var inner:DisplayObject = findNamed(child as DisplayObjectContainer, name);
                    if (inner != null) return inner;
                }
            }
            return null;
        }
    }
}
