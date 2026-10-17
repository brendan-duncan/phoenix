package cases {
    import fl.controls.CheckBox;
    import fl.controls.UIScrollBar;
    import flash.display.Sprite;
    import flash.display.Stage;
    import flash.geom.Point;
    import flash.text.TextField;

    // What the runtime does with a component a timeline places (see
    // resources/as3/README.md): the instance made with its avatar and
    // matrix first, the constructors run, then the parameters set between
    // componentInspectorSetting = true and false, then drawn at render.
    public class StagePlacementTest {
        public function run():void {
            var stage:Stage = new Stage();
            var root:Sprite = new Sprite();
            stage.addChild(root);

            // The text field beside the scroll bar, placed first.
            var messageEdit:TextField = new TextField();
            messageEdit.name = "messageEdit";
            messageEdit.width = 537.95;
            messageEdit.height = 90;
            var lines:Array = [];
            for (var i:int = 0; i < 30; i++) lines.push("Line " + i);
            messageEdit.text = lines.join("\n");
            root.addChild(messageEdit);

            // CheckBox lockPainting: Matrix a=1.75 tx=53.3 ty=142.25 over an avatar of 100 by 22.
            var lock:CheckBox = new PlacedCheckBox(1.75, 1, 53.3, 142.25);
            lock.name = "lockPainting";
            root.addChild(lock);
            check(lock.width == 175 && lock.height == 22, "size from the avatar: " + lock.width + "x" + lock.height);
            check(lock.scaleX == 1 && lock.numChildren > 0 && !(lock.getChildAt(0) is AvatarShape), "scale back to 1, avatar gone");
            check(lock.localToGlobal(new Point(0, 0)).x == 53 && lock.localToGlobal(new Point(0, 0)).y == 142,
                  "placed on whole pixels: " + lock.localToGlobal(new Point(0, 0)));
            lock["componentInspectorSetting"] = true;
            lock.enabled = true;
            lock.label = "Lock Painting From Quotes:";
            lock.labelPlacement = "left";
            lock.selected = false;
            lock.visible = true;
            lock["componentInspectorSetting"] = false;
            stage.frame();
            check(lock.label == "Lock Painting From Quotes:" && lock.labelPlacement == "left" && !lock.selected &&
                  lock.textField.text == "Lock Painting From Quotes:", "parameters applied");

            // UIScrollBar: Matrix d=0.94 tx=596.95 ty=40.95 over an avatar of 15 by 100.
            var bar:UIScrollBar = new PlacedScrollBar(1, 0.94, 596.95, 40.95);
            bar.name = "__id0_";
            root.addChild(bar);
            check(bar.width == 15 && Math.abs(bar.height - 94) < 0.001, "bar size " + bar.width + "x" + bar.height);
            bar["componentInspectorSetting"] = true;
            bar.direction = "vertical";
            bar.scrollTargetName = "messageEdit";
            bar.visible = true;
            bar["componentInspectorSetting"] = false;
            check(bar.scrollTarget == messageEdit, "found its text field by name");
            stage.frame();
            check(bar.minScrollPosition == 1 && bar.maxScrollPosition == messageEdit.maxScrollV && bar.enabled,
                  "follows the field: " + bar.minScrollPosition + ".." + bar.maxScrollPosition);

            // Scrolling the bar scrolls the field, and the other way.
            bar.scrollPosition = 10;
            check(messageEdit.scrollV == 10, "bar scrolls the field: " + messageEdit.scrollV);
            messageEdit.scrollV = 3;
            check(bar.scrollPosition == 3, "field scrolls the bar: " + bar.scrollPosition);

            // Text set in code: the bar sees it on the next frame.
            messageEdit.text = "short";
            stage.frame();
            check(bar.maxScrollPosition == 1 && !bar.enabled, "short text: nothing to scroll " + bar.maxScrollPosition);

            var threw:Boolean = false;
            try {
                bar.scrollTargetName = "noSuchField";
            } catch (e:Error) {
                threw = true;
            }
            check(threw, "a missing target is an error");
        }
    }
}

import flash.display.Shape;

// The avatar: a hairline-free 80 by 22 rectangle, as Component_avatar's fill.
class AvatarShape extends Shape {
    public function AvatarShape(width:Number, height:Number) {
        graphics.drawRect(0, 0, width, height);
    }
}

// Stand-ins for what the runtime does: the symbol's avatar and the
// instance's matrix are there before the constructors run.
class PlacedCheckBox extends fl.controls.CheckBox {
    public function PlacedCheckBox(a:Number, d:Number, tx:Number, ty:Number) {
        var avatar:AvatarShape = new AvatarShape(80, 22);
        avatar.scaleX = 1.25;
        addChild(avatar);
        setScaleX(a);
        setScaleY(d);
        setNativePosition(tx, ty);
        super();
    }

    // (UIComponent's x and y setters: the host sets the native ones.)
    private function setNativePosition(tx:Number, ty:Number):void {
        super.x = tx;
        super.y = ty;
    }
}

class PlacedScrollBar extends fl.controls.UIScrollBar {
    public function PlacedScrollBar(a:Number, d:Number, tx:Number, ty:Number) {
        var avatar:AvatarShape = new AvatarShape(15, 100);
        addChild(avatar);
        setScaleX(a);
        setScaleY(d);
        setNativePosition(tx, ty);
        super();
    }

    // (UIComponent's x and y setters: the host sets the native ones.)
    private function setNativePosition(tx:Number, ty:Number):void {
        super.x = tx;
        super.y = ty;
    }
}
