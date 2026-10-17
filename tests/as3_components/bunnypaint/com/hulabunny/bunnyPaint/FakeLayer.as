package com.hulabunny.bunnyPaint {
    // A stand-in for a bunnyPaint layer: name, look and filter settings.
    public dynamic class FakeLayer {
        public var layerName:String;
        public var visible:Boolean = true;
        public var opacity:Number = 1;
        public var blendMode:String = "normal";
        public var enabledFilters:Array = [false, true, false, false, false];
        public var filterUpdates:int = 0;
        public var dropShadowFilter:Object = {alpha: 0.8, strength: 0.65, distance: 10, angle: 45, hideObject: false,
                                              inner: false, knockout: false, color: 0, blurX: 4, blurY: 4};
        public var blurFilter:Object = {blurX: 4, blurY: 4};
        public var glowFilter:Object = {alpha: 1, blurX: 6, blurY: 6, inner: false, knockout: false, color: 0xFF0000};
        public var bevelFilter:Object = {strength: 1, distance: 4, angle: 45, blurX: 4, blurY: 4, knockout: false,
                                         highlightColor: 0xFFFFFF, shadowColor: 0};
        public var colorFilter:Object = {brightness: 0, contrast: 0, hue: 0, saturation: 0};

        public function FakeLayer(name:String) {
            layerName = name;
        }

        public function setBlendMode(mode:String):void { blendMode = mode; }
        public function setVisible(value:Boolean):void { visible = value; }
        public function hasFilter():Boolean { return enabledFilters.indexOf(true) >= 0; }
        public function isFilterEnabled(i:int):Boolean { return enabledFilters[i]; }
        public function setFilterEnabled(i:int, value:Boolean):void { enabledFilters[i] = value; }
        public function getFilterName(i:int):String { return ["Color", "Drop Shadow", "Blur", "Bevel", "Glow"][i]; }
        public function updateFilterArray():void { filterUpdates++; }
    }
}
