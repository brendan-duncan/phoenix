package fl.motion {
    /**
     * A matrix of numbers of any size, rows by columns, for ColorMatrix.
     */
    public class DynamicMatrix {
        public static const MATRIX_ORDER_PREPEND:int = 0;
        public static const MATRIX_ORDER_APPEND:int = 1;

        protected var m_width:int;
        protected var m_height:int;
        protected var m_matrix:Array;

        public function DynamicMatrix(width:int, height:int) {
            Create(width, height);
        }

        protected function Create(width:int, height:int):void {
            m_width = width;
            m_height = height;
            m_matrix = [];
            for (var row:int = 0; row < height; row++) {
                var line:Array = [];
                for (var column:int = 0; column < width; column++) line.push(0);
                m_matrix.push(line);
            }
        }

        public function GetWidth():Number {
            return m_width;
        }

        public function GetHeight():Number {
            return m_height;
        }

        public function GetValue(row:int, col:int):Number {
            if (row < 0 || row >= m_height || col < 0 || col >= m_width) return 0;
            return m_matrix[row][col];
        }

        public function SetValue(row:int, col:int, value:Number):void {
            if (row < 0 || row >= m_height || col < 0 || col >= m_width) return;
            m_matrix[row][col] = value;
        }

        public function LoadIdentity():void {
            for (var row:int = 0; row < m_height; row++) {
                for (var column:int = 0; column < m_width; column++) {
                    m_matrix[row][column] = row == column ? 1 : 0;
                }
            }
        }

        public function LoadZeros():void {
            for (var row:int = 0; row < m_height; row++) {
                for (var column:int = 0; column < m_width; column++) m_matrix[row][column] = 0;
            }
        }

        /**
         * Multiplies by another matrix: this = other * this (prepend) or
         * this * other (append). False when the sizes do not fit.
         */
        public function Multiply(inMatrix:DynamicMatrix, order:int = MATRIX_ORDER_PREPEND):Boolean {
            var a:DynamicMatrix = order == MATRIX_ORDER_PREPEND ? inMatrix : this;
            var b:DynamicMatrix = order == MATRIX_ORDER_PREPEND ? this : inMatrix;
            if (a.GetWidth() != b.GetHeight()) return false;
            var rows:int = a.GetHeight();
            var columns:int = b.GetWidth();
            var inner:int = a.GetWidth();
            var result:Array = [];
            for (var row:int = 0; row < rows; row++) {
                var line:Array = [];
                for (var column:int = 0; column < columns; column++) {
                    var sum:Number = 0;
                    for (var k:int = 0; k < inner; k++) sum += a.GetValue(row, k) * b.GetValue(k, column);
                    line.push(sum);
                }
                result.push(line);
            }
            m_matrix = result;
            m_height = rows;
            m_width = columns;
            return true;
        }

        public function MultiplyNumber(value:Number):Boolean {
            for (var row:int = 0; row < m_height; row++) {
                for (var column:int = 0; column < m_width; column++) m_matrix[row][column] *= value;
            }
            return true;
        }

        public function Add(inMatrix:DynamicMatrix):Boolean {
            if (inMatrix.GetWidth() != m_width || inMatrix.GetHeight() != m_height) return false;
            for (var row:int = 0; row < m_height; row++) {
                for (var column:int = 0; column < m_width; column++) {
                    m_matrix[row][column] += inMatrix.GetValue(row, column);
                }
            }
            return true;
        }
    }
}
