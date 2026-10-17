package fl.data {
    import flash.events.EventDispatcher;
    import fl.events.DataChangeEvent;
    import fl.events.DataChangeType;

    /**
     * An ordered list of items for the list-based components (List,
     * ComboBox, ...). Each change dispatches DataChangeEvent.PRE_DATA_CHANGE
     * before it and DataChangeEvent.DATA_CHANGE after it, so the components
     * showing the items can follow.
     *
     * Made from an Array (its items, copied), another DataProvider (its
     * items) or XML (each child element an object of its attributes and
     * child elements' text).
     */
    public class DataProvider extends EventDispatcher {
        protected var data:Array;

        public function DataProvider(value:Object = null) {
            super();
            data = value == null ? [] : getDataFromObject(value);
        }

        public function get length():uint {
            return data.length;
        }

        public function invalidateItemAt(index:int):void {
            checkIndex(index, data.length - 1);
            dispatchChangeEvent(DataChangeType.INVALIDATE, [data[index]], index, index);
        }

        public function invalidateItem(item:Object):void {
            var index:int = getItemIndex(item);
            if (index == -1) return;
            invalidateItemAt(index);
        }

        public function invalidate():void {
            dispatchEvent(new DataChangeEvent(DataChangeEvent.DATA_CHANGE, DataChangeType.INVALIDATE_ALL, data.concat(), 0, data.length));
        }

        public function addItemAt(item:Object, index:uint):void {
            checkIndex(index, data.length);
            dispatchPreChangeEvent(DataChangeType.ADD, [item], index, index);
            data.splice(index, 0, item);
            dispatchChangeEvent(DataChangeType.ADD, [item], index, index);
        }

        public function addItem(item:Object):void {
            dispatchPreChangeEvent(DataChangeType.ADD, [item], data.length, data.length);
            data.push(item);
            dispatchChangeEvent(DataChangeType.ADD, [item], data.length - 1, data.length - 1);
        }

        public function addItemsAt(items:Object, index:uint):void {
            checkIndex(index, data.length);
            var list:Array = getDataFromObject(items);
            dispatchPreChangeEvent(DataChangeType.ADD, list, index, index + list.length - 1);
            data.splice.apply(data, [index, 0].concat(list));
            dispatchChangeEvent(DataChangeType.ADD, list, index, index + list.length - 1);
        }

        public function addItems(items:Object):void {
            addItemsAt(items, data.length);
        }

        public function concat(items:Object):void {
            addItems(items);
        }

        /** Adds the items not already in the list. */
        public function merge(newData:Object):void {
            var list:Array = getDataFromObject(newData);
            var added:Array = [];
            var start:int = data.length;
            dispatchPreChangeEvent(DataChangeType.ADD, data.concat(), 0, data.length);
            for (var i:int = 0; i < list.length; i++) {
                if (data.indexOf(list[i]) == -1) {
                    data.push(list[i]);
                    added.push(list[i]);
                }
            }
            if (added.length > 0) {
                dispatchChangeEvent(DataChangeType.ADD, added, start, data.length - 1);
            } else {
                dispatchChangeEvent(DataChangeType.ADD, [], -1, -1);
            }
        }

        public function getItemAt(index:uint):Object {
            checkIndex(index, data.length - 1);
            return data[index];
        }

        public function getItemIndex(item:Object):int {
            return data.indexOf(item);
        }

        public function removeItemAt(index:uint):Object {
            checkIndex(index, data.length - 1);
            dispatchPreChangeEvent(DataChangeType.REMOVE, data.slice(index, index + 1), index, index);
            var removed:Array = data.splice(index, 1);
            dispatchChangeEvent(DataChangeType.REMOVE, removed, index, index);
            return removed[0];
        }

        public function removeItem(item:Object):Object {
            var index:int = getItemIndex(item);
            if (index != -1) return removeItemAt(index);
            return null;
        }

        public function removeAll():void {
            var old:Array = data.concat();
            dispatchPreChangeEvent(DataChangeType.REMOVE_ALL, old, 0, old.length);
            data = [];
            dispatchChangeEvent(DataChangeType.REMOVE_ALL, old, 0, old.length);
        }

        public function replaceItem(newItem:Object, oldItem:Object):Object {
            var index:int = getItemIndex(oldItem);
            if (index != -1) return replaceItemAt(newItem, index);
            return null;
        }

        public function replaceItemAt(newItem:Object, index:uint):Object {
            checkIndex(index, data.length - 1);
            var items:Array = [data[index]];
            dispatchPreChangeEvent(DataChangeType.REPLACE, items, index, index);
            data[index] = newItem;
            dispatchChangeEvent(DataChangeType.REPLACE, items, index, index);
            return items[0];
        }

        /** Array.sort on the items, with its arguments. */
        public function sort(...sortArgs):* {
            dispatchPreChangeEvent(DataChangeType.SORT, data.concat(), 0, data.length - 1);
            var result:* = data.sort.apply(data, sortArgs);
            dispatchChangeEvent(DataChangeType.SORT, data.concat(), 0, data.length - 1);
            return result;
        }

        /** Array.sortOn on the items. */
        public function sortOn(fieldName:Object, options:Object = null):* {
            dispatchPreChangeEvent(DataChangeType.SORT, data.concat(), 0, data.length - 1);
            var result:* = options == null ? data.sortOn(fieldName) : data.sortOn(fieldName, options);
            dispatchChangeEvent(DataChangeType.SORT, data.concat(), 0, data.length - 1);
            return result;
        }

        public function clone():DataProvider {
            return new DataProvider(data);
        }

        public function toArray():Array {
            return data.concat();
        }

        override public function toString():String {
            return "DataProvider [" + data.join(" , ") + "]";
        }

        protected function getDataFromObject(obj:Object):Array {
            if (obj is Array) {
                return (obj as Array).concat();
            }
            if (obj is DataProvider) {
                return (obj as DataProvider).toArray();
            }
            if (obj is XML || obj is XMLList) {
                var list:Array = [];
                var nodes:XMLList = obj is XML ? (obj as XML).children() : (obj as XMLList);
                for each (var node:XML in nodes) {
                    if (node.nodeKind() != "element") continue;
                    var item:Object = {};
                    for each (var attribute:XML in node.attributes()) {
                        item[String(attribute.name())] = attribute.toString();
                    }
                    for each (var child:XML in node.elements()) {
                        item[String(child.name())] = child.text().toString();
                    }
                    list.push(item);
                }
                return list;
            }
            throw new TypeError("Error: Type Coercion failed: cannot convert " + obj + " to Array or DataProvider.");
        }

        protected function checkIndex(index:int, maximum:int):void {
            if (index > maximum || index < 0) {
                throw new RangeError("DataProvider index (" + index + ") is not in acceptable range (0 - " + maximum + ")");
            }
        }

        protected function dispatchChangeEvent(evtType:String, items:Array, startIndex:int, endIndex:int):void {
            dispatchEvent(new DataChangeEvent(DataChangeEvent.DATA_CHANGE, evtType, items, startIndex, endIndex));
        }

        protected function dispatchPreChangeEvent(evtType:String, items:Array, startIndex:int, endIndex:int):void {
            dispatchEvent(new DataChangeEvent(DataChangeEvent.PRE_DATA_CHANGE, evtType, items, startIndex, endIndex));
        }
    }
}
