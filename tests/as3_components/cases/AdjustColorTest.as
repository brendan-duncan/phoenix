package cases {
    import fl.motion.AdjustColor;
    import fl.motion.ColorMatrix;

    // The same numbers as Phoenix's own Adjust Color filter
    // (BitmapFilter::adjustColor in src/play/display/bitmap_filter.cpp).
    public class AdjustColorTest {
        private static function near(a:Array, b:Array):Boolean {
            if (a.length != b.length) return false;
            for (var i:int = 0; i < a.length; i++) {
                if (Math.abs(a[i] - b[i]) > 1e-6) return false;
            }
            return true;
        }

        private static function matrix(brightness:Number, contrast:Number, hue:Number, saturation:Number):Array {
            var m:AdjustColor = new AdjustColor();
            m.brightness = brightness;
            m.contrast = contrast;
            m.hue = hue;
            m.saturation = saturation;
            return m.CalculateFinalFlatArray();
        }

        public function run():void {
            var identity:Array = [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0];
            check(near(matrix(0, 0, 0, 0), identity), "nothing changed: identity " + matrix(0, 0, 0, 0));
            check(near(matrix(50, 0, 0, 0), [1, 0, 0, 0, 127.5, 0, 1, 0, 0, 127.5, 0, 0, 1, 0, 127.5, 0, 0, 0, 1, 0]), "brightness");
            check(near(matrix(0, 100, 0, 0), [2, 0, 0, 0, -128, 0, 2, 0, 0, -128, 0, 0, 2, 0, -128, 0, 0, 0, 1, 0]), "contrast");
            var r:Number = 0.3086, g:Number = 0.6094, b:Number = 0.0820;
            check(near(matrix(0, 0, 0, -100), [r, g, b, 0, 0, r, g, b, 0, 0, r, g, b, 0, 0, 0, 0, 0, 1, 0]), "no saturation");
            var third:Number = 1 / 3;
            check(near(matrix(0, 0, 180, 0), [-third, 2 * third, 2 * third, 0, 0, 2 * third, -third, 2 * third, 0, 0,
                                               2 * third, 2 * third, -third, 0, 0, 0, 0, 0, 1, 0]), "hue turned half way " + matrix(0, 0, 180, 0));
            check(near(matrix(500, 0, 0, 0), matrix(100, 0, 0, 0)), "limited to 100");
            // A hue turn keeps gray gray: each row sums to 1.
            var turned:Array = matrix(0, 0, 37, 0);
            for (var row:int = 0; row < 3; row++) {
                check(Math.abs(turned[row * 5] + turned[row * 5 + 1] + turned[row * 5 + 2] - 1) < 1e-9, "row " + row + " sums to 1");
            }
            var m:AdjustColor = new AdjustColor();
            check(!m.AllValuesAreSet(), "not set");
            m.brightness = m.contrast = m.hue = m.saturation = 0;
            check(m.AllValuesAreSet() && m.clone().AllValuesAreSet(), "all set, and cloned");

            var cm:ColorMatrix = new ColorMatrix();
            check(near(cm.GetFlatArray(), identity), "ColorMatrix starts as the identity");
            cm.SetBrightnessMatrix(10);
            check(cm.GetValue(0, 4) == 10 && cm.GetValue(2, 4) == 10 && cm.GetValue(3, 4) == 0, "brightness matrix");
        }
    }
}
