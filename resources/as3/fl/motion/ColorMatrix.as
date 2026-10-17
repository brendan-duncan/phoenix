package fl.motion {
    /**
     * A 5 by 5 color matrix (red, green, blue, alpha and the offset), with
     * the adjustments AdjustColor is made of. GetFlatArray gives the 20
     * numbers a ColorMatrixFilter takes.
     */
    public class ColorMatrix extends DynamicMatrix {
        // The share of red, green and blue in what the eye takes as lightness.
        protected static const LUMINANCER:Number = 0.3086;
        protected static const LUMINANCEG:Number = 0.6094;
        protected static const LUMINANCEB:Number = 0.0820;

        public function ColorMatrix() {
            super(5, 5);
            LoadIdentity();
        }

        /** Adds an offset to red, green and blue (-255 to 255). */
        public function SetBrightnessMatrix(value:Number):void {
            LoadIdentity();
            SetValue(0, 4, value);
            SetValue(1, 4, value);
            SetValue(2, 4, value);
        }

        /** Scales red, green and blue about the middle gray (1 leaves them). */
        public function SetContrastMatrix(value:Number):void {
            LoadIdentity();
            var offset:Number = 128 * (1 - value);
            for (var i:int = 0; i < 3; i++) {
                SetValue(i, i, value);
                SetValue(i, 4, offset);
            }
        }

        /** Scales the saturation about the gray of the same lightness (1 leaves it, 0 is gray). */
        public function SetSaturationMatrix(value:Number):void {
            LoadIdentity();
            var luminance:Array = [LUMINANCER, LUMINANCEG, LUMINANCEB];
            for (var row:int = 0; row < 3; row++) {
                for (var column:int = 0; column < 3; column++) {
                    SetValue(row, column, luminance[column] * (1 - value) + (row == column ? value : 0));
                }
            }
        }

        /** Turns the hue about the gray axis, by an angle in radians. */
        public function SetHueMatrix(angle:Number):void {
            LoadIdentity();
            var cosA:Number = Math.cos(angle);
            var sinA:Number = Math.sin(angle);
            var third:Number = 1 / 3;
            var root:Number = Math.sqrt(third);
            for (var row:int = 0; row < 3; row++) {
                for (var column:int = 0; column < 3; column++) {
                    var turn:int = (column - row + 3) % 3;
                    var cross:Number = turn == 1 ? 1 : (turn == 2 ? -1 : 0);
                    SetValue(row, column, third + cosA * ((row == column ? 1 : 0) - third) + sinA * root * cross);
                }
            }
        }

        /** The first four rows, row by row: the 20 numbers of a ColorMatrixFilter. */
        public function GetFlatArray():Array {
            var flat:Array = [];
            for (var row:int = 0; row < 4; row++) {
                for (var column:int = 0; column < 5; column++) flat.push(GetValue(row, column));
            }
            return flat;
        }
    }
}
