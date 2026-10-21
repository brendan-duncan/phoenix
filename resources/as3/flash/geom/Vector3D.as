package flash.geom
{
    /// A point or a direction in three dimensions, with a fourth number (w)
    /// for a rotation or a perspective divide.
    public class Vector3D
    {
        public static const X_AXIS:Vector3D = new Vector3D(1, 0, 0);
        public static const Y_AXIS:Vector3D = new Vector3D(0, 1, 0);
        public static const Z_AXIS:Vector3D = new Vector3D(0, 0, 1);

        public var x:Number;
        public var y:Number;
        public var z:Number;
        public var w:Number;

        public function Vector3D(x:Number = 0, y:Number = 0, z:Number = 0, w:Number = 0)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.w = w;
        }

        public function get length():Number
        {
            return Math.sqrt(x * x + y * y + z * z);
        }

        public function get lengthSquared():Number
        {
            return x * x + y * y + z * z;
        }

        public static function angleBetween(a:Vector3D, b:Vector3D):Number
        {
            return Math.acos(a.dotProduct(b) / (a.length * b.length));
        }

        public static function distance(a:Vector3D, b:Vector3D):Number
        {
            return b.subtract(a).length;
        }

        public function add(a:Vector3D):Vector3D
        {
            return new Vector3D(x + a.x, y + a.y, z + a.z);
        }

        public function subtract(a:Vector3D):Vector3D
        {
            return new Vector3D(x - a.x, y - a.y, z - a.z);
        }

        public function incrementBy(a:Vector3D):void
        {
            x += a.x;
            y += a.y;
            z += a.z;
        }

        public function decrementBy(a:Vector3D):void
        {
            x -= a.x;
            y -= a.y;
            z -= a.z;
        }

        public function crossProduct(a:Vector3D):Vector3D
        {
            return new Vector3D(y * a.z - z * a.y, z * a.x - x * a.z, x * a.y - y * a.x, 1);
        }

        public function dotProduct(a:Vector3D):Number
        {
            return x * a.x + y * a.y + z * a.z;
        }

        public function equals(toCompare:Vector3D, allFour:Boolean = false):Boolean
        {
            return x == toCompare.x && y == toCompare.y && z == toCompare.z && (!allFour || w == toCompare.w);
        }

        public function nearEquals(toCompare:Vector3D, tolerance:Number, allFour:Boolean = false):Boolean
        {
            if (!(Math.abs(x - toCompare.x) < tolerance && Math.abs(y - toCompare.y) < tolerance &&
                    Math.abs(z - toCompare.z) < tolerance))
                return false;
            if (!allFour)
                return true;
            // As the player has it: w takes the other's w, and that, not the
            // difference, is held to the tolerance.
            w = toCompare.w;
            return Math.abs(w) < tolerance;
        }

        public function negate():void
        {
            x = -x;
            y = -y;
            z = -z;
        }

        /// Makes it a unit vector; its length before.
        public function normalize():Number
        {
            var l:Number = length;
            if (l != 0)
            {
                x /= l;
                y /= l;
                z /= l;
            }
            else
            {
                x = y = z = 0;
            }
            return l;
        }

        public function project():void
        {
            x /= w;
            y /= w;
            z /= w;
        }

        public function scaleBy(s:Number):void
        {
            x *= s;
            y *= s;
            z *= s;
        }

        public function clone():Vector3D
        {
            return new Vector3D(x, y, z, w);
        }

        public function copyFrom(source:Vector3D):void
        {
            x = source.x;
            y = source.y;
            z = source.z;
        }

        public function setTo(x:Number, y:Number, z:Number):void
        {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public function toString():String
        {
            return "Vector3D(" + x + ", " + y + ", " + z + ")";
        }
    }
}
