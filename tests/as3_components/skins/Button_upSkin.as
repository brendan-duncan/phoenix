package {
    import flash.display.MovieClip;

    // Stands for the library symbol exported as Button_upSkin: 82 by 22.
    public class Button_upSkin extends MovieClip {
        public static var made:int = 0;
        public function Button_upSkin() {
            super();
            made++;
            graphics.drawRect(0, 0, 82, 22);
        }
    }
}
