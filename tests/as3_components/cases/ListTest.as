package cases {
    import fl.controls.List;
    import fl.controls.listClasses.CellRenderer;
    import fl.controls.listClasses.ICellRenderer;
    import fl.data.DataProvider;
    import fl.events.ListEvent;
    import flash.display.DisplayObject;
    import flash.display.DisplayObjectContainer;
    import flash.display.Sprite;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.MouseEvent;

    public class ListTest {
        private var log:Array = [];

        public function run():void {
            var stage:Stage = new Stage();
            var list:List = new List();
            stage.addChild(list);
            check(list.width == 100 && list.height == 100 && list.rowHeight == 20 && list.labelField == "label" &&
                  list.iconField == "icon" && list.selectedIndex == -1 && list.selectedItem == null, "defaults");

            // As BP_EffectsWindow does: items with a Sprite icon each.
            list.setSize(125, 335);
            var dp:DataProvider = new DataProvider();
            var icons:Array = [];
            var selected:Object = null;
            for (var i:int = 0; i < 6; i++) {
                var icon:Sprite = new Sprite();
                icon.graphics.drawRect(0, 0, 20, 14);
                icons.push(icon);
                var item:Object = {label: "Effect " + i, iconSource: icon, index: i};
                dp.addItem(item);
                if (i == 2) selected = item;
            }
            list.dataProvider = dp;
            list.iconField = "iconSource";
            list.selectedItem = selected;
            check(list.selectedIndex == 2 && list.selectedItem.index == 2 && list.length == 6, "selectedItem " + list.selectedIndex);
            check(list.getItemAt(4).label == "Effect 4", "getItemAt");

            list.addEventListener(ListEvent.ITEM_CLICK, function(e:ListEvent):void { log.push("click:" + e.index + ":" + e.item.label); });
            list.addEventListener(Event.CHANGE, function(e:Event):void { log.push("change:" + list.selectedIndex); });
            list.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void { log.push("mouse"); });
            list.drawNow();

            var renderers:Array = renderersOf(list);
            check(renderers.length == 6, "a renderer a row: " + renderers.length);
            var third:CellRenderer = renderers[2];
            check(third.selected && !(renderers[1] as CellRenderer).selected, "the selected row shows it");
            check(third.label == "Effect 2" && third.y == 40 && third.width == 125 - 2 && third.height == 20,
                  "row layout " + third.label + " " + third.y + " " + third.width + "x" + third.height);
            // The icon is the item's own Sprite, at the left padding.
            check(icons[2].parent == third && icons[2].x == 5, "icon in the row " + (icons[2].parent == third) + " " + icons[2].x);
            check(third.textField.x == 5 + 20 + 5, "label after the icon: " + third.textField.x);

            // A click on a row: ITEM_CLICK, the selection, CHANGE, then the click bubbles to the list.
            (renderers[4] as DisplayObject).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(log.join("|") == "click:4:Effect 4|change:4|mouse", "click events " + log.join("|"));
            check(list.selectedIndex == 4 && list.selectedItem.index == 4, "selected by the click");
            log = [];
            (renderers[4] as DisplayObject).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(log.join("|") == "click:4:Effect 4|mouse", "the selected row again: no change " + log.join("|"));

            // Cancelling ITEM_CLICK keeps the selection.
            var cancel:Function = function(e:ListEvent):void { e.preventDefault(); };
            list.addEventListener(ListEvent.ITEM_CLICK, cancel);
            log = [];
            list.drawNow();
            renderers = renderersOf(list);
            (renderers[1] as DisplayObject).dispatchEvent(new MouseEvent(MouseEvent.CLICK));
            check(list.selectedIndex == 4 && log.indexOf("change:1") < 0, "cancelled click");
            list.removeEventListener(ListEvent.ITEM_CLICK, cancel);

            // The selection follows the items.
            dp.addItemAt({label: "first"}, 0);
            check(list.selectedIndex == 5 && list.selectedItem.index == 4, "after an add before it: " + list.selectedIndex);
            dp.removeItemAt(0);
            check(list.selectedIndex == 4, "after a remove before it");
            dp.removeItemAt(4);
            check(list.selectedIndex == -1 && list.selectedItem == null, "removed itself");
            list.selectedIndex = 3;
            dp.sortOn("label", Array.DESCENDING);
            check(list.selectedItem.label == "Effect 3" && list.selectedIndex == 1, "after a sort: " + list.selectedIndex);

            // Several.
            list.allowMultipleSelection = true;
            list.selectedIndices = [0, 2];
            check(list.selectedItems.length == 2 && list.isItemSelected(dp.getItemAt(2)) && list.selectedIndex == 2, "multiple");
            list.allowMultipleSelection = false;
            check(list.selectedIndices.length == 1, "back to one");

            // Scrolling: more rows than fit.
            var many:List = new List();
            stage.addChild(many);
            for (i = 0; i < 30; i++) many.addItem({label: "Row " + i});
            many.drawNow();
            check(many.verticalScrollBar.visible && many.maxVerticalScrollPosition == 30 * 20 - 98,
                  "scroll bar " + many.verticalScrollBar.visible + " " + many.maxVerticalScrollPosition);
            check(many.rowCount == 5, "row count " + many.rowCount);
            many.scrollToIndex(20);
            check(many.verticalScrollPosition == 21 * 20 - 98, "scrolled to row 20: " + many.verticalScrollPosition);
            many.drawNow();
            var first:ICellRenderer = renderersOf(many)[0];
            check(first.listData.index == Math.floor((21 * 20 - 98) / 20), "first row shown " + first.listData.index);
            many.rowCount = 8;
            check(many.height == 8 * 20 + 2, "rowCount sizes: " + many.height);

            // A labelFunction.
            many.labelFunction = function(o:Object):String { return "<" + o.label + ">"; };
            check(many.itemToLabel({label: "x"}) == "<x>", "labelFunction");

            // removeAll.
            many.removeAll();
            check(many.length == 0 && many.selectedIndex == -1, "removeAll");
        }

        private static function renderersOf(list:List):Array {
            var found:Array = [];
            collect(list, found);
            found.sortOn("y", Array.NUMERIC);
            return found;
        }

        private static function collect(c:DisplayObjectContainer, found:Array):void {
            for (var i:int = 0; i < c.numChildren; i++) {
                var child:DisplayObject = c.getChildAt(i);
                if (child is CellRenderer) {
                    found.push(child);
                } else if (child is DisplayObjectContainer && !(child is fl.controls.ScrollBar)) {
                    collect(child as DisplayObjectContainer, found);
                }
            }
        }
    }
}
