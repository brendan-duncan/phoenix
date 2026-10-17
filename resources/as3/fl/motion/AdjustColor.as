package fl.motion {
    /**
     * Brightness, contrast, saturation and hue in one color matrix, as the
     * Adjust Color filter has them: brightness, contrast and saturation from
     * -100 to 100, hue from -180 to 180. CalculateFinalFlatArray gives the
     * matrix for a ColorMatrixFilter: saturation first, then the hue turned,
     * then contrast about the middle gray, then the brightness added.
     */
    public class AdjustColor {
        private var _brightness:Number = NaN;
        private var _contrast:Number = NaN;
        private var _saturation:Number = NaN;
        private var _hue:Number = NaN;

        public function AdjustColor() {}

        public function set brightness(value:Number):void {
            _brightness = value;
        }

        public function get brightness():Number {
            return _brightness;
        }

        public function set contrast(value:Number):void {
            _contrast = value;
        }

        public function get contrast():Number {
            return _contrast;
        }

        public function set saturation(value:Number):void {
            _saturation = value;
        }

        public function get saturation():Number {
            return _saturation;
        }

        public function set hue(value:Number):void {
            _hue = value;
        }

        public function get hue():Number {
            return _hue;
        }

        public function clone():AdjustColor {
            var copy:AdjustColor = new AdjustColor();
            copy.brightness = _brightness;
            copy.contrast = _contrast;
            copy.saturation = _saturation;
            copy.hue = _hue;
            return copy;
        }

        /** True when all four are set. */
        public function AllValuesAreSet():Boolean {
            return !isNaN(_brightness) && !isNaN(_contrast) && !isNaN(_saturation) && !isNaN(_hue);
        }

        private static function limit(value:Number, low:Number, high:Number):Number {
            if (isNaN(value)) return 0;
            return Math.max(low, Math.min(high, value));
        }

        /** The 20 numbers of the matrix. */
        public function CalculateFinalFlatArray():Array {
            var saturationMatrix:ColorMatrix = new ColorMatrix();
            saturationMatrix.SetSaturationMatrix(1 + limit(_saturation, -100, 100) / 100);
            var hueMatrix:ColorMatrix = new ColorMatrix();
            hueMatrix.SetHueMatrix(limit(_hue, -180, 180) * Math.PI / 180);
            var contrastScale:Number = 1 + limit(_contrast, -100, 100) / 100;
            var offset:Number = 128 * (1 - contrastScale) + limit(_brightness, -100, 100) * 2.55;

            var flat:Array = [];
            for (var row:int = 0; row < 4; row++) {
                for (var column:int = 0; column < 5; column++) {
                    if (row == 3) {
                        flat.push(column == 3 ? 1 : 0);
                    } else if (column == 4) {
                        flat.push(offset);
                    } else if (column == 3) {
                        flat.push(0);
                    } else {
                        var sum:Number = 0;
                        for (var k:int = 0; k < 3; k++) {
                            sum += hueMatrix.GetValue(row, k) * saturationMatrix.GetValue(k, column);
                        }
                        flat.push(contrastScale * sum);
                    }
                }
            }
            return flat;
        }
    }
}
