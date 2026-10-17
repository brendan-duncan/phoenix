package fl.controls {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import fl.events.ColorPickerEvent;
    import fl.managers.IFocusManagerComponent;
    import flash.display.DisplayObject;
    import flash.display.Sprite;
    import flash.display.Stage;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.geom.ColorTransform;
    import flash.geom.Point;
    import flash.text.TextField;
    import flash.text.TextFieldType;
    import flash.text.TextFormat;
    import flash.ui.Keyboard;
    import flash.utils.Dictionary;

    /** Dispatched when the user picks a swatch. */
    [Event(name="change", type="fl.events.ColorPickerEvent")]
    /** Dispatched when the user types a color and presses Enter. */
    [Event(name="enter", type="fl.events.ColorPickerEvent")]
    [Event(name="itemRollOver", type="fl.events.ColorPickerEvent")]
    [Event(name="itemRollOut", type="fl.events.ColorPickerEvent")]
    /** Dispatched when the palette opens. */
    [Event(name="open", type="flash.events.Event")]
    /** Dispatched when the palette closes. */
    [Event(name="close", type="flash.events.Event")]

    /**
     * A button showing a color that opens a palette of swatches: the 216
     * web colors in 18 columns unless colors is set, with the color under
     * the mouse shown large beside a field of its hex value (showTextField),
     * which can be typed in (editable).
     *
     * The colors getter gives the palette's array itself: pushing colors
     * onto it adds swatches (to every picker using the default palette).
     * Setting selectedColor in code dispatches nothing; picking a swatch
     * dispatches ColorPickerEvent.CHANGE with the color.
     */
    public class ColorPicker extends UIComponent implements IFocusManagerComponent {
        /** The hex field in the palette. */
        public var textField:TextField;

        /** The 216 web colors, made on first use; shared by the pickers without colors of their own. */
        public static var defaultColors:Array;

        protected var customColors:Array;
        protected var colorHash:Object;
        protected var paletteBG:DisplayObject;
        protected var selectedSwatch:Sprite;
        protected var _selectedColor:uint = 0;
        protected var rollOverColor:int = -1;
        protected var _editable:Boolean = true;
        protected var _showTextField:Boolean = true;
        protected var isOpen:Boolean = false;
        protected var doOpen:Boolean = false;
        protected var swatchButton:BaseButton;
        protected var buttonColor:DisplayObject;
        protected var colorWell:DisplayObject;
        protected var swatchSelectedSkin:DisplayObject;
        protected var palette:Sprite;
        protected var textFieldBG:DisplayObject;
        protected var swatches:Sprite;
        protected var swatchMap:Array;
        protected var currRowIndex:int = 0;
        protected var currColIndex:int = 0;
        private var swatchColors:Dictionary;
        private var paletteStage:Stage = null;
        private var builtFor:String = "";

        private static var defaultStyles:Object = {
            upSkin: "ColorPicker_upSkin",
            disabledSkin: "ColorPicker_disabledSkin",
            overSkin: "ColorPicker_overSkin",
            downSkin: "ColorPicker_downSkin",
            colorWell: "ColorPicker_colorWell",
            swatchSkin: "ColorPicker_swatchSkin",
            swatchSelectedSkin: "ColorPicker_swatchSelectedSkin",
            swatchWidth: 10,
            swatchHeight: 10,
            columnCount: 18,
            swatchPadding: 1,
            textFieldSkin: "ColorPicker_textFieldSkin",
            textFieldWidth: null,
            textFieldHeight: null,
            textPadding: 3,
            background: "ColorPicker_backgroundSkin",
            backgroundPadding: 5,
            textFormat: null,
            focusRectSkin: null,
            focusRectPadding: null,
            embedFonts: false
        };

        protected static const POPUP_BUTTON_STYLES:Object = {
            disabledSkin: "disabledSkin",
            downSkin: "downSkin",
            overSkin: "overSkin",
            upSkin: "upSkin"
        };

        public static function getStyleDefinition():Object {
            return defaultStyles;
        }

        public function ColorPicker() {
            super();
        }

        override protected function defaultSize():Array {
            return [24, 24];
        }

        // The web palette: red in blocks of six columns (0, 33, 66 in the
        // top six rows, 99, CC, FF below), blue across each block, green down.
        private static function makeDefaultColors():Array {
            var colors:Array = [];
            for (var i:int = 0; i < 216; i++) {
                var column:int = i % 18;
                var row:int = int(i / 18);
                var red:int = int(column / 6) + (row >= 6 ? 3 : 0);
                var green:int = row % 6;
                var blue:int = column % 6;
                colors.push((red * 0x33) << 16 | (green * 0x33) << 8 | (blue * 0x33));
            }
            return colors;
        }

        /** The color shown, 0xRRGGBB. */
        public function get selectedColor():uint {
            return _selectedColor;
        }

        public function set selectedColor(value:uint):void {
            _selectedColor = value & 0xFFFFFF;
            rollOverColor = -1;
            currColIndex = currRowIndex = 0;
            setButtonColor(_selectedColor);
            if (isOpen) {
                showColor(_selectedColor);
                findSwatch(_selectedColor);
            }
            invalidate(InvalidationType.STATE);
        }

        /** The selected color as six hex digits, without "#" or "0x". */
        public function get hexValue():String {
            return colorToHex(_selectedColor);
        }

        override public function get enabled():Boolean {
            return super.enabled;
        }

        override public function set enabled(value:Boolean):void {
            super.enabled = value;
            if (!value) close();
            swatchButton.enabled = value;
        }

        /** Whether a color can be typed in the palette's field. */
        public function get editable():Boolean {
            return _editable;
        }

        public function set editable(value:Boolean):void {
            _editable = value;
            invalidate(InvalidationType.STATE);
        }

        /** Whether the palette shows the large color and its hex field. */
        public function get showTextField():Boolean {
            return _showTextField;
        }

        public function set showTextField(value:Boolean):void {
            invalidate(InvalidationType.STYLES);
            _showTextField = value;
        }

        /** The palette's colors (the shared default palette unless set). */
        public function get colors():Array {
            if (customColors != null) return customColors;
            if (defaultColors == null) defaultColors = makeDefaultColors();
            return defaultColors;
        }

        public function set colors(value:Array):void {
            customColors = value;
            invalidate(InvalidationType.DATA);
        }

        /** Opens the palette. Dispatches Event.OPEN. */
        public function open():void {
            if (!_enabled || isOpen || stage == null) return;
            doOpen = false;
            buildPalette();
            paletteStage = stage;
            positionPalette();
            paletteStage.addChild(palette);
            isOpen = true;
            rollOverColor = -1;
            showColor(_selectedColor);
            findSwatch(_selectedColor);
            if (_editable && _showTextField && textField != null) {
                paletteStage.focus = textField;
                textField.setSelection(0, textField.length);
            }
            // Clicks elsewhere close it, from the next frame on.
            addEventListener(Event.ENTER_FRAME, addCloseListener, false, 0, true);
            dispatchEvent(new Event(Event.OPEN));
        }

        /** Closes the palette. Dispatches Event.CLOSE when it was open. */
        public function close():void {
            if (!isOpen) return;
            removeEventListener(Event.ENTER_FRAME, addCloseListener);
            if (paletteStage != null) {
                paletteStage.removeEventListener(MouseEvent.MOUSE_DOWN, onStageClick);
                if (palette != null && palette.parent == paletteStage) paletteStage.removeChild(palette);
            }
            paletteStage = null;
            isOpen = false;
            dispatchEvent(new Event(Event.CLOSE));
        }

        override protected function configUI():void {
            super.configUI();
            swatchSelectedSkin = null;
            swatchMap = [];
            swatchColors = new Dictionary(true);

            buttonColor = getDisplayObjectInstance(getStyleValue("swatchSkin"));
            if (buttonColor != null) {
                addChild(buttonColor);
            }
            swatchButton = new BaseButton();
            swatchButton.focusEnabled = false;
            copyStylesToChild(swatchButton, POPUP_BUTTON_STYLES);
            addChild(swatchButton);

            addEventListener(MouseEvent.MOUSE_DOWN, onButtonDown, false, 0, true);
            addEventListener(Event.REMOVED_FROM_STAGE, onRemovedFromStage, false, 0, true);
            setButtonColor(_selectedColor);
        }

        override protected function draw():void {
            if (isInvalid(InvalidationType.STYLES)) {
                copyStylesToChild(swatchButton, POPUP_BUTTON_STYLES);
                var old:DisplayObject = buttonColor;
                buttonColor = getDisplayObjectInstance(getStyleValue("swatchSkin"));
                if (old != null && old != buttonColor && old.parent == this) removeChild(old);
                if (buttonColor != null) addChildAt(buttonColor, 0);
                setButtonColor(_selectedColor);
                builtFor = "";
                invalidate(InvalidationType.SIZE, false);
            }
            if (isInvalid(InvalidationType.DATA, InvalidationType.STATE)) {
                builtFor = "";
            }
            if (isInvalid(InvalidationType.SIZE, InvalidationType.STATE)) {
                drawLayout();
            }
            swatchButton.enabled = enabled;
            swatchButton.drawNow();
            if (isOpen) {
                buildPalette();
            }
            super.draw();
        }

        /** The button over the whole size; the color showing through its middle. */
        protected function drawLayout():void {
            swatchButton.setSize(width, height);
            if (buttonColor != null) {
                buttonColor.x = 3;
                buttonColor.y = 3;
                buttonColor.width = Math.max(0, width - 6);
                buttonColor.height = Math.max(0, height - 6);
            }
        }

        protected function setButtonColor(color:uint):void {
            if (buttonColor == null) return;
            tint(buttonColor, color);
        }

        // Tints a skin to a color: its shapes all that color.
        protected static function tint(target:DisplayObject, color:uint):void {
            var transform:ColorTransform = new ColorTransform();
            transform.color = color;
            target.transform.colorTransform = transform;
        }

        // Makes the palette for the current colors and styles, unless made already.
        protected function buildPalette():void {
            var list:Array = colors;
            var key:String = list.length + ":" + list.join(",") + ":" + _showTextField + ":" + _editable;
            if (key == builtFor && palette != null) return;
            builtFor = key;

            var wasOpen:Boolean = palette != null && palette.parent != null;
            var parentOfPalette:Object = wasOpen ? palette.parent : null;
            if (wasOpen) parentOfPalette.removeChild(palette);

            palette = new Sprite();
            palette.tabChildren = false;
            var padding:Number = numberStyle("backgroundPadding", 5);
            var columns:int = Math.max(1, int(numberStyle("columnCount", 18)));
            var swatchWidth:Number = numberStyle("swatchWidth", 10);
            var swatchHeight:Number = numberStyle("swatchHeight", 10);
            var swatchPadding:Number = numberStyle("swatchPadding", 1);

            paletteBG = getDisplayObjectInstance(getStyleValue("background"));
            if (paletteBG != null) palette.addChild(paletteBG);

            var gridWidth:Number = columns * (swatchWidth + swatchPadding) + swatchPadding;
            var top:Number = padding;
            textField = null;
            colorWell = null;
            textFieldBG = null;
            if (_showTextField) {
                var rowHeight:Number = numberStyle("textFieldHeight", 22);
                colorWell = getDisplayObjectInstance(getStyleValue("colorWell"));
                if (colorWell != null) {
                    colorWell.x = padding;
                    colorWell.y = top;
                    colorWell.width = rowHeight;
                    colorWell.height = rowHeight;
                    palette.addChild(colorWell);
                }
                var fieldX:Number = padding + rowHeight + padding;
                var fieldWidth:Number = numberStyle("textFieldWidth", Math.max(40, gridWidth - rowHeight - padding));
                textFieldBG = getDisplayObjectInstance(getStyleValue("textFieldSkin"));
                if (textFieldBG != null) {
                    textFieldBG.x = fieldX;
                    textFieldBG.y = top;
                    textFieldBG.width = fieldWidth;
                    textFieldBG.height = rowHeight;
                    palette.addChild(textFieldBG);
                }
                textField = new TextField();
                textField.type = _editable ? TextFieldType.INPUT : TextFieldType.DYNAMIC;
                textField.selectable = _editable;
                textField.maxChars = 6;
                textField.restrict = "A-Fa-f0-9#";
                var format:TextFormat = getStyleValue("textFormat") as TextFormat;
                if (format == null) format = UIComponent.getStyleDefinition().defaultTextFormat as TextFormat;
                textField.defaultTextFormat = format;
                var embed:Object = getStyleValue("embedFonts");
                if (embed != null) setNativeProperty(textField, "embedFonts", Boolean(embed));
                var textPadding:Number = numberStyle("textPadding", 3);
                textField.x = fieldX + textPadding;
                textField.width = Math.max(0, fieldWidth - textPadding * 2);
                textField.height = rowHeight - textPadding;
                textField.y = top + Math.max(0, (rowHeight - textField.height) / 2);
                textField.text = colorToHex(_selectedColor);
                textField.addEventListener(KeyboardEvent.KEY_DOWN, onTextKeyDown, false, 0, true);
                palette.addChild(textField);
                top += rowHeight + padding;
            }

            swatches = new Sprite();
            swatches.x = padding;
            swatches.y = top;
            swatchMap = [];
            swatchColors = new Dictionary(true);
            for (var i:int = 0; i < list.length; i++) {
                var color:uint = uint(list[i]) & 0xFFFFFF;
                var swatch:Sprite = new Sprite();
                var skin:DisplayObject = getDisplayObjectInstance(getStyleValue("swatchSkin"));
                if (skin == null) {
                    swatch.graphics.beginFill(0);
                    swatch.graphics.drawRect(0, 0, swatchWidth, swatchHeight);
                    swatch.graphics.endFill();
                    tint(swatch, color);
                } else {
                    skin.width = swatchWidth;
                    skin.height = swatchHeight;
                    tint(skin, color);
                    swatch.addChild(skin);
                }
                var column:int = i % columns;
                var row:int = int(i / columns);
                swatch.x = swatchPadding + column * (swatchWidth + swatchPadding);
                swatch.y = swatchPadding + row * (swatchHeight + swatchPadding);
                swatch.mouseChildren = false;
                swatch.name = "swatch" + i;
                swatchColors[swatch] = color;
                if (swatchMap[row] == null) swatchMap[row] = [];
                swatchMap[row][column] = swatch;
                swatches.addChild(swatch);
            }
            var rows:int = Math.ceil(list.length / columns);
            var gridHeight:Number = rows * (swatchHeight + swatchPadding) + swatchPadding;
            // The swatches sit on the palette's border color.
            swatches.graphics.beginFill(0x000000);
            swatches.graphics.drawRect(0, 0, gridWidth, gridHeight);
            swatches.graphics.endFill();
            swatches.addEventListener(MouseEvent.MOUSE_OVER, onSwatchOver, false, 0, true);
            swatches.addEventListener(MouseEvent.MOUSE_OUT, onSwatchOut, false, 0, true);
            swatches.addEventListener(MouseEvent.CLICK, onSwatchClick, false, 0, true);
            palette.addChild(swatches);

            swatchSelectedSkin = getDisplayObjectInstance(getStyleValue("swatchSelectedSkin"));
            if (swatchSelectedSkin != null) {
                swatchSelectedSkin.width = swatchWidth + 2;
                swatchSelectedSkin.height = swatchHeight + 2;
                swatchSelectedSkin.visible = false;
                if (swatchSelectedSkin is Sprite) (swatchSelectedSkin as Sprite).mouseEnabled = false;
                swatches.addChild(swatchSelectedSkin);
            }

            if (paletteBG != null) {
                paletteBG.width = gridWidth + padding * 2;
                paletteBG.height = top + gridHeight + padding;
            }
            if (wasOpen) {
                parentOfPalette.addChild(palette);
                positionPalette();
            }
        }

        private function numberStyle(name:String, fallback:Number):Number {
            var value:Number = Number(getStyleValue(name));
            return isNaN(value) || getStyleValue(name) == null ? fallback : value;
        }

        /** Below the button, or above it when the stage has no room below. */
        protected function positionPalette():void {
            if (paletteStage == null || palette == null) return;
            var p:Point = paletteStage.globalToLocal(localToGlobal(new Point(0, 0)));
            var paletteHeight:Number = palette.height;
            var paletteWidth:Number = palette.width;
            palette.x = p.x;
            if (p.x + paletteWidth > paletteStage.stageWidth && p.x + width - paletteWidth >= 0) {
                palette.x = p.x + width - paletteWidth;
            }
            if (p.y + height + paletteHeight > paletteStage.stageHeight && p.y - paletteHeight >= 0) {
                palette.y = p.y - paletteHeight;
            } else {
                palette.y = p.y + height;
            }
        }

        // Shows a color large and in the field (rolled over, or the selection).
        protected function showColor(color:uint):void {
            if (colorWell != null) tint(colorWell, color);
            if (textField != null) textField.text = colorToHex(color);
        }

        // Puts the selection mark on the swatch of a color, if there is one.
        protected function findSwatch(color:uint):void {
            if (swatchSelectedSkin == null) return;
            swatchSelectedSkin.visible = false;
            for (var row:int = 0; row < swatchMap.length; row++) {
                var line:Array = swatchMap[row];
                for (var column:int = 0; line != null && column < line.length; column++) {
                    var swatch:Sprite = line[column];
                    if (swatch != null && swatchColors[swatch] == color) {
                        markSwatch(swatch);
                        currRowIndex = row;
                        currColIndex = column;
                        return;
                    }
                }
            }
        }

        private function markSwatch(swatch:Sprite):void {
            if (swatchSelectedSkin == null) return;
            swatchSelectedSkin.x = swatch.x - 1;
            swatchSelectedSkin.y = swatch.y - 1;
            swatchSelectedSkin.visible = true;
            swatches.setChildIndex(swatchSelectedSkin, swatches.numChildren - 1);
        }

        private function swatchOf(target:Object):Sprite {
            var d:DisplayObject = target as DisplayObject;
            while (d != null && d != swatches) {
                if (swatchColors[d] != null) return d as Sprite;
                d = d.parent;
            }
            return null;
        }

        protected function onSwatchOver(event:MouseEvent):void {
            var swatch:Sprite = swatchOf(event.target);
            if (swatch == null) return;
            var color:uint = swatchColors[swatch];
            rollOverColor = color;
            showColor(color);
            markSwatch(swatch);
            dispatchEvent(new ColorPickerEvent(ColorPickerEvent.ITEM_ROLL_OVER, color));
        }

        protected function onSwatchOut(event:MouseEvent):void {
            var swatch:Sprite = swatchOf(event.target);
            if (swatch == null) return;
            rollOverColor = -1;
            showColor(_selectedColor);
            findSwatch(_selectedColor);
            dispatchEvent(new ColorPickerEvent(ColorPickerEvent.ITEM_ROLL_OUT, swatchColors[swatch]));
        }

        protected function onSwatchClick(event:MouseEvent):void {
            var swatch:Sprite = swatchOf(event.target);
            if (swatch == null) return;
            pick(swatchColors[swatch]);
        }

        // The user chose a color: select it, close, and tell.
        private function pick(color:uint):void {
            selectedColor = color;
            close();
            dispatchEvent(new ColorPickerEvent(ColorPickerEvent.CHANGE, color));
        }

        protected function onButtonDown(event:MouseEvent):void {
            if (!_enabled) return;
            if (isOpen) {
                close();
            } else {
                open();
            }
        }

        protected function addCloseListener(event:Event):void {
            removeEventListener(Event.ENTER_FRAME, addCloseListener);
            if (!isOpen || paletteStage == null) return;
            paletteStage.addEventListener(MouseEvent.MOUSE_DOWN, onStageClick, false, 0, true);
        }

        protected function onStageClick(event:MouseEvent):void {
            var target:DisplayObject = event.target as DisplayObject;
            if (target != null && (contains(target) || (palette != null && palette.contains(target)))) return;
            close();
        }

        private function onRemovedFromStage(event:Event):void {
            close();
        }

        protected function onTextKeyDown(event:KeyboardEvent):void {
            if (event.keyCode == Keyboard.ENTER && textField != null) {
                var color:uint = hexToColor(textField.text);
                selectedColor = color;
                close();
                dispatchEvent(new ColorPickerEvent(ColorPickerEvent.ENTER, color));
            } else if (event.keyCode == Keyboard.ESCAPE) {
                close();
            }
        }

        override protected function keyDownHandler(event:KeyboardEvent):void {
            if (!_enabled) return;
            if (!isOpen) {
                if (event.ctrlKey && event.keyCode == Keyboard.DOWN) open();
                else if (event.keyCode == Keyboard.SPACE) open();
                return;
            }
            var row:int = currRowIndex;
            var column:int = currColIndex;
            switch (event.keyCode) {
                case Keyboard.ESCAPE:
                    close();
                    return;
                case Keyboard.ENTER:
                    var current:Sprite = swatchMap[row] == null ? null : swatchMap[row][column];
                    if (current != null) pick(swatchColors[current]);
                    return;
                case Keyboard.LEFT: column--; break;
                case Keyboard.RIGHT: column++; break;
                case Keyboard.UP: row--; break;
                case Keyboard.DOWN: row++; break;
                default: return;
            }
            if (row < 0 || row >= swatchMap.length) return;
            var line:Array = swatchMap[row];
            if (line == null || column < 0 || column >= line.length || line[column] == null) return;
            currRowIndex = row;
            currColIndex = column;
            var swatch:Sprite = line[column];
            rollOverColor = swatchColors[swatch];
            showColor(uint(rollOverColor));
            markSwatch(swatch);
        }

        /** "RRGGBB" for a color. */
        protected static function colorToHex(color:uint):String {
            var text:String = (color & 0xFFFFFF).toString(16).toUpperCase();
            while (text.length < 6) text = "0" + text;
            return text;
        }

        /** A color for "RRGGBB", "#RRGGBB" or "0xRRGGBB"; 0 for nothing valid. */
        protected static function hexToColor(text:String):uint {
            var clean:String = text.replace(/^(#|0x)/i, "");
            var value:Number = parseInt(clean, 16);
            return isNaN(value) ? 0 : uint(value) & 0xFFFFFF;
        }
    }
}
