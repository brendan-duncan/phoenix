package fl.managers {
    import fl.core.InvalidationType;
    import fl.core.UIComponent;
    import flash.text.TextFormat;
    import flash.utils.getDefinitionByName;
    import flash.utils.getQualifiedClassName;
    import flash.utils.getQualifiedSuperclassName;

    /**
     * Styles shared by components: global ones (setStyle), for every
     * instance of one component class (setComponentStyle), and each class's
     * defaults (its static getStyleDefinition()).
     *
     * A component finds a style first among its own (UIComponent.setStyle),
     * then its class's (setComponentStyle), then the global ones (which start
     * as UIComponent's defaults), then its class's defaults. A null style is
     * one not set.
     */
    public class StyleManager {
        // Global styles, UIComponent's defaults to begin with.
        private static var globalStyles:Object = null;
        // Class name -> styles set with setComponentStyle.
        private static var componentStyles:Object = {};
        // Class name -> that class's default styles.
        private static var defaultStyles:Object = {};
        // Class name -> the instances made, to invalidate on a change.
        private static var instances:Object = {};

        public function StyleManager() {}

        private static function globals():Object {
            if (globalStyles == null) {
                globalStyles = {};
                var defaults:Object = UIComponent.getStyleDefinition();
                for (var name:String in defaults) globalStyles[name] = defaults[name];
            }
            return globalStyles;
        }

        /** The name a class or an instance's class is kept under. */
        private static function classKey(component:Object):String {
            return getQualifiedClassName(component).replace("::", ".");
        }

        /**
         * A class's default styles: the static getStyleDefinition() of the
         * class or, for a subclass without one, of the nearest superclass
         * with one.
         */
        private static function classDefaults(key:String):Object {
            var styles:Object = defaultStyles[key];
            if (styles != null) return styles;
            var name:String = key;
            while (name != null && name != "" && name != "Object") {
                var cls:Object = null;
                try {
                    cls = getDefinitionByName(name);
                } catch (e:Error) {
                    cls = null;
                }
                if (cls == null) break;
                var getter:Object = null;
                try {
                    getter = cls["getStyleDefinition"];
                } catch (e2:Error) {
                    getter = null;
                }
                if (getter is Function) {
                    styles = getter();
                    break;
                }
                var superName:String = getQualifiedSuperclassName(cls);
                name = superName == null ? null : superName.replace("::", ".");
            }
            if (styles == null) styles = UIComponent.getStyleDefinition();
            defaultStyles[key] = styles;
            return styles;
        }

        /** Keeps track of a component, so a style change reaches it. */
        public static function registerInstance(instance:UIComponent):void {
            var key:String = classKey(instance);
            classDefaults(key);
            var list:Array = instances[key];
            if (list == null) {
                list = [];
                instances[key] = list;
            }
            list.push(instance);
        }

        /** Stops keeping track of a component (the player would forget it on its own). */
        public static function unregisterInstance(instance:UIComponent):void {
            var list:Array = instances[classKey(instance)];
            if (list == null) return;
            var index:int = list.indexOf(instance);
            if (index >= 0) list.splice(index, 1);
        }

        /** The style an instance has unless it sets its own. */
        public static function getSharedStyle(instance:UIComponent, name:String):Object {
            var key:String = classKey(instance);
            var set:Object = componentStyles[key];
            if (set != null && set[name] != null) return set[name];
            var global:Object = globals()[name];
            if (global != null) return global;
            return classDefaults(key)[name];
        }

        public static function setStyle(name:String, style:Object):void {
            var styles:Object = globals();
            if (styles[name] === style && !(style is TextFormat)) return;
            styles[name] = style;
            for (var key:String in instances) invalidateAll(key);
        }

        public static function getStyle(name:String):Object {
            return globals()[name];
        }

        public static function clearStyle(name:String):void {
            setStyle(name, null);
        }

        /** A style for every instance of one component class (not its subclasses). */
        public static function setComponentStyle(component:Object, name:String, style:Object):void {
            var key:String = classKey(component);
            var styles:Object = componentStyles[key];
            if (styles == null) {
                styles = {};
                componentStyles[key] = styles;
            }
            if (styles[name] === style && !(style is TextFormat)) return;
            styles[name] = style;
            invalidateAll(key);
        }

        public static function getComponentStyle(component:Object, name:String):Object {
            var styles:Object = componentStyles[classKey(component)];
            return styles == null ? null : styles[name];
        }

        public static function clearComponentStyle(component:Object, name:String):void {
            var styles:Object = componentStyles[classKey(component)];
            if (styles == null || styles[name] == null) return;
            delete styles[name];
            invalidateAll(classKey(component));
        }

        private static function invalidateAll(key:String):void {
            var list:Array = instances[key];
            if (list == null) return;
            for each (var instance:UIComponent in list.concat()) {
                instance.invalidate(InvalidationType.STYLES);
                instance.invalidate(InvalidationType.RENDERER_STYLES);
            }
        }
    }
}
