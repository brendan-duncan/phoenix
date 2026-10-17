package com.hulabunny.bunnyPaint {
    import flash.display.Sprite;

    // A stand-in for bunnyPaint's canvas: the parts its panels read and set.
    public class BP_Canvas extends Sprite {
        public var brushColor:uint = 0x112233;
        public var canvasColor:uint = 0xFFFFFF;
        public var layers:Array = [];
        public var currentLayerIndex:int = 1;
        public var effectsWindow:Object;

        public function BP_Canvas() {
            super();
            for (var i:int = 0; i < 3; i++) layers.push(new FakeLayer("Layer " + i));
            layers[1].blendMode = "screen";
            layers[1].opacity = 0.75;
        }

        public function get currentLayer():Object {
            return layers[currentLayerIndex];
        }
    }
}
