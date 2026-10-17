package cases {
    import fl.controls.listClasses.ListData;
    import fl.controls.Button;
    import fl.events.ColorPickerEvent;
    import fl.events.ComponentEvent;
    import fl.events.InteractionInputType;
    import fl.events.ListEvent;
    import fl.events.SliderEvent;
    import fl.events.SliderEventClickTarget;
    import flash.events.Event;

    public class EventsTest {
        public function run():void {
            check(SliderEvent.CHANGE == Event.CHANGE && SliderEvent.THUMB_DRAG == "thumbDrag", "SliderEvent names");
            var slide:SliderEvent = new SliderEvent(SliderEvent.THUMB_DRAG, 4.5, SliderEventClickTarget.THUMB, InteractionInputType.MOUSE, 37);
            var slideCopy:SliderEvent = slide.clone() as SliderEvent;
            check(slideCopy.value == 4.5 && slideCopy.clickTarget == "thumb" && slideCopy.triggerEvent == "mouse" &&
                  slideCopy.keyCode == 37 && slideCopy.type == "thumbDrag", "SliderEvent fields and clone");

            var item:Object = {label: "x"};
            var listEvent:ListEvent = new ListEvent(ListEvent.ITEM_CLICK, false, true, 0, 3, 7, item);
            var listCopy:ListEvent = listEvent.clone() as ListEvent;
            check(listCopy.item == item && listCopy.index == 7 && listCopy.rowIndex == 3 && listCopy.columnIndex == 0 &&
                  listCopy.cancelable, "ListEvent fields and clone");
            check(ListEvent.ITEM_CLICK == "itemClick" && ListEvent.ITEM_ROLL_OVER == "itemRollOver", "ListEvent names");

            var colorEvent:ColorPickerEvent = new ColorPickerEvent(ColorPickerEvent.CHANGE, 0xFF8800);
            check(colorEvent.color == 0xFF8800 && (colorEvent.clone() as ColorPickerEvent).color == 0xFF8800 &&
                  ColorPickerEvent.CHANGE == "change", "ColorPickerEvent");

            check(ComponentEvent.RESIZE == "resize" && ComponentEvent.BUTTON_DOWN == "buttonDown" &&
                  (new ComponentEvent(ComponentEvent.MOVE).clone() as ComponentEvent).type == "move", "ComponentEvent");

            var owner:Button = new Button();
            var data:ListData = new ListData("Label", null, owner, 5, 2, 1);
            check(data.label == "Label" && data.icon == null && data.owner == owner && data.index == 5 &&
                  data.row == 2 && data.column == 1, "ListData");
        }
    }
}
