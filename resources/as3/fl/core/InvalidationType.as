package fl.core {
    /**
     * What about a component needs drawing again, for UIComponent.invalidate
     * and isInvalid.
     */
    public class InvalidationType {
        public static const ALL:String = "all";
        public static const SIZE:String = "size";
        public static const STYLES:String = "styles";
        public static const RENDERER_STYLES:String = "rendererStyles";
        public static const STATE:String = "state";
        public static const DATA:String = "data";
        public static const SCROLL:String = "scroll";
        public static const SELECTED:String = "selected";
    }
}
