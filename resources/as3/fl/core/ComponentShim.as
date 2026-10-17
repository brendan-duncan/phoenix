package fl.core {
    import flash.display.MovieClip;

    /**
     * The compiled clip a movie's component symbols carry (Component
     * Assets/_private/ComponentShim): in the authoring tool it holds the
     * compiled component classes. Here the classes are these sources, so
     * the shim is an empty clip, there only so its linkage name resolves.
     */
    public class ComponentShim extends MovieClip {
        public function ComponentShim() {
            super();
        }
    }
}
