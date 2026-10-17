package cases {
    import fl.data.DataProvider;
    import fl.events.DataChangeEvent;
    import fl.events.DataChangeType;

    public class DataProviderTest {
        private var log:Array = [];

        private function record(event:DataChangeEvent):void {
            log.push(event.type + ":" + event.changeType + ":" + event.startIndex + "-" + event.endIndex + ":" + event.items.length);
        }

        public function run():void {
            var dp:DataProvider = new DataProvider();
            dp.addEventListener(DataChangeEvent.PRE_DATA_CHANGE, record);
            dp.addEventListener(DataChangeEvent.DATA_CHANGE, record);
            check(dp.length == 0, "starts empty");

            var a:Object = {label: "a", data: 1};
            var b:Object = {label: "b", data: 2};
            var c:Object = {label: "c", data: 3};
            dp.addItem(a);
            check(log.join("|") == "preDataChange:add:0-0:1|dataChange:add:0-0:1", "addItem events " + log.join("|"));
            dp.addItem(b);
            dp.addItemAt(c, 0);
            check(dp.length == 3 && dp.getItemAt(0) == c && dp.getItemAt(1) == a && dp.getItemAt(2) == b, "addItemAt order");
            check(dp.getItemIndex(b) == 2 && dp.getItemIndex({}) == -1, "getItemIndex");

            log = [];
            check(dp.removeItemAt(0) == c, "removeItemAt returns the item");
            check(log[1] == "dataChange:remove:0-0:1", "remove event " + log[1]);
            check(dp.removeItem(c) == null, "removeItem of a missing item is null");
            check(dp.removeItem(b) == b && dp.length == 1, "removeItem");

            dp.addItems([b, c]);
            check(dp.length == 3 && dp.getItemAt(2) == c, "addItems");
            check(dp.replaceItemAt({label: "z"}, 1) == b && dp.getItemAt(1).label == "z", "replaceItemAt");
            check(dp.replaceItem(b, dp.getItemAt(1)) != null && dp.getItemAt(1) == b, "replaceItem");

            // Sorting, with the arguments Array.sort takes.
            dp.sortOn("label", Array.DESCENDING);
            check(dp.getItemAt(0) == c && dp.getItemAt(2) == a, "sortOn descending " + dp.toArray().map(function(o:*, i:int, x:Array):String { return o.label; }).join());
            dp.sort(function(x:Object, y:Object):int { return x.data - y.data; });
            check(dp.getItemAt(0) == a && dp.getItemAt(2) == c, "sort with a function");

            // merge adds only new items.
            dp.merge([a, {label: "d"}]);
            check(dp.length == 4 && dp.getItemAt(3).label == "d", "merge");

            // toArray is a copy.
            var copy:Array = dp.toArray();
            copy.pop();
            check(dp.length == 4, "toArray copies");

            // Out of range.
            var threw:Boolean = false;
            try {
                dp.getItemAt(10);
            } catch (e:RangeError) {
                threw = true;
            }
            check(threw, "getItemAt out of range throws RangeError");
            threw = false;
            try {
                dp.addItemAt({}, 9);
            } catch (e2:RangeError) {
                threw = true;
            }
            check(threw, "addItemAt out of range throws RangeError");

            log = [];
            dp.removeAll();
            check(dp.length == 0 && log[1].indexOf("dataChange:removeAll") == 0, "removeAll " + log.join("|"));

            // Made from an array (copied), another provider, or XML.
            var source:Array = [1, 2, 3];
            var fromArray:DataProvider = new DataProvider(source);
            source.push(4);
            check(fromArray.length == 3, "array copied");
            check(new DataProvider(fromArray).length == 3, "from a DataProvider");
            var xml:XML = <items><item label="One" data="1"/><item><label>Two</label><data>2</data></item></items>;
            var fromXML:DataProvider = new DataProvider(xml);
            check(fromXML.length == 2 && fromXML.getItemAt(0).label == "One" && fromXML.getItemAt(1).label == "Two" &&
                  fromXML.getItemAt(1).data == "2", "from XML");
            check(fromArray.toString() == "DataProvider [1 , 2 , 3]", "toString " + fromArray.toString());

            log = [];
            var shown:DataProvider = new DataProvider([a, b]);
            shown.addEventListener(DataChangeEvent.DATA_CHANGE, record);
            shown.invalidateItemAt(1);
            shown.invalidate();
            check(log[0] == "dataChange:invalidate:1-1:1" && log[1].indexOf("dataChange:invalidateAll") == 0, "invalidate " + log.join("|"));
        }
    }
}
