package flash.geom
{
    /// A 4 by 4 matrix for transforms in three dimensions: sixteen numbers,
    /// a column at a time, held as 32-bit floats as the player holds them.
    /// Its arithmetic is in floats too: each step is rounded to one.
    public class Matrix3D
    {
        private var _raw:Vector.<Number>;

        public function Matrix3D(v:Vector.<Number> = null)
        {
            _raw = new Vector.<Number>(16, true);
            if (v != null && v.length == 16)
            {
                for (var i:int = 0; i < 16; i++)
                    _raw[i] = __f32(v[i]);
            }
            else
            {
                _raw[0] = _raw[5] = _raw[10] = _raw[15] = 1;
            }
        }

        // ---- Float arithmetic

        private static function f(n:Number):Number
        {
            return __f32(n);
        }

        /// The product a times b, each a matrix's sixteen floats.
        private static function times(a:Vector.<Number>, b:Vector.<Number>):Vector.<Number>
        {
            var out:Vector.<Number> = new Vector.<Number>(16, true);
            for (var column:int = 0; column < 4; column++)
            {
                for (var row:int = 0; row < 4; row++)
                {
                    var c:int = column * 4;
                    var sum:Number = f(a[row] * b[c]);
                    sum = f(sum + f(a[4 + row] * b[c + 1]));
                    sum = f(sum + f(a[8 + row] * b[c + 2]));
                    sum = f(sum + f(a[12 + row] * b[c + 3]));
                    out[c + row] = sum;
                }
            }
            return out;
        }

        private static function identityRaw():Vector.<Number>
        {
            var m:Vector.<Number> = new Vector.<Number>(16, true);
            m[0] = m[5] = m[10] = m[15] = 1;
            return m;
        }

        private static function scaleRaw(x:Number, y:Number, z:Number):Vector.<Number>
        {
            var m:Vector.<Number> = identityRaw();
            m[0] = f(x);
            m[5] = f(y);
            m[10] = f(z);
            return m;
        }

        private static function translateRaw(x:Number, y:Number, z:Number):Vector.<Number>
        {
            var m:Vector.<Number> = identityRaw();
            m[12] = f(x);
            m[13] = f(y);
            m[14] = f(z);
            return m;
        }

        /// The determinant of a 3 by 3 matrix, a column at a time.
        private static function det3(m0:Number, m1:Number, m2:Number, m3:Number, m4:Number, m5:Number, m6:Number,
            m7:Number, m8:Number):Number
        {
            var a:Number = f(m0 * f(f(m4 * m8) - f(m7 * m5)));
            var b:Number = f(m3 * f(f(m1 * m8) - f(m7 * m2)));
            var c:Number = f(m6 * f(f(m1 * m5) - f(m4 * m2)));
            return f(f(a - b) + c);
        }

        // ---- Properties

        public function get rawData():Vector.<Number>
        {
            var out:Vector.<Number> = new Vector.<Number>(16);
            for (var i:int = 0; i < 16; i++)
                out[i] = _raw[i];
            return out;
        }

        /// Taken only when it is sixteen numbers; anything else is passed over.
        public function set rawData(v:Vector.<Number>):void
        {
            if (v == null || v.length != 16)
                return;
            for (var i:int = 0; i < 16; i++)
                _raw[i] = f(v[i]);
        }

        public function get position():Vector3D
        {
            return new Vector3D(_raw[12], _raw[13], _raw[14]);
        }

        public function set position(pos:Vector3D):void
        {
            if (pos == null)
                return;
            _raw[12] = f(pos.x);
            _raw[13] = f(pos.y);
            _raw[14] = f(pos.z);
        }

        public function get determinant():Number
        {
            var m:Vector.<Number> = _raw;
            // No projection: the last row is 0, 0, 0 and the corner.
            if (m[3] == 0 && m[7] == 0 && m[11] == 0)
                return det3(m[0], m[1], f(m[2] * m[15]), m[4], m[5], f(m[6] * m[15]), m[8], m[9], f(m[10] * m[15]));
            var d0:Number = det3(m[5], m[6], m[7], m[9], m[10], m[11], m[13], m[14], m[15]);
            var d4:Number = det3(m[1], m[2], m[3], m[9], m[10], m[11], m[13], m[14], m[15]);
            var d8:Number = det3(m[1], m[2], m[3], m[5], m[6], m[7], m[13], m[14], m[15]);
            var d12:Number = det3(m[1], m[2], m[3], m[5], m[6], m[7], m[9], m[10], m[11]);
            var sum:Number = f(f(m[0] * d0) - f(m[4] * d4));
            sum = f(sum + f(m[8] * d8));
            return f(sum - f(m[12] * d12));
        }

        // ---- Combining

        public function identity():void
        {
            _raw = identityRaw();
        }

        public function append(lhs:Matrix3D):void
        {
            if (lhs == null)
                throw new TypeError("Error #2007: Parameter lhs must be non-null.", 2007);
            _raw = times(lhs._raw, _raw);
        }

        public function prepend(rhs:Matrix3D):void
        {
            if (rhs == null)
                throw new TypeError("Error #2007: Parameter rhs must be non-null.", 2007);
            _raw = times(_raw, rhs._raw);
        }

        public function appendTranslation(x:Number, y:Number, z:Number):void
        {
            _raw[12] = f(_raw[12] + f(x));
            _raw[13] = f(_raw[13] + f(y));
            _raw[14] = f(_raw[14] + f(z));
        }

        public function prependTranslation(x:Number, y:Number, z:Number):void
        {
            _raw = times(_raw, translateRaw(x, y, z));
        }

        public function appendScale(xScale:Number, yScale:Number, zScale:Number):void
        {
            if (xScale == 0 || yScale == 0 || zScale == 0)
                throw new ArgumentError("Error #2183: Scale values must not be zero.", 2183);
            _raw = times(scaleRaw(xScale, yScale, zScale), _raw);
        }

        public function prependScale(xScale:Number, yScale:Number, zScale:Number):void
        {
            if (xScale == 0 || yScale == 0 || zScale == 0)
                throw new ArgumentError("Error #2183: Scale values must not be zero.", 2183);
            _raw = times(_raw, scaleRaw(xScale, yScale, zScale));
        }

        /// A turn of \a degrees about \a axis through \a pivotPoint (the
        /// origin when null).
        private static function rotationRaw(degrees:Number, axis:Vector3D, pivotPoint:Vector3D):Vector.<Number>
        {
            var tx:Number = pivotPoint ? pivotPoint.x : 0;
            var ty:Number = pivotPoint ? pivotPoint.y : 0;
            var tz:Number = pivotPoint ? pivotPoint.z : 0;
            var radians:Number = degrees * Math.PI / 180;
            var c:Number = Math.cos(radians);
            var s:Number = Math.sin(radians);
            var x:Number = axis.x, y:Number = axis.y, z:Number = axis.z;
            var x2:Number = x * x, y2:Number = y * y, z2:Number = z * z;
            var ls:Number = x2 + y2 + z2;
            if (ls != 0)
            {
                var l:Number = Math.sqrt(ls);
                x /= l;
                y /= l;
                z /= l;
                x2 /= ls;
                y2 /= ls;
                z2 /= ls;
            }
            var cc:Number = 1 - c;
            var m:Vector.<Number> = new Vector.<Number>(16, true);
            // x2 + (1 - x2) c rather than x2 + (y2 + z2) c: the same for an
            // axis of length 1, and the identity for none.
            m[0] = f(x2 + (1 - x2) * c);
            m[1] = f(x * y * cc + z * s);
            m[2] = f(x * z * cc - y * s);
            m[3] = 0;
            m[4] = f(x * y * cc - z * s);
            m[5] = f(y2 + (1 - y2) * c);
            m[6] = f(y * z * cc + x * s);
            m[7] = 0;
            m[8] = f(x * z * cc + y * s);
            m[9] = f(y * z * cc - x * s);
            m[10] = f(z2 + (1 - z2) * c);
            m[11] = 0;
            m[12] = f((tx * (y2 + z2) - x * (ty * y + tz * z)) * cc + (ty * z - tz * y) * s);
            m[13] = f((ty * (x2 + z2) - y * (tx * x + tz * z)) * cc + (tz * x - tx * z) * s);
            m[14] = f((tz * (x2 + y2) - z * (tx * x + ty * y)) * cc + (tx * y - ty * x) * s);
            m[15] = 1;
            return m;
        }

        public function appendRotation(degrees:Number, axis:Vector3D, pivotPoint:Vector3D = null):void
        {
            if (axis == null)
                throw new TypeError("Error #2007: Parameter axis must be non-null.", 2007);
            _raw = times(rotationRaw(degrees, axis, pivotPoint), _raw);
        }

        public function prependRotation(degrees:Number, axis:Vector3D, pivotPoint:Vector3D = null):void
        {
            if (axis == null)
                throw new TypeError("Error #2007: Parameter axis must be non-null.", 2007);
            _raw = times(_raw, rotationRaw(degrees, axis, pivotPoint));
        }

        public function transpose():void
        {
            var m:Vector.<Number> = _raw;
            var swaps:Array = [1, 4, 2, 8, 3, 12, 6, 9, 7, 13, 11, 14];
            for (var i:int = 0; i < swaps.length; i += 2)
            {
                var t:Number = m[swaps[i]];
                m[swaps[i]] = m[swaps[i + 1]];
                m[swaps[i + 1]] = t;
            }
        }

        public function invert():Boolean
        {
            var d:Number = determinant;
            if (!(Math.abs(d) > 0.00000000001))
                return false;
            d = f(1 / d);
            var m:Vector.<Number> = _raw;
            var m11:Number = m[0], m21:Number = m[4], m31:Number = m[8], m41:Number = m[12];
            var m12:Number = m[1], m22:Number = m[5], m32:Number = m[9], m42:Number = m[13];
            var m13:Number = m[2], m23:Number = m[6], m33:Number = m[10], m43:Number = m[14];
            var m14:Number = m[3], m24:Number = m[7], m34:Number = m[11], m44:Number = m[15];
            // A 2 by 2 determinant, and three of them weighted.
            function d2(a:Number, b:Number, c:Number, e:Number):Number
            {
                return f(f(a * b) - f(c * e));
            }
            function cof(a:Number, p:Number, b:Number, q:Number, c:Number, r:Number):Number
            {
                return f(f(f(a * p) - f(b * q)) + f(c * r));
            }
            var out:Vector.<Number> = new Vector.<Number>(16, true);
            out[0] = f(d * cof(m22, d2(m33, m44, m43, m34), m32, d2(m23, m44, m43, m24), m42, d2(m23, m34, m33, m24)));
            out[1] = f(-d * cof(m12, d2(m33, m44, m43, m34), m32, d2(m13, m44, m43, m14), m42, d2(m13, m34, m33, m14)));
            out[2] = f(d * cof(m12, d2(m23, m44, m43, m24), m22, d2(m13, m44, m43, m14), m42, d2(m13, m24, m23, m14)));
            out[3] = f(-d * cof(m12, d2(m23, m34, m33, m24), m22, d2(m13, m34, m33, m14), m32, d2(m13, m24, m23, m14)));
            out[4] = f(-d * cof(m21, d2(m33, m44, m43, m34), m31, d2(m23, m44, m43, m24), m41, d2(m23, m34, m33, m24)));
            out[5] = f(d * cof(m11, d2(m33, m44, m43, m34), m31, d2(m13, m44, m43, m14), m41, d2(m13, m34, m33, m14)));
            out[6] = f(-d * cof(m11, d2(m23, m44, m43, m24), m21, d2(m13, m44, m43, m14), m41, d2(m13, m24, m23, m14)));
            out[7] = f(d * cof(m11, d2(m23, m34, m33, m24), m21, d2(m13, m34, m33, m14), m31, d2(m13, m24, m23, m14)));
            out[8] = f(d * cof(m21, d2(m32, m44, m42, m34), m31, d2(m22, m44, m42, m24), m41, d2(m22, m34, m32, m24)));
            out[9] = f(-d * cof(m11, d2(m32, m44, m42, m34), m31, d2(m12, m44, m42, m14), m41, d2(m12, m34, m32, m14)));
            out[10] = f(d * cof(m11, d2(m22, m44, m42, m24), m21, d2(m12, m44, m42, m14), m41, d2(m12, m24, m22, m14)));
            out[11] = f(-d * cof(m11, d2(m22, m34, m32, m24), m21, d2(m12, m34, m32, m14), m31, d2(m12, m24, m22, m14)));
            out[12] = f(-d * cof(m21, d2(m32, m43, m42, m33), m31, d2(m22, m43, m42, m23), m41, d2(m22, m33, m32, m23)));
            out[13] = f(d * cof(m11, d2(m32, m43, m42, m33), m31, d2(m12, m43, m42, m13), m41, d2(m12, m33, m32, m13)));
            out[14] = f(-d * cof(m11, d2(m22, m43, m42, m23), m21, d2(m12, m43, m42, m13), m41, d2(m12, m23, m22, m13)));
            out[15] = f(d * cof(m11, d2(m22, m33, m32, m23), m21, d2(m12, m33, m32, m13), m31, d2(m12, m23, m22, m13)));
            _raw = out;
            return true;
        }

        // ---- Points and directions

        public function transformVector(v:Vector3D):Vector3D
        {
            if (v == null)
                throw new TypeError("Error #2007: Parameter vector must be non-null.", 2007);
            var m:Vector.<Number> = _raw;
            var x:Number = f(v.x), y:Number = f(v.y), z:Number = f(v.z);
            return new Vector3D(f(row(m, 0, x, y, z) + m[12]), f(row(m, 1, x, y, z) + m[13]),
                f(row(m, 2, x, y, z) + m[14]), f(row(m, 3, x, y, z) + m[15]));
        }

        public function deltaTransformVector(v:Vector3D):Vector3D
        {
            if (v == null)
                throw new TypeError("Error #2007: Parameter vector must be non-null.", 2007);
            var m:Vector.<Number> = _raw;
            var x:Number = f(v.x), y:Number = f(v.y), z:Number = f(v.z);
            return new Vector3D(row(m, 0, x, y, z), row(m, 1, x, y, z), row(m, 2, x, y, z), row(m, 3, x, y, z));
        }

        /// One row of the matrix times (x, y, z).
        private static function row(m:Vector.<Number>, r:int, x:Number, y:Number, z:Number):Number
        {
            var sum:Number = f(f(m[r] * x) + f(m[4 + r] * y));
            return f(sum + f(m[8 + r] * z));
        }

        public function transformVectors(vin:Vector.<Number>, vout:Vector.<Number>):void
        {
            if (vin == null)
                throw new TypeError("Error #2007: Parameter vin must be non-null.", 2007);
            if (vout == null)
                throw new TypeError("Error #2007: Parameter vout must be non-null.", 2007);
            var count:int = int(vin.length / 3) * 3;
            if (count > vout.length)
                vout.length = count;
            var m:Vector.<Number> = _raw;
            // In doubles, unlike transformVector.
            for (var i:int = 0; i < count; i += 3)
            {
                var x:Number = vin[i], y:Number = vin[i + 1], z:Number = vin[i + 2];
                vout[i] = m[0] * x + m[4] * y + m[8] * z + m[12];
                vout[i + 1] = m[1] * x + m[5] * y + m[9] * z + m[13];
                vout[i + 2] = m[2] * x + m[6] * y + m[10] * z + m[14];
            }
        }

        // ---- Parts

        public function decompose(orientationStyle:String = "eulerAngles"):Vector.<Vector3D>
        {
            if (orientationStyle == null)
                throw new TypeError("Error #2007: Parameter orientationStyle must be non-null.", 2007);
            if (orientationStyle != Orientation3D.EULER_ANGLES && orientationStyle != Orientation3D.AXIS_ANGLE &&
                    orientationStyle != Orientation3D.QUATERNION)
                throw new Error("Error #2187: Invalid orientation style " + orientationStyle + ".  Value must be " +
                    "one of 'Orientation3D.EULER_ANGLES', 'Orientation3D.AXIS_ANGLE', or 'Orientation3D.QUATERNION'.",
                    2187);
            var m:Vector.<Number> = rawData;
            var translation:Vector3D = new Vector3D(m[12], m[13], m[14]);
            // The columns made square to each other (Gram-Schmidt), as the
            // player does: a skewed matrix's scale is along the axes it keeps.
            var c0:Vector3D = new Vector3D(m[0], m[1], m[2]);
            var c1:Vector3D = new Vector3D(m[4], m[5], m[6]);
            var c2:Vector3D = new Vector3D(m[8], m[9], m[10]);
            var sx:Number = c0.length;
            c0.scaleBy(1 / sx);
            var d:Number = c0.dotProduct(c1);
            c1 = new Vector3D(c1.x - d * c0.x, c1.y - d * c0.y, c1.z - d * c0.z);
            var sy:Number = c1.length;
            c1.scaleBy(1 / sy);
            var d0:Number = c0.dotProduct(c2);
            var d1:Number = c1.dotProduct(c2);
            c2 = new Vector3D(c2.x - d0 * c0.x - d1 * c1.x, c2.y - d0 * c0.y - d1 * c1.y,
                c2.z - d0 * c0.z - d1 * c1.z);
            var sz:Number = c2.length;
            c2.scaleBy(1 / sz);
            if (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                    m[2] * (m[4] * m[9] - m[5] * m[8]) < 0)
            {
                sz = -sz;
                c2.scaleBy(-1);
            }
            m[0] = c0.x;
            m[1] = c0.y;
            m[2] = c0.z;
            m[4] = c1.x;
            m[5] = c1.y;
            m[6] = c1.z;
            m[8] = c2.x;
            m[9] = c2.y;
            m[10] = c2.z;
            var rotation:Vector3D = new Vector3D();
            if (orientationStyle == Orientation3D.EULER_ANGLES)
            {
                rotation.y = Math.asin(-m[2]);
                if (m[2] != 1 && m[2] != -1)
                {
                    rotation.x = Math.atan2(m[6], m[10]);
                    rotation.z = Math.atan2(m[1], m[0]);
                }
                else
                {
                    rotation.z = 0;
                    rotation.x = Math.atan2(m[4], m[5]);
                }
            }
            else
            {
                var q:Array = quaternionOf(m);
                if (orientationStyle == Orientation3D.QUATERNION)
                {
                    rotation.x = q[0];
                    rotation.y = q[1];
                    rotation.z = q[2];
                    rotation.w = q[3];
                }
                else
                {
                    // An axis and the angle about it.
                    var half:Number = Math.sqrt(1 - q[3] * q[3]);
                    rotation.w = 2 * Math.acos(q[3]);
                    if (half != 0)
                    {
                        rotation.x = q[0] / half;
                        rotation.y = q[1] / half;
                        rotation.z = q[2] / half;
                    }
                }
            }
            var out:Vector.<Vector3D> = new Vector.<Vector3D>();
            out.push(translation, rotation, new Vector3D(sx, sy, sz));
            return out;
        }

        public function recompose(components:Vector.<Vector3D>, orientationStyle:String = "eulerAngles"):Boolean
        {
            if (components == null)
                throw new TypeError("Error #2007: Parameter components must be non-null.", 2007);
            if (orientationStyle == null)
                throw new TypeError("Error #2007: Parameter orientationStyle must be non-null.", 2007);
            if (orientationStyle != Orientation3D.EULER_ANGLES && orientationStyle != Orientation3D.AXIS_ANGLE &&
                    orientationStyle != Orientation3D.QUATERNION)
                throw new Error("Error #2187: Invalid orientation style " + orientationStyle + ".  Value must be " +
                    "one of 'Orientation3D.EULER_ANGLES', 'Orientation3D.AXIS_ANGLE', or 'Orientation3D.QUATERNION'.",
                    2187);
            if (components.length < 3 || components[0] == null || components[1] == null || components[2] == null)
                return false;
            var t:Vector3D = components[0], r:Vector3D = components[1], s:Vector3D = components[2];
            var rot:Vector.<Number> = new Vector.<Number>(16, true);
            if (orientationStyle == Orientation3D.EULER_ANGLES)
            {
                var cx:Number = Math.cos(r.x), cy:Number = Math.cos(r.y), cz:Number = Math.cos(r.z);
                var sx:Number = Math.sin(r.x), sy:Number = Math.sin(r.y), sz:Number = Math.sin(r.z);
                rot[0] = f(cy * cz);
                rot[1] = f(cy * sz);
                rot[2] = f(-sy);
                rot[4] = f(sx * sy * cz - cx * sz);
                rot[5] = f(sx * sy * sz + cx * cz);
                rot[6] = f(sx * cy);
                rot[8] = f(cx * sy * cz + sx * sz);
                rot[9] = f(cx * sy * sz - sx * cz);
                rot[10] = f(cx * cy);
            }
            else
            {
                var x:Number = r.x, y:Number = r.y, z:Number = r.z, w:Number = r.w;
                // A quaternion has to be a finite one of length 1.
                var square:Number = x * x + y * y + z * z + w * w;
                if (orientationStyle == Orientation3D.QUATERNION && !(Math.abs(square - 1) < 0.0001))
                    throw new ArgumentError("Error #2004: One of the parameters is invalid.", 2004);
                if (orientationStyle == Orientation3D.AXIS_ANGLE)
                {
                    var half:Number = Math.sin(w / 2);
                    x *= half;
                    y *= half;
                    z *= half;
                    w = Math.cos(w / 2);
                }
                rot[0] = f(1 - 2 * y * y - 2 * z * z);
                rot[1] = f(2 * x * y + 2 * w * z);
                rot[2] = f(2 * x * z - 2 * w * y);
                rot[4] = f(2 * x * y - 2 * w * z);
                rot[5] = f(1 - 2 * x * x - 2 * z * z);
                rot[6] = f(2 * y * z + 2 * w * x);
                rot[8] = f(2 * x * z + 2 * w * y);
                rot[9] = f(2 * y * z - 2 * w * x);
                rot[10] = f(1 - 2 * x * x - 2 * y * y);
            }
            rot[15] = 1;
            _raw = times(translateRaw(t.x, t.y, t.z), times(rot, scaleRaw(s.x, s.y, s.z)));
            return true;
        }

        /// The rotation of a matrix's upper 3 by 3 (scale left in) as a
        /// quaternion: x, y, z, w.
        private static function quaternionOf(m:Vector.<Number>):Array
        {
            var trace:Number = m[0] + m[5] + m[10];
            var x:Number, y:Number, z:Number, w:Number;
            if (trace > 0)
            {
                w = Math.sqrt(1 + trace) / 2;
                x = (m[6] - m[9]) / (4 * w);
                y = (m[8] - m[2]) / (4 * w);
                z = (m[1] - m[4]) / (4 * w);
            }
            else if (m[0] > m[5] && m[0] > m[10])
            {
                x = Math.sqrt(1 + m[0] - m[5] - m[10]) / 2;
                w = (m[6] - m[9]) / (4 * x);
                y = (m[1] + m[4]) / (4 * x);
                z = (m[8] + m[2]) / (4 * x);
            }
            else if (m[5] > m[10])
            {
                y = Math.sqrt(1 + m[5] - m[0] - m[10]) / 2;
                x = (m[1] + m[4]) / (4 * y);
                w = (m[8] - m[2]) / (4 * y);
                z = (m[6] + m[9]) / (4 * y);
            }
            else
            {
                z = Math.sqrt(1 + m[10] - m[0] - m[5]) / 2;
                x = (m[8] + m[2]) / (4 * z);
                y = (m[6] + m[9]) / (4 * z);
                w = (m[1] - m[4]) / (4 * z);
            }
            return [x, y, z, w];
        }

        private static function unit(q:Array):Array
        {
            var l:Number = Math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            return l == 0 ? q : [q[0] / l, q[1] / l, q[2] / l, q[3] / l];
        }

        /// Part way from a to b (percent): the translation along a line, the
        /// rotation along the sphere; the scale is not kept.
        private static function between(a:Vector.<Number>, b:Vector.<Number>, percent:Number):Vector.<Number>
        {
            // Read off the matrices as they are, scale and all, then made unit.
            var q0:Array = unit(quaternionOf(a)), q1:Array = unit(quaternionOf(b));
            var dot:Number = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
            if (dot < 0)
            {
                dot = -dot;
                q1 = [-q1[0], -q1[1], -q1[2], -q1[3]];
            }
            var k0:Number = 1 - percent, k1:Number = percent;
            if (dot < 0.999999)
            {
                var angle:Number = Math.acos(dot);
                var s:Number = Math.sin(angle);
                k0 = Math.sin((1 - percent) * angle) / s;
                k1 = Math.sin(percent * angle) / s;
            }
            var x:Number = q0[0] * k0 + q1[0] * k1, y:Number = q0[1] * k0 + q1[1] * k1;
            var z:Number = q0[2] * k0 + q1[2] * k1, w:Number = q0[3] * k0 + q1[3] * k1;
            var out:Vector.<Number> = new Vector.<Number>(16, true);
            out[0] = f(1 - 2 * y * y - 2 * z * z);
            out[1] = f(2 * x * y + 2 * w * z);
            out[2] = f(2 * x * z - 2 * w * y);
            out[4] = f(2 * x * y - 2 * w * z);
            out[5] = f(1 - 2 * x * x - 2 * z * z);
            out[6] = f(2 * y * z + 2 * w * x);
            out[8] = f(2 * x * z + 2 * w * y);
            out[9] = f(2 * y * z - 2 * w * x);
            out[10] = f(1 - 2 * x * x - 2 * y * y);
            out[12] = f(a[12] + (b[12] - a[12]) * percent);
            out[13] = f(a[13] + (b[13] - a[13]) * percent);
            out[14] = f(a[14] + (b[14] - a[14]) * percent);
            out[15] = 1;
            return out;
        }

        public static function interpolate(thisMat:Matrix3D, toMat:Matrix3D, percent:Number):Matrix3D
        {
            if (thisMat == null)
                throw new TypeError("Error #2007: Parameter fromMat must be non-null.", 2007);
            if (toMat == null)
                throw new TypeError("Error #2007: Parameter toMat must be non-null.", 2007);
            var out:Matrix3D = new Matrix3D();
            out._raw = between(thisMat._raw, toMat._raw, percent);
            return out;
        }

        public function interpolateTo(toMat:Matrix3D, percent:Number):void
        {
            if (toMat == null)
                throw new TypeError("Error #2007: Parameter toMat must be non-null.", 2007);
            _raw = between(_raw, toMat._raw, percent);
        }

        /// Turns it to face \a at from where it is, its up \a up.
        public function pointAt(pos:Vector3D, at:Vector3D = null, up:Vector3D = null):void
        {
            if (at == null)
                at = new Vector3D(0, 0, -1);
            if (up == null)
                up = new Vector3D(0, -1, 0);
            var dir:Vector3D = pos.subtract(position);
            dir.normalize();
            var right:Vector3D = up.crossProduct(dir);
            right.normalize();
            var newUp:Vector3D = dir.crossProduct(right);
            newUp.normalize();
            var parts:Vector.<Vector3D> = decompose();
            var s:Vector3D = parts[2];
            _raw[0] = f(right.x * s.x);
            _raw[1] = f(right.y * s.x);
            _raw[2] = f(right.z * s.x);
            _raw[4] = f(newUp.x * s.y);
            _raw[5] = f(newUp.y * s.y);
            _raw[6] = f(newUp.z * s.y);
            _raw[8] = f(dir.x * s.z);
            _raw[9] = f(dir.y * s.z);
            _raw[10] = f(dir.z * s.z);
        }

        // ---- Copies

        public function clone():Matrix3D
        {
            var out:Matrix3D = new Matrix3D();
            for (var i:int = 0; i < 16; i++)
                out._raw[i] = _raw[i];
            return out;
        }

        public function copyFrom(sourceMatrix3D:Matrix3D):void
        {
            if (sourceMatrix3D == null)
                throw new TypeError("Error #2007: Parameter source must be non-null.", 2007);
            for (var i:int = 0; i < 16; i++)
                _raw[i] = sourceMatrix3D._raw[i];
        }

        public function copyToMatrix3D(dest:Matrix3D):void
        {
            if (dest == null)
                throw new TypeError("Error #2007: Parameter dest must be non-null.", 2007);
            for (var i:int = 0; i < 16; i++)
                dest._raw[i] = _raw[i];
        }

        public function copyRawDataFrom(vector:Vector.<Number>, index:uint = 0, transpose:Boolean = false):void
        {
            if (vector == null)
                throw new TypeError("Error #2007: Parameter source must be non-null.", 2007);
            if (vector.length < index + 16)
                throw new ArgumentError("Error #2004: One of the parameters is invalid.", 2004);
            for (var i:int = 0; i < 16; i++)
                _raw[i] = f(vector[index + i]);
            if (transpose)
                this.transpose();
        }

        public function copyRawDataTo(vector:Vector.<Number>, index:uint = 0, transpose:Boolean = false):void
        {
            if (vector == null)
                throw new TypeError("Error #2007: Parameter dest must be non-null.", 2007);
            var source:Matrix3D = this;
            if (transpose)
            {
                source = clone();
                source.transpose();
            }
            if (vector.length < index + 16)
            {
                if (vector.fixed)
                    throw new RangeError("Error #1126: Cannot change the length of a fixed Vector.", 1126);
                vector.length = index + 16;
            }
            for (var i:int = 0; i < 16; i++)
                vector[index + i] = source._raw[i];
        }

        public function copyColumnFrom(column:uint, vector3D:Vector3D):void
        {
            if (vector3D == null)
                throw new TypeError("Error #2007: Parameter vector3D must be non-null.", 2007);
            if (column > 3)
                throw new ArgumentError("Error #2004: One of the parameters is invalid.", 2004);
            var c:int = column * 4;
            _raw[c] = f(vector3D.x);
            _raw[c + 1] = f(vector3D.y);
            _raw[c + 2] = f(vector3D.z);
            _raw[c + 3] = f(vector3D.w);
        }

        public function copyColumnTo(column:uint, vector3D:Vector3D):void
        {
            if (vector3D == null)
                throw new TypeError("Error #2007: Parameter vector3D must be non-null.", 2007);
            if (column > 3)
                throw new ArgumentError("Error #2004: One of the parameters is invalid.", 2004);
            var c:int = column * 4;
            vector3D.x = _raw[c];
            vector3D.y = _raw[c + 1];
            vector3D.z = _raw[c + 2];
            vector3D.w = _raw[c + 3];
        }

        public function copyRowFrom(row:uint, vector3D:Vector3D):void
        {
            if (vector3D == null)
                throw new TypeError("Error #2007: Parameter vector3D must be non-null.", 2007);
            if (row > 3)
                throw new ArgumentError("Error #2004: One of the parameters is invalid.", 2004);
            _raw[row] = f(vector3D.x);
            _raw[row + 4] = f(vector3D.y);
            _raw[row + 8] = f(vector3D.z);
            _raw[row + 12] = f(vector3D.w);
        }

        public function copyRowTo(row:uint, vector3D:Vector3D):void
        {
            if (vector3D == null)
                throw new TypeError("Error #2007: Parameter vector3D must be non-null.", 2007);
            if (row > 3)
                throw new ArgumentError("Error #2004: One of the parameters is invalid.", 2004);
            vector3D.x = _raw[row];
            vector3D.y = _raw[row + 4];
            vector3D.z = _raw[row + 8];
            vector3D.w = _raw[row + 12];
        }
    }
}
