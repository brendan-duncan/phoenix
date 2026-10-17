package cases {
    import fl.controls.Button;
    import fl.controls.CheckBox;
    import fl.core.UIComponent;
    import fl.managers.StyleManager;
    import flash.display.Stage;
    import flash.text.TextFormat;

    public class StyleTest {
        public function run():void {
            var stage:Stage = new Stage();
            var a:StyledButton = new StyledButton();
            var b:StyledButton = new StyledButton();
            var box:CheckBox = new CheckBox();
            stage.addChild(a);
            stage.addChild(b);
            stage.addChild(box);

            // Defaults: the class's, and UIComponent's through the global styles.
            check(a.style("upSkin") == "Button_upSkin" && a.style("textPadding") == 5 &&
                  a.style("emphasizedSkin") == "Button_emphasizedSkin" && a.style("focusRectSkin") == "focusRectSkin",
                  "class defaults " + a.style("focusRectSkin"));
            var format:TextFormat = a.style("textFormat") as TextFormat;
            check(format != null && format.font == "_sans" && format.size == 11 && format.color == 0, "default text format");
            var disabled:TextFormat = a.style("disabledTextFormat") as TextFormat;
            check(disabled != null && disabled.color == 0x999999, "disabled text format");
            // A subclass with no getStyleDefinition of its own has its superclass's.
            check(a.style("selectedUpSkin") == "Button_selectedUpSkin", "subclass defaults");

            // Instance styles win; clearing falls back.
            a.setStyle("upSkin", "MyUpSkin");
            check(a.getStyle("upSkin") == "MyUpSkin" && a.style("upSkin") == "MyUpSkin" && b.style("upSkin") == "Button_upSkin",
                  "instance style");
            a.clearStyle("upSkin");
            check(a.getStyle("upSkin") == null && a.style("upSkin") == "Button_upSkin", "cleared");

            // A component style: every instance of that class (not other classes).
            StyleManager.setComponentStyle(StyledButton, "textPadding", 9);
            check(a.style("textPadding") == 9 && b.style("textPadding") == 9 && StyleManager.getComponentStyle(StyledButton, "textPadding") == 9,
                  "component style");
            check(box.getStyle("textPadding") == null, "not on another class");
            a.setStyle("textPadding", 2);
            check(a.style("textPadding") == 2 && b.style("textPadding") == 9, "instance over class");
            StyleManager.clearComponentStyle(StyledButton, "textPadding");
            check(b.style("textPadding") == 5, "component style cleared");

            // A global style reaches every component.
            var red:TextFormat = new TextFormat("Arial", 14, 0xFF0000);
            StyleManager.setStyle("textFormat", red);
            check(StyleManager.getStyle("textFormat") == red && b.style("textFormat") == red, "global style");
            b.drawNow();
            check(b.textField.defaultTextFormat.color == 0xFF0000 && b.textField.defaultTextFormat.size == 14, "drawn with it");
            StyleManager.setComponentStyle(StyledButton, "textFormat", new TextFormat("Courier", 10));
            check((b.style("textFormat") as TextFormat).font == "Courier", "class over global");
            StyleManager.clearComponentStyle(StyledButton, "textFormat");
            StyleManager.clearStyle("textFormat");
            check(b.style("textFormat") == null || (b.style("textFormat") as TextFormat).font != "Arial", "global cleared");
            StyleManager.setStyle("textFormat", UIComponent.getStyleDefinition().textFormat);

            // A style change invalidates the instances, to draw again.
            b.drawNow();
            check(!b.styleInvalid(), "drawn");
            StyleManager.setComponentStyle(StyledButton, "upSkin", "Button_overSkin");
            check(b.styleInvalid(), "invalidated by a component style");

            // mergeStyles: the first list's style where several have one.
            var merged:Object = UIComponent.mergeStyles({a: 1, b: null}, {a: 2, b: 3, c: 4});
            check(merged.a == 1 && merged.b == 3 && merged.c == 4, "mergeStyles");
        }
    }
}

// Names in full: libas3 keeps only the first of several imports outside a package.
class StyledButton extends fl.controls.Button {
    public function style(name:String):Object { return getStyleValue(name); }
    public function styleInvalid():Boolean { return isInvalid(fl.core.InvalidationType.STYLES); }
}
