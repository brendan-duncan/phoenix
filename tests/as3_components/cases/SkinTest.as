package cases {
    import fl.controls.Button;
    import fl.controls.CheckBox;
    import fl.skins.DefaultSkin;
    import flash.display.DisplayObject;
    import flash.display.Shape;
    import flash.display.Stage;
    import flash.events.MouseEvent;
    import flash.utils.getDefinitionByName;

    // The movie's skin symbols are used where it has them (here the
    // stand-ins in tests/skins: Button_upSkin and CheckBox_upIcon), drawn
    // skins where it does not.
    public class SkinTest {
        public function run():void {
            var stage:Stage = new Stage();
            var button:Button = new Button();
            stage.addChild(button);
            button.setSize(120, 30);
            button.drawNow();
            var skinClass:Object = getDefinitionByName("Button_upSkin");
            var background:DisplayObject = button.getChildAt(0);
            check(background is (skinClass as Class), "the movie's Button_upSkin: " + background);
            check(Math.abs(background.width - 120) < 1e-9 && Math.abs(background.height - 30) < 1e-9, "sized to the button: " + background.width + "x" + background.height);

            // Over: the movie has no Button_overSkin, so a drawn one.
            button.dispatchEvent(new MouseEvent(MouseEvent.ROLL_OVER));
            button.drawNow();
            background = button.getChildAt(0);
            check(background is DefaultSkin && (background as DefaultSkin).skinName == "Button_overSkin" &&
                  background.width == 120 && background.height == 30, "drawn over skin " + background);
            check(button.numChildren == 2, "the old skin removed: " + button.numChildren);

            // Back to up: the class is remembered, a new instance made.
            var made:int = skinClass.made;
            button.dispatchEvent(new MouseEvent(MouseEvent.ROLL_OUT));
            button.drawNow();
            check(button.getChildAt(0) is (skinClass as Class) && skinClass.made == made + 1, "up again");

            // An icon from the movie and one drawn.
            var box:CheckBox = new CheckBox();
            stage.addChild(box);
            box.drawNow();
            var iconClass:Class = getDefinitionByName("CheckBox_upIcon") as Class;
            var icon:DisplayObject = null;
            for (var i:int = 0; i < box.numChildren; i++) {
                if (box.getChildAt(i) is iconClass) icon = box.getChildAt(i);
            }
            check(icon != null, "the movie's CheckBox_upIcon");
            box.selected = true;
            box.drawNow();
            var drawn:Boolean = false;
            for (i = 0; i < box.numChildren; i++) {
                var child:DefaultSkin = box.getChildAt(i) as DefaultSkin;
                if (child != null && child.skinName == "CheckBox_selectedUpIcon") drawn = true;
            }
            check(drawn && icon.parent == null, "drawn selected icon, the old one gone");

            // A style may also be a class, or a display object.
            button.setStyle("upSkin", Shape);
            button.drawNow();
            check(button.getChildAt(0) is Shape, "a class as a skin");
            var mine:Shape = new Shape();
            mine.x = 7;
            button.setStyle("upSkin", mine);
            button.drawNow();
            check(button.getChildAt(0) == mine && mine.x == 0, "a display object as a skin");

            // Names that are no skin and no class give nothing.
            button.setStyle("upSkin", "NoSuchThing");
            button.drawNow();
            check(!(button.getChildAt(0) is Shape) && !(button.getChildAt(0) is DefaultSkin), "an unknown name: no skin");
            check(DefaultSkin.handles("ScrollArrowUp_downSkin") && DefaultSkin.handles("focusRectSkin") &&
                  !DefaultSkin.handles("NoSuchThing"), "DefaultSkin knows the standard names");

            // Drawn skins have the symbols' natural sizes.
            check(new DefaultSkin("SliderThumb_upSkin").width == 13 && new DefaultSkin("ScrollArrowUp_upSkin").height == 14 &&
                  new DefaultSkin("NumericStepperUpArrow_upSkin").width == 21 && new DefaultSkin("ColorPicker_swatchSkin").width == 8,
                  "natural sizes");
        }
    }
}
