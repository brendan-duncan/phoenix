package fl.skins {
    import flash.display.Graphics;
    import flash.display.Sprite;

    /**
     * The skin a component draws for itself when the movie has no symbol
     * exported under the skin's class name (a movie without the component
     * assets in its library). Named as the symbol would be
     * ("Button_upSkin", "CheckBox_selectedOverIcon", "ScrollArrowUp_downSkin"
     * ...), it draws a plain look for that part and state, and draws again
     * at each size it is given, as a scale-9 symbol would scale.
     *
     * Its natural size is the matching symbol's, so layouts that measure a
     * skin before sizing it come out the same.
     */
    public class DefaultSkin extends Sprite {
        private var _name:String;
        private var _part:String;
        private var _state:String;
        private var _w:Number;
        private var _h:Number;

        // Colors of the plain look.
        private static const BORDER:uint = 0x919999;
        private static const BORDER_OVER:uint = 0x4C8ED6;
        private static const BORDER_DISABLED:uint = 0xC8CCCC;
        private static const FACE:uint = 0xF3F4F4;
        private static const FACE_OVER:uint = 0xE8F2FB;
        private static const FACE_DOWN:uint = 0xD3DCE3;
        private static const FACE_DISABLED:uint = 0xF6F6F6;
        private static const FACE_SELECTED:uint = 0xDCE6EF;
        private static const MARK:uint = 0x333333;
        private static const MARK_DISABLED:uint = 0xAAAAAA;

        public function DefaultSkin(name:String) {
            super();
            mouseEnabled = false;
            _name = name;
            var cut:int = name.lastIndexOf("_");
            _part = cut < 0 ? name : name.substring(0, cut);
            var state:String = cut < 0 ? "" : name.substring(cut + 1);
            // "selectedOverSkin" -> "selectedOver"; "upIcon" -> "up".
            state = state.replace(/(Skin|Icon)$/, "");
            _state = state == "" || state == "skin" ? "up" : state;
            var size:Array = naturalSize(_part);
            if (_part == "ColorPicker") {
                switch (_state) {
                    case "swatch": size = [8, 8]; break;
                    case "swatchSelected": size = [11, 11]; break;
                    case "colorWell": size = [19, 19]; break;
                    case "background": size = [272, 191]; break;
                    case "textField": size = [76, 36]; break;
                }
            }
            _w = size[0];
            _h = size[1];
            redraw();
        }

        private static const PARTS:Array = [
            "Button", "CheckBox", "RadioButton", "ScrollArrowUp", "ScrollArrowDown", "ScrollThumb",
            "ScrollTrack", "ScrollBar", "SliderThumb", "SliderTrack", "SliderTick", "NumericStepperUpArrow",
            "NumericStepperDownArrow", "ColorPicker", "CellRenderer", "TextInput", "TextArea", "ComboBox",
            "List", "ScrollPane", "TileList", "DataGrid", "focusRectSkin"
        ];

        /** True for the names of the standard component skins, which it can draw. */
        public static function handles(name:String):Boolean {
            var cut:int = name.lastIndexOf("_");
            return PARTS.indexOf(cut < 0 ? name : name.substring(0, cut)) >= 0;
        }

        /** The name it stands for: the skin's class name. */
        public function get skinName():String { return _name; }

        override public function get width():Number { return _w; }
        override public function set width(value:Number):void {
            if (value == _w) return;
            _w = value;
            redraw();
        }
        override public function get height():Number { return _h; }
        override public function set height(value:Number):void {
            if (value == _h) return;
            _h = value;
            redraw();
        }

        public function setSize(w:Number, h:Number):void {
            _w = w;
            _h = h;
            redraw();
        }

        // The symbols' sizes in the standard component assets.
        private static function naturalSize(part:String):Array {
            switch (part) {
                case "CheckBox":
                case "RadioButton": return [14, 14];
                case "ScrollArrowUp":
                case "ScrollArrowDown": return [15, 14];
                case "ScrollThumb": return [15, 52];
                case "ScrollTrack": return [15, 148];
                case "ScrollBar": return [5, 7];             // ScrollBar_thumbIcon
                case "SliderThumb": return [13, 13];
                case "SliderTrack": return [80, 3];
                case "SliderTick": return [1, 2];
                case "NumericStepperUpArrow":
                case "NumericStepperDownArrow": return [21, 12];
                case "ColorPicker": return [22, 22];
                case "CellRenderer":
                case "TextInput":
                case "TextArea": return [152, 22];
                case "ComboBox": return [148, 22];
                case "List":
                case "ScrollPane":
                case "TileList":
                case "DataGrid": return [273, 192];
                case "focusRectSkin": return [82, 22];
                default: return [82, 22];
            }
        }

        private function get disabled():Boolean { return _state.toLowerCase().indexOf("disabled") >= 0; }
        private function get selected():Boolean { return _state.indexOf("selected") == 0; }
        private function get over():Boolean { return _state.toLowerCase().indexOf("over") >= 0; }
        private function get down():Boolean { return _state.toLowerCase().indexOf("down") >= 0; }

        private function faceColor():uint {
            if (disabled) return FACE_DISABLED;
            if (down) return FACE_DOWN;
            if (over) return FACE_OVER;
            if (selected) return FACE_SELECTED;
            return FACE;
        }

        private function borderColor():uint {
            if (disabled) return BORDER_DISABLED;
            if (over || down) return BORDER_OVER;
            return BORDER;
        }

        private function redraw():void {
            var g:Graphics = graphics;
            g.clear();
            var w:Number = Math.max(0, _w);
            var h:Number = Math.max(0, _h);
            if (_name == "focusRectSkin") {
                frame(g, 0, 0, w, h, 2, BORDER_OVER, 0.6);
                return;
            }
            switch (_part) {
                case "CheckBox": drawCheckBox(g, w, h); break;
                case "RadioButton": drawRadio(g, w, h); break;
                case "ScrollArrowUp": drawButton(g, w, h); arrow(g, w / 2, h / 2, true); break;
                case "ScrollArrowDown": drawButton(g, w, h); arrow(g, w / 2, h / 2, false); break;
                case "ScrollThumb": drawButton(g, w, h); break;
                case "ScrollTrack": box(g, 0, 0, w, h, 0xE9EBEB, disabled ? BORDER_DISABLED : 0xD5D9D9); break;
                case "ScrollBar":
                    g.beginFill(0x7A8080);
                    for (var i:int = 0; i < 3; i++) g.drawRect(0, i * 3, w, 1);
                    g.endFill();
                    break;
                case "SliderThumb": drawSliderThumb(g, w, h); break;
                case "SliderTrack": box(g, 0, 0, w, h, disabled ? 0xE4E6E6 : 0xC5CBCB, disabled ? BORDER_DISABLED : BORDER); break;
                case "SliderTick": g.beginFill(BORDER); g.drawRect(-w / 2, 0, w, h); g.endFill(); break;
                case "NumericStepperUpArrow": drawButton(g, w, h); arrow(g, w / 2, h / 2 + 0.5, true); break;
                case "NumericStepperDownArrow": drawButton(g, w, h); arrow(g, w / 2, h / 2 - 0.5, false); break;
                case "ComboBox": drawComboBox(g, w, h); break;
                case "ColorPicker": drawColorPickerPart(g, w, h); break;
                case "TextInput":
                case "TextArea":
                    box(g, 0, 0, w, h, disabled ? 0xF3F3F3 : 0xFFFFFF, disabled ? BORDER_DISABLED : BORDER);
                    break;
                case "List":
                case "ScrollPane":
                case "TileList":
                case "DataGrid":
                    box(g, 0, 0, w, h, 0xFFFFFF, disabled ? BORDER_DISABLED : 0xB4BABA);
                    break;
                case "CellRenderer": drawCell(g, w, h); break;
                case "Button":
                    if (_state == "emphasized") {
                        frame(g, 0, 0, w, h, 2, 0x5E6666, 1);
                    } else {
                        drawButton(g, w, h);
                    }
                    break;
                default:
                    drawButton(g, w, h);
                    break;
            }
        }

        // A filled box with a one pixel border.
        private static function box(g:Graphics, x:Number, y:Number, w:Number, h:Number, fill:uint, line:uint):void {
            if (w <= 0 || h <= 0) return;
            g.beginFill(line);
            g.drawRect(x, y, w, h);
            if (w > 2 && h > 2) g.drawRect(x + 1, y + 1, w - 2, h - 2);
            g.endFill();
            if (w > 2 && h > 2) {
                g.beginFill(fill);
                g.drawRect(x + 1, y + 1, w - 2, h - 2);
                g.endFill();
            }
        }

        // A border of the given thickness, nothing inside.
        private static function frame(g:Graphics, x:Number, y:Number, w:Number, h:Number, t:Number, color:uint, alpha:Number):void {
            if (w <= 0 || h <= 0) return;
            g.beginFill(color, alpha);
            g.drawRect(x, y, w, h);
            if (w > 2 * t && h > 2 * t) g.drawRect(x + t, y + t, w - 2 * t, h - 2 * t);
            g.endFill();
        }

        private function drawButton(g:Graphics, w:Number, h:Number):void {
            box(g, 0, 0, w, h, faceColor(), borderColor());
            // A highlight along the top half.
            if (!disabled && w > 4 && h > 4) {
                g.beginFill(0xFFFFFF, down ? 0.15 : 0.5);
                g.drawRect(1, 1, w - 2, Math.floor((h - 2) / 2));
                g.endFill();
            }
        }

        private function arrow(g:Graphics, cx:Number, cy:Number, up:Boolean):void {
            g.beginFill(disabled ? MARK_DISABLED : MARK);
            if (up) {
                g.moveTo(cx - 3.5, cy + 2);
                g.lineTo(cx + 3.5, cy + 2);
                g.lineTo(cx, cy - 2);
                g.lineTo(cx - 3.5, cy + 2);
            } else {
                g.moveTo(cx - 3.5, cy - 2);
                g.lineTo(cx + 3.5, cy - 2);
                g.lineTo(cx, cy + 2);
                g.lineTo(cx - 3.5, cy - 2);
            }
            g.endFill();
        }

        private function drawCheckBox(g:Graphics, w:Number, h:Number):void {
            box(g, 0, 0, w, h, disabled ? FACE_DISABLED : (down ? FACE_DOWN : 0xFFFFFF), borderColor());
            if (selected) {
                // A check mark.
                var s:Number = Math.min(w, h) / 14;
                g.beginFill(disabled ? MARK_DISABLED : MARK);
                g.moveTo(3 * s, 7 * s);
                g.lineTo(5.5 * s, 9.5 * s);
                g.lineTo(11 * s, 3.5 * s);
                g.lineTo(11 * s, 5.5 * s);
                g.lineTo(5.5 * s, 11.5 * s);
                g.lineTo(3 * s, 9 * s);
                g.lineTo(3 * s, 7 * s);
                g.endFill();
            }
        }

        private function drawRadio(g:Graphics, w:Number, h:Number):void {
            var r:Number = Math.min(w, h) / 2;
            g.beginFill(borderColor());
            g.drawCircle(w / 2, h / 2, r);
            g.endFill();
            g.beginFill(disabled ? FACE_DISABLED : (down ? FACE_DOWN : 0xFFFFFF));
            g.drawCircle(w / 2, h / 2, r - 1);
            g.endFill();
            if (selected) {
                g.beginFill(disabled ? MARK_DISABLED : MARK);
                g.drawCircle(w / 2, h / 2, r * 0.4);
                g.endFill();
            }
        }

        private function drawSliderThumb(g:Graphics, w:Number, h:Number):void {
            // Pointing up, its tip at (0, 0), as the standard thumb.
            var half:Number = w / 2;
            g.beginFill(borderColor());
            g.moveTo(0, 0);
            g.lineTo(half, h * 0.4);
            g.lineTo(half, h);
            g.lineTo(-half, h);
            g.lineTo(-half, h * 0.4);
            g.lineTo(0, 0);
            g.endFill();
            g.beginFill(faceColor());
            g.moveTo(0, 1.4);
            g.lineTo(half - 1, h * 0.4 + 0.5);
            g.lineTo(half - 1, h - 1);
            g.lineTo(-half + 1, h - 1);
            g.lineTo(-half + 1, h * 0.4 + 0.5);
            g.lineTo(0, 1.4);
            g.endFill();
        }

        private function drawComboBox(g:Graphics, w:Number, h:Number):void {
            drawButton(g, w, h);
            // The drop button's divider and arrow, in the rightmost 24 pixels.
            var bx:Number = Math.max(0, w - 24);
            g.beginFill(borderColor());
            g.drawRect(bx, 3, 1, Math.max(0, h - 6));
            g.endFill();
            arrow(g, bx + 12, h / 2, false);
        }

        private function drawColorPickerPart(g:Graphics, w:Number, h:Number):void {
            switch (_state) {
                case "swatch":
                    // A plain square the picker tints to each color.
                    g.beginFill(0x00CCFF);
                    g.drawRect(0, 0, w, h);
                    g.endFill();
                    return;
                case "swatchSelected":
                    frame(g, 0, 0, w, h, 1, 0xFFFFFF, 1);
                    return;
                case "colorWell":
                    g.beginFill(0x999999);
                    g.drawRect(0, 0, w, h);
                    g.endFill();
                    return;
                case "background":
                    box(g, 0, 0, w, h, 0xF0F2F2, BORDER);
                    return;
                case "textField":
                    box(g, 0, 0, w, h, 0xFFFFFF, BORDER);
                    return;
            }
            // The button: a frame with the color showing through the middle,
            // and a small arrow in the lower right corner.
            frame(g, 0, 0, w, h, 1, borderColor(), 1);
            frame(g, 1, 1, w - 2, h - 2, 2, faceColor(), 1);
            if (w > 12 && h > 12) {
                g.beginFill(faceColor());
                g.drawRect(w - 9, h - 7, 6, 4);
                g.endFill();
                g.beginFill(disabled ? MARK_DISABLED : MARK);
                g.moveTo(w - 8.5, h - 6);
                g.lineTo(w - 3.5, h - 6);
                g.lineTo(w - 6, h - 3.5);
                g.lineTo(w - 8.5, h - 6);
                g.endFill();
            }
        }

        private function drawCell(g:Graphics, w:Number, h:Number):void {
            var color:uint = 0xFFFFFF;
            if (selected) color = disabled ? 0xE6E6E6 : (over || down ? 0xB9D7F2 : 0xCDE3F6);
            else if (disabled) color = 0xFFFFFF;
            else if (down) color = 0xCDE3F6;
            else if (over) color = 0xE8F2FB;
            g.beginFill(color);
            g.drawRect(0, 0, w, h);
            g.endFill();
        }
    }
}
