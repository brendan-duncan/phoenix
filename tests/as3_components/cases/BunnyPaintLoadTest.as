package cases {
    import flash.utils.getDefinitionByName;

    // bunnyPaint's classes that use the components load against them.
    public class BunnyPaintLoadTest {
        public function run():void {
            var names:Array = ["BP_ColorButton", "BP_ColorControl", "BP_EffectsWindow", "BP_LayerOptionsPanel",
                               "BP_SimpleBrush", "BP_SliderControl", "BrushTool", "CircleTool", "MultiBrushTool",
                               "MultiSmudgeTool", "OilTool", "RectangleTool", "SprayTool", "BP_AdjustColorFilter"];
            for each (var name:String in names) {
                var found:Object = null;
                try {
                    found = getDefinitionByName("com.hulabunny.bunnyPaint." + name);
                } catch (e:Error) {
                    trace(name + ": " + e);
                }
                check(found is Class, name + " loads");
            }
        }
    }
}
