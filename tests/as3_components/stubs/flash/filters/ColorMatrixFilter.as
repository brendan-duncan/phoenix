package flash.filters {
    public class ColorMatrixFilter {
        public var matrix:Array;
        public function ColorMatrixFilter(matrix:Array = null) {
            this.matrix = matrix == null ? [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0] : matrix.concat();
        }
    }
}
