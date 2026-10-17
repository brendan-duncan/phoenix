package cases {
    import fl.controls.Button;
    import fl.controls.CheckBox;
    import fl.controls.ColorPicker;
    import fl.controls.ComboBox;
    import fl.controls.Label;
    import fl.controls.List;
    import fl.controls.NumericStepper;
    import fl.controls.RadioButton;
    import fl.controls.Slider;
    import fl.controls.TextArea;
    import fl.controls.TextInput;
    import fl.controls.UIScrollBar;
    import fl.core.UIComponent;
    import flash.display.Stage;

    // Every component made, put on the stage and drawn, with its default size.
    public class Smoke {
        public function run():void {
            var stage:Stage = new Stage();
            var made:Array = [
                [new Button(), 100, 22],
                [new CheckBox(), 100, 22],
                [new RadioButton(), 100, 22],
                [new ColorPicker(), 24, 24],
                [new ComboBox(), 100, 22],
                [new List(), 100, 100],
                [new NumericStepper(), 80, 22],
                [new Slider(), 80, 3],
                [new TextInput(), 100, 22],
                [new TextArea(), 100, 100],
                [new Label(), 100, 22],
                [new UIScrollBar(), 15, 100]
            ];
            for each (var entry:Array in made) {
                var c:UIComponent = entry[0];
                stage.addChild(c);
                check(c.width == entry[1] && c.height == entry[2],
                      c + " default size " + c.width + "x" + c.height);
            }
            stage.frame();
            for each (entry in made) {
                (entry[0] as UIComponent).drawNow();
            }
        }
    }
}
